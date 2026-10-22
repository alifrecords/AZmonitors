#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Analyzer.h"
#include "engine/Diagnosis.h"
#include "engine/ProbeRegistry.h"
#include "rack/Rack.h"

//==============================================================================
// AZ Monitoring — Azimxodjayev Music
//
// Bitta plagin, ikki rejim:
//   Monitor — vokal insert zanjirining OXIRIDA: umumiy tashxis + zanjir bahosi
//   Probe   — har bir plagindan KEYIN: o'sha nuqtadagi signalni o'lchaydi
// Audio signal hech qachon o'zgartirilmaydi (bit-transparent, 0 latency).
class AZMonitoringProcessor final : public juce::AudioProcessor,
                                    private juce::TimeSliceClient,
                                    private juce::Timer
{
public:
    AZMonitoringProcessor();
    ~AZMonitoringProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { rack.release(); }
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AZ Monitoring"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return rack.size() > 0 ? 2.0 : 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void updateTrackProperties (const TrackProperties&) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    az::Analyzer analyzer;

    bool isProbe() const;
    int  getSlotIndex() const;
    bool isMasterMode() const;
    juce::String getGroupKey() const;
    juce::String getTrackName() const;

    juce::String getPluginLabel() const;
    void setPluginLabel (const juce::String&);

    const az::Profile& currentProfile() const;
    const az::DeliveryStandard& currentStandard() const;
    bool hasCustomProfile() const { return customProfileLoaded; }
    void setCustomProfile (const az::Profile&);   // saqlaydi va tanlaydi

    /** Monitor rejimi: umumiy tashxis + zanjir (GUI oqimida chaqiriladi) */
    az::Report buildReport (az::Features& finalOut, int& probeCount, juce::StringArray& duplicateSlots);

    /** Monitor ham, probe'lar ham — shu guruhda tahlilni qaytadan boshlash */
    void resetAnalysis();

    static juce::StringArray profileNames();

    //==========================================================================
    // AZ Rack — plaginlarni AZ Monitoring ichiga yuklash
    az::Rack rack;
    az::Analyzer inputAnalyzer;                         // rack kirishi
    az::PluginCatalog& getCatalog() { return *catalog; }

    /** Plaginni yaratib, rack'ka qo'shadi (GUI oqimida). Xato bo'lsa matn qaytaradi. */
    juce::String addPlugin (const juce::PluginDescription&, int index);
    void updateLatency();

    bool saveRackPreset (const juce::File&) const;
    juce::String loadRackPreset (const juce::File&);

private:
    int useTimeSlice() override;
    void timerCallback() override;

    template <typename Sample>
    void processAudio (juce::AudioBuffer<Sample>&);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct AnalysisThread
    {
        AnalysisThread()  { thread.startThread (juce::Thread::Priority::low); }
        ~AnalysisThread() { thread.stopThread (3000); }
        juce::TimeSliceThread thread { "AZ Monitoring analysis" };
    };
    juce::SharedResourcePointer<AnalysisThread> analysisThread;

    std::shared_ptr<az::SharedPoint> point;
    juce::uint32 lastPublish = 0;

    std::atomic<juce::AudioParameterChoice*> modeParam { nullptr };
    juce::AudioParameterChoice* slotParam = nullptr;
    juce::AudioParameterChoice* profileParam = nullptr;
    juce::AudioParameterChoice* sourceParam = nullptr;
    juce::AudioParameterChoice* standardParam = nullptr;
    juce::AudioParameterChoice* groupParam = nullptr;
    juce::AudioParameterBool* playOnlyParam = nullptr;

    mutable juce::SpinLock textLock;
    juce::String trackName, pluginLabel;

    juce::SharedResourcePointer<az::PluginCatalog> catalog;
    juce::AudioBuffer<float> work;
    int maxBlock = 512;
    juce::CriticalSection pendingLock;
    juce::ValueTree pendingRack;

    az::Profile customProfile;
    bool customProfileLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AZMonitoringProcessor)
};
