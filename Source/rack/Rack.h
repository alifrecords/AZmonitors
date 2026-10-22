#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Analyzer.h"
#include "engine/Diagnosis.h"

namespace az
{
//==============================================================================
// O'rnatilgan VST3/AU plaginlar katalogi. Barcha AZ Monitoring nusxalari uchun bitta.
// Skanerlash fon oqimida; plaginni ochganda xost qulasa, keyingi safar o'sha fayl
// o'tkazib yuboriladi (dead man's pedal).
class PluginCatalog final : public juce::ChangeBroadcaster,
                            private juce::Thread,
                            private juce::ChangeListener
{
public:
    PluginCatalog();
    ~PluginCatalog() override;

    void startScan (bool rescanAll);
    bool isScanning() const noexcept  { return isThreadRunning(); }
    juce::String getStatus() const;
    float getProgress() const noexcept { return progress.load(); }
    bool hasBeenScanned() const noexcept { return listFile().existsAsFile(); }

    /** Effekt plaginlari (instrumentlar va AZ Monitoring'ning o'zi chiqarib tashlangan) */
    juce::Array<juce::PluginDescription> getEffects() const;

    juce::AudioPluginFormatManager& getFormats() noexcept { return formats; }

    /** true: har bir plagin alohida jarayonda skanerlanadi (DAW qulamaydi) */
    bool isSafeScan() const noexcept { return scannerExe.existsAsFile(); }
    juce::StringArray getLastFailed() const { const juce::SpinLock::ScopedLockType sl (statusLock); return lastFailed; }

    static juce::String categoryOf (const juce::PluginDescription&);   // EQ, Dinamika, De-esser, ...
    static bool isUad (const juce::PluginDescription&);

private:
    void run() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { sendChangeMessage(); }
    static juce::File dataDir();
    static juce::File listFile();
    void save();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList list;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> rescanAll { false };
    mutable juce::SpinLock statusLock;
    juce::String status;
    juce::StringArray lastFailed;
    juce::File scannerExe;
};

//==============================================================================
// Kirish darajasi va plagin gain'i o'rtasidagi bog'liqlik (100 ms bloklar bo'yicha).
// Kompressor bo'lsa: gain kirish darajasiga qarab o'zgaradi -> gain reduction va samarali ratio.
struct GainStats
{
    int n = 0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;          // x = kirish dB, y = gain dB
    std::array<juce::uint32, 400> hist {};             // gain: -60..+40 dB, 0.25 dB qadam

    void add (float inDb, float gainDb) noexcept
    {
        ++n; sx += inDb; sy += gainDb; sxx += (double) inDb * inDb; sxy += (double) inDb * gainDb;
        hist[(size_t) juce::jlimit (0, 399, (int) ((gainDb + 60.0f) / 0.25f))] += 1;
    }
    float slope() const noexcept
    {
        const double d = n * sxx - sx * sx;
        return n > 2 && d > 1.0e-9 ? (float) ((n * sxy - sx * sy) / d) : 0.0f;
    }
    float percentile (double pct) const noexcept
    {
        juce::uint64 total = 0; for (auto v : hist) total += v;
        if (total == 0) return 0;
        juce::uint64 acc = 0; const double target = pct * (double) (total - 1);
        for (size_t i = 0; i < hist.size(); ++i) { acc += hist[i]; if ((double) acc > target) return -60.0f + ((float) i + 0.5f) * 0.25f; }
        return 40.0f;
    }
    /** Gain reduction oralig'i: eng yuqori va eng past gain farqi (P95 - P5) */
    float grRange() const noexcept { return n >= 20 ? percentile (0.95) - percentile (0.05) : -1.0f; }
};

// Rack slotidagi bitta plagin
struct RackSlot
{
    juce::uint32 id = 0;
    juce::PluginDescription desc;
    std::unique_ptr<juce::AudioPluginInstance> plugin;   // nullptr = plagin topilmadi (holati saqlanadi)
    juce::MemoryBlock savedState;                        // topilmagan plagin holati
    std::atomic<bool> bypass { false };
    bool monoOnly = false;
    std::atomic<int> latency { 0 };
    std::shared_ptr<Analyzer> analyzer = std::make_shared<Analyzer>();

