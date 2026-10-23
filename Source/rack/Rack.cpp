#include "Rack.h"

namespace az
{
//==============================================================================
// XAVFSIZ SKANERLASH: har bir plagin alohida jarayonda (AZPluginScanner) ochiladi.
// Plagin qulasa — faqat yordamchi dastur yopiladi, DAW ishlashda davom etadi.
//==============================================================================
namespace
{
    juce::File findScannerExe()
    {
       #if JUCE_WINDOWS
        const char* name = "AZPluginScanner.exe";
       #else
        const char* name = "AZPluginScanner";
       #endif
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

        // Plagin bundle ichida: .../AZ Monitoring.vst3/Contents/Resources/AZPluginScanner
        for (auto dir = self.getParentDirectory(); dir.exists() && ! dir.isRoot(); dir = dir.getParentDirectory())
            if (dir.getFileName() == "Contents")
            {
                const auto f = dir.getChildFile ("Resources").getChildFile (name);
                if (f.existsAsFile()) return f;
                break;
            }
        if (const auto f = self.getSiblingFile (name); f.existsAsFile()) return f;
        return {};
    }

    class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
    {
    public:
        OutOfProcessScanner (juce::File exeFile, juce::File tempDir) : exe (std::move (exeFile)), tmp (std::move (tempDir)) {}

        bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                                 const juce::String& fileOrIdentifier) override
        {
            if (! exe.existsAsFile())
            {
                format.findAllTypesForFile (result, fileOrIdentifier);   // zaxira: eski usul
                return true;
            }

            tmp.createDirectory();
            const auto out = tmp.getNonexistentChildFile ("scan", ".xml");
            juce::ChildProcess child;
            if (! child.start (juce::StringArray { exe.getFullPathName(), format.getName(), fileOrIdentifier, out.getFullPathName() }, 0))
                return false;

            // Waves kabi ko'p plaginli "shell" fayllar uzoq skanerlanadi — 3 daqiqagacha kutamiz
            const auto started = juce::Time::getMillisecondCounter();
            while (child.isRunning())
            {
                if (shouldExit()) { child.kill(); out.deleteFile(); return true; }
                if (juce::Time::getMillisecondCounter() - started > 180000) { child.kill(); out.deleteFile(); return false; }
                juce::Thread::sleep (40);
            }
            const auto code = child.getExitCode();

            if (auto xml = juce::XmlDocument::parse (out))
                for (auto* e : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*e)) result.add (d.release());
                }
            out.deleteFile();

            // 0 = topildi, 3 = faylda effekt yo'q (xato emas); boshqa kod = plagin qulab tushdi
            return code == 0 || code == 3 || ! result.isEmpty();
        }

    private:
        juce::File exe, tmp;
    };
}

//==============================================================================
// KATALOG
//==============================================================================
PluginCatalog::PluginCatalog() : juce::Thread ("AZ plugin scan")
{
    formats.addDefaultFormats();
    if (auto xml = juce::XmlDocument::parse (listFile()))
        list.recreateFromXml (*xml);
    scannerExe = findScannerExe();
    list.setCustomScanner (std::make_unique<OutOfProcessScanner> (scannerExe, dataDir().getChildFile ("scan_tmp")));
    list.addChangeListener (this);
}

PluginCatalog::~PluginCatalog()
{
    list.removeChangeListener (this);
    stopThread (10000);
}

juce::File PluginCatalog::dataDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Azimxodjayev Music").getChildFile ("AZ Monitoring");
}

juce::File PluginCatalog::listFile() { return dataDir().getChildFile ("plugins.xml"); }

void PluginCatalog::save()
{
    dataDir().createDirectory();
    if (auto xml = list.createXml())
        xml->writeTo (listFile());
}

void PluginCatalog::startScan (bool all)
{
    if (isThreadRunning()) return;
    rescanAll = all;
    progress = 0.0f;
    startThread (juce::Thread::Priority::low);
}

juce::String PluginCatalog::getStatus() const
{
    const juce::SpinLock::ScopedLockType sl (statusLock);
    return status;
}

