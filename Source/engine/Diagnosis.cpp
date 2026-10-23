#include "Diagnosis.h"
#include <map>

namespace az
{
//==============================================================================
juce::String statusLabel (Status s)
{
    switch (s)
    {
        case Status::Ideal:     return "IDEAL";
        case Status::Warn:      return "DIQQAT";
        case Status::Problem:   return "MUAMMO";
        case Status::Redundant: return "ORTIQCHA";
        case Status::Info:      return "MA'LUMOT";
        case Status::NoData:    return "KUTILMOQDA";
    }
    return {};
}

juce::String fmtDb (float v, int decimals, const char* unit)
{
    if (v <= kNoValue + 1) return "--";
    return (v > 0.05f ? "+" : "") + juce::String (v, decimals) + unit;
}

juce::String fmtTime (float s)
{
    if (s < 0) return "--";
    const int t = (int) s;
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}

juce::String fmtHz (float hz)
{
    if (hz >= 10000.0f) return juce::String (juce::roundToInt (hz / 1000.0f)) + " kHz";
    if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 1) + " kHz";
    return juce::String ((int) std::round (hz)) + " Hz";
}

namespace
{
    int scoreFor (Status s) { return s == Status::Ideal ? 100 : s == Status::Warn ? 65 : s == Status::Problem ? 30 : 0; }

    struct RegionDev { float dev = 0; int n = 0; float worstHz = 0; float worst = 0; };

    RegionDev regionDeviation (const Features& f, const Profile& p, float lo, float hi)
    {
        RegionDev r; double s = 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float fc = bandCentre (b);
            if (fc < lo || fc > hi || f.bandRel[(size_t) b] <= kNoValue + 1) continue;
            const float d = f.bandRel[(size_t) b] - p.target[(size_t) b];
            s += d; ++r.n;
            if (std::abs (d) > std::abs (r.worst)) { r.worst = d; r.worstHz = fc; }
        }
        r.dev = r.n > 0 ? (float) (s / r.n) : 0.0f;
        return r;
    }

    float regionTol (const Profile& p, float lo, float hi)
    {
        double s = 0; int n = 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float fc = bandCentre (b);
            if (fc >= lo && fc <= hi) { s += p.tol[(size_t) b]; ++n; }
        }
        // region o'rtachasi yakka polosadan barqarorroq: ruxsat 0.7 x polosa tolerantligi
        return n > 0 ? (float) (0.7 * s / n) : 3.0f;
    }

    Check spectralCheck (const Features& f, const Profile& p, const juce::String& title, float lo, float hi,
                         const juce::String& tooMuch, const juce::String& tooLittle)
    {
        Check c;
        c.title = title;
        c.freqLo = lo; c.freqHi = hi;
        const auto r = regionDeviation (f, p, lo, hi);
        const float tol = regionTol (p, lo, hi);
        c.measured = fmtDb (r.dev) + " (etalonga nisbatan)";
        c.target = juce::String::fromUTF8 ("±") + juce::String (tol, 1) + " dB";

        const float a = std::abs (r.dev);
        c.status = a <= tol ? Status::Ideal : (a <= tol * 2.0f ? Status::Warn : Status::Problem);
        if (c.status != Status::Ideal)
        {
            const float cut = juce::jlimit (1.0f, 6.0f, std::round ((a - tol * 0.5f) * 2.0f) / 2.0f);
            c.advice = (r.dev > 0 ? tooMuch : tooLittle)
                         .replace ("{hz}", fmtHz (r.worstHz))
                         .replace ("{db}", juce::String (cut, 1));
        }
        return c;
    }
}

