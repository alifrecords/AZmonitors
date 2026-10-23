#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "CorrectionEngine.h"

class MonitorMatchProcessor : public juce::AudioProcessor,
                              private juce::Timer,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    MonitorMatchProcessor();
    ~MonitorMatchProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // --- Interfeys uchun ---
    mm::Settings currentSettings() const;
    void computeDisplay (std::vector<float>& freqs, std::vector<float>& src,
                         std::vector<float>& tgt, std::vector<float>& corr) const;
    bool loadMeasurementFile (const juce::File&, juce::String& error);
    juce::String getMeasurementName() const;
    bool hasMeasurement() const;
    double getCurrentSampleRate() const { return currentSampleRate.load(); }
    float getAutoGainDb() const         { return autoGainDb.load(); }
    float takePeak (int ch)             { return peaks[ch & 1].exchange (0.0f); }

    static constexpr int customSourceIndex = 2;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override;
    void rebuildImpulse (bool force);

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 << 16 };
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> wetMix, outGain;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<bool>   prepared { false }, dirty { true };
    std::atomic<int>    latencyTarget { 0 };
    std::atomic<float>  autoGainDb { 0.0f };
    std::atomic<float>  peaks[2] {};
    int currentDryDelay = -1;

    mutable juce::CriticalSection modelLock;
    mm::CorrectionModel model;
    juce::String measurementName;
    mm::Settings lastBuilt;
    double lastBuiltSr = 0.0;
    bool hasBuilt = false;

    std::atomic<float>* pSource   = nullptr;
    std::atomic<float>* pTargetA  = nullptr;
    std::atomic<float>* pTargetB  = nullptr;
    std::atomic<float>* pSlot     = nullptr;
    std::atomic<float>* pAmount   = nullptr;
    std::atomic<float>* pBass     = nullptr;
    std::atomic<float>* pMaxBoost = nullptr;
    std::atomic<float>* pOutput   = nullptr;
    std::atomic<float>* pAutoGain = nullptr;
    std::atomic<float>* pMode     = nullptr;
    std::atomic<float>* pBypass   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorMatchProcessor)
};