void PluginCatalog::run()
{
    const auto pedal = dataDir().getChildFile ("scan_crash.txt");
    dataDir().createDirectory();
    // Ro'yxat hech qachon oldindan tozalanmaydi: skanerlash to'xtatilsa ham eski plaginlar saqlanib qoladi
    const bool full = rescanAll.load();
    if (full) list.clearBlacklistedFiles();   // to'liq qayta skanerlashda avval qulagan plaginlarga yana imkon beramiz
    juce::StringArray failed;

    auto setStatus = [this] (const juce::String& s)
    {
        const juce::SpinLock::ScopedLockType sl (statusLock);
        status = s;   // GUI taymer orqali o'qiydi
    };

    const int numFormats = formats.getNumFormats();
    for (int i = 0; i < numFormats && ! threadShouldExit(); ++i)
    {
        auto* format = formats.getFormat (i);
        const auto paths = format->getDefaultLocationsToSearch();
        if (paths.getNumPaths() == 0 && format->getName() != "AudioUnit")
            continue;

        juce::PluginDirectoryScanner scanner (list, *format, paths, true, pedal, false);
        juce::String name;
        for (;;)
        {
            if (threadShouldExit()) break;
            const auto next = scanner.getNextPluginFileThatWillBeScanned();
            setStatus (format->getName() + ": " + juce::File::createFileWithoutCheckingPath (next).getFileNameWithoutExtension());
            if (! scanner.scanNextFile (! full, name)) break;
            progress = ((float) i + scanner.getProgress()) / (float) juce::jmax (1, numFormats);
        }
        for (auto& f : scanner.getFailedFiles())
        {
            list.addToBlacklist (f);   // keyingi skanerlashda vaqt yo'qotmaslik uchun
            failed.add (juce::File::createFileWithoutCheckingPath (f).getFileNameWithoutExtension());
        }
    }

    if (threadShouldExit())
    {
        // DAW yopildi yoki skanerlash to'xtatildi: yarim ro'yxatni diskka yozmaymiz
        setStatus ("Skanerlash to'xtatildi - avvalgi ro'yxat saqlanib qoldi");
        return;
    }

    // To'liq skanerlashdan keyin: kompyuterdan o'chirilgan plaginlarni ro'yxatdan olib tashlaymiz
    if (full)
        for (auto& d : list.getTypes())
            for (int i = 0; i < formats.getNumFormats(); ++i)
                if (auto* f = formats.getFormat (i); f->getName() == d.pluginFormatName && ! f->doesPluginStillExist (d))
                    list.removeType (d);

    save();
    progress = 1.0f;
    { const juce::SpinLock::ScopedLockType sl (statusLock); lastFailed = failed; }
    setStatus (juce::String (getEffects().size()) + " ta effekt plagini topildi"
               + (failed.isEmpty() ? juce::String() : ", " + juce::String (failed.size()) + " ta plagin ochilmadi va o'tkazib yuborildi: "
                                                       + failed.joinIntoString (", ")));
}

juce::Array<juce::PluginDescription> PluginCatalog::getEffects() const
{
    juce::Array<juce::PluginDescription> out;
    for (auto& d : list.getTypes())
    {
        if (d.isInstrument) continue;
        if (d.manufacturerName.containsIgnoreCase ("Azimxodjayev")) continue;   // o'zini o'ziga yuklamaslik
        out.add (d);
    }
    std::sort (out.begin(), out.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
               { return a.name.compareNatural (b.name) < 0; });
    // Bir plaginning VST3 va AU versiyalari bo'lsa — ikkalasi ham ko'rsatiladi (format belgisi bilan)
    return out;
}

bool PluginCatalog::isUad (const juce::PluginDescription& d)
{
    return d.manufacturerName.containsIgnoreCase ("Universal Audio") || d.name.startsWithIgnoreCase ("UAD");
}

juce::String PluginCatalog::categoryOf (const juce::PluginDescription& d)
{
    const auto n = (d.name + " " + d.descriptiveName).toLowerCase();
    const auto c = d.category.toLowerCase();
    auto has = [&n] (std::initializer_list<const char*> words) { for (auto* w : words) if (n.contains (w)) return true; return false; };

    if (has ({ "de-ess", "deess", "de ess", "sibil", "s-ess", "esser" })) return "De-esser";
    if (has ({ "eq", "pultec", "equal", "filter", "pro-q", "nova", "tone" }) || c.contains ("eq") || c.contains ("filter")) return "EQ";
    if (has ({ "comp", "limit", "1176", "la-2", "la2", "la-3", "cla-", "gate", "expander", "dynamic", "fairchild", "vca", "opto", "leveler", "maxim", "dbx" })
        || c.contains ("dynamics")) return "Dinamika";
    if (has ({ "satur", "decapitator", "tape", "distort", "drive", "exciter", "harmonic", "tube", "warm" }) || c.contains ("distortion")) return "Saturatsiya";
    if (has ({ "verb", "delay", "echo", "plate", "room", "hall", "chamber" }) || c.contains ("reverb") || c.contains ("delay")) return "Reverb / Delay";
    if (has ({ "tune", "pitch", "melodyne", "harmon" }) || c.contains ("pitch")) return "Pitch";
    return "Boshqa";
}

