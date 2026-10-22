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
}

AZMonitoringProcessor::~AZMonitoringProcessor()
{
    analysisThread->thread.removeTimeSliceClient (this);
    az::ProbeRegistry::instance().remove (point.get());
}

//==============================================================================
void AZMonitoringProcessor::prepareToPlay (double sampleRate, int)
{
    analyzer.prepare (sampleRate, juce::jmax (1, getTotalNumInputChannels()));
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

    // Kirish = chiqish (signal o'zgartirilmaydi). Ortiqcha chiqish kanallarini tozalaymiz.
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    double t = 0;
    bool timeValid = false, playing = true, playheadKnown = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playheadKnown = true;
            playing = pos->getIsPlaying();
            if (auto s = pos->getTimeInSeconds()) { t = *s; timeValid = true; }
        }

    if (playOnlyParam != nullptr && playOnlyParam->get() && playheadKnown && ! playing)
        return;

    analyzer.pushAudio (buffer.getArrayOfReadPointers(), getTotalNumInputChannels(),
                        buffer.getNumSamples(), t, timeValid);
}

void AZMonitoringProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { processAudio (b); }
void AZMonitoringProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { processAudio (b); }

//==============================================================================
int AZMonitoringProcessor::useTimeSlice()
{
    if (point->resetRequested.exchange (false))
        analyzer.requestReset();

    analyzer.runBackground();

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
    if (! pts.empty())
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
void AZMonitoringProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("label", getPluginLabel(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void AZMonitoringProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const juce::SpinLock::ScopedLockType sl (textLock);
            pluginLabel = apvts.state.getProperty ("label").toString();
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
