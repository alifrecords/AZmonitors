#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Analyzer.h"
#include "dsp/Transfer.h"
#include <deque>
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
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;  // x = kirish dB, y = gain dB
    std::array<juce::uint32, 400> inHist {};           // kirish: -100..0 dB, 0.25 dB qadam

    // Oxirgi 3 s (30 ta 100 ms blok) — real vaqtdagi gain reduction
    static constexpr int kRecent = 30;
    std::array<float, kRecent> recentIn {}, recentGain {};
    int recentPos = 0, recentCount = 0;

    void add (float inDb, float gainDb) noexcept
    {
        ++n; sx += inDb; sy += gainDb; sxx += (double) inDb * inDb; syy += (double) gainDb * gainDb; sxy += (double) inDb * gainDb;
        inHist[(size_t) juce::jlimit (0, 399, (int) ((inDb + 100.0f) / 0.25f))] += 1;
        recentIn[(size_t) recentPos] = inDb; recentGain[(size_t) recentPos] = gainDb;
        recentPos = (recentPos + 1) % kRecent; recentCount = juce::jmin (recentCount + 1, kRecent);
    }
    float slope() const noexcept
    {
        const double d = n * sxx - sx * sx;
        return n > 2 && d > 1.0e-9 ? (float) ((n * sxy - sx * sy) / d) : 0.0f;
    }
    /** Kirish darajasi va gain korrelyatsiyasi: kompressorda kuchli manfiy (-0.5 ... -1), EQ'da ~0 */
    float correlation() const noexcept
    {
        const double dx = n * sxx - sx * sx, dy = n * syy - sy * sy;
        return n > 2 && dx > 1.0e-9 && dy > 1.0e-9 ? (float) ((n * sxy - sx * sy) / std::sqrt (dx * dy)) : 0.0f;
    }
    float inPercentile (double pct) const noexcept
    {
        juce::uint64 total = 0; for (auto v : inHist) total += v;
        if (total == 0) return -100.0f;
        juce::uint64 acc = 0; const double target = pct * (double) (total - 1);
        for (size_t i = 0; i < inHist.size(); ++i) { acc += inHist[i]; if ((double) acc > target) return -100.0f + ((float) i + 0.5f) * 0.25f; }
        return 0.0f;
    }
    /** Samarali gain reduction: darajaga bog'liq gain o'zgarishi (-slope x kirish oralig'i P10..P95).
        EQ va oddiy gain uchun ~0, chunki ularda gain kirish darajasiga bog'liq emas. */
    float grRange() const noexcept
    {
        if (n < 20) return -1.0f;
        return juce::jmax (0.0f, -slope() * (inPercentile (0.95) - inPercentile (0.10)));
    }
    /** Oxirgi 3 soniyadagi gain reduction (knob burilganda tez yangilanadi) */
    float grRecent() const noexcept
    {
        if (recentCount < 8) return -1.0f;
        double rx = 0, ry = 0, rxx = 0, rxy = 0, mn = 1e9, mx = -1e9;
        for (int i = 0; i < recentCount; ++i)
        {
            const double x = recentIn[(size_t) i], y = recentGain[(size_t) i];
            rx += x; ry += y; rxx += x * x; rxy += x * y; mn = juce::jmin (mn, x); mx = juce::jmax (mx, x);
        }
        const double d = recentCount * rxx - rx * rx;
        if (d < 1.0e-9) return 0.0f;
        const double sl = (recentCount * rxy - rx * ry) / d;
        return (float) juce::jmax (0.0, -sl * (mx - mn));
    }
};

// Plagin parametri o'zgarishi va unga ovozning reaksiyasi
struct ParamEvent
{
    juce::Time when;
    juce::String param, from, to;
    std::array<float, kNumBands> hBefore {};
    std::array<bool, kNumBands> usableBefore {};
    bool hasBefore = false;
    float grBefore = -1;
    double lastChangeMs = 0;
    double playedSinceChange = 0;   // sekund
    juce::int64 samplesAtChange = -1; // audio oqimi namunalar hisobi (o'zgarish paytida)
    juce::String reaction;          // bo'sh = hali o'lchanmagan
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

    // Plaginning haqiqiy chastota javobi (H(f)), kogerentlik va spektrogramma
    std::shared_ptr<TransferAnalyzer> transfer = std::make_shared<TransferAnalyzer>();

    // Parametr o'zgarishlari jurnali (faqat GUI oqimi)
    std::vector<float> lastParams;
    std::deque<ParamEvent> events;

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

    /** GUI oqimi, ~10 Hz: parametr o'zgarishlarini aniqlaydi va reaksiyani o'lchaydi */
    void pollParameters (bool playing, double dtSeconds);
    std::vector<std::shared_ptr<TransferAnalyzer>> getTransfers() const;

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
    std::vector<float> monoIn, monoOut;
    DelayLine preDelay, deltaDelay;
    juce::MidiBuffer midi;
    juce::uint32 nextId = 1;
    juce::String restoreErrors;

    class PluginWindow;
    std::map<juce::uint32, std::unique_ptr<PluginWindow>> windows;
};

} // namespace az