//==============================================================================
// SLOT
//==============================================================================
juce::String RackSlot::displayName() const
{
    return desc.name.isNotEmpty() ? desc.name : juce::String ("Noma'lum plagin");
}

juce::String RackSlot::formatLabel() const
{
    juce::String f = desc.pluginFormatName == "AudioUnit" ? juce::String ("AU") : desc.pluginFormatName;
    if (PluginCatalog::isUad (desc)) f << juce::String::fromUTF8 (" · UAD-2");
    if (plugin == nullptr) f << juce::String::fromUTF8 (" · topilmadi");
    return f;
}

//==============================================================================
// PLAGIN OYNASI
//==============================================================================
class Rack::PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& p, std::function<void()> onClose)
        : DocumentWindow (p.getName() + juce::String::fromUTF8 ("  —  AZ Rack"), juce::Colour (0xff181b20), DocumentWindow::closeButton),
          closeFn (std::move (onClose))
    {
       #if JUCE_LINUX
        setUsingNativeTitleBar (false);   // oyna menejerisiz muhitlarda ham ishonchli
       #else
        setUsingNativeTitleBar (true);
       #endif
        if (auto* ed = p.createEditorIfNeeded())
            setContentOwned (ed, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (p), true);
        auto* ed = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent());
        setResizable (ed != nullptr && ed->isResizable(), false);
       #if ! JUCE_LINUX
        setAlwaysOnTop (true);
       #endif
        centreWithSize (juce::jmax (200, getWidth()), juce::jmax (120, getHeight()));
        setVisible (true);
        toFront (true);
    }
    void closeButtonPressed() override { if (closeFn) closeFn(); }
private:
    std::function<void()> closeFn;
};

//==============================================================================
// RACK
//==============================================================================
Rack::Rack() = default;

Rack::~Rack()
{
    closeAllEditors();
}

void Rack::DelayLine::process (juce::AudioBuffer<float>& b, int n, int delay) noexcept
{
    const int size = buf.getNumSamples();
    delay = juce::jlimit (0, size - 1, delay);
    if (delay == 0) return;
    for (int c = 0; c < 2; ++c)
    {
        auto* d = buf.getWritePointer (c);
        auto* x = b.getWritePointer (c);
        int p = pos;
        for (int i = 0; i < n; ++i)
        {
            d[p] = x[i];
            int r = p - delay; if (r < 0) r += size;
            x[i] = d[r];
            if (++p >= size) p = 0;
        }
    }
    pos = (pos + n) % size;
}

void Rack::prepare (double sr, int block, int channels)
{
    sampleRate = sr;
    maxBlock = juce::jmax (32, block);
    numCh = juce::jlimit (1, 2, channels);
    hopSize = juce::jmax (64, (int) (sr * 0.1));
    hopPos = 0;
    preBuf.setSize (2, maxBlock);
    deltaBuf.setSize (2, maxBlock);
    preDelay.prepare ((int) sr);
    deltaDelay.prepare ((int) sr);
    midi.ensureSize (256);

    for (auto& s : getSlots())
    {
        if (s->plugin != nullptr)
        {
            s->plugin->setRateAndBufferSizeDetails (sr, maxBlock);
            s->plugin->prepareToPlay (sr, maxBlock);
            s->latency = s->plugin->getLatencySamples();
        }
        s->analyzer->prepare (sr, numCh);
        s->transfer->prepare (sr);
    }
    monoIn.assign ((size_t) maxBlock, 0.0f);
    monoOut.assign ((size_t) maxBlock, 0.0f);
}

void Rack::release()
{
    for (auto& s : getSlots())
        if (s->plugin != nullptr)
            s->plugin->releaseResources();
}

