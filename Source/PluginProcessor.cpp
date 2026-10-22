#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::uint32 nextInstanceId()
    {
        static std::atomic<juce::uint32> counter { 1 };
        return counter++;
    }
}

//==============================================================================
juce::StringArray AZMonitoringProcessor::profileNames()
{
    juce::StringArray names;
    for (auto& p : az::Profiles::builtIns()) names.add (p.name);
    names.add ("Maxsus etalon (o'z treklaringiz)");
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout AZMonitoringProcessor::createLayout()
{
    using Choice = juce::AudioParameterChoice;
    auto noAuto = juce::AudioParameterChoiceAttributes().withAutomatable (false);

    juce::StringArray slots { "IN (zanjir boshi)" };
    for (int i = 1; i <= 10; ++i)
        slots.add ("Slot " + az::ProbeRegistry::slotName (i) + " dan keyin");

    juce::StringArray standards;
    for (auto& s : az::Profiles::standards()) standards.add (s.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Choice> (juce::ParameterID { "mode", 1 }, "Rejim",
                                          juce::StringArray { "Monitor (oxirgi insert)", "Probe (o'lchov nuqtasi)" }, 0, noAuto));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "slot", 1 }, "Probe joyi", slots, 1, noAuto));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "profile", 1 }, "Janr etaloni", profileNames(), 0, noAuto));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "source", 1 }, "Manba",
                                          juce::StringArray { "Vokal treki", "Master" }, 0, noAuto));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "standard", 1 }, "Standart", standards, 0, noAuto));
    layout.add (std::make_unique<Choice> (juce::ParameterID { "group", 1 }, "Guruh",
                                          juce::StringArray { "Avto (trek nomi)", "Guruh 1", "Guruh 2", "Guruh 3", "Guruh 4" }, 0, noAuto));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "playonly", 1 }, "Faqat ijro paytida", true,
                                                            juce::AudioParameterBoolAttributes().withAutomatable (false)));
    return layout;
}

//==============================================================================
AZMonitoringProcessor::AZMonitoringProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AZMonitoring", createLayout())
{
    modeParam     = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("mode"));
    slotParam     = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("slot"));
    profileParam  = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("profile"));
    sourceParam   = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("source"));
    standardParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("standard"));
    groupParam    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("group"));
    playOnlyParam = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("playonly"));

    customProfileLoaded = az::Profiles::loadCustom (customProfile);

    point = std::make_shared<az::SharedPoint>();
    point->instanceId = nextInstanceId();
    point->setText (getGroupKey(), {});
    az::ProbeRegistry::instance().add (point);

    analysisThread->thread.addTimeSliceClient (this);
    startTimer (400);
}

AZMonitoringProcessor::~AZMonitoringProcessor()
{
    stopTimer();
    rack.closeAllEditors();
    analysisThread->thread.removeTimeSliceClient (this);
    az::ProbeRegistry::instance().remove (point.get());
}

//==============================================================================
void AZMonitoringProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = juce::jmax (1, getTotalNumInputChannels());
    maxBlock = juce::jmax (32, samplesPerBlock);
    work.setSize (2, maxBlock);
    analyzer.prepare (sampleRate, ch);
    inputAnalyzer.prepare (sampleRate, ch);
    rack.prepare (sampleRate, maxBlock, ch);
    setLatencySamples (isProbe() ? 0 : rack.computeTotalLatency());
}

bool AZMonitoringProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

template <typename Sample>
void AZMonitoringProcessor::processAudio (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, n);

    double t = 0;
    bool timeValid = false, playing = true, playheadKnown = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playheadKnown = true;
            playing = pos->getIsPlaying();
            if (auto s = pos->getTimeInSeconds()) { t = *s; timeValid = true; }
        }
    const bool analyse = ! (playOnlyParam != nullptr && playOnlyParam->get() && playheadKnown && ! playing);

    // Rack bo'sh yoki Probe rejimi: signal o'zgarmaydi
    if (isProbe() || rack.size() == 0 || numIn == 0 || work.getNumSamples() == 0)
    {
        if (analyse)
            analyzer.pushAudio (buffer.getArrayOfReadPointers(), numIn, n, t, timeValid);
        return;
    }

    // Rack: ichki ishlov har doim stereo float'da
    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - pos);
        const double tt = t + pos / sr;
        for (int c = 0; c < 2; ++c)
        {
            const auto* src = buffer.getReadPointer (juce::jmin (c, numIn - 1), pos);
            auto* dst = work.getWritePointer (c);
            for (int i = 0; i < len; ++i) dst[i] = (float) src[i];
        }
        if (analyse)
            inputAnalyzer.pushAudio (work.getArrayOfReadPointers(), numIn, len, tt, timeValid);

        rack.process (work, numIn, len, tt, timeValid, analyse);

        for (int c = 0; c < numOut; ++c)
        {
            const auto* src = work.getReadPointer (juce::jmin (c, 1));
            auto* dst = buffer.getWritePointer (c, pos);
            for (int i = 0; i < len; ++i) dst[i] = (Sample) src[i];
        }
        if (analyse)
            analyzer.pushAudio (work.getArrayOfReadPointers(), numIn, len, tt, timeValid);
    }
}

void AZMonitoringProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { processAudio (b); }
void AZMonitoringProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { processAudio (b); }

//==============================================================================
int AZMonitoringProcessor::useTimeSlice()
{
    if (point->resetRequested.exchange (false))
    {
        analyzer.requestReset();
        inputAnalyzer.requestReset();
        rack.resetAnalyzers();
    }

    analyzer.runBackground();
    if (rack.size() > 0)
    {
        inputAnalyzer.runBackground();
        for (auto& a : rack.getAnalyzers())
            a->runBackground();
    }

    point->mode = isProbe() ? 1 : 0;
    point->slotIndex = getSlotIndex();

    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPublish > 250)
    {
        lastPublish = now;
        point->setText (getGroupKey(), getPluginLabel());
        point->setFeatures (analyzer.getSnapshot());
    }
    return 40;
}

//==============================================================================
bool AZMonitoringProcessor::isProbe() const          { auto* m = modeParam.load(); return m != nullptr && m->getIndex() == 1; }
int  AZMonitoringProcessor::getSlotIndex() const     { return slotParam != nullptr ? slotParam->getIndex() : 0; }
bool AZMonitoringProcessor::isMasterMode() const     { return sourceParam != nullptr && sourceParam->getIndex() == 1; }

juce::String AZMonitoringProcessor::getTrackName() const
{
    const juce::SpinLock::ScopedLockType sl (textLock);
    return trackName;
}

juce::String AZMonitoringProcessor::getGroupKey() const
{
    const int g = groupParam != nullptr ? groupParam->getIndex() : 0;
    if (g > 0) return "Guruh " + juce::String (g);
    const auto tn = getTrackName();
    return tn.isNotEmpty() ? tn : juce::String ("Guruh 1");
}

juce::String AZMonitoringProcessor::getPluginLabel() const
{
    const juce::SpinLock::ScopedLockType sl (textLock);
    return pluginLabel;
}

void AZMonitoringProcessor::setPluginLabel (const juce::String& s)
{
    {
        const juce::SpinLock::ScopedLockType sl (textLock);
        pluginLabel = s.substring (0, 48);
    }
    apvts.state.setProperty ("label", s.substring (0, 48), nullptr);
}

void AZMonitoringProcessor::updateTrackProperties (const TrackProperties& p)
{
    const juce::SpinLock::ScopedLockType sl (textLock);
    trackName = p.name.value_or (juce::String());
}

const az::Profile& AZMonitoringProcessor::currentProfile() const
{
    const auto& list = az::Profiles::builtIns();
    const int idx = profileParam != nullptr ? profileParam->getIndex() : 0;
    if (idx >= (int) list.size())
        return customProfileLoaded ? customProfile : list.front();
    return list[(size_t) idx];
}

const az::DeliveryStandard& AZMonitoringProcessor::currentStandard() const
{
    const auto& list = az::Profiles::standards();
    const int idx = standardParam != nullptr ? standardParam->getIndex() : 0;
    return list[(size_t) juce::jlimit (0, (int) list.size() - 1, idx)];
}

void AZMonitoringProcessor::setCustomProfile (const az::Profile& p)
{
    customProfile = p;
    customProfileLoaded = true;
    az::Profiles::saveCustom (p);
    if (profileParam != nullptr)
        profileParam->setValueNotifyingHost (profileParam->convertTo0to1 ((float) az::Profiles::builtIns().size()));
}

void AZMonitoringProcessor::resetAnalysis()
{
    az::ProbeRegistry::instance().requestGroupReset (getGroupKey());
    point->resetRequested = true;
}

