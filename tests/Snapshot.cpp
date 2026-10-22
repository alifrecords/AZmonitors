// AZ Snapshot — sintetik vokal + 6 plaginli zanjir orqali AZ Monitoring interfeysini suratga oladi.
// Ishlatish: AZSnapshot <chiqish papkasi>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct FakePlayHead final : juce::AudioPlayHead
{
    double time = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setTimeInSeconds (time);
        return p;
    }
};

// Vokalga o'xshash signal: garmonik manba + formantlar + sillabalar + sibilyantlar + pauzalar + fon shovqini
juce::AudioBuffer<float> makeVocal (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> out (1, n);
    juce::Random rnd (42);

    using IIR = juce::dsp::IIR::Filter<float>;
    using Co = juce::dsp::IIR::Coefficients<float>;
    IIR f1 (Co::makePeakFilter (sr, 650.0f, 2.0f, 4.0f)), f2 (Co::makePeakFilter (sr, 1250.0f, 2.5f, 3.0f)),
        f3 (Co::makePeakFilter (sr, 2800.0f, 3.0f, 2.5f)), lp (Co::makeLowPass (sr, 7000.0f));
    IIR sibHp (Co::makeHighPass (sr, 5200.0f)), sibPk (Co::makePeakFilter (sr, 7000.0f, 1.5f, 6.0f)), rumble (Co::makeLowPass (sr, 60.0f));

    double phase = 0, f0 = 220, f0Target = 220;
    float env = 0, envTarget = 0, sibEnv = 0;
    int segLeft = 0, sibLeft = 0;
    bool voiced = false;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        if (--segLeft <= 0)
        {
            const bool phraseGap = std::fmod (t, 7.0) > 6.0;
            voiced = ! phraseGap && rnd.nextFloat() > 0.18f;
            segLeft = (int) (sr * (voiced ? 0.16 + rnd.nextDouble() * 0.22 : 0.06 + rnd.nextDouble() * 0.12));
            if (phraseGap) segLeft = (int) (sr * 0.2);
            envTarget = voiced ? 0.35f + rnd.nextFloat() * 0.75f : 0.0f;
            f0Target = 110.0 + rnd.nextDouble() * 230.0;
            if (voiced && rnd.nextFloat() < 0.22f) sibLeft = (int) (sr * (0.05 + rnd.nextDouble() * 0.06));
        }
        f0 += (f0Target - f0) * 0.0008;
        env += (envTarget - env) * (envTarget > env ? 0.0025f : 0.0012f);
        sibEnv += ((sibLeft-- > 0 ? 1.0f : 0.0f) - sibEnv) * 0.01f;

        phase += f0 / sr;
        if (phase >= 1.0) phase -= 1.0;
        // Bandlimited "glottal" manba (polyBLEP arra)
        double saw = 2.0 * phase - 1.0;
        const double dt = f0 / sr;
        if (phase < dt) { const double x = phase / dt; saw -= x + x - x * x - 1.0; }
        else if (phase > 1.0 - dt) { const double x = (phase - 1.0) / dt; saw -= x * x + x + x + 1.0; }

        float v = (float) saw * 0.5f;
        v = lp.processSample (f3.processSample (f2.processSample (f1.processSample (v))));
        float sib = sibPk.processSample (sibHp.processSample (rnd.nextFloat() * 2.0f - 1.0f)) * 0.55f;
        float noise = (rnd.nextFloat() * 2.0f - 1.0f) * 0.00025f + rumble.processSample (rnd.nextFloat() * 2.0f - 1.0f) * 0.02f;
        const float breath = sibHp.processSample (0.0f) * 0.0f + (rnd.nextFloat() * 2.0f - 1.0f) * 0.02f;
        out.setSample (0, i, v * env * 0.32f + breath * env + sib * sibEnv * env + noise);
    }
    return out;
}

struct Stage
{
    juce::String label;
    std::function<void (juce::AudioBuffer<float>&, double)> process;
};

void compress (juce::AudioBuffer<float>& b, double sr, float thr, float ratio, float att, float rel, float makeupDb)
{
    juce::dsp::Compressor<float> c;
    c.prepare ({ sr, 512, 1 });
    c.setThreshold (thr); c.setRatio (ratio); c.setAttack (att); c.setRelease (rel);
    const float mk = juce::Decibels::decibelsToGain (makeupDb);
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, c.processSample (0, b.getSample (0, i)) * mk);
}

void filter (juce::AudioBuffer<float>& b, juce::dsp::IIR::Coefficients<float>::Ptr co)
{
    juce::dsp::IIR::Filter<float> f (co);
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, f.processSample (b.getSample (0, i)));
}