void Rack::process (juce::AudioBuffer<float>& work, int chForAnalysis, int n, double t, bool tv, bool analyse) noexcept
{
    const juce::SpinLock::ScopedLockType sl (lock);
    const int count = (int) slots.size();
    if (count == 0) return;

    const int ds = deltaSlot.load();
    const bool doDelta = ds >= 0 && ds < count && slots[(size_t) ds]->plugin != nullptr && ! slots[(size_t) ds]->bypass.load();
    if (ds != lastDelta) { preDelay.clear(); deltaDelay.clear(); lastDelta = ds; }
    int downstream = 0;

    juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 2, n);

    for (int k = 0; k < count; ++k)
    {
        auto& s = *slots[(size_t) k];
        const bool byp = s.bypass.load();

        if (doDelta && k == ds)
        {
            for (int c = 0; c < 2; ++c) preBuf.copyFrom (c, 0, view, c, 0, n);
            juce::AudioBuffer<float> pv (preBuf.getArrayOfWritePointers(), 2, n);
            preDelay.process (pv, n, s.latency.load());
        }

        auto energy = [&view, n]
        {
            double e = 0;
            for (int c = 0; c < 2; ++c) { auto* x = view.getReadPointer (c); for (int i = 0; i < n; ++i) e += (double) x[i] * x[i]; }
            return e;
        };
        if (analyse && ! byp)
        {
            s.hopIn += energy();
            const auto* l = view.getReadPointer (0); const auto* r = view.getReadPointer (1);
            for (int i = 0; i < n; ++i) monoIn[(size_t) i] = 0.5f * (l[i] + r[i]);
        }

        if (s.plugin != nullptr)
        {
            midi.clear();
            if (s.monoOnly)
            {
                juce::AudioBuffer<float> mono (view.getArrayOfWritePointers(), 1, n);
                if (byp) s.plugin->processBlockBypassed (mono, midi); else s.plugin->processBlock (mono, midi);
                view.copyFrom (1, 0, view, 0, 0, n);
            }
            else
            {
                if (byp) s.plugin->processBlockBypassed (view, midi); else s.plugin->processBlock (view, midi);
            }
        }

        if (analyse && ! byp)
        {
            s.analyzer->pushAudio (view.getArrayOfReadPointers(), chForAnalysis, n, t, tv);
            s.hopOut += energy();
            const auto* l = view.getReadPointer (0); const auto* r = view.getReadPointer (1);
            for (int i = 0; i < n; ++i) monoOut[(size_t) i] = 0.5f * (l[i] + r[i]);
            s.transfer->push (monoIn.data(), monoOut.data(), n, s.latency.load());
        }

        if (doDelta)
        {
            if (k == ds)
                for (int c = 0; c < 2; ++c)
                {
                    deltaBuf.copyFrom (c, 0, view, c, 0, n);
                    deltaBuf.addFrom (c, 0, preBuf, c, 0, n, -1.0f);
                }
            else if (k > ds)
                downstream += s.latency.load();
        }
    }

    // 100 ms blok tugadi: har bir slot uchun (kirish dB, gain dB) juftini yozamiz
    if (analyse && (hopPos += n) >= hopSize)
    {
        hopPos -= hopSize;
        for (auto& sp : slots)
        {
            auto& s = *sp;
            if (s.statsReset.exchange (false)) s.liveStats = GainStats();
            const double samples = 2.0 * hopSize;
            const float inDb  = (float) (10.0 * std::log10 (s.hopIn / samples + 1.0e-20));
            const float outDb = (float) (10.0 * std::log10 (s.hopOut / samples + 1.0e-20));
            if (inDb > -55.0f && outDb > -90.0f && s.hopIn > 0 && s.hopOut > 0)
            {
                s.liveStats.add (inDb, outDb - inDb);
                const juce::SpinLock::ScopedTryLockType tl (s.statsLock);
                if (tl.isLocked()) s.publishedStats = s.liveStats;
            }
            s.hopIn = s.hopOut = 0;
        }
    }

    if (doDelta)
    {
        juce::AudioBuffer<float> dv (deltaBuf.getArrayOfWritePointers(), 2, n);
        deltaDelay.process (dv, n, downstream);
        for (int c = 0; c < 2; ++c) view.copyFrom (c, 0, dv, c, 0, n);
    }
}

