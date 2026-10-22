// Rack'ni sinash uchun ikkita oddiy VST3 plagin: AZTest Comp va AZTest EQ (256 sample kechikish bilan)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace
{
juce::AudioParameterFloatAttributes unit (const char* suffix, int decimals)
{
    const juce::String sfx (suffix);
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([sfx, decimals] (float v, int) { return juce::String (v, decimals) + sfx; })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue(); });
}

class TestProcessor : public juce::AudioProcessor
{
public:
    TestProcessor() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                                      .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == l.getMainOutputChannelSet() && ! l.getMainInputChannelSet().isDisabled();
    }
    void getStateInformation (juce::MemoryBlock& mb) override
    {
        juce::XmlElement x ("S");
        for (auto* p : getParameters()) x.setAttribute (dynamic_cast<juce::AudioProcessorParameterWithID*> (p)->paramID, p->getValue());
        copyXmlToBinary (x, mb);
    }
    void setStateInformation (const void* d, int n) override
    {
        if (auto x = getXmlFromBinary (d, n))
            for (auto* p : getParameters())
                p->setValueNotifyingHost ((float) x->getDoubleAttribute (dynamic_cast<juce::AudioProcessorParameterWithID*> (p)->paramID, p->getValue()));
    }
};

#if AZTEST_CRASH
// Yuklanishi bilan qulaydigan plagin: xavfsiz skanerlashni sinash uchun
struct CrashOnLoad { CrashOnLoad() { if (std::getenv ("AZ_NO_CRASH") == nullptr) { volatile int* p = nullptr; *p = 42; } } };
static CrashOnLoad crashOnLoad;
#endif

#if AZTEST_COMP
class Plugin final : public TestProcessor
{
public:
    Plugin()
    {
        addParameter (thr = new juce::AudioParameterFloat ({ "thr", 1 }, "Threshold", juce::NormalisableRange<float> (-60, 0, 0.1f), -18, unit (" dB", 1)));
        addParameter (ratio = new juce::AudioParameterFloat ({ "ratio", 1 }, "Ratio", juce::NormalisableRange<float> (1, 20, 0.1f), 4, unit (":1", 1)));
        addParameter (att = new juce::AudioParameterFloat ({ "att", 1 }, "Attack", juce::NormalisableRange<float> (0.1f, 100, 0.1f, 0.4f), 10, unit (" ms", 1)));
        addParameter (rel = new juce::AudioParameterFloat ({ "rel", 1 }, "Release", juce::NormalisableRange<float> (10, 1000, 1, 0.4f), 100, unit (" ms", 0)));
        addParameter (out = new juce::AudioParameterFloat ({ "out", 1 }, "Output", juce::NormalisableRange<float> (-24, 24, 0.1f), 0, unit (" dB", 1)));
    }
    void prepareToPlay (double sr, int bs) override { comp.prepare ({ sr, (juce::uint32) bs, 2 }); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        comp.setThreshold (*thr); comp.setRatio (*ratio); comp.setAttack (*att); comp.setRelease (*rel);
        juce::dsp::AudioBlock<float> blk (b);
        comp.process (juce::dsp::ProcessContextReplacing<float> (blk));
        b.applyGain (juce::Decibels::decibelsToGain (out->get()));
    }
private:
    juce::AudioParameterFloat *thr, *ratio, *att, *rel, *out;
    juce::dsp::Compressor<float> comp;
};
#else  // EQ (va Crash)
class Plugin final : public TestProcessor
{
public:
    Plugin()
    {
        addParameter (freq = new juce::AudioParameterFloat ({ "freq", 1 }, "Frequency", juce::NormalisableRange<float> (20, 20000, 1, 0.25f), 1000, unit (" Hz", 0)));
        addParameter (gain = new juce::AudioParameterFloat ({ "gain", 1 }, "Gain", juce::NormalisableRange<float> (-18, 18, 0.1f), 0, unit (" dB", 1)));
        addParameter (q = new juce::AudioParameterFloat ({ "q", 1 }, "Q", juce::NormalisableRange<float> (0.1f, 10, 0.01f), 1.4f, unit ("", 2)));
        setLatencySamples (256);   // "lookahead" — PDC sinovi uchun
    }
    void prepareToPlay (double sr, int) override
    {
        rate = sr;
        for (auto& f : filt) f.reset();
        delay.setSize (2, 256); delay.clear(); pos = 0;
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (rate, *freq, *q, juce::Decibels::decibelsToGain (gain->get()));
        for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
        {
            filt[ch].coefficients = c;
            auto* x = b.getWritePointer (ch);
            auto* d = delay.getWritePointer (ch);
            int p = pos;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float y = filt[ch].processSample (x[i]);
                x[i] = d[p]; d[p] = y;
                if (++p >= 256) p = 0;
            }
        }
        pos = (pos + b.getNumSamples()) % 256;
    }
private:
    juce::AudioParameterFloat *freq, *gain, *q;
    juce::dsp::IIR::Filter<float> filt[2];
    juce::AudioBuffer<float> delay;
    int pos = 0;
    double rate = 48000;
};
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Plugin(); }