//==============================================================================
Report diagnose (const Features& f, const Profile& p, const DeliveryStandard& std, bool master)
{
    Report rep;
    rep.hasData = f.hasEnoughData();
    if (! rep.hasData)
    {
        rep.verdict = "Tahlil kutilmoqda";
        return rep;
    }

    auto add = [&rep] (Check c) { c.score = scoreFor (c.status); rep.checks.push_back (std::move (c)); };

    // 1. Loudness
    {
        Check c;
        c.title = "Loudness (BS.1770-4)";
        c.measured = juce::String (f.integrated, 1) + " LUFS";
        if (master)
        {
            c.target = juce::String (juce::roundToInt (std.lufs)) + " LUFS";
            const float d = f.integrated - std.lufs;
            if (std::abs (d) <= 1.0f) c.status = Status::Ideal;
            else if (d > 1.0f)
            {
                c.status = d > 3.0f ? Status::Problem : Status::Warn;
                c.advice = "Platforma trekni " + juce::String (d, 1) + " dB ga pasaytiradi - dinamika behuda yo'qotilgan. "
                           "Limiter'ni " + juce::String (d, 1) + " dB kamroq ishlating.";
            }
            else
            {
                c.status = d < -3.0f ? Status::Warn : Status::Ideal;
                c.advice = "Trek me'yordan " + juce::String (-d, 1) + " dB past. True Peak zaxirasi bo'lsa, umumiy darajani ko'taring.";
            }
        }
        else
        {
            c.status = Status::Info;
            c.target = "Vokal trekida baholanmaydi";
            c.advice = "Loudness standarti master'ga qo'llanadi. Master'ni tekshirish uchun Manba = Master qiling.";
        }
        add (c);
    }

    // 2. True Peak
    {
        Check c;
        c.title = "True Peak";
        c.measured = fmtDb (f.truePeakMax, 1, " dBTP");
        const float limit = master ? std.truePeakMax : 0.0f;
        c.target = (master ? "max " : "vokal: max ") + juce::String (limit, 1) + " dBTP";
        if (f.truePeakMax > limit) c.status = Status::Problem;
        else if (! master && f.truePeakMax > -1.0f) c.status = Status::Warn;
        else c.status = Status::Ideal;
        if (c.status != Status::Ideal)
            c.advice = master ? "True-peak limiter ceiling'ini " + juce::String (std.truePeakMax, 1) + " dBTP ga qo'ying (oversampling yoqilgan holda). "
                                "Aks holda MP3/AAC kodeklarida buzilish paydo bo'ladi."
                              : "Vokal treki 0 dBTP ga juda yaqin. Fader yoki clip gain bilan 3-6 dB pasaytiring - mix bus va analog emulyatsiya plaginlari uchun zaxira qoladi.";
        add (c);
    }

    // 3. Clipping
    {
        Check c;
        c.title = "Clipping (raqamli overload)";
        c.measured = f.clippedSamples > 0 ? juce::String (f.clippedSamples) + " ta namuna" : "yo'q";
        c.target = "0";
        c.status = f.clippedSamples > 0 ? Status::Problem : Status::Ideal;
        if (c.status == Status::Problem)
            c.advice = "Signal 0 dBFS ga yetgan. Clip'ni zanjirning qaysi joyida paydo bo'lishini Zanjir rejimida toping; yozuvning o'zida bo'lsa, qayta yozish kerak.";
        add (c);
    }

    // 4. Dinamika: 100 ms darajalar tarqalishi (siqish chuqurligi)
    {
        Check c;
        c.title = "Dinamika (darajalar tarqalishi)";
        c.measured = juce::String (f.levelSpread, 1) + " dB";
        const float lo = master ? 3.0f : 4.0f, hi = master ? 8.0f : 10.0f;
        c.target = juce::String (juce::roundToInt (lo)) + "-" + juce::String (juce::roundToInt (hi)) + " dB";
        const float out = f.levelSpread < lo ? lo - f.levelSpread : (f.levelSpread > hi ? f.levelSpread - hi : 0.0f);
        c.status = out <= 0 ? Status::Ideal : (out <= 1.5f ? Status::Warn : Status::Problem);
        if (f.levelSpread < lo)
            c.advice = "Haddan ortiq siqilgan: so'zlar orasidagi tabiiy balandlik farqi deyarli yo'q, vokal \"yassi\". Kompressor(lar)da umumiy gain reduction'ni "
                       "3-5 dB gacha kamaytiring, ketma-ket ikki kompressor bo'lsa ikkinchisini yumshating (2-3:1).";
        else if (f.levelSpread > hi)
            c.advice = "Vokal mixda \"suzib\" yuradi: baland va past so'zlar farqi katta. Kompressor qo'shing (3:1, 3-6 dB GR) yoki clip gain/avtomatizatsiya bilan tekislang.";
        add (c);
    }

    // 4b. Crest factor (peak va o'rtacha daraja farqi)
    {
        Check c;
        c.title = master ? "Crest factor (PLR)" : "Crest factor (peak)";
        c.measured = juce::String (f.crest, 1) + " dB";
        const float lo = master ? 7.0f : p.crestMin, hi = master ? 13.0f : p.crestMax;
        c.target = juce::String (juce::roundToInt (lo)) + "-" + juce::String (juce::roundToInt (hi)) + " dB";
        const float out = f.crest < lo ? lo - f.crest : (f.crest > hi ? f.crest - hi : 0.0f);
        c.status = out <= 0 ? Status::Ideal : (out <= 2.0f ? Status::Warn : Status::Problem);
        if (f.crest < lo)
            c.advice = "Peaklar haddan ortiq kesilgan (limiter/clipper): undosh hujumlari yo'qolgan. Limiter yoki tez attack'li kompressorda gain reduction'ni kamaytiring, attack'ni 10-30 ms qiling.";
        else if (f.crest > hi && f.levelSpread <= 10.0f)
            c.advice = "Darajalar tekis, lekin alohida peaklar keskin (undoshlar, \"P/T\" zarbalari). Tez attack'li (1176 tipidagi) kompressor yoki yumshoq clipper bilan 2-3 dB peak'ni kesing.";
        else if (f.crest > hi)
            c.advice = "Peaklar keskin. Kompressor bilan dinamikani tekislang (yuqoridagi tavsiyaga qarang).";
        add (c);
    }

    // 5. LRA
    {
        Check c;
        c.title = "Loudness Range (LRA)";
        c.measured = juce::String (f.lra, 1) + " LU";
        const float lo = master ? 3.0f : p.lraMin, hi = master ? 10.0f : p.lraMax;
        c.target = juce::String (lo, 1) + "-" + juce::String (hi, 1) + " LU";
        if (f.secondsActive < 20.0)
        {
            c.status = Status::Info;
            c.advice = "LRA uchun kamida 20 s faol signal kerak.";
        }
        else
        {
            const float out = f.lra < lo ? lo - f.lra : (f.lra > hi ? f.lra - hi : 0.0f);
            c.status = out <= 0 ? Status::Ideal : (out <= 2.0f ? Status::Warn : Status::Problem);
            if (f.lra < lo) c.advice = "Qo'shiq bo'limlari orasida dinamik farq juda kam - naqarot va kuplet bir xil balandlikda. Avtomatizatsiya bilan bo'limlarni ajrating.";
            if (f.lra > hi) c.advice = "Bo'limlar orasidagi balandlik farqi katta. Kuplet/naqarot fader avtomatizatsiyasi yoki sekin (leveling) kompressor qo'llang.";
        }
        add (c);
    }

    // 6-10. Spektral balans (vokal rejimida yoki maxsus etalonda)
    if (f.spectrumValid && (! master || p.custom))
    {
        add (spectralCheck (f, p, "Past chastota (< 100 Hz)", 20.0f, 100.0f,
            "100 Hz dan pastda ortiqcha energiya (rumble, pop, mikrofon stoyka tovushi). High-pass filtr: 80-100 Hz, 18-24 dB/okt.",
            "Past qism etalondan kam - HPF juda baland qo'yilgan bo'lishi mumkin. Kesish chastotasini 70-90 Hz ga tushiring."));
        add (spectralCheck (f, p, "Mud / Box (200-500 Hz)", 200.0f, 500.0f,
            "{hz} atrofida \"qutidek\"/loyqa rang. EQ: {hz}, Q 1.5-2, -{db} dB.",
            "Vokalda tana (body) yetishmaydi. EQ: 200-300 Hz, keng Q (0.7), +{db} dB."));
        add (spectralCheck (f, p, "Nazal (700 Hz-1.5 kHz)", 700.0f, 1500.0f,
            "{hz} atrofida burun ohangi / \"telefon\" rangi. EQ: {hz}, Q 2-3, -{db} dB.",
            "O'rta qism cho'kkan, vokal mixda yo'qolishi mumkin. EQ: 1 kHz, keng Q, +{db} dB."));
        add (spectralCheck (f, p, "Presence (2-5 kHz)", 2000.0f, 5000.0f,
            "{hz} atrofida qattiq/achchiq rang (quloqni charchatadi). EQ: {hz}, Q 2, -{db} dB yoki dinamik EQ.",
            "Vokal xira, mixdan oldinga chiqmaydi. EQ: 3-4 kHz, keng Q, +{db} dB."));
        add (spectralCheck (f, p, "Air (10-16 kHz)", 10000.0f, 16000.0f,
            "Yuqori qism o'tkir/shitirlagan. Shelf 12 kHz, -{db} dB.",
            "Vokalda \"havo\" yetishmaydi. High-shelf 10-12 kHz, +{db} dB (de-esser'dan keyin)."));
    }

    // 11. Sibilyant
    if (f.sibilanceP97 > kNoValue + 1)
    {
        Check c;
        c.title = "Sibilyant (S/Sh, 5-9 kHz)";
        c.freqLo = 5000.0f; c.freqHi = 9000.0f;
        c.measured = fmtDb (f.sibilanceP97);
        c.target = "max " + fmtDb (p.sibMax);
        const float over = f.sibilanceP97 - p.sibMax;
        c.status = over <= 0 ? Status::Ideal : (over <= 3.0f ? Status::Warn : Status::Problem);
        if (c.status != Status::Ideal)
            c.advice = "S/Sh tovushlari " + juce::String (over, 1) + " dB ortiq. De-esser: 5-9 kHz, split-band rejimi, "
                       + juce::String (juce::roundToInt (juce::jlimit (2.0f, 8.0f, over + 2.0f))) + " dB gacha kamaytirish. Vaqtlarni pastdagi shkalada ko'ring.";
        add (c);
    }

    // 12. Shovqin foni
    if (f.noiseFloor > kNoValue + 1)
    {
        Check c;
        c.title = "Shovqin foni";
        c.measured = juce::String (juce::roundToInt (f.noiseFloor)) + " dBFS";
        c.target = "< " + juce::String (juce::roundToInt (p.noiseFloorMax)) + " dBFS";
        const float over = f.noiseFloor - p.noiseFloorMax;
        if (f.rmsActive - f.noiseFloor < 25.0f)
        {
            // Pauza topilmadi: eng past qism ham signal — shovqinni o'lchab bo'lmaydi
            c.status = Status::Info;
            c.measured = "pauza topilmadi";
            c.advice = "Shovqinni o'lchash uchun trekda pauzalar (nafas oralig'i) bo'lishi kerak.";
        }
        else
            c.status = over <= 0 ? Status::Ideal : (over <= 8.0f ? Status::Warn : Status::Problem);
        if (c.status == Status::Warn || c.status == Status::Problem)
            c.advice = "Pauzalarda shovqin eshitiladi (xona, kompyuter, pre-amp). Pauzalarni tozalang yoki yumshoq gate/expander qo'llang (threshold fon + 6 dB).";
        add (c);
    }

    // Umumiy ball
    double s = 0; int n = 0;
    for (auto& c : rep.checks)
    {
        if (c.status == Status::Info || c.status == Status::NoData) continue;
        s += c.score; ++n;
        if (c.status == Status::Ideal) ++rep.nIdeal;
        else if (c.status == Status::Warn) ++rep.nWarn;
        else if (c.status == Status::Problem) ++rep.nProblem;
    }
    rep.score = n > 0 ? (int) std::round (s / n) : 0;
    rep.verdict = rep.score >= 90 ? "Release darajasida" : rep.score >= 75 ? "Release'ga yaqin"
                : rep.score >= 55 ? "Ishlov kerak" : "Jiddiy ishlov kerak";
    return rep;
}