//------------------------------------------------------------------------------
void Rack::setupBuses (juce::AudioPluginInstance& p, bool& monoOnly) const
{
    p.disableNonMainBuses();
    monoOnly = false;
    auto stereo = p.getBusesLayout();
    if (stereo.inputBuses.size() > 0)  stereo.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
    if (stereo.outputBuses.size() > 0) stereo.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (p.setBusesLayout (stereo)) return;

    auto mono = p.getBusesLayout();
    if (mono.inputBuses.size() > 0)  mono.inputBuses.getReference (0)  = juce::AudioChannelSet::mono();
    if (mono.outputBuses.size() > 0) mono.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    if (p.setBusesLayout (mono)) monoOnly = true;
}

std::unique_ptr<juce::AudioPluginInstance> Rack::createInstance (juce::AudioPluginFormatManager& fm, const juce::PluginDescription& d, juce::String& error) const
{
    auto inst = fm.createPluginInstance (d, sampleRate, maxBlock, error);
    return inst;
}

void Rack::swapIn (std::vector<std::shared_ptr<RackSlot>> newSlots)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (slots, newSlots);
        numSlots = (int) slots.size();
    }
    // newSlots endi eski ro'yxat: olib tashlangan plaginlar shu yerda (GUI oqimida) yo'q qilinadi
}

void Rack::insert (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::PluginDescription& d, int index)
{
    auto slot = std::make_shared<RackSlot>();
    slot->id = nextId++;
    slot->desc = d;
    if (inst != nullptr)
    {
        bool mono = false;
        setupBuses (*inst, mono);
        slot->monoOnly = mono;
        inst->setRateAndBufferSizeDetails (sampleRate, maxBlock);
        inst->prepareToPlay (sampleRate, maxBlock);
        slot->latency = inst->getLatencySamples();
        slot->plugin = std::move (inst);
    }
    slot->analyzer->prepare (sampleRate, numCh);
    slot->transfer->prepare (sampleRate);

    auto copy = getSlots();
    index = juce::jlimit (0, (int) copy.size(), index < 0 ? (int) copy.size() : index);
    copy.insert (copy.begin() + index, slot);
    if (deltaSlot.load() >= index) deltaSlot = -1;
    swapIn (std::move (copy));
}

void Rack::remove (int index)
{
    auto copy = getSlots();
    if (index < 0 || index >= (int) copy.size()) return;
    windows.erase (copy[(size_t) index]->id);
    copy.erase (copy.begin() + index);
    deltaSlot = -1;
    swapIn (std::move (copy));
}

void Rack::move (int from, int to)
{
    auto copy = getSlots();
    if (from < 0 || from >= (int) copy.size()) return;
    to = juce::jlimit (0, (int) copy.size() - 1, to);
    if (from == to) return;
    auto s = copy[(size_t) from];
    copy.erase (copy.begin() + from);
    copy.insert (copy.begin() + to, s);
    deltaSlot = -1;
    swapIn (std::move (copy));
    resetAnalyzers();
}

void Rack::setBypass (int index, bool b)
{
    auto copy = getSlots();
    if (index >= 0 && index < (int) copy.size())
        copy[(size_t) index]->bypass = b;
}

std::vector<std::shared_ptr<RackSlot>> Rack::getSlots() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return slots;
}

std::vector<std::shared_ptr<Analyzer>> Rack::getAnalyzers() const
{
    std::vector<std::shared_ptr<Analyzer>> out;
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& s : slots) out.push_back (s->analyzer);
    return out;
}

std::vector<std::shared_ptr<TransferAnalyzer>> Rack::getTransfers() const
{
    std::vector<std::shared_ptr<TransferAnalyzer>> out;
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& s : slots) out.push_back (s->transfer);
    return out;
}

