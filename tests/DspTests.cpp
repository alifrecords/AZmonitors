// AZ Monitoring — DSP testlari (EBU Tech 3341 / 3342, ITU-R BS.1770-4 asosida)
#include "dsp/Analyzer.h"
#include "engine/Diagnosis.h"
#include <cstdio>

using namespace az;

static int failures = 0;

static void expectNear (const char* name, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol;
    std::printf ("%s %-58s got %8.3f  want %8.3f  (+-%.2f)\n", ok ? "[ OK ]" : "[FAIL]", name, got, want, tol);
    if (! ok) ++failures;
}

static void expectTrue (const char* name, bool cond, const juce::String& info = {})
{
    std::printf ("%s %-58s %s\n", cond ? "[ OK ]" : "[FAIL]", name, info.toRawUTF8());
    if (! cond) ++failures;
}

static juce::AudioBuffer<float> sine (double sr, double seconds, double hz, double dbfs, int chans = 2, double phase = 0)
{
    juce::AudioBuffer<float> b (chans, (int) (sr * seconds));
    const double amp = std::pow (10.0, dbfs / 20.0);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * i / sr + phase));
        for (int c = 0; c < chans; ++c) b.setSample (c, i, v);
    }
    return b;
}

static void append (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src)
{
    const int old = dst.getNumSamples();
    dst.setSize (src.getNumChannels(), old + src.getNumSamples(), true, true);
    for (int c = 0; c < src.getNumChannels(); ++c)
        dst.copyFrom (c, old, src, c, 0, src.getNumSamples());
}

// Paul Kellet pushti shovqin filtri
static juce::AudioBuffer<float> pink (double sr, double seconds, double dbfs, int seed = 1)
{
    juce::Random r (seed);
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double sumSq = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double w = r.nextDouble() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        b.setSample (0, i, (float) p);
        sumSq += p * p;
    }
    const double g = std::pow (10.0, dbfs / 20.0) / std::sqrt (sumSq / b.getNumSamples());
    b.applyGain ((float) g);
    return b;
}

// Oddiy "vokalga o'xshash" signal: shovqin + amplituda o'zgarishi (sillabalar)
static juce::AudioBuffer<float> syllables (double sr, double seconds, int seed)
{
    auto b = pink (sr, seconds, -20.0, seed);
    juce::Random r (seed + 7);
    double env = 0, target = 0.5;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        if (i % (int) (sr * 0.18) == 0) target = 0.15 + r.nextDouble() * 1.0;
        env += (target - env) * (40.0 / sr);
        b.setSample (0, i, b.getSample (0, i) * (float) env);
    }
    return b;
}

static void applyPeak (juce::AudioBuffer<float>& b, double sr, double hz, double q, double gainDb)
{
    auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, (float) hz, (float) q, juce::Decibels::decibelsToGain ((float) gainDb));
    juce::dsp::IIR::Filter<float> f (c);
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, f.processSample (b.getSample (0, i)));
}

static void applyCompressor (juce::AudioBuffer<float>& b, double sr, float thresholdDb, float ratio)
{
    juce::dsp::Compressor<float> comp;
    comp.prepare ({ sr, 512, 1 });
    comp.setThreshold (thresholdDb); comp.setRatio (ratio); comp.setAttack (0.5f); comp.setRelease (40.0f);
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, comp.processSample (0, b.getSample (0, i)));
}