//==============================================================================
void diagnoseChain (const std::vector<ChainPoint>& pts, const Profile& p, Report& rep)
{
    rep.stages.clear();
    rep.chainScore = rep.stIdeal = rep.stWarn = rep.stProblem = rep.stRedundant = 0;

    if (pts.size() < 2)
    {
        rep.chainHint = pts.empty() ? juce::String ("Probe topilmadi.")
                                    : juce::String ("Zanjirni baholash uchun har bir plagindan keyin AZ Monitoring'ni Probe rejimida qo'ying "
                                                    "(eng boshida - IN).");
        return;
    }
    if (pts.front().slotIndex != 0)
        rep.chainHint = "Maslahat: zanjir boshiga IN probe qo'ying - birinchi plagin ham baholanadi.";

    struct Move { size_t stage; int group; float db; };
    std::vector<Move> moves;

    for (size_t i = 1; i < pts.size(); ++i)
    {
        const auto& a = pts[i - 1].f;
        const auto& b = pts[i].f;
        StageResult st;
        st.slot = pts[i - 1].slot + juce::String::fromUTF8 (" → ") + pts[i].slot;
        st.name = pts[i].name;
        st.rackSlot = pts[i].rackSlot;

        if (! a.hasEnoughData() || ! b.hasEnoughData() || ! a.spectrumValid || ! b.spectrumValid)
        {
            st.status = Status::NoData;
            st.effect = "Ma'lumot yetarli emas - trekni ijro eting.";
            rep.stages.push_back (st);
            continue;
        }

        st.valid = true;
        st.gainChange  = b.integrated - a.integrated;
        st.crestBefore = a.crest;  st.crestAfter = b.crest;
        st.spreadBefore = a.levelSpread; st.spreadAfter = b.levelSpread;
        st.sibBefore   = a.sibilanceP97; st.sibAfter = b.sibilanceP97;
        st.snrChange   = (b.rmsActive - b.noiseFloor) - (a.rmsActive - a.noiseFloor);
        st.levelIn     = a.rmsActive;
        st.tpOut       = b.truePeakMax;

        // Polosa farqlari, keng polosali gain'ni (mediana) olib tashlab.
        // Signal energiyasi juda past polosalar (asosiy darajadan 35 dB pastda) baholanmaydi.
        auto refLevel = [] (const Features& f)
        {
            double s = 0; int c = 0;
            for (int k = 0; k < kNumBands; ++k)
                if (bandCentre (k) >= 200.0f && bandCentre (k) <= 5000.0f && f.bandLevel[(size_t) k] > kNoValue + 1) { s += f.bandLevel[(size_t) k]; ++c; }
            return c > 0 ? (float) (s / c) : 0.0f;
        };
        const float gateA = refLevel (a) - 35.0f, gateB = refLevel (b) - 35.0f;
        std::array<bool, kNumBands> usable {};
        std::vector<float> mid;
        for (int k = 0; k < kNumBands; ++k)
        {
            const float la = a.bandLevel[(size_t) k], lb = b.bandLevel[(size_t) k];
            usable[(size_t) k] = la > gateA || lb > gateB;
            const float d = (usable[(size_t) k] && la > kNoValue + 1 && lb > kNoValue + 1) ? lb - la : 0.0f;
            st.bandDelta[(size_t) k] = d;
            const float fc = bandCentre (k);
            if (usable[(size_t) k] && fc >= 100.0f && fc <= 10000.0f) mid.push_back (d);
        }
        if (mid.empty()) mid.push_back (0.0f);
        std::nth_element (mid.begin(), mid.begin() + (long) mid.size() / 2, mid.end());
        const float ref = mid[mid.size() / 2];
        float maxBoost = 0, maxCut = 0, boostHz = 0, cutHz = 0;
        float lfRise = 0; int lfN = 0;
        for (int k = 0; k < kNumBands; ++k)
        {
            auto& d = st.bandDelta[(size_t) k];
            if (! usable[(size_t) k]) { d = 0; continue; }
            d -= ref;
            const float fc = bandCentre (k);
            if (fc >= 40.0f && fc <= 120.0f) { lfRise += d; ++lfN; }
            if (fc < 40.0f || fc > 16000.0f) continue;
            if (d > maxBoost) { maxBoost = d; boostHz = fc; }
            if (d < maxCut)   { maxCut = d;   cutHz = fc; }
        }
        lfRise = lfN > 0 ? lfRise / (float) lfN : 0.0f;

        const float crestD = b.crest - a.crest;
        const float sibD = b.sibilanceP97 - a.sibilanceP97;
        const float maxEq = juce::jmax (maxBoost, -maxCut);
        // Kompressor ham uzoq muddatli spektrni biroz o'zgartiradi (pauzalardagi shovqin ko'tariladi) —
        // shuning uchun dinamika bosqichida EQ deb hisoblash chegarasi yuqoriroq
        const float spreadD = b.levelSpread - a.levelSpread;   // manfiy = darajalar tekislandi (siqildi)
        const bool hasT = pts[i].hasTransfer;
        const bool hasGr = pts[i].grRange >= 0.0f;
        st.grRange = pts[i].grRange;
        st.effRatio = pts[i].grSlope > -0.95f ? 1.0f / (1.0f + pts[i].grSlope) : 20.0f;
        st.measuredResponse = hasT;
        const float cohAvg = hasT ? pts[i].cohAvg : 1.0f;

        // Rack slotlari: o'lchangan H(f) — plagin har bir chastotada aniq qancha o'zgartirgani
        // (o'rtacha spektrlar farqidan farqli o'laroq, kirish signalidagi tasodifiy o'zgarishlarga bog'liq emas)
        if (hasT)
        {
            std::vector<float> hv;
            for (int k = 0; k < kNumBands; ++k)
                if (pts[i].usable[(size_t) k] && bandCentre (k) >= 200.0f && bandCentre (k) <= 5000.0f) hv.push_back (pts[i].h[(size_t) k]);
            float overall = 0;
            if (! hv.empty()) { std::nth_element (hv.begin(), hv.begin() + (long) hv.size() / 2, hv.end()); overall = hv[hv.size() / 2]; }
            maxBoost = maxCut = boostHz = cutHz = 0; lfRise = 0; lfN = 0;
            for (int k = 0; k < kNumBands; ++k)
            {
                const bool u = pts[i].usable[(size_t) k] && pts[i].coh[(size_t) k] > 0.3f;
                st.bandDelta[(size_t) k] = u ? pts[i].h[(size_t) k] - overall : 0.0f;
                usable[(size_t) k] = u;
                const float fc = bandCentre (k), d = st.bandDelta[(size_t) k];
                if (! u) continue;
                if (fc >= 40.0f && fc <= 120.0f) { lfRise += d; ++lfN; }
                if (fc < 40.0f || fc > 16000.0f) continue;
                if (d > maxBoost) { maxBoost = d; boostHz = fc; }
                if (d < maxCut)   { maxCut = d;   cutHz = fc; }
            }
            lfRise = lfN > 0 ? lfRise / (float) lfN : 0.0f;
        }

        bool isDyn;
        if (hasT && hasGr) isDyn = pts[i].grCorr <= -0.45f && pts[i].grRange >= 1.5f;       // gain kirish darajasiga bog'liq
        else if (hasGr)    isDyn = pts[i].grRange >= 2.0f && pts[i].grSlope < -0.08f;
        else               isDyn = crestD <= -1.0f || spreadD <= -1.5f;
        // Nochiziqli (saturatsiya / clipper): peaklar kesiladi (crest kamayadi), lekin gain kirish darajasiga
        // bog'liq emas (kompressor emas). Ovoz davriy bo'lgani uchun garmonikalar kogerent — faqat kogerentlikka tayanmaymiz.
        const bool nonLinear = hasT && ! isDyn && (cohAvg < 0.90f || crestD <= -1.5f);
        const float maxEqLocal = juce::jmax (maxBoost, -maxCut);
        const float eqThreshold = hasT ? (isDyn ? 5.0f : 1.0f) : (isDyn ? 3.0f : 1.5f);
        float maxEqMid = 0, midBoost = 0, midCut = 0, midBoostHz = 0, midCutHz = 0;
        for (int k = bandForFreq (120.0f); k <= bandForFreq (12000.0f); ++k)
        {
            const float d = st.bandDelta[(size_t) k];
            maxEqMid = juce::jmax (maxEqMid, std::abs (d));
            if (d > midBoost) { midBoost = d; midBoostHz = bandCentre (k); }
            if (d < midCut)   { midCut = d;   midCutHz = bandCentre (k); }
        }
        const float eqForRole = hasT ? maxEqLocal : (isDyn ? maxEqMid : maxEq);

        // 1/3-oktava guruhlar (konflikt qidirish uchun), 120 Hz dan yuqori
        for (int g = 0; g * 2 + 1 < kNumBands; ++g)
        {
            const float fc = bandCentre (g * 2);
            if (fc < 120.0f || fc > 14000.0f) continue;
            const float d = 0.5f * (st.bandDelta[(size_t) (g * 2)] + st.bandDelta[(size_t) (g * 2 + 1)]);
            if (std::abs (d) >= (isDyn && ! hasT ? 3.0f : 1.5f)) moves.push_back ({ rep.stages.size(), g, d });
        }

        // Vazifani aniqlash
        juce::StringArray roles;
        if (isDyn) roles.add ("Kompressor / limiter");
        if (sibD <= -1.5f && eqForRole < 4.0f) roles.add ("De-esser");
        if (nonLinear) roles.add (crestD <= -1.5f ? "Saturatsiya / clipper" : "Saturatsiya / effekt");
        else if (eqForRole >= eqThreshold) roles.add ("EQ / tembr");
        if (! hasT && st.snrChange < -6.0f && ! isDyn) roles.add ("Effekt (reverb/shovqin)");
        if (roles.isEmpty() && std::abs (st.gainChange) >= 0.5f) roles.add ("Gain");
        const bool noEffect = roles.isEmpty() && std::abs (st.gainChange) < 0.3f && std::abs (crestD) < 0.5f
                              && maxEqLocal < (hasT ? 0.5f : 0.6f) && cohAvg > 0.95f;
        st.role = noEffect ? juce::String ("Ta'siri sezilmadi")
                           : roles.isEmpty() ? juce::String ("Kichik tembr o'zgarishi") : roles.joinIntoString (" + ");
        if (hasT)
            st.confidence = isDyn ? juce::roundToInt (std::abs (pts[i].grCorr) * 100.0f)
                          : nonLinear ? juce::jlimit (60, 95, juce::roundToInt (60.0f + std::abs (crestD) * 8.0f))
                          : juce::roundToInt (cohAvg * 100.0f);

        // Effekt matni: eng muhim 3 ta EQ o'zgarishi (mahalliy cho'qqilar)
        juce::StringArray fx;
        fx.add ("Daraja " + fmtDb (st.gainChange, 1, " LU"));
        if (hasGr && isDyn)
            fx.add ("GR " + juce::String (pts[i].grRange, 1) + " dB, ratio ~" + juce::String (st.effRatio, 1) + ":1");
        if (! nonLinear)   // saturatsiyada H(f) EQ ma'nosiga ega emas
        {
            struct Peak { float hz, db; };
            std::vector<Peak> peaks;
            const int lo = bandForFreq (40.0f), hi = bandForFreq (16000.0f);
            const float minDb = isDyn ? 2.5f : 1.0f;
            for (int k = lo; k <= hi; ++k)
            {
                const float d = st.bandDelta[(size_t) k];
                if (std::abs (d) < minDb || ! usable[(size_t) k]) continue;
                const float prev = k > lo ? st.bandDelta[(size_t) (k - 1)] : 0.0f, next = k < hi ? st.bandDelta[(size_t) (k + 1)] : 0.0f;
                if ((d > 0 && d >= prev && d >= next) || (d < 0 && d <= prev && d <= next)) peaks.push_back ({ bandCentre (k), d });
            }
            std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return std::abs (x.db) > std::abs (y.db); });
            if (peaks.size() > 3) peaks.resize (3);
            std::sort (peaks.begin(), peaks.end(), [] (const Peak& x, const Peak& y) { return x.hz < y.hz; });
            for (auto& pk : peaks) fx.add (fmtHz (pk.hz) + " " + fmtDb (pk.db));
        }
        if (nonLinear) fx.add ("peaklar " + fmtDb (crestD) + " kesildi");
        if (std::abs (sibD) >= 1.0f) fx.add ("Sibilyant " + fmtDb (sibD));
        st.effect = fx.joinIntoString (" | ");

        // Qoidalar (eng muhimlari birinchi)
        int problems = 0, warns = 0;
        std::vector<std::pair<int, juce::String>> advList;
        auto addAdv = [&advList] (int severity, const juce::String& text) { advList.push_back ({ severity, text }); };
        if (hasGr && isDyn && pts[i].grRange >= 8.0f)
        {
            if (pts[i].grRange >= 12.0f) ++problems; else ++warns;
            addAdv (pts[i].grRange >= 12.0f ? 2 : 1, "Gain reduction " + juce::String (pts[i].grRange, 1) + " dB oralig'ida o'zgaradi, samarali ratio ~" + juce::String (st.effRatio, 1)
                     + ":1. Vokal uchun bitta kompressorda 3-6 dB GR va 2-4:1 yetarli - threshold'ni ko'taring, ratio'ni kamaytiring.");
        }
        else if ((! hasT || isDyn) && (crestD <= -7.0f || spreadD <= -7.0f))
        {
            ++problems;
            addAdv (2, "Haddan ortiq siqish: crest " + juce::String (-crestD, 1) + " dB, darajalar tarqalishi " + juce::String (-spreadD, 1) + " dB kamaydi. Gain reduction'ni 3-5 dB ga tushiring, "
                     "attack'ni 10-20 ms, release'ni 80-150 ms qiling yoki ratio'ni 2.5-3:1 ga kamaytiring.");
        }
        else if ((! hasT || isDyn) && (crestD <= -5.0f || spreadD <= -4.5f))
        {
            ++warns;
            addAdv (1, "Siqish kuchli (crest " + juce::String (crestD, 1) + " dB, tarqalish " + juce::String (spreadD, 1) + " dB). Agar oldinda boshqa kompressor bo'lsa, bu yerda 1-2 dB kamroq siqing.");
        }
        if (isDyn && lfRise >= 4.0f)
        {
            ++warns;
            addAdv (1, "Kompressor past chastotali shovqin/rumble'ni " + fmtDb (lfRise) + " ko'tardi (pauzalarda). "
                     "Undan oldingi HPF'ni kuchaytiring (100 Hz, 24 dB/okt) yoki kompressordan oldin gate qo'ying.");
        }
        if (maxEqMid >= 9.0f && ! isDyn)
        {
            ++warns;
            addAdv (1, "Keskin EQ (" + fmtDb (maxEqMid) + "). Katta o'zgarish odatda manba/mikrofon muammosini bildiradi - kichikroq qadamlarda, kengroq Q bilan ishlang.");
        }
        else if (maxBoost >= 4.0f && boostHz >= 2500.0f && boostHz <= 5000.0f)
        {
            ++warns;
            addAdv (1, fmtHz (boostHz) + " da " + fmtDb (maxBoost) + " ko'tarish - vokal achchiq bo'lishi mumkin. Boost'ni 2 dB gacha kamaytiring.");
        }
        if (sibD >= 2.0f)
        {
            ++warns;
            addAdv (1, "Bu bosqich sibilyantni " + fmtDb (sibD) + " kuchaytirdi. Undan keyin de-esser qo'ying yoki yuqori chastota boost'ini kamaytiring.");
        }
        if (! hasT && st.snrChange < -6.0f && ! roles.contains ("Kompressor / limiter"))
        {
            ++warns;
            addAdv (1, "Signal/shovqin nisbati " + juce::String (juce::roundToInt (-st.snrChange)) + " dB yomonlashdi (shovqin yoki hum qo'shildi). Plaginning noise/hum sozlamasini o'chiring.");
        }
        if (crestD >= 5.0f && ! isDyn)
        {
            ++warns;
            addAdv (1, "Bu plagindan keyin peaklar keskinlashdi (crest +" + juce::String (crestD, 1) + " dB). Sababi odatda katta EQ boost'lari "
                       "yoki chiziqli-faza (linear phase) EQ'ning pre-ringing'i: boost'larni kamaytiring yoki minimal-faza rejimiga o'ting.");
        }
        if (i + 1 == pts.size() && b.truePeakMax > 0.0f)
        {
            ++problems;
            addAdv (2, "Chiqish " + fmtDb (b.truePeakMax, 1, " dBTP") + ": signal 0 dBFS dan oshdi (clipping xavfi). "
                       "Shu plaginning output/ceiling'ini kamida " + juce::String (b.truePeakMax + 1.0f, 1) + " dB pasaytiring.");
        }
        if (i + 1 < pts.size() && b.truePeakMax > 0.0f)
        {
            ++warns;
            addAdv (1, "Bu bosqich chiqishi " + fmtDb (b.truePeakMax, 1, " dBTP") + ". DAW floating-point'da saqlab qoladi, lekin keyingi analog emulyatsiya "
                     "(UAD va boshqalar) plagini kirishida overload bo'ladi. Plagin output'ini pasaytiring.");
        }
        auto outOfRange = [] (float rms) { return rms > kNoValue + 1 && (rms > -10.0f || rms < -30.0f); };
        if (outOfRange (a.rmsActive) && (i == 1 || ! outOfRange (pts[i - 2].f.rmsActive)))
        {
            ++warns;
            addAdv (1, "Kirish darajasi " + juce::String (a.rmsActive, 1) + " dBFS RMS. Analog emulyatsiya plaginlari (UAD-2, 1176/LA-2A tipidagi) "
                     "-18 dBFS RMS (= 0 VU) atrofida eng to'g'ri ishlaydi - oldingi bosqich output'i bilan moslang.");
        }

        std::stable_sort (advList.begin(), advList.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });
        juce::StringArray adv;
        for (size_t k = 0; k < advList.size() && k < 3; ++k) adv.add (advList[k].second);
        st.advice = adv.joinIntoString ("\n");
        if (noEffect)
        {
            st.status = Status::Redundant;
            st.score = 50;
            st.advice = "Signalga deyarli ta'sir qilmayapti. Agar maqsadli bo'lmasa, CPU va kechikishni kamaytirish uchun olib tashlang yoki bypass qiling.";
        }
        else
        {
            st.score = juce::jmax (10, 100 - 45 * problems - 20 * warns);
            st.status = problems > 0 ? Status::Problem : (warns > 0 ? Status::Warn : Status::Ideal);
        }
        rep.stages.push_back (st);
    }

    // EQ konflikti: bir bosqich ko'targan polosani keyingisi kesadi (yoki aksincha).
    // Qo'shni polosalar bitta konfliktga birlashtiriladi.
    struct Conflict { float lo = 1e9f, hi = 0, bestHz = 0, bestE = 0, bestL = 0, strength = 0; };
    std::map<std::pair<size_t, size_t>, Conflict> conflicts;
    for (size_t x = 0; x < moves.size(); ++x)
        for (size_t y = x + 1; y < moves.size(); ++y)
        {
            const auto& m1 = moves[x];
            const auto& m2 = moves[y];
            if (m1.stage == m2.stage || m1.group != m2.group || (m1.db > 0) == (m2.db > 0)) continue;
            const auto& me = m1.stage < m2.stage ? m1 : m2;
            const auto& ml = m1.stage < m2.stage ? m2 : m1;
            auto& c = conflicts[{ me.stage, ml.stage }];
            const float hz = bandCentre (m1.group * 2) * std::pow (2.0f, 1.0f / 12.0f);
            const float strength = juce::jmin (std::abs (me.db), std::abs (ml.db));
            c.lo = juce::jmin (c.lo, hz); c.hi = juce::jmax (c.hi, hz);
            if (strength > c.strength) { c.strength = strength; c.bestHz = hz; c.bestE = me.db; c.bestL = ml.db; }
        }
    for (auto& [key, c] : conflicts)
    {
        auto& earlier = rep.stages[key.first];
        auto& later = rep.stages[key.second];
        const bool strong = c.strength >= 2.5f;
        const auto range = c.hi / c.lo > 1.2f ? fmtHz (c.lo) + "-" + fmtHz (c.hi) : fmtHz (c.bestHz);
        const auto msg = "EQ konflikti " + range + ": " + earlier.slot + " " + fmtDb (c.bestE) + ", " + later.slot + " " + fmtDb (c.bestL)
                       + " (" + fmtHz (c.bestHz) + "). Ikki plagin bir-birining ishini bekor qilmoqda - o'zgarishni bittasida qoldiring.";
        later.advice = (later.advice.isEmpty() ? juce::String() : later.advice + "\n") + msg;
        later.score = juce::jmax (10, later.score - (strong ? 45 : 20));
        if (later.status != Status::Problem && later.status != Status::Redundant)
            later.status = strong ? Status::Problem : Status::Warn;
    }

    // De-esser yetarliligi: yakuniy sibilyant etalondan baland bo'lsa
    const auto& lastF = pts.back().f;
    if (lastF.hasEnoughData() && lastF.sibilanceP97 > p.sibMax + 1.0f)
    {
        int deEss = -1;
        for (size_t i = 0; i < rep.stages.size(); ++i)
            if (rep.stages[i].valid && (rep.stages[i].role.contains ("De-esser") || rep.stages[i].name.containsIgnoreCase ("ess")))
                deEss = (int) i;
        const auto over = juce::String (lastF.sibilanceP97 - p.sibMax, 1);
        if (deEss >= 0)
        {
            auto& st = rep.stages[(size_t) deEss];
            st.advice = "De-esser yetarli ishlamayapti: yakuniy sibilyant etalondan " + over + " dB baland, bu bosqich faqat "
                        + fmtDb (st.sibAfter - st.sibBefore) + " o'zgartirdi. Chastotani 5-9 kHz ga, threshold'ni pasaytirib 3-6 dB kamaytirishga sozlang.\n" + st.advice;
            st.advice = st.advice.trimEnd();
            if (st.status == Status::Ideal || st.status == Status::Redundant) st.status = Status::Warn;
            st.score = juce::jmin (st.score, 65);
        }
        else
            rep.chainHint = "Zanjirda de-esser aniqlanmadi, yakuniy sibilyant etalondan " + over + " dB baland - kompressordan keyin de-esser qo'ying.";
    }

    const auto& finalF = pts.back().f;

    // Ketma-ket ortiqcha siqish: yakuniy signal juda tekis (100 ms darajalar tarqalishi < 4 dB)
    if (finalF.hasEnoughData() && finalF.levelSpread > 0 && finalF.levelSpread < 4.0f)
    {
        for (auto& st : rep.stages)
        {
            if (! st.valid || ! st.role.contains ("Kompressor") || st.spreadBefore >= 6.0f) continue;
            st.advice = "Signal bu kompressorga allaqachon tekis kelgan (tarqalish " + juce::String (st.spreadBefore, 1) + " dB), u esa yana "
                        + juce::String (st.spreadBefore - st.spreadAfter, 1) + " dB tekislab, vokalni \"yassi\" qilmoqda (yakuniy tarqalish "
                        + juce::String (finalF.levelSpread, 1) + " dB). Ratio'ni 2-4:1 ga, gain reduction'ni 2-3 dB ga tushiring yoki bu kompressorni olib tashlang.\n" + st.advice;
            st.advice = st.advice.trimEnd();
            const bool severe = finalF.levelSpread < 3.0f;
            if (severe) { st.status = Status::Problem; st.score = juce::jmin (st.score, 40); }
            else if (st.status == Status::Ideal) { st.status = Status::Warn; st.score = juce::jmin (st.score, 70); }
        }
    }

    // Umumiy siqish: yakuniy crest etalondan past bo'lsa — eng ko'p siqqan bosqich asosiy sababchi
    if (finalF.hasEnoughData() && finalF.crest < p.crestMin - 1.0f)
    {
        int worst = -1; float worstD = 0;
        for (size_t i = 0; i < rep.stages.size(); ++i)
            if (rep.stages[i].valid && rep.stages[i].crestAfter - rep.stages[i].crestBefore < worstD)
                { worstD = rep.stages[i].crestAfter - rep.stages[i].crestBefore; worst = (int) i; }
        if (worst >= 0)
        {
            auto& st = rep.stages[(size_t) worst];
            st.advice = "Yakuniy vokal haddan ortiq siqilgan (crest " + juce::String (finalF.crest, 1) + " dB, etalon "
                        + juce::String (p.crestMin, 0) + "-" + juce::String (p.crestMax, 0) + " dB). Asosiy sababchi shu bosqich ("
                        + juce::String (worstD, 1) + " dB). Attack'ni 10-20 ms, gain reduction'ni 3-5 dB qiling.\n" + st.advice;
            st.status = Status::Problem;
            st.score = juce::jmin (st.score, 40);
        }
    }

    // Bitta jumlalik xulosa (yakuniy holatga ko'ra)
    for (auto& st : rep.stages)
    {
        if (! st.valid) { st.verdict = "Tahlil kutilmoqda - trekni ijro eting."; continue; }
        if (st.status == Status::Redundant) { st.verdict = "Signalga deyarli ta'sir qilmayapti."; continue; }
        juce::StringArray keep;
        for (auto& part : juce::StringArray::fromTokens (st.effect, "|", ""))
            if (! part.trim().startsWith ("Daraja") && keep.size() < 3) keep.add (part.trim());
        const juce::String what = keep.isEmpty() ? juce::String() : ": " + keep.joinIntoString (", ");
        const juce::String tail = st.status == Status::Ideal ? " - me'yorda."
                                : st.status == Status::Warn ? " - kichik kamchilik bor."
                                : " - tuzatish kerak.";
        st.verdict = st.role + what + tail;
    }

    double s = 0; int n = 0;
    for (auto& st : rep.stages)
    {
        if (! st.valid) continue;
        s += st.score; ++n;
        switch (st.status)
        {
            case Status::Ideal: ++rep.stIdeal; break;
            case Status::Warn: ++rep.stWarn; break;
            case Status::Problem: ++rep.stProblem; break;
            case Status::Redundant: ++rep.stRedundant; break;
            default: break;
        }
    }
    rep.chainScore = n > 0 ? (int) std::round (s / n) : 0;
}