void Rack::pollParameters (bool playing, double dt)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    for (auto& sp : getSlots())
    {
        auto& s = *sp;
        if (s.plugin == nullptr) continue;
        const auto params = s.plugin->getParameters();
        const bool first = (int) s.lastParams.size() != params.size();
        if (first) s.lastParams.assign ((size_t) params.size(), 0.0f);

        const auto tsnap = s.transfer->getSnapshot();
        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            const float v = p->getValue();
            const float old = s.lastParams[(size_t) i];
            s.lastParams[(size_t) i] = v;
            if (first || std::abs (v - old) < 1.0e-4f) continue;
            if (p->getName (40).containsIgnoreCase ("bypass") || p->getName (40).containsIgnoreCase ("meter")) continue;

            // Bir knobni burish ketma-ketligi bitta voqea sifatida yoziladi
            if (! s.events.empty() && s.events.front().param == p->getName (40) && nowMs - s.events.front().lastChangeMs < 2500.0)
            {
                auto& e = s.events.front();
                e.to = p->getCurrentValueAsText();
                e.lastChangeMs = nowMs;
                e.playedSinceChange = 0;
                e.samplesAtChange = s.transfer->getPushedSamples();
                e.reaction.clear();
                continue;
            }
            ParamEvent e;
            e.when = juce::Time::getCurrentTime();
            e.param = p->getName (40);
            e.from = p->getText (old, 32);
            e.to = p->getCurrentValueAsText();
            e.lastChangeMs = nowMs;
            e.samplesAtChange = s.transfer->getPushedSamples();
            if (tsnap.valid) { e.hBefore = tsnap.hFast; e.usableBefore = tsnap.usable; e.hasBefore = true; }
            e.grBefore = s.getGainStats().grRecent();
            s.events.push_front (e);
            while (s.events.size() > 12) s.events.pop_back();
        }

        // Reaksiya: o'zgarishdan keyin ~1.5 s ijro bo'lgach, javob qanchalik o'zgarganini o'lchaymiz
        for (auto& e : s.events)
        {
            if (e.reaction.isNotEmpty()) continue;
            // O'zgarishdan keyin ~1.5 s audio haqiqatda tahlil qilingach (ijro to'xtasa — kutadi)
            juce::ignoreUnused (playing, dt);
            if (! tsnap.valid || e.samplesAtChange < 0) continue;
            if ((double) (tsnap.analysedSamples - e.samplesAtChange) < 1.8 * tsnap.sampleRate) continue;

            juce::StringArray parts;
            if (e.hasBefore)
            {
                float best = 0, bestHz = 0, sum = 0; int cnt = 0;
                std::vector<float> diffs;
                for (int b = bandForFreq (40.0f); b <= bandForFreq (16000.0f); ++b)
                {
                    if (! tsnap.usable[(size_t) b] || ! e.usableBefore[(size_t) b]) continue;
                    const float d = tsnap.hFast[(size_t) b] - e.hBefore[(size_t) b];
                    diffs.push_back (d); sum += d; ++cnt;
                }
                if (cnt > 0)
                {
                    std::sort (diffs.begin(), diffs.end());
                    const float median = diffs[diffs.size() / 2];
                    for (int b = bandForFreq (40.0f); b <= bandForFreq (16000.0f); ++b)
                    {
                        if (! tsnap.usable[(size_t) b] || ! e.usableBefore[(size_t) b]) continue;
                        const float d = tsnap.hFast[(size_t) b] - e.hBefore[(size_t) b] - median;
                        if (std::abs (d) > std::abs (best)) { best = d; bestHz = bandCentre (b); }
                    }
                    if (std::abs (median) >= 0.5f) parts.add ("umumiy daraja " + fmtDb (median));
                    if (std::abs (best) >= 0.7f)   parts.add (fmtHz (bestHz) + " " + fmtDb (best));
                }
            }
            const float grNow = s.getGainStats().grRecent();
            if (e.grBefore >= 0 && grNow >= 0 && std::abs (grNow - e.grBefore) >= 1.0f)
                parts.add ("gain reduction " + juce::String (e.grBefore, 1) + juce::String::fromUTF8 (" → ") + juce::String (grNow, 1) + " dB");
            e.reaction = parts.isEmpty() ? juce::String ("Ovozda sezilarli o'zgarish yo'q (0.5 dB dan kam)")
                                         : "Ovoz reaksiyasi: " + parts.joinIntoString (", ");
        }
    }
}

int Rack::computeTotalLatency() const
{
    int total = 0;
    for (auto& s : getSlots())
    {
        if (s->plugin != nullptr) s->latency = s->plugin->getLatencySamples();
        total += s->latency.load();
    }
    return total;
}

void Rack::resetAnalyzers()
{
    for (auto& s : getSlots())
    {
        s->analyzer->requestReset();
        s->transfer->requestReset();
        s->statsReset = true;
        const juce::SpinLock::ScopedLockType sl (s->statsLock);
        s->publishedStats = GainStats();
    }
}

//------------------------------------------------------------------------------
juce::ValueTree Rack::saveState() const
{
    juce::ValueTree rack ("RACK");
    for (auto& s : getSlots())
    {
        juce::ValueTree st ("SLOT");
        st.setProperty ("bypass", s->bypass.load(), nullptr);
        if (auto xml = s->desc.createXml())
            st.setProperty ("desc", xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);
        juce::MemoryBlock mb;
        if (s->plugin != nullptr) s->plugin->getStateInformation (mb);
        else mb = s->savedState;
        st.setProperty ("state", mb.toBase64Encoding(), nullptr);
        rack.appendChild (st, nullptr);
    }
    return rack;
}

