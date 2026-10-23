#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::StringArray sourceNames()
{
    juce::StringArray s;
    for (auto& p : mm::sourceProfiles()) s.add (juce::String (p.brand) + " " + p.model);
    s.add ("O'z o'lchovim");
    return s;
}

juce::StringArray targetNames()
{
    juce::StringArray s;
    for (auto& p : mm::targetProfiles()) s.add (juce::String (p.brand) + " " + p.model);
    return s;
}

juce::String dbText (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MonitorMatchProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Manba monitor", sourceNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "targetA", 1 }, "Target A", targetNames(), 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "targetB", 1 }, "Target B", targetNames(), 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "abSlot", 1 }, "A/B", false,
                                                   AudioParameterBoolAttributes().withStringFromValueFunction (
                                                       [] (bool b, int) { return String (b ? "B" : "A"); })));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "amount", 1 }, "Emulyatsiya",
                                                   NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "bass", 1 }, "Bass korreksiya",
                                                   NormalisableRange<float> (0.0f, 100.0f, 0.1f), 85.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "maxBoost", 1 }, "Maks. boost",
                                                   NormalisableRange<float> (0.0f, 12.0f, 0.1f), 6.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "output", 1 }, "Chiqish",
                                                   NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "autoGain", 1 }, "Auto-gain", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Rejim",
                                                   StringArray { "Linear phase", "Zero latency" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

MonitorMatchProcessor::MonitorMatchProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MonitorMatch", createLayout())
{
    pSource   = apvts.getRawParameterValue ("source");
    pTargetA  = apvts.getRawParameterValue ("targetA");
    pTargetB  = apvts.getRawParameterValue ("targetB");
    pSlot     = apvts.getRawParameterValue ("abSlot");
    pAmount   = apvts.getRawParameterValue ("amount");
    pBass     = apvts.getRawParameterValue ("bass");
    pMaxBoost = apvts.getRawParameterValue ("maxBoost");
    pOutput   = apvts.getRawParameterValue ("output");
    pAutoGain = apvts.getRawParameterValue ("autoGain");
    pMode     = apvts.getRawParameterValue ("mode");
    pBypass   = apvts.getRawParameterValue ("bypass");

    for (auto id : { "source", "targetA", "targetB", "abSlot", "amount", "bass", "maxBoost", "autoGain", "mode" })
        apvts.addParameterListener (id, this);

    startTimerHz (20);
}

MonitorMatchProcessor::~MonitorMatchProcessor()
{
    stopTimer();
    for (auto id : { "source", "targetA", "targetB", "abSlot", "amount", "bass", "maxBoost", "autoGain", "mode" })
        apvts.removeParameterListener (id, this);
}

mm::Settings MonitorMatchProcessor::currentSettings() const
{
    mm::Settings s;
    s.source     = juce::roundToInt (pSource->load());
    const bool b = pSlot->load() > 0.5f;
    s.target     = juce::roundToInt ((b ? pTargetB : pTargetA)->load());
    s.amount     = pAmount->load() / 100.0;
    s.bass       = pBass->load() / 100.0;
    s.maxBoostDb = pMaxBoost->load();
    s.autoGain   = pAutoGain->load() > 0.5f;
    s.minPhase   = juce::roundToInt (pMode->load()) == 1;
    return s;
}

double MonitorMatchProcessor::getTailLengthSeconds() const
{
    const double sr = currentSampleRate.load();
    return sr > 0.0 ? (double) mm::firLengthFor (sr) / sr : 0.2;
}

bool MonitorMatchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MonitorMatchProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const auto numCh = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), numCh };

    convolution.prepare (spec);
    dryDelay.prepare (spec);
    dryDelay.reset();
    currentDryDelay = -1;
    dryBuffer.setSize ((int) numCh, juce::jmax (1, samplesPerBlock));

    wetMix.reset (sampleRate, 0.03);
    wetMix.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 0.0f : 1.0f);
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    peaks[0] = peaks[1] = 0.0f;

    prepared = true;
    rebuildImpulse (true);
}

void MonitorMatchProcessor::releaseResources()
{
    convolution.reset();
    dryDelay.reset();
}

void MonitorMatchProcessor::rebuildImpulse (bool force)
{
    const juce::ScopedLock sl (modelLock);
    const double sr = currentSampleRate.load();
    if (sr <= 0.0) return;

    const auto s = currentSettings();
    dirty = false;
    if (! force && hasBuilt && s == lastBuilt && std::abs (sr - lastBuiltSr) < 0.5)
        return;

    const int N = mm::firLengthFor (sr);
    const auto ir = model.buildImpulse (s, sr, N);
    autoGainDb = s.autoGain ? (float) model.gainCompDb (s) : 0.0f;

    juce::AudioBuffer<float> buf (1, N);
    std::copy (ir.begin(), ir.end(), buf.getWritePointer (0));
    convolution.loadImpulseResponse (std::move (buf), sr,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);

    const int lat = s.minPhase ? 0 : N / 2;
    latencyTarget = lat;
    if (getLatencySamples() != lat)
        setLatencySamples (lat);

    lastBuilt = s;
    lastBuiltSr = sr;
    hasBuilt = true;
}