//==============================================================================
az::Report AZMonitoringProcessor::buildReport (az::Features& finalOut, int& probeCount, juce::StringArray& dups)
{
    finalOut = analyzer.getSnapshot();
    const bool master = isMasterMode();
    auto report = az::diagnose (finalOut, currentProfile(), currentStandard(), master);

    const auto probes = az::ProbeRegistry::instance().probesInGroup (getGroupKey(), &dups);
    probeCount = (int) probes.size();

    std::vector<az::ChainPoint> pts;
    for (auto& p : probes)
    {
        az::ChainPoint cp;
        cp.slotIndex = p.slotIndex;
        cp.slot = az::ProbeRegistry::slotName (p.slotIndex);
        cp.name = p.name.isNotEmpty() ? p.name : (p.slotIndex == 0 ? juce::String ("Kirish") : "Slot " + cp.slot + " plagini");
        cp.f = p.features;
        pts.push_back (std::move (cp));
    }
    const auto slots = rack.getSlots();
    bool anyActive = false;
    for (auto& sl : slots) anyActive = anyActive || (sl->plugin != nullptr && ! sl->bypass.load());

    if (anyActive && ! isProbe())
    {
        az::ChainPoint in;
        in.slotIndex = 12;
        in.slot = "AZ IN";
        in.name = pts.empty() ? juce::String ("Kirish") : juce::String ("DAW insertlari");
        in.f = inputAnalyzer.getSnapshot();
        pts.push_back (std::move (in));
        for (int k = 0; k < (int) slots.size(); ++k)
        {
            const auto& sl = slots[(size_t) k];
            if (sl->plugin == nullptr || sl->bypass.load()) continue;
            az::ChainPoint cp;
            cp.slotIndex = 13 + k;
            cp.slot = "R" + juce::String (k + 1);
            cp.name = sl->displayName();
            cp.f = sl->analyzer->getSnapshot();
            cp.rackSlot = k;
            const auto gs = sl->getGainStats();
            cp.grRange = gs.grRange();
            cp.grSlope = gs.slope();
            pts.push_back (std::move (cp));
        }
    }
    else if (! pts.empty())
    {
        az::ChainPoint out;
        out.slotIndex = 11;
        out.slot = "OUT";
        const auto label = getPluginLabel();
        out.name = label.isNotEmpty() ? label : juce::String ("Oxirgi probe'dan keyingi plaginlar");
        out.f = finalOut;
        pts.push_back (std::move (out));
    }
    az::diagnoseChain (pts, currentProfile(), report);
    return report;
}

//==============================================================================
juce::String AZMonitoringProcessor::addPlugin (const juce::PluginDescription& d, int index)
{
    juce::String err;
    auto inst = rack.createInstance (catalog->getFormats(), d, err);
    if (inst == nullptr)
        return err.isNotEmpty() ? err : juce::String ("Plaginni ochib bo'lmadi");
    rack.insert (std::move (inst), d, index);
    updateLatency();
    resetAnalysis();
    return {};
}

void AZMonitoringProcessor::updateLatency()
{
    const int lat = isProbe() ? 0 : rack.computeTotalLatency();
    if (lat != getLatencySamples())
        setLatencySamples (lat);
}

void AZMonitoringProcessor::timerCallback()
{
    juce::ValueTree pending;
    {
        const juce::ScopedLock sl (pendingLock);
        std::swap (pending, pendingRack);
    }
    if (pending.isValid())
    {
        rack.restoreState (pending, catalog->getFormats());
        resetAnalysis();
    }
    updateLatency();
}

bool AZMonitoringProcessor::saveRackPreset (const juce::File& f) const
{
    if (auto xml = rack.saveState().createXml())
        return xml->writeTo (f);
    return false;
}

juce::String AZMonitoringProcessor::loadRackPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("RACK"))
        return "Fayl AZ Rack preseti emas";
    rack.restoreState (juce::ValueTree::fromXml (*xml), catalog->getFormats());
    updateLatency();
    resetAnalysis();
    return rack.getRestoreErrors();
}

//==============================================================================
void AZMonitoringProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("label", getPluginLabel(), nullptr);
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("RACK"), nullptr);
    state.appendChild (rack.saveState(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void AZMonitoringProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            auto rackTree = state.getChildWithName ("RACK").createCopy();
            state.removeChild (state.getChildWithName ("RACK"), nullptr);
            apvts.replaceState (state);
            {
                const juce::SpinLock::ScopedLockType sl (textLock);
                pluginLabel = apvts.state.getProperty ("label").toString();
            }
            if (rackTree.isValid())
            {
                if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
                     && juce::MessageManager::getInstance()->isThisTheMessageThread())
                {
                    rack.restoreState (rackTree, catalog->getFormats());
                    updateLatency();
                }
                else
                {
                    const juce::ScopedLock sl2 (pendingLock);
                    pendingRack = rackTree;   // xabar oqimida taymer tiklaydi
                }
            }
        }
}

juce::AudioProcessorEditor* AZMonitoringProcessor::createEditor()
{
    return new AZMonitoringEditor (*this);
}

#if ! AZ_SNAPSHOT
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AZMonitoringProcessor();
}
#endif