void Rack::restoreState (const juce::ValueTree& rack, juce::AudioPluginFormatManager& fm)
{
    closeAllEditors();
    restoreErrors.clear();
    std::vector<std::shared_ptr<RackSlot>> fresh;

    for (auto st : rack)
    {
        if (! st.hasType ("SLOT")) continue;
        auto slot = std::make_shared<RackSlot>();
        slot->id = nextId++;
        if (auto xml = juce::parseXML (st.getProperty ("desc").toString()))
            slot->desc.loadFromXml (*xml);
        slot->bypass = (bool) st.getProperty ("bypass");
        juce::MemoryBlock mb;
        mb.fromBase64Encoding (st.getProperty ("state").toString());

        juce::String err;
        auto inst = createInstance (fm, slot->desc, err);
        if (inst != nullptr)
        {
            bool mono = false;
            setupBuses (*inst, mono);
            slot->monoOnly = mono;
            inst->setRateAndBufferSizeDetails (sampleRate, maxBlock);
            if (mb.getSize() > 0) inst->setStateInformation (mb.getData(), (int) mb.getSize());
            inst->prepareToPlay (sampleRate, maxBlock);
            slot->latency = inst->getLatencySamples();
            slot->plugin = std::move (inst);
        }
        else
        {
            slot->savedState = mb;   // keyingi saqlashda yo'qolmaydi
            restoreErrors << slot->displayName() << ": " << (err.isNotEmpty() ? err : juce::String ("topilmadi")) << "\n";
        }
        slot->analyzer->prepare (sampleRate, numCh);
        slot->transfer->prepare (sampleRate);
        fresh.push_back (slot);
    }
    deltaSlot = -1;
    swapIn (std::move (fresh));
}

void Rack::openEditor (int index)
{
    auto copy = getSlots();
    if (index < 0 || index >= (int) copy.size() || copy[(size_t) index]->plugin == nullptr) return;
    const auto id = copy[(size_t) index]->id;
    if (auto it = windows.find (id); it != windows.end())
    {
        it->second->toFront (true);
        return;
    }
    windows[id] = std::make_unique<PluginWindow> (*copy[(size_t) index]->plugin, [this, id]
    {
        juce::MessageManager::callAsync ([this, id] { windows.erase (id); });
    });
}

void Rack::closeAllEditors()
{
    windows.clear();
}

//==============================================================================
// PARAMETRLAR VA TAVSIYALAR
//==============================================================================
namespace
{
    bool nameHas (const juce::String& n, std::initializer_list<const char*> words)
    {
        const auto l = n.toLowerCase();
        for (auto* w : words) if (l.contains (w)) return true;
        return false;
    }

    // "12:1", "4.0 : 1", "-18.5 dB", "15 ms" — birinchi sonni ajratib oladi
    bool parseNumber (const juce::String& text, double& out)
    {
        auto t = text.trim();
        int i = 0;
        while (i < t.length() && ! (juce::CharacterFunctions::isDigit (t[i]) || ((t[i] == '-' || t[i] == '+' || t[i] == '.') && i + 1 < t.length() && juce::CharacterFunctions::isDigit (t[i + 1]))))
            ++i;
        if (i >= t.length()) return false;
        out = t.substring (i).getDoubleValue();
        return true;
    }

    /** Parametrni matn orqali o'rnatadi va natijani tekshiradi (plagin shu qiymatni tanidimi?) */
    bool makeProposal (juce::AudioProcessorParameter& p, int index, double target, const juce::String& suffix, ParamProposal& out)
    {
        const auto candidates = { juce::String (target, target == std::floor (target) ? 0 : 1) + suffix,
                                  juce::String (target, 1) + suffix,
                                  juce::String (target, 1),
                                  juce::String (target, 0) + ":1" };
        for (auto& c : candidates)
        {
            const float v = p.getValueForText (c);
            const auto back = p.getText (v, 64);
            double got = 0;
            if (parseNumber (back, got) && std::abs (got - target) <= juce::jmax (0.6, std::abs (target) * 0.12))
            {
                out.paramIndex = index;
                out.name = p.getName (40);
                out.fromText = p.getCurrentValueAsText();
                out.fromValue = p.getValue();
                out.toValue = v;
                out.toText = back;
                return std::abs (v - p.getValue()) > 1.0e-4f;
            }
        }
        return false;
    }
}