void MonitorMatchProcessor::timerCallback()
{
    if (prepared.load() && dirty.load())
        rebuildImpulse (false);
}

void MonitorMatchProcessor::parameterChanged (const juce::String&, float)
{
    dirty = true;
}

void MonitorMatchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
    if (n == 0) return;

    const int numCh = juce::jmin (buffer.getNumChannels(), juce::jmax (1, numOut), 2);

    const int lat = latencyTarget.load();
    if (lat != currentDryDelay)
    {
        dryDelay.setDelay ((float) lat);
        currentDryDelay = lat;
    }

    if (dryBuffer.getNumChannels() < numCh || dryBuffer.getNumSamples() < n)
        dryBuffer.setSize (numCh, n, false, false, true);

    // Quruq signalni FIR kechikishiga moslab kechiktiramiz (bypass bosilganda "sakrash" bo'lmaydi).
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        float* d = dryBuffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            dryDelay.pushSample (ch, in[i]);
            d[i] = dryDelay.popSample (ch);
        }
    }

    {
        auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) numCh);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    wetMix.setTargetValue (pBypass->load() > 0.5f ? 0.0f : 1.0f);
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));

    float pk[2] = { 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        const float m = wetMix.getNextValue();
        const float g = outGain.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* o = buffer.getWritePointer (ch);
            const float y = dryBuffer.getSample (ch, i) * (1.0f - m) + o[i] * m * g;
            o[i] = y;
            pk[ch] = juce::jmax (pk[ch], std::abs (y));
        }
    }
    if (numCh == 1) pk[1] = pk[0];
    for (int ch = 0; ch < 2; ++ch)
    {
        float prev = peaks[ch].load();
        while (pk[ch] > prev && ! peaks[ch].compare_exchange_weak (prev, pk[ch])) {}
    }
}

juce::AudioProcessorParameter* MonitorMatchProcessor::getBypassParameter() const
{
    return apvts.getParameter ("bypass");
}

void MonitorMatchProcessor::computeDisplay (std::vector<float>& freqs, std::vector<float>& src,
                                            std::vector<float>& tgt, std::vector<float>& corr) const
{
    const juce::ScopedLock sl (modelLock);
    const auto s = currentSettings();
    constexpr int n = 240;
    freqs.resize (n); src.resize (n); tgt.resize (n); corr.resize (n);
    const double comp = s.autoGain ? model.gainCompDb (s) : 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / (n - 1));
        freqs[(size_t) i] = (float) f;
        src[(size_t) i]   = (float) model.sourceDb (s, f);
        tgt[(size_t) i]   = (float) model.targetDb (s, f);
        corr[(size_t) i]  = (float) (model.smoothedCorrectionDb (s, f) + comp);
    }
}

bool MonitorMatchProcessor::loadMeasurementFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile()) { error = "Fayl topilmadi"; return false; }
    auto pts = mm::parseMeasurementText (file.loadFileAsString().toStdString());
    if (pts.empty())
    {
        error = "Faylda \"chastota  dB\" ustunlari topilmadi";
        return false;
    }
    {
        const juce::ScopedLock sl (modelLock);
        model.custom = std::move (pts);
        measurementName = file.getFileNameWithoutExtension();
    }
    if (auto* p = apvts.getParameter ("source"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) customSourceIndex));
        p->endChangeGesture();
    }
    dirty = true;
    return true;
}

juce::String MonitorMatchProcessor::getMeasurementName() const
{
    const juce::ScopedLock sl (modelLock);
    return measurementName;
}

bool MonitorMatchProcessor::hasMeasurement() const
{
    const juce::ScopedLock sl (modelLock);
    return ! model.custom.empty();
}

void MonitorMatchProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return;
    {
        const juce::ScopedLock sl (modelLock);
        if (! model.custom.empty())
        {
            auto* m = xml->createNewChildElement ("Measurement");
            m->setAttribute ("name", measurementName);
            juce::String pts;
            for (auto& p : model.custom)
                pts << juce::String (p.hz, 2) << ":" << juce::String (p.db, 2) << ";";
            m->setAttribute ("points", pts);
        }
    }
    copyXmlToBinary (*xml, dest);
}

void MonitorMatchProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    if (auto* m = xml->getChildByName ("Measurement"))
    {
        std::vector<mm::CurvePoint> pts;
        for (auto& tok : juce::StringArray::fromTokens (m->getStringAttribute ("points"), ";", ""))
        {
            const auto pair = juce::StringArray::fromTokens (tok, ":", "");
            if (pair.size() == 2)
                pts.push_back ({ pair[0].getDoubleValue(), pair[1].getDoubleValue() });
        }
        const juce::ScopedLock sl (modelLock);
        model.custom = std::move (pts);
        measurementName = m->getStringAttribute ("name");
        xml->removeChildElement (m, true);
    }
    else
    {
        const juce::ScopedLock sl (modelLock);
        model.custom.clear();
        measurementName.clear();
    }
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    dirty = true;
}

juce::AudioProcessorEditor* MonitorMatchProcessor::createEditor()
{
    return new MonitorMatchEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MonitorMatchProcessor();
}