int main()
{
    juce::ignoreUnused (0);
    std::printf ("AZ Monitoring DSP testlari\n\n");

    // --- EBU Tech 3341, 1-holat: 1 kHz sinus, -23 dBFS, stereo -> -23.0 LUFS (+-0.1)
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto f = Analyzer::analyseBuffer (sine (sr, 20.0, 1000.0, -23.0), sr);
        juce::String n = "3341 #1 integrated @ " + juce::String ((int) sr) + " Hz";
        expectNear (n.toRawUTF8(), f.integrated, -23.0, 0.1);
    }
    {
        auto f = Analyzer::analyseBuffer (sine (48000.0, 20.0, 1000.0, -33.0), 48000.0);
        expectNear ("3341 #2 integrated (-33 dBFS)", f.integrated, -33.0, 0.1);
        expectNear ("3341 momentary (statsionar)", f.momentary, -33.0, 0.1);
        expectNear ("3341 short-term (statsionar)", f.shortTerm, -33.0, 0.1);
    }
    // --- 3341, 3-holat: -36 / -23 / -36 dBFS (10 s / 60 s / 10 s) -> gating -> -23.0
    {
        const double sr = 48000.0;
        auto b = sine (sr, 10.0, 1000.0, -36.0);
        append (b, sine (sr, 60.0, 1000.0, -23.0));
        append (b, sine (sr, 10.0, 1000.0, -36.0));
        auto f = Analyzer::analyseBuffer (b, sr);
        expectNear ("3341 #3 relative gating", f.integrated, -23.0, 0.1);
    }
    // --- 3341, 5-holat: -26 / -20 / -26 dBFS (20 s / 20.1 s / 20 s) -> -23.0
    {
        const double sr = 48000.0;
        auto b = sine (sr, 20.0, 1000.0, -26.0);
        append (b, sine (sr, 20.1, 1000.0, -20.0));
        append (b, sine (sr, 20.0, 1000.0, -26.0));
        auto f = Analyzer::analyseBuffer (b, sr);
        expectNear ("3341 #5 relative gating", f.integrated, -23.0, 0.1);
    }
    // --- EBU Tech 3342 LRA: -20 dBFS 20 s + -30 dBFS 20 s -> 10 LU (+-1)
    {
        const double sr = 48000.0;
        auto b = sine (sr, 20.0, 1000.0, -20.0);
        append (b, sine (sr, 20.0, 1000.0, -30.0));
        auto f = Analyzer::analyseBuffer (b, sr);
        expectNear ("3342 #1 LRA", f.lra, 10.0, 1.0);
    }
    {
        const double sr = 48000.0;
        auto b = sine (sr, 20.0, 1000.0, -20.0);
        append (b, sine (sr, 20.0, 1000.0, -15.0));
        auto f = Analyzer::analyseBuffer (b, sr);
        expectNear ("3342 #2 LRA", f.lra, 5.0, 1.0);
    }

    // --- True peak: fs/4 sinus, 45 daraja faza: namuna peak -3.01 dB, haqiqiy peak 0 dBTP
    for (double sr : { 48000.0, 96000.0 })
    {
        auto f = Analyzer::analyseBuffer (sine (sr, 5.0, sr / 4.0, 0.0, 2, juce::MathConstants<double>::pi / 4.0), sr);
        juce::String n = "True peak, inter-sample (fs/4, 45 deg) @ " + juce::String ((int) sr);
        expectNear (n.toRawUTF8(), f.truePeakMax, 0.0, 0.3);
        juce::String n2 = "  sample peak (o'sha signal) @ " + juce::String ((int) sr);
        expectNear (n2.toRawUTF8(), f.samplePeakMax, -3.01, 0.05);
    }
    {
        auto f = Analyzer::analyseBuffer (sine (48000.0, 5.0, 997.0, -6.0), 48000.0);
        expectNear ("True peak, 997 Hz -6 dBFS", f.truePeakMax, -6.0, 0.2);
        expectNear ("Sample peak, 997 Hz -6 dBFS", f.samplePeakMax, -6.0, 0.05);
        expectNear ("Crest factor, sinus (3.01 dB)", f.crest, 3.01, 0.15);
        expectTrue ("Clip yo'q", f.clippedSamples == 0);
    }

    // --- Spektr: pushti shovqin 1/6-oktava polosalarda tekis bo'lishi kerak
    {
        const double sr = 48000.0;
        auto f = Analyzer::analyseBuffer (pink (sr, 20.0, -20.0), sr);
        float worst = 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float fc = bandCentre (b);
            if (fc < 60.0f || fc > 15000.0f) continue;
            worst = juce::jmax (worst, std::abs (f.bandRel[(size_t) b]));
        }
        expectNear ("Pushti shovqin: 60 Hz-15 kHz notekislik (dB)", worst, 0.0, 1.5);
        expectNear ("Pushti shovqin: RMS faol qism", f.rmsActive, -20.0, 0.5);
    }

    // --- Zanjir: EQ, kompressor va ortiqcha plagin aniqlanishi
    {
        const double sr = 48000.0;
        auto in = syllables (sr, 30.0, 3);
        auto eq = in;  applyPeak (eq, sr, 3000.0, 1.0, 6.0);
        auto cmp = eq; applyCompressor (cmp, sr, -36.0f, 8.0f);
        auto same = cmp;

        std::vector<ChainPoint> pts;
        pts.push_back ({ 0, "IN", "Kirish", Analyzer::analyseBuffer (in, sr) });
        pts.push_back ({ 1, "A", "Test EQ", Analyzer::analyseBuffer (eq, sr) });
        pts.push_back ({ 2, "B", "Test Comp", Analyzer::analyseBuffer (cmp, sr) });
        pts.push_back ({ 11, "OUT", "Bo'sh", Analyzer::analyseBuffer (same, sr) });

        Report rep;
        diagnoseChain (pts, Profiles::builtIns().front(), rep);
        expectTrue ("Zanjir: 3 bosqich", rep.stages.size() == 3, juce::String ((int) rep.stages.size()));
        if (rep.stages.size() == 3)
        {
            const auto& s0 = rep.stages[0];
            const float d3k = s0.bandDelta[(size_t) bandForFreq (3000.0f)];
            expectNear ("EQ bosqichi: 3 kHz o'zgarish (dB)", d3k, 6.0, 1.5);
            expectTrue ("EQ bosqichi vazifasi = EQ", s0.role.contains ("EQ"), s0.role);
            expectTrue ("Kompressor aniqlandi", rep.stages[1].role.contains ("Kompressor"), rep.stages[1].role + " | " + rep.stages[1].effect);
            expectTrue ("Kuchli siqish -> MUAMMO/DIQQAT", rep.stages[1].status == Status::Problem || rep.stages[1].status == Status::Warn,
                        statusLabel (rep.stages[1].status));
            expectTrue ("Bo'sh bosqich -> ORTIQCHA", rep.stages[2].status == Status::Redundant, statusLabel (rep.stages[2].status));
        }

        auto r2 = diagnose (pts.back().f, Profiles::builtIns().front(), Profiles::standards().front(), false);
        expectTrue ("Umumiy tashxis hisoblandi", r2.hasData && ! r2.checks.empty(), juce::String (r2.score) + "/100 " + r2.verdict);
        std::printf ("\n%s\n", reportToText (r2, Profiles::builtIns().front(), Profiles::standards().front(), false, "Test").toRawUTF8());
    }

    // --- Maxsus etalon
    {
        std::vector<Features> refs { Analyzer::analyseBuffer (pink (48000.0, 10.0, -20.0, 5), 48000.0),
                                     Analyzer::analyseBuffer (pink (48000.0, 10.0, -18.0, 6), 48000.0) };
        auto p = Profiles::fromReferences (refs, "Test etalon");
        expectTrue ("Maxsus etalon: 2 ta trek", p.numReferences == 2);
        expectNear ("Maxsus etalon: 1 kHz nishon (pushti = tekis)", p.target[(size_t) bandForFreq (1000.0f)], 0.0, 1.0);
    }

    std::printf ("\n%s: %d ta xato\n", failures == 0 ? "HAMMASI O'TDI" : "XATOLAR BOR", failures);
    return failures == 0 ? 0 : 1;
}
