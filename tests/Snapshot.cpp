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

namespace
{
    void setParam (juce::AudioPluginInstance& p, const juce::String& name, const juce::String& text)
    {
        for (auto* prm : p.getParameters())
            if (prm->getName (40) == name) { prm->setValueNotifyingHost (prm->getValueForText (text)); return; }
        std::printf ("  ! parametr topilmadi: %s\n", name.toRawUTF8());
    }

    juce::String paramText (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* prm : p.getParameters())
            if (prm->getName (40) == name) return prm->getCurrentValueAsText();
        return "?";
    }

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "[ OK ]" : "[FAIL]", what.toRawUTF8());
    }

    double rmsOf (const juce::AudioBuffer<float>& b)
    {
        double s = 0;
        for (int i = 0; i < b.getNumSamples(); ++i) s += (double) b.getSample (0, i) * b.getSample (0, i);
        return std::sqrt (s / juce::jmax (1, b.getNumSamples()));
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    const double sr = 48000.0;
    const double seconds = 40.0;
    using Co = juce::dsp::IIR::Coefficients<float>;

    // DAW tomoni: xom vokal -> (AAX "EQ3 7-Band" — rack'ga yuklanmaydi) -> AZ Monitoring (rack ichida 4 plagin)
    auto raw = makeVocal (sr, seconds);
    auto dawOut = raw;
    filter (dawOut, Co::makeHighPass (sr, 90.0f, 0.7f));
    filter (dawOut, Co::makeHighPass (sr, 90.0f, 0.7f));

    auto setChoice = [] (AZMonitoringProcessor& p, const char* id, int idx)
    {
        auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id));
        c->setValueNotifyingHost (c->convertTo0to1 ((float) idx));
    };

    FakePlayHead headProbe, headMon;
    AZMonitoringProcessor probe;
    setChoice (probe, "group", 1); setChoice (probe, "mode", 1); setChoice (probe, "slot", 0);
    probe.setPluginLabel ("Kirish (xom vokal)");
    probe.setPlayConfigDetails (2, 2, sr, 512); probe.setPlayHead (&headProbe); probe.prepareToPlay (sr, 512);

    auto mon = std::make_unique<AZMonitoringProcessor>();
    setChoice (*mon, "group", 1);
    if (auto* ar = dynamic_cast<juce::AudioParameterBool*> (mon->apvts.getParameter ("autoreset"))) ar->setValueNotifyingHost (0.0f);
    mon->setPluginLabel ("AZ Monitoring");
    mon->setPlayConfigDetails (2, 2, sr, 512); mon->setPlayHead (&headMon); mon->prepareToPlay (sr, 512);

    // 1) Katalog skanerlash
    auto& cat = mon->getCatalog();
    cat.startScan (true);
    while (cat.isScanning()) pump (100);
    pump (100);
    const auto effects = cat.getEffects();
    std::printf ("Katalog: %d ta effekt (%s)\n", effects.size(), cat.getStatus().toRawUTF8());
    juce::PluginDescription eqDesc, compDesc, satDesc;
    for (auto& d : effects)
    {
        std::printf ("  - %s | %s | %s | toifa: %s\n", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(), d.pluginFormatName.toRawUTF8(),
                     az::PluginCatalog::categoryOf (d).toRawUTF8());
        if (d.name == "AZTest EQ") eqDesc = d;
        if (d.name == "AZTest Comp") compDesc = d;
        if (d.name == "AZTest Sat") satDesc = d;
    }
    check (eqDesc.name.isNotEmpty() && compDesc.name.isNotEmpty(), "Test plaginlari katalogda topildi");
    check (! [&] { for (auto& d : effects) if (d.manufacturerName.contains ("Azimxodjayev")) return true; return false; }(),
           "AZ Monitoring o'zini o'ziga yuklash ro'yxatida yo'q");

    // 2) Rack: EQ (cut) -> Comp (yumshoq) -> Comp (haddan ortiq) -> EQ (konflikt)
    check (mon->addPlugin (eqDesc, -1).isEmpty(), "R1 EQ yuklandi");
    check (mon->addPlugin (compDesc, -1).isEmpty(), "R2 Comp yuklandi");
    check (mon->addPlugin (compDesc, -1).isEmpty(), "R3 Comp yuklandi");
    check (mon->addPlugin (eqDesc, -1).isEmpty(), "R4 EQ yuklandi");
    check (mon->addPlugin (satDesc, -1).isEmpty(), "R5 Saturatsiya yuklandi");
    {
        auto s = mon->rack.getSlots();
        setParam (*s[0]->plugin, "Frequency", "320"); setParam (*s[0]->plugin, "Gain", "-3.5");
        setParam (*s[1]->plugin, "Threshold", "-30"); setParam (*s[1]->plugin, "Ratio", "3");  setParam (*s[1]->plugin, "Attack", "10"); setParam (*s[1]->plugin, "Output", "2");
        setParam (*s[2]->plugin, "Threshold", "-42"); setParam (*s[2]->plugin, "Ratio", "12"); setParam (*s[2]->plugin, "Attack", "0.5");
        setParam (*s[2]->plugin, "Release", "30"); setParam (*s[2]->plugin, "Output", "10");
        setParam (*s[3]->plugin, "Frequency", "320"); setParam (*s[3]->plugin, "Gain", "4");
        setParam (*s[4]->plugin, "Drive", "30");
        for (int i = 0; i < 5; ++i) std::printf ("  R%d %s: %s\n", i + 1, s[(size_t) i]->displayName().toRawUTF8(), s[(size_t) i]->formatLabel().toRawUTF8());
    }
    mon->prepareToPlay (sr, 512);
    check (mon->getLatencySamples() == 512, "PDC: rack kechikishi 2 x 256 = " + juce::String (mon->getLatencySamples()) + " sample DAW ga xabar qilindi");

    // 3) Audio
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const int total = raw.getNumSamples();
    for (int pos = 0; pos < total; pos += 512)
    {
        const int len = juce::jmin (512, total - pos);
        block.setSize (2, len, false, false, true);
        block.copyFrom (0, 0, raw, 0, pos, len); block.copyFrom (1, 0, raw, 0, pos, len);
        headProbe.time = pos / sr; probe.processBlock (block, midi);
        block.copyFrom (0, 0, dawOut, 0, pos, len); block.copyFrom (1, 0, dawOut, 0, pos, len);
        headMon.time = pos / sr; mon->processBlock (block, midi);
        if ((pos / 512) % 90 == 0) juce::Thread::sleep (15);
        if (pos >= (int) (sr * 19) && pos < (int) (sr * 24)) juce::Thread::sleep (3);   // o'zgarish atrofida real vaqtga yaqin
        if ((pos / 512) % 9 == 0)   // ~0.1 s
        {
            if (pos >= (int) (sr * 20) && pos < (int) (sr * 20) + 512 * 9)
                setParam (*mon->rack.getSlots()[3]->plugin, "Gain", "8");   // foydalanuvchi knobni buradi
            juce::Thread::sleep (2);
            mon->rack.pollParameters (true, 0.1);
        }
    }
    juce::Thread::sleep (1500);

    az::Features fin; int pc = 0; juce::StringArray dups;
    auto rep = mon->buildReport (fin, pc, dups);
    std::printf ("\nZanjir: %d/100, %d bosqich\n", rep.chainScore, (int) rep.stages.size());
    for (auto& st : rep.stages)
        std::printf ("  [%s] %-12s %-28s rack=%d  %s\n", az::statusLabel (st.status).toRawUTF8(), st.slot.toRawUTF8(), st.name.toRawUTF8(), st.rackSlot, st.effect.toRawUTF8());
    check (rep.stages.size() == 6, "6 bosqich: 1 DAW (probe orqali) + 5 rack plagini");
    if (rep.stages.size() == 6)
    {
        const auto& r1 = rep.stages[1]; const auto& r2 = rep.stages[2]; const auto& r3 = rep.stages[3];
        const auto& r4 = rep.stages[4]; const auto& r5 = rep.stages[5];
        const float m320 = r1.bandDelta[(size_t) az::bandForFreq (320.0f)];
        check (r1.measuredResponse && std::abs (m320 - (-3.5f)) < 0.8f, "R1 EQ: o'lchangan javob 320 Hz = " + juce::String (m320, 2) + " dB (sozlangan -3.5)");
        check (r1.role == "EQ / tembr" && r1.confidence >= 90, "R1 vazifasi: " + r1.role + " (ishonch " + juce::String (r1.confidence) + "%)");
        check (r2.role.startsWith ("Kompressor"), "R2 vazifasi: " + r2.role + " (ishonch " + juce::String (r2.confidence) + "%)");
        check (r3.role.startsWith ("Kompressor"), "R3 vazifasi: " + r3.role + " (ishonch " + juce::String (r3.confidence) + "%)");
        check (r4.role == "EQ / tembr", "R4 vazifasi: " + r4.role + " (ishonch " + juce::String (r4.confidence) + "%)");
        check (r5.role.contains ("Saturatsiya"), "R5 vazifasi: " + r5.role + " (ishonch " + juce::String (r5.confidence) + "%)");
        check (r4.advice.contains ("konflikt"), "R1/R4 EQ konflikti aniqlandi");
        for (auto& st : rep.stages) std::printf ("  XULOSA %-6s %s\n", st.slot.toRawUTF8(), st.verdict.toRawUTF8());
    }
    {
        auto s = mon->rack.getSlots();
        const auto& ev = s[3]->events;
        check (! ev.empty() && ev.front().param == "Gain", "R4 parametr o'zgarishi yozildi: " + (ev.empty() ? juce::String ("-") : ev.front().param + " " + ev.front().from + " -> " + ev.front().to));
        if (! ev.empty())
            check (ev.front().reaction.contains ("+4") || ev.front().reaction.contains ("+3."), "Reaksiya: " + ev.front().reaction);
    }

    // 4) Tavsiyani qo'llash -> qaytarish
    {
        auto s = mon->rack.getSlots();
        const az::StageResult* comp2 = nullptr;
        for (auto& st : rep.stages) if (st.rackSlot == 2) comp2 = &st;
        if (comp2 != nullptr)
        {
            auto props = az::Rack::proposeFor (*s[2]->plugin, *comp2, fin, mon->currentProfile());
            std::printf ("\nR3 tavsiyalari (%d):\n", (int) props.size());
            for (auto& p : props) std::printf ("  %s: %s -> %s\n", p.name.toRawUTF8(), p.fromText.toRawUTF8(), p.toText.toRawUTF8());
            check (props.size() >= 2, "Haddan ortiq kompressor uchun parametr tavsiyalari tuzildi");
            az::Rack::apply (*s[2]->plugin, props);
            std::printf ("  qo'llangandan keyin: Ratio %s, Attack %s, Threshold %s\n", paramText (*s[2]->plugin, "Ratio").toRawUTF8(),
                         paramText (*s[2]->plugin, "Attack").toRawUTF8(), paramText (*s[2]->plugin, "Threshold").toRawUTF8());
            check (paramText (*s[2]->plugin, "Ratio").startsWith ("4"), "Ratio 4:1 ga o'rnatildi");
            az::Rack::apply (*s[2]->plugin, props, true);
            check (paramText (*s[2]->plugin, "Ratio").startsWith ("12"), "Qaytarish: Ratio yana 12:1");
        }
    }

    // 5) Holatni saqlash / tiklash (DAW loyihasini qayta ochish)
    {
        juce::MemoryBlock mb;
        mon->getStateInformation (mb);
        AZMonitoringProcessor restored;
        restored.setPlayConfigDetails (2, 2, sr, 512);
        restored.prepareToPlay (sr, 512);
        restored.setStateInformation (mb.getData(), (int) mb.getSize());
        auto rs = restored.rack.getSlots();
        check (rs.size() == 5, "Holat tiklandi: " + juce::String ((int) rs.size()) + " ta plagin");
        if (rs.size() == 5 && rs[2]->plugin != nullptr)
            check (paramText (*rs[2]->plugin, "Ratio") == "12.0:1" && paramText (*rs[3]->plugin, "Gain") == "8.0 dB",
                   "Plagin parametrlari tiklandi (R3 Ratio " + paramText (*rs[2]->plugin, "Ratio") + ", R4 Gain " + paramText (*rs[3]->plugin, "Gain") + ")");
        check (restored.getLatencySamples() == 512, "Tiklangandan keyin PDC = " + juce::String (restored.getLatencySamples()));
        restored.rack.closeAllEditors();
    }

    // 6) Delta tinglash: R3 (kompressor) olib tashlagan signal
    {
        auto measure = [&] (int deltaSlot)
        {
            mon->rack.setDelta (deltaSlot);
            juce::AudioBuffer<float> outAll (1, (int) (sr * 4));
            for (int pos = 0; pos < outAll.getNumSamples(); pos += 512)
            {
                const int len = juce::jmin (512, outAll.getNumSamples() - pos);
                block.setSize (2, len, false, false, true);
                block.copyFrom (0, 0, dawOut, 0, (int) (sr * 10) + pos, len); block.copyFrom (1, 0, dawOut, 0, (int) (sr * 10) + pos, len);
                mon->processBlock (block, midi);
                outAll.copyFrom (0, pos, block, 0, 0, len);
            }
            return rmsOf (outAll);
        };
        const double normal = measure (-1), delta = measure (2), deltaEmpty = measure (3);
        std::printf ("\nDelta: oddiy RMS %.4f, R3 delta RMS %.4f, R4 delta RMS %.4f\n", normal, delta, deltaEmpty);
        check (delta > 0.0005 && delta != normal, "Delta tinglash: faqat R3 o'zgartirgan signal chiqdi");
        mon->rack.setDelta (-1);
    }

    // 6b) Rack ichidagi plaginning o'z oynasini ochish / yopish
    {
        mon->rack.openEditor (2);
        pump (300);
        bool found = false;
        for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
            if (auto* c = juce::Desktop::getInstance().getComponent (i); c != nullptr && c->getName().contains ("AZTest Comp")) found = c->isVisible();
        check (found, "R3 plaginining o'z oynasi ochildi");
        mon->rack.closeAllEditors();
        pump (100);
    }

    // 7) Suratlar
    {
        std::unique_ptr<AZMonitoringEditor> ed (dynamic_cast<AZMonitoringEditor*> (mon->createEditor()));
        ed->setSize (1280, 900);
        ed->selectPage (1); pump (400);
        auto sl = mon->rack.getSlots();
        ed->selectRackSlot (sl[3]->id, 0); pump (400);
        save (*ed, outDir.getChildFile ("6_rack.png"));
        ed->selectRackSlot (sl[2]->id, 1); pump (400);
        save (*ed, outDir.getChildFile ("8_spektrogramma.png"));
        ed->selectRackSlot (sl[0]->id, 0); pump (300);
        save (*ed, outDir.getChildFile ("9_eq_egri.png"));
        ed->selectPage (0); pump (300);
        save (*ed, outDir.getChildFile ("1_tahlil.png"));
        ed->selectPage (1); ed->showBrowser (-1); pump (400);
        save (*ed, outDir.getChildFile ("7_plagin_qoshish.png"));
    }

    outDir.getChildFile ("hisobot.txt").replaceWithText (az::reportToText (rep, mon->currentProfile(), mon->currentStandard(), false, "Lead Vox (sintetik)"));
    mon->rack.closeAllEditors();
    mon.reset();
    return 0;
}