std::vector<Rack::ParamRow> Rack::keyParameters (juce::AudioPluginInstance& inst, int maxRows)
{
    std::vector<ParamRow> key, rest;
    for (auto* p : inst.getParameters())
    {
        if (p == nullptr || ! p->isAutomatable()) continue;
        const auto n = p->getName (40);
        if (n.isEmpty() || nameHas (n, { "bypass", "midi cc", "program" })) continue;
        ParamRow r { n, p->getCurrentValueAsText() + (p->getLabel().isNotEmpty() && ! p->getCurrentValueAsText().containsIgnoreCase (p->getLabel()) ? " " + p->getLabel() : juce::String()) };
        if (nameHas (n, { "thresh", "ratio", "attack", "release", "input", "output", "gain", "makeup", "knee", "mix", "freq", "range", "ceiling", "depth", "drive", "q " }))
            key.push_back (r);
        else
            rest.push_back (r);
    }
    for (auto& r : rest) { if ((int) key.size() >= maxRows) break; key.push_back (r); }
    if ((int) key.size() > maxRows) key.resize ((size_t) maxRows);
    return key;
}

std::vector<ParamProposal> Rack::proposeFor (juce::AudioPluginInstance& inst, const StageResult& st, const Features& finalOut, const Profile& prof)
{
    std::vector<ParamProposal> out;
    const auto params = inst.getParameters();
    const bool compressor = st.role.contains ("Kompressor");
    const float crestDrop = st.crestBefore - st.crestAfter;
    const float spreadDrop = st.spreadBefore - st.spreadAfter;
    const bool overCompressed = compressor && (st.grRange >= 8.0f || crestDrop >= 5.0f || spreadDrop >= 4.5f || (st.spreadAfter > 0 && st.spreadAfter < 4.0f) || (finalOut.crest > 0 && finalOut.crest < prof.crestMin - 1.0f));
    const bool deEsser = st.role.contains ("De-esser") || st.name.containsIgnoreCase ("ess");
    const bool sibilant = finalOut.sibilanceP97 > prof.sibMax + 1.0f;

    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        if (p == nullptr || ! p->isAutomatable()) continue;
        const auto n = p->getName (40);
        const auto txt = p->getCurrentValueAsText() + " " + p->getLabel();
        double cur = 0;
        if (! parseNumber (p->getCurrentValueAsText(), cur)) continue;
        ParamProposal pr;

        if (overCompressed)
        {
            if (nameHas (n, { "ratio" }) && cur > 4.5)
            { if (makeProposal (*p, i, 4.0, "", pr)) out.push_back (pr); continue; }
            if (nameHas (n, { "attack" }) && txt.containsIgnoreCase ("ms") && cur < 8.0)
            { if (makeProposal (*p, i, 15.0, " ms", pr)) out.push_back (pr); continue; }
            if (nameHas (n, { "release" }) && txt.containsIgnoreCase ("ms") && cur < 60.0)
            { if (makeProposal (*p, i, 100.0, " ms", pr)) out.push_back (pr); continue; }
            if (nameHas (n, { "thresh" }) && txt.containsIgnoreCase ("db"))
            { if (makeProposal (*p, i, cur + 4.0, " dB", pr)) out.push_back (pr); continue; }
        }
        if (deEsser && sibilant && nameHas (n, { "thresh" }) && txt.containsIgnoreCase ("db"))
        { if (makeProposal (*p, i, cur - 3.0, " dB", pr)) out.push_back (pr); continue; }
        if (st.tpOut > 0.0f && nameHas (n, { "output", "out gain", "makeup", "ceiling" }) && txt.containsIgnoreCase ("db"))
        { if (makeProposal (*p, i, cur - std::ceil (st.tpOut + 1.0f), " dB", pr)) out.push_back (pr); continue; }
    }
    return out;
}

void Rack::apply (juce::AudioPluginInstance& inst, const std::vector<ParamProposal>& props, bool undo)
{
    const auto params = inst.getParameters();
    for (auto& pr : props)
        if (pr.paramIndex >= 0 && pr.paramIndex < params.size())
            params[pr.paramIndex]->setValueNotifyingHost (undo ? pr.fromValue : pr.toValue);
}

} // namespace az