    // Gain statistikasi: audio oqimi yozadi, GUI nusxasini oladi
    GainStats liveStats;
    double hopIn = 0, hopOut = 0;
    std::atomic<bool> statsReset { false };
    mutable juce::SpinLock statsLock;
    GainStats publishedStats;
    GainStats getGainStats() const { const juce::SpinLock::ScopedLockType sl (statsLock); return publishedStats; }

    juce::String displayName() const;
    juce::String formatLabel() const;
};

//==============================================================================
// Parametr tavsiyasi ("Tavsiyani qo'llash")
struct ParamProposal
{
    int paramIndex = -1;
    juce::String name, fromText, toText;
    float fromValue = 0, toValue = 0;
};

//==============================================================================
class Rack
{
public:
    Rack();
    ~Rack();

    void prepare (double sampleRate, int maxBlock, int numChannels);
    void release();

    /** Audio oqimi. work — 2 kanalli, numSamples <= maxBlock. */
    void process (juce::AudioBuffer<float>& work, int numChannelsForAnalysis, int numSamples,
                  double hostTime, bool hostTimeValid, bool analyse) noexcept;

    // ---- GUI oqimi
    void insert (std::unique_ptr<juce::AudioPluginInstance>, const juce::PluginDescription&, int index);
    void remove (int index);
    void move (int from, int to);
    void setBypass (int index, bool);
    void setDelta (int index) { deltaSlot = index; }
    int  getDelta() const noexcept { return deltaSlot.load(); }

    std::vector<std::shared_ptr<RackSlot>> getSlots() const;
    std::vector<std::shared_ptr<Analyzer>> getAnalyzers() const;
    int size() const noexcept { return numSlots.load(); }
    int computeTotalLatency() const;
    void resetAnalyzers();

    juce::ValueTree saveState() const;
    void restoreState (const juce::ValueTree&, juce::AudioPluginFormatManager&);
    juce::String getRestoreErrors() const { return restoreErrors; }

    void openEditor (int index);
    void closeAllEditors();

    /** Plagin yaratish va rack uchun tayyorlash (GUI oqimida) */
    std::unique_ptr<juce::AudioPluginInstance> createInstance (juce::AudioPluginFormatManager&, const juce::PluginDescription&, juce::String& error) const;

    // ---- Parametrlar va tavsiyalar
    struct ParamRow { juce::String name, value; };
    static std::vector<ParamRow> keyParameters (juce::AudioPluginInstance&, int maxRows = 9);
    static std::vector<ParamProposal> proposeFor (juce::AudioPluginInstance&, const StageResult&, const Features& finalOut, const Profile&);
    static void apply (juce::AudioPluginInstance&, const std::vector<ParamProposal>&, bool undo = false);

private:
    void setupBuses (juce::AudioPluginInstance&, bool& monoOnly) const;
    void swapIn (std::vector<std::shared_ptr<RackSlot>> newSlots);

    struct DelayLine
    {
        juce::AudioBuffer<float> buf;
        int pos = 0;
        void prepare (int maxDelay) { buf.setSize (2, juce::jmax (1, maxDelay + 1)); buf.clear(); pos = 0; }
        void clear() { buf.clear(); pos = 0; }
        void process (juce::AudioBuffer<float>& b, int n, int delay) noexcept;
    };

    mutable juce::SpinLock lock;
    std::vector<std::shared_ptr<RackSlot>> slots;
    std::atomic<int> numSlots { 0 };
    std::atomic<int> deltaSlot { -1 };
    int lastDelta = -1;

    double sampleRate = 48000.0;
    int maxBlock = 512, numCh = 2;
    int hopSize = 4800, hopPos = 0;
    juce::AudioBuffer<float> preBuf, deltaBuf;
    DelayLine preDelay, deltaDelay;
    juce::MidiBuffer midi;
    juce::uint32 nextId = 1;
    juce::String restoreErrors;

    class PluginWindow;
    std::map<juce::uint32, std::unique_ptr<PluginWindow>> windows;
};

} // namespace az
