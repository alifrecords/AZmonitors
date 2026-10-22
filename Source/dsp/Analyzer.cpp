#include "Analyzer.h"

namespace az
{
namespace
{
    inline double energyToLufs (double e) noexcept { return e > 0 ? -0.691 + 10.0 * std::log10 (e) : -1000.0; }
    inline double lufsToEnergy (double l) noexcept { return std::pow (10.0, (l + 0.691) / 10.0); }
    inline float  toDb (double lin) noexcept { return lin > 1.0e-12 ? (float) (20.0 * std::log10 (lin)) : kNoValue; }

    // Histogramma bo'yicha persentil (bins, minimal qiymat, qadam)
    float percentile (const std::vector<juce::uint32>& hist, float minVal, float step, double pct, int fromBin = 0)
    {
        juce::uint64 total = 0;
        for (size_t i = (size_t) fromBin; i < hist.size(); ++i) total += hist[i];
        if (total == 0) return kNoValue;

        const double target = pct * (double) (total - 1);
        juce::uint64 acc = 0;
        for (size_t i = (size_t) fromBin; i < hist.size(); ++i)
        {
            acc += hist[i];
            if ((double) acc > target)
                return minVal + ((float) i + 0.5f) * step;
        }
        return minVal + ((float) hist.size() - 0.5f) * step;
    }