void save (juce::Component& c, const juce::File& file)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("saqlandi: %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    const double sr = 48000.0;
    const double seconds = 45.0;
    using Co = juce::dsp::IIR::Coefficients<float>;

    // Zanjir: IN -> A EQ -> B LA-2A -> C Dyn3 Comp (haddan ortiq) -> D De-esser (kuchsiz) -> E EQ (konflikt) -> F Maxim (ta'sirsiz) -> Monitor
    std::vector<Stage> stages {
        { "EQ3 7-Band",       [] (auto& b, double s) { filter (b, Co::makeHighPass (s, 90.0f, 0.7f)); filter (b, Co::makeHighPass (s, 90.0f, 0.7f));
                                                       filter (b, Co::makePeakFilter (s, 320.0f, 1.4f, juce::Decibels::decibelsToGain (-3.5f))); } },
        { "UAD LA-2A",        [] (auto& b, double s) { compress (b, s, -30.0f, 3.0f, 10.0f, 150.0f, 2.0f); } },
        { "Dyn3 Compressor",  [] (auto& b, double s) { compress (b, s, -40.0f, 10.0f, 0.2f, 35.0f, 10.0f); } },
        { "Dyn3 De-Esser",    [] (auto& b, double s) { filter (b, Co::makeHighShelf (s, 7000.0f, 0.7f, juce::Decibels::decibelsToGain (-0.8f))); } },
        { "EQ3 7-Band (2)",   [] (auto& b, double s) { filter (b, Co::makePeakFilter (s, 320.0f, 1.4f, juce::Decibels::decibelsToGain (4.0f)));
                                                       filter (b, Co::makePeakFilter (s, 3500.0f, 1.2f, juce::Decibels::decibelsToGain (2.0f))); } },
        { "Maxim",            [] (auto&, double) {} },
    };

    // Signal har bir nuqtada
    std::vector<juce::AudioBuffer<float>> taps;
    taps.push_back (makeVocal (sr, seconds));
    for (auto& st : stages)
    {
        auto next = taps.back();
        st.process (next, sr);
        taps.push_back (std::move (next));
    }

    // Plagin nusxalari: IN probe, A..F probe'lar va Monitor (hammasi bitta guruhda)
    std::vector<std::unique_ptr<AZMonitoringProcessor>> procs;
    std::vector<std::unique_ptr<FakePlayHead>> heads;
    for (size_t i = 0; i < taps.size(); ++i)
    {
        auto p = std::make_unique<AZMonitoringProcessor>();
        p->setPlayConfigDetails (2, 2, sr, 512);
        const bool isMonitor = i == taps.size() - 1;
        auto setChoice = [&p] (const char* id, int idx)
        {
            auto* c = dynamic_cast<juce::AudioParameterChoice*> (p->apvts.getParameter (id));
            c->setValueNotifyingHost (c->convertTo0to1 ((float) idx));
        };
        setChoice ("group", 1);
        if (! isMonitor)
        {
            setChoice ("mode", 1);
            setChoice ("slot", (int) i);  // 0 = IN, 1 = A ...
            p->setPluginLabel (i == 0 ? juce::String ("Kirish (xom vokal)") : stages[i - 1].label);
        }
        heads.push_back (std::make_unique<FakePlayHead>());
        p->setPlayHead (heads.back().get());
        p->prepareToPlay (sr, 512);
        procs.push_back (std::move (p));
    }

    // Monitor oxirgi probe (E) dan keyingi bo'lakni — "Maxim" ni — o'lchaydi
    procs.back()->setPluginLabel ("Maxim");

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const int total = taps.front().getNumSamples();
    for (int pos = 0; pos < total; pos += 512)
    {
        const int len = juce::jmin (512, total - pos);
        for (size_t k = 0; k < procs.size(); ++k)
        {
            block.setSize (2, len, false, false, true);
            block.copyFrom (0, 0, taps[k], 0, pos, len);
            block.copyFrom (1, 0, taps[k], 0, pos, len);
            heads[k]->time = pos / sr;
            procs[k]->processBlock (block, midi);
        }
        if ((pos / 512) % 90 == 0) juce::Thread::sleep (15);   // fon tahlili ulgursin
    }
    juce::Thread::sleep (1500);

    // Diagnostika: har bir nuqtaning spektri (CSV)
    {
        juce::String csv = "hz";
        std::vector<az::Features> fs;
        for (auto& p : procs) { fs.push_back (p->analyzer.getSnapshot()); csv << "," << p->getPluginLabel(); }
        csv << "\n";
        for (int k = 0; k < az::kNumBands; ++k)
        {
            csv << juce::String (az::bandCentre (k), 0);
            for (auto& f : fs) csv << "," << juce::String (f.bandLevel[(size_t) k], 2);
            csv << "\n";
        }
        outDir.getChildFile ("spektr.csv").replaceWithText (csv);
    }

    auto& monitor = *procs.back();
    {
        std::unique_ptr<AZMonitoringEditor> ed (dynamic_cast<AZMonitoringEditor*> (monitor.createEditor()));
        ed->setSize (1280, 900);
        const char* names[] = { "1_tahlil.png", "2_zanjir.png", "3_hisobot.png", "4_sozlamalar.png" };
        for (int i = 0; i < 4; ++i)
        {
            ed->selectPage (i);
            pump (400);
            save (*ed, outDir.getChildFile (names[i]));
        }
    }
    {
        auto& probe = *procs[2];
        std::unique_ptr<AZMonitoringEditor> ed (dynamic_cast<AZMonitoringEditor*> (probe.createEditor()));
        ed->setSize (1280, 900);
        pump (400);
        save (*ed, outDir.getChildFile ("5_probe.png"));
    }

    // Matnli hisobot
    az::Features f; int pc = 0; juce::StringArray d;
    auto rep = monitor.buildReport (f, pc, d);
    outDir.getChildFile ("hisobot.txt").replaceWithText (az::reportToText (rep, monitor.currentProfile(), monitor.currentStandard(), false, "Lead Vox (sintetik)"));

    procs.clear();
    return 0;
}