//==============================================================================
juce::String reportToText (const Report& r, const Profile& p, const DeliveryStandard& std, bool master, const juce::String& track)
{
    juce::String t;
    t << "AZ MONITORING - tahlil hisoboti\n"
      << "Azimxodjayev Music\n"
      << "Sana: " << juce::Time::getCurrentTime().toString (true, true, false, true) << "\n"
      << "Trek: " << (track.isEmpty() ? juce::String ("-") : track) << "\n"
      << "Etalon: " << p.name << (p.custom ? " (maxsus, " + juce::String (p.numReferences) + " ta trekdan)" : juce::String()) << "\n"
      << "Manba: " << (master ? "Master" : "Vokal treki") << " | Standart: " << std.name << "\n\n";

    if (! r.hasData)
        return t + "Ma'lumot yetarli emas: trekni kamida 4 soniya ijro eting.\n";

    t << "UMUMIY BAHO: " << r.score << "/100 - " << r.verdict << "\n"
      << "Ideal: " << r.nIdeal << ", Diqqat: " << r.nWarn << ", Muammo: " << r.nProblem << "\n\n";

    for (auto& c : r.checks)
    {
        t << "[" << statusLabel (c.status) << "] " << c.title << ": " << c.measured << "  (me'yor: " << c.target << ")\n";
        if (c.advice.isNotEmpty()) t << "    -> " << c.advice << "\n";
    }

    if (! r.stages.empty())
    {
        t << "\nZANJIR (har bir plagin): " << r.chainScore << "/100\n";
        for (auto& s : r.stages)
        {
            t << "[" << statusLabel (s.status) << "] " << s.slot << "  " << s.name << "  (" << s.role << ")  " << (s.valid ? juce::String (s.score) : juce::String ("-")) << "\n"
              << "    " << s.effect << "\n";
            if (s.advice.isNotEmpty())
                t << "    -> " << s.advice.replace ("\n", "\n    -> ") << "\n";
        }
    }
    return t;
}

} // namespace az