    constexpr float kPeakMin = -100.0f, kPeakStep = 0.05f;   constexpr int kPeakBins = 2240;  // -100..+12
    constexpr float kFloorMin = -140.0f, kFloorStep = 0.25f; constexpr int kFloorBins = 560;  // -140..0
    constexpr float kSibMin = -40.0f, kSibStep = 0.1f;       constexpr int kSibBins = 800;    // -40..+40
}

Analyzer::Analyzer()
{
    prepare (48000.0, 2);
}

//==============================================================================
void Analyzer::prepare (double sr, int numChannels)
{
    const juce::ScopedLock sl (bgLock);

    sampleRate = juce::jmax (8000.0, sr);
    numCh = juce::jlimit (1, kMaxChannels, numChannels);
    hopSize = juce::jmax (1, (int) std::round (sampleRate * 0.1));

    // BS.1770: true peak uchun kamida ~192 kHz gacha oversampling
    tpFactor = sampleRate < 96000.0 ? 4 : (sampleRate < 192000.0 ? 2 : 1);

    designKWeighting();
    designTruePeak();

    monoScratch.assign (2048, 0.0f);
    hopBuffer.assign (512, {});
    hopFifo.setTotalSize (512);
    hopFifo.reset();

    const int monoCap = (int) (sampleRate * 3.0);
    monoBuffer.assign ((size_t) monoCap, 0.0f);
    monoFifo.setTotalSize (monoCap);
    monoFifo.reset();

    // FFT: ~85 ms oyna (48 kHz da 4096, 192 kHz da 16384)
    fftSize = juce::nextPowerOfTwo ((int) (sampleRate * 0.0853));
    fftOrder = (int) std::round (std::log2 ((double) fftSize));
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    frame.assign ((size_t) fftSize, 0.0f);
    fftWork.assign ((size_t) fftSize * 2, 0.0f);
    powerSum.assign ((size_t) fftSize / 2 + 1, 0.0);

    const double binHz = sampleRate / fftSize;
    const int maxBin = fftSize / 2;
    for (int k = 0; k < kNumBands; ++k)
    {
        const double fc = bandCentre (k);
        const double lo = fc * std::pow (2.0, -1.0 / 12.0);
        const double hi = fc * std::pow (2.0, 1.0 / 12.0);
        BandRange r;
        r.lo = juce::jlimit (1, maxBin, (int) std::ceil (lo / binHz));
        r.hi = juce::jlimit (0, maxBin, (int) std::floor (hi / binHz));
        r.nearest = juce::jlimit (1, maxBin, (int) std::round (fc / binHz));
        r.nearestScale = (float) ((hi - lo) / binHz);
        if (hi >= sampleRate * 0.5) { r.lo = 1; r.hi = 0; r.nearest = -1; } // Nyquist'dan yuqori
        bandRanges[(size_t) k] = r;
    }

    auto bin = [binHz, maxBin] (double hz) { return juce::jlimit (1, maxBin, (int) std::round (hz / binHz)); };
    sibLo1 = bin (1000.0); sibHi1 = bin (4000.0);
    sibLo2 = bin (5000.0); sibHi2 = bin (juce::jmin (9000.0, sampleRate * 0.45));

    resetAudioState();
    resetBackgroundState();
    resetAudio = false;
    resetBg = false;
}

void Analyzer::designKWeighting()
{
    // ITU-R BS.1770-4 K-filtri, ixtiyoriy namuna chastotasi uchun (analog prototipdan)
    const double fs = sampleRate;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& b : kPre)
        {
            b.b0 = (Vh + Vb * K / Q + K * K) / a0;
            b.b1 = 2.0 * (K * K - Vh) / a0;
            b.b2 = (Vh - Vb * K / Q + K * K) / a0;
            b.a1 = 2.0 * (K * K - 1.0) / a0;
            b.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& b : kRlb)
        {
            b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
            b.a1 = 2.0 * (K * K - 1.0) / a0;
            b.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
}

void Analyzer::designTruePeak()
{
    tpTaps = tpFactor == 4 ? 12 : (tpFactor == 2 ? 24 : 1);
    const int F = tpFactor, T = tpTaps, N = F * T;
    tpCoeffs.assign ((size_t) N, 0.0f);
    if (F == 1) return;

    // Kaiser oynali sinc (beta = 8), kesish chastotasi — asl Nyquist'ning 0.9 qismi
    std::vector<double> h ((size_t) N);
    const double centre = N * 0.5;   // butun son: 0-faza asl namunalarni aynan takrorlaydi, qolganlari 1/F oraliqda
    const double beta = 8.0;
    auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
    const double cutoff = 0.45 / F;  // upsampled tezlikka nisbatan: asl Nyquist'ning 90%
    for (int n = 0; n < N; ++n)
    {
        const double t = n - centre;
        const double x = 2.0 * cutoff * t;
        const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
        const double r = t / centre;
        const double w = bessel0 (beta * std::sqrt (juce::jmax (0.0, 1.0 - r * r))) / bessel0 (beta);  // r = t / centre
        h[(size_t) n] = sinc * w;
    }

    // Har bir fazani DC=1 ga normallashtiramiz; koeffitsientlar tarixdagi tartibga (eskidan yangiga) moslanadi
    for (int p = 0; p < F; ++p)
    {
        double sum = 0;
        for (int k = 0; k < T; ++k) sum += h[(size_t) (p + k * F)];
        for (int k = 0; k < T; ++k)
            tpCoeffs[(size_t) (p * T + (T - 1 - k))] = (float) (h[(size_t) (p + k * F)] / sum);
    }
}

void Analyzer::resetAudioState() noexcept
{
    for (auto& b : kPre) b.clear();
    for (auto& b : kRlb) b.clear();
    for (auto& h : tpHist) h.fill (0.0);
    tpPos = 0; hopCount = 0;
    for (auto& v : hopK) v = 0;
    hopRaw = 0; hopSPeak = 0; hopTPeak = 0; hopClipped = 0;
}

void Analyzer::resetBackgroundState()
{
    recentK.fill (0); recentTP.fill (0);
    recentCount = 0; recentPos = 0; totalHops = 0;
    integEnergy.assign (kHistBins, 0.0);
    integRaw.assign (kHistBins, 0.0);
    integCount.assign (kHistBins, 0);
    stCount.assign (kHistBins, 0);
    stEnergyAbove70 = 0; stCountAbove70 = 0;
    peakHist.assign (kPeakBins, 0);
    floorHist.assign (kFloorBins, 0);
    sibHist.assign (kSibBins, 0);
    tpMax = 0; spMax = 0; clipTotal = 0;
    timeFirst = timeLast = -1;
    momentaryLufs = shortTermLufs = recentTpDb = kNoValue;
    events.clear();
    for (auto& t : lastEventTime) t = -1e9;
    std::fill (powerSum.begin(), powerSum.end(), 0.0);
    powerFrames = 0; frameFill = 0; frameTime = -1;
    liveBands.fill (kNoValue);

    const juce::SpinLock::ScopedLockType sl (snapLock);
    snapshot = Features();
}

//==============================================================================
bool Analyzer::runBackground()
{
    const juce::ScopedTryLock sl (bgLock);
    if (! sl.isLocked())
        return false;

    if (resetBg.exchange (false))
    {
        hopFifo.reset();     // eski ma'lumotni tashlab yuboramiz
        monoFifo.reset();
        resetBackgroundState();
    }

    bool any = false;

    // 1) 100 ms o'lchovlar
    {
        const auto scope = hopFifo.read (hopFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) processHop (hopBuffer[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) processHop (hopBuffer[(size_t) (scope.startIndex2 + i)]);
        any = any || scope.blockSize1 + scope.blockSize2 > 0;
    }

    // 2) Spektr (50% overlap)
    for (;;)
    {
        const int need = fftSize - frameFill;
        const int ready = monoFifo.getNumReady();
        if (ready <= 0) break;
        const int take = juce::jmin (need, ready);
        const auto scope = monoFifo.read (take);
        if (scope.blockSize1 > 0)
            std::copy (monoBuffer.begin() + scope.startIndex1, monoBuffer.begin() + scope.startIndex1 + scope.blockSize1, frame.begin() + frameFill);
        if (scope.blockSize2 > 0)
            std::copy (monoBuffer.begin() + scope.startIndex2, monoBuffer.begin() + scope.startIndex2 + scope.blockSize2, frame.begin() + frameFill + scope.blockSize1);
        frameFill += take;
        any = true;

        if (frameFill >= fftSize)
        {
            processFftFrame();
            const int half = fftSize / 2;
            std::copy (frame.begin() + half, frame.end(), frame.begin());
            frameFill = half;
        }
    }

    if (any)
        computeSnapshot();
    return any;
}

void Analyzer::processHop (const HopMeasure& m)
{
    ++totalHops;
    recentK[(size_t) recentPos] = m.kEnergy;
    recentTP[(size_t) (totalHops % 4)] = m.truePeak;
    recentPos = (recentPos + 1) % kShortHops;
    recentCount = juce::jmin (recentCount + 1, kShortHops);

    tpMax = juce::jmax (tpMax, m.truePeak);
    spMax = juce::jmax (spMax, m.samplePeak);
    clipTotal += m.clipped;

    if (m.time >= 0)
    {
        if (timeFirst < 0 || m.time < timeFirst) timeFirst = m.time;
        timeLast = juce::jmax (timeLast, m.time + 0.1);
    }

    auto addEvent = [this, &m] (int type, float value)
    {
        if (m.time < 0 || events.size() >= 400) return;
        if (m.time - lastEventTime[type] < 0.75) return;
        lastEventTime[type] = m.time;
        events.push_back ({ (float) m.time, type, value });
    };

    // Momentary (400 ms = oxirgi 4 hop) — 75% overlap bilan gating bloki
    if (recentCount >= 4)
    {
        double sum = 0;
        for (int i = 1; i <= 4; ++i)
            sum += recentK[(size_t) ((recentPos - i + kShortHops) % kShortHops)];
        const double e = sum / 4.0;
        momentaryLufs = (float) energyToLufs (e);

        float tp4 = 0;
        for (auto v : recentTP) tp4 = juce::jmax (tp4, v);
        recentTpDb = toDb (tp4);

        if (momentaryLufs > kHistMin)
        {
            const int bin = juce::jlimit (0, kHistBins - 1, (int) ((momentaryLufs - kHistMin) / kHistStep));
            integEnergy[(size_t) bin] += e;
            integCount[(size_t) bin] += 1;
            // faol qism uchun xom RMS (o'sha 400 ms, taxminan: joriy hop)
            integRaw[(size_t) bin] += m.rawEnergy;
        }
    }

    // Short-term (3 s) — LRA uchun
    if (recentCount >= kShortHops)
    {
        double sum = 0;
        for (auto v : recentK) sum += v;
        const double e = sum / kShortHops;
        shortTermLufs = (float) energyToLufs (e);
        if (shortTermLufs > kHistMin)
        {
            const int bin = juce::jlimit (0, kHistBins - 1, (int) ((shortTermLufs - kHistMin) / kHistStep));
            stCount[(size_t) bin] += 1;
            stEnergyAbove70 += e;
            stCountAbove70 += 1;
        }
    }

    // 100 ms peak va RMS histogrammalari
    const float tpDb = toDb (m.truePeak);
    if (tpDb > kPeakMin)
        peakHist[(size_t) juce::jlimit (0, kPeakBins - 1, (int) ((tpDb - kPeakMin) / kPeakStep))] += 1;

    if (m.rawEnergy > 1.0e-14)
    {
        const float rmsDb = (float) (10.0 * std::log10 (m.rawEnergy));
        floorHist[(size_t) juce::jlimit (0, kFloorBins - 1, (int) ((rmsDb - kFloorMin) / kFloorStep))] += 1;
    }

    if (m.clipped > 0)            addEvent (Event::Clip, tpDb);
    else if (tpDb > -1.0f)        addEvent (Event::TruePeakOver, tpDb);
}

void Analyzer::processFftFrame()
{
    // Freym energiyasi (sukutni o'tkazib yuboramiz)
    double frameEnergy = 0;
    for (int i = 0; i < fftSize; ++i)
        frameEnergy += (double) frame[(size_t) i] * frame[(size_t) i];
    const double frameRmsDb = 10.0 * std::log10 (frameEnergy / fftSize + 1.0e-30);

    if (frameTime < 0) frameTime = timeLast;
    frameTime = timeLast;

    if (frameRmsDb < -60.0)
        return;

    for (int i = 0; i < fftSize; ++i)
        fftWork[(size_t) i] = frame[(size_t) i] * window[(size_t) i];
    std::fill (fftWork.begin() + fftSize, fftWork.end(), 0.0f);
    fft->performRealOnlyForwardTransform (fftWork.data(), true);

    const int nb = fftSize / 2;
    // Hann oyna quvvat koeffitsienti (ENBW) — nisbiy taqqoslash uchun doimiy, kompensatsiya shart emas
    for (int k = 0; k <= nb; ++k)
    {
        const double re = fftWork[(size_t) (2 * k)], im = fftWork[(size_t) (2 * k + 1)];
        const double p = (re * re + im * im) / ((double) fftSize * fftSize);
        powerSum[(size_t) k] += p;
        fftWork[(size_t) k] = (float) p;   // freym quvvatini vaqtincha saqlaymiz (re/im endi kerak emas)
    }
    ++powerFrames;

    // Jonli polosalar
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& r = bandRanges[(size_t) b];
        double pw = 0;
        if (r.nearest < 0) { liveBands[(size_t) b] = kNoValue; continue; }
        if (r.hi >= r.lo) for (int k = r.lo; k <= r.hi; ++k) pw += fftWork[(size_t) k];
        else pw = fftWork[(size_t) r.nearest] * r.nearestScale;
        const float db = (float) (10.0 * std::log10 (pw + 1.0e-30));
        auto& lb = liveBands[(size_t) b];
        lb = lb <= kNoValue + 1 ? db : lb + 0.35f * (db - lb);
    }

    // Sibilyant indeksi
    double p1 = 0, p2 = 0;
    for (int k = sibLo1; k <= sibHi1; ++k) p1 += fftWork[(size_t) k];
    for (int k = sibLo2; k <= sibHi2; ++k) p2 += fftWork[(size_t) k];
    if (p1 > 1.0e-20 && frameRmsDb > -50.0)
    {
        const float ratio = (float) (10.0 * std::log10 ((p2 + 1.0e-30) / p1));
        sibHist[(size_t) juce::jlimit (0, kSibBins - 1, (int) ((ratio - kSibMin) / kSibStep))] += 1;

        if (ratio > 6.0f && frameTime >= 0 && events.size() < 400 && frameTime - lastEventTime[Event::Sibilance] > 0.75)
        {
            lastEventTime[Event::Sibilance] = frameTime;
            events.push_back ({ (float) frameTime, Event::Sibilance, ratio });
        }
    }
}

void Analyzer::computeSnapshot()
{
    Features f;
    f.secondsAnalysed = (double) totalHops * 0.1;

    // Integrated loudness (absolyut -70 va nisbiy -10 LU gating)
    double eSum = 0; juce::uint64 n = 0;
    for (int i = 0; i < kHistBins; ++i) { eSum += integEnergy[(size_t) i]; n += integCount[(size_t) i]; }
    if (n > 0)
    {
        const double relGate = energyToLufs (eSum / (double) n) - 10.0;
        const int fromBin = juce::jlimit (0, kHistBins, (int) std::ceil ((relGate - kHistMin) / kHistStep));
        double e2 = 0, r2 = 0; juce::uint64 n2 = 0;
        for (int i = fromBin; i < kHistBins; ++i) { e2 += integEnergy[(size_t) i]; r2 += integRaw[(size_t) i]; n2 += integCount[(size_t) i]; }
        if (n2 > 0)
        {
            f.integrated = (float) energyToLufs (e2 / (double) n2);
            f.rmsActive  = (float) (10.0 * std::log10 (r2 / (double) n2 + 1.0e-30));
            f.secondsActive = (double) n2 * 0.1;
        }
    }

    // LRA (EBU Tech 3342): short-term, abs -70, nisbiy -20 LU, P10..P95
    if (stCountAbove70 > 0)
    {
        const double relGate = energyToLufs (stEnergyAbove70 / (double) stCountAbove70) - 20.0;
        const int fromBin = juce::jlimit (0, kHistBins - 1, (int) std::ceil ((relGate - kHistMin) / kHistStep));
        const float p10 = percentile (stCount, kHistMin, kHistStep, 0.10, fromBin);
        const float p95 = percentile (stCount, kHistMin, kHistStep, 0.95, fromBin);
        if (p10 > kNoValue && p95 > kNoValue) f.lra = juce::jmax (0.0f, p95 - p10);
    }

    f.momentary = momentaryLufs;
    f.shortTerm = recentCount >= kShortHops ? shortTermLufs : kNoValue;
    f.truePeakMax = toDb (tpMax);
    f.truePeakRecent = recentTpDb;
    f.samplePeakMax = toDb (spMax);
    f.clippedSamples = clipTotal;
    f.peakP995 = percentile (peakHist, kPeakMin, kPeakStep, 0.995);
    if (f.peakP995 > kNoValue && f.rmsActive > kNoValue)
        f.crest = f.peakP995 - f.rmsActive;
    f.noiseFloor = percentile (floorHist, kFloorMin, kFloorStep, 0.03);
    f.sibilanceP97 = percentile (sibHist, kSibMin, kSibStep, 0.97);
    f.sibilanceMedian = percentile (sibHist, kSibMin, kSibStep, 0.50);

    // Uzoq muddatli spektr
    if (powerFrames > 0)
    {
        f.spectrumValid = true;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto& r = bandRanges[(size_t) b];
            if (r.nearest < 0) { f.bandLevel[(size_t) b] = kNoValue; continue; }
            double pw = 0;
            if (r.hi >= r.lo) for (int k = r.lo; k <= r.hi; ++k) pw += powerSum[(size_t) k];
            else pw = powerSum[(size_t) r.nearest] * r.nearestScale;
            f.bandLevel[(size_t) b] = (float) (10.0 * std::log10 (pw / (double) powerFrames + 1.0e-30));
        }

        auto normalise = [] (const std::array<float, kNumBands>& src, std::array<float, kNumBands>& dst)
        {
            double s = 0; int c = 0;
            for (int b = 0; b < kNumBands; ++b)
            {
                const float fc = bandCentre (b);
                if (fc >= 200.0f && fc <= 5000.0f && src[(size_t) b] > kNoValue) { s += src[(size_t) b]; ++c; }
            }
            const float ref = c > 0 ? (float) (s / c) : 0.0f;
            for (int b = 0; b < kNumBands; ++b)
                dst[(size_t) b] = src[(size_t) b] > kNoValue ? src[(size_t) b] - ref : kNoValue;
        };
        normalise (f.bandLevel, f.bandRel);
        normalise (liveBands, f.bandLive);
    }

    f.timeFirst = timeFirst;
    f.timeLast = timeLast;
    f.events = events;

    const juce::SpinLock::ScopedLockType sl (snapLock);
    snapshot = std::move (f);
}

Features Analyzer::getSnapshot() const
{
    const juce::SpinLock::ScopedLockType sl (snapLock);
    return snapshot;
}

//==============================================================================
Features Analyzer::analyseBuffer (const juce::AudioBuffer<float>& buffer, double sr)
{
    Analyzer a;
    a.prepare (sr, juce::jmin (2, buffer.getNumChannels()));
    const int total = buffer.getNumSamples();
    const int chans = juce::jmin (2, buffer.getNumChannels());
    const int block = 1024;
    const float* ptrs[2] {};

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        for (int c = 0; c < chans; ++c) ptrs[c] = buffer.getReadPointer (c, pos);
        a.pushAudio (ptrs, chans, n, (double) pos / sr, true);
        if (a.monoFifo.getFreeSpace() < block * 4 || a.hopFifo.getFreeSpace() < 16)
            a.runBackground();
    }
    a.runBackground();
    return a.getSnapshot();
}

} // namespace az
