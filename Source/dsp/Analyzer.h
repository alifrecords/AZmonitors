#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace az
{
//==============================================================================
// 1/6-oktava polosalar: 20 Hz ... ~18 kHz (60 ta)
constexpr int kNumBands = 60;
inline float bandCentre (int k) noexcept { return 20.0f * std::pow (2.0f, (float) k / 6.0f); }
inline int bandForFreq (float hz) noexcept
{
    return juce::jlimit (0, kNumBands - 1, (int) std::round (6.0f * std::log2 (juce::jmax (20.0f, hz) / 20.0f)));
}

constexpr float kNoValue = -200.0f;   // "o'lchanmagan" qiymat

struct Event
{
    enum Type : int { TruePeakOver = 0, Sibilance = 1, Clip = 2 };
    float time = 0;     // xost vaqti, sekund
    int   type = 0;
    float value = 0;    // dB
};

//==============================================================================
// Bitta o'lchov nuqtasining (probe yoki monitor) to'plangan natijalari
struct Features
{
    double secondsAnalysed = 0;     // tahlil qilingan audio (sekund)
    double secondsActive   = 0;     // gating'dan o'tgan (signal bor) qism

    // ITU-R BS.1770-4 / EBU R128
    float momentary  = kNoValue;    // LUFS, 400 ms
    float shortTerm  = kNoValue;    // LUFS, 3 s
    float integrated = kNoValue;    // LUFS, gated
    float lra        = 0;           // LU (EBU Tech 3342)

    float truePeakMax    = kNoValue;   // dBTP
    float truePeakRecent = kNoValue;   // dBTP, oxirgi 400 ms
    float samplePeakMax  = kNoValue;   // dBFS
    juce::int64 clippedSamples = 0;    // |x| >= 1.0

    float rmsActive  = kNoValue;    // dBFS, faol qism o'rtacha RMS
    float peakP995   = kNoValue;    // dBTP, 100 ms peaklarning 99.5-persentili
    float crest      = 0;           // dB = peakP995 - rmsActive
    float noiseFloor = kNoValue;    // dBFS, 100 ms RMS ning 3-persentili

    // Sibilyant indeksi: (5-9 kHz) / (1-4 kHz) quvvat nisbati, faol freymlar bo'yicha
    float sibilanceP97    = kNoValue;
    float sibilanceMedian = kNoValue;

    bool  spectrumValid = false;
    std::array<float, kNumBands> bandLevel {};   // uzoq muddatli polosa quvvati, dB (absolyut)
    std::array<float, kNumBands> bandRel {};     // 200 Hz–5 kHz o'rtachasiga normallashtirilgan
    std::array<float, kNumBands> bandLive {};    // jonli ko'rinish (normallashtirilgan)

    double timeFirst = -1, timeLast = -1;        // xost vaqt oralig'i
    std::vector<Event> events;

    bool hasEnoughData() const noexcept { return secondsActive >= 4.0 && integrated > -70.0f; }
};

//==============================================================================
// Real vaqt analizatori.
//   pushAudio()   — audio oqimida (lock-free, allokatsiyasiz)
//   runBackground() — fon oqimida (FFT, gating, histogrammalar)
//   getSnapshot() — istalgan oqimda
class Analyzer
{
public:
    Analyzer();

    void prepare (double sampleRate, int numChannels);
    void requestReset() noexcept { resetAudio = true; resetBg = true; }

    template <typename Sample>
    void pushAudio (const Sample* const* channels, int numChannels, int numSamples,
                    double hostTimeSec, bool hostTimeValid) noexcept;

    /** Fon oqimida chaqiriladi. true qaytarsa — yangi ma'lumot qayta ishlandi. */
    bool runBackground();

    Features getSnapshot() const;

    double getSampleRate() const noexcept { return sampleRate; }
    int getOversamplingFactor() const noexcept { return tpFactor; }

    /** Butun audio faylni oflayn tahlil qiladi (etalon profil yaratish va testlar uchun). */
    static Features analyseBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    //--------------------------------------------------------------------------
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0; }
    };

    struct HopMeasure
    {
        double kEnergy = 0;      // sum_c G_c * meanSquare(K-weighted)
        double rawEnergy = 0;    // kanallar bo'yicha o'rtacha meanSquare
        float  samplePeak = 0;   // chiziqli
        float  truePeak = 0;     // chiziqli
        int    clipped = 0;
        double time = -1;
    };

    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxTapsPerPhase = 24;   // jami 48 tap: 4x->12/faza, 2x->24/faza
    static constexpr int kShortHops = 30;          // 3 s / 100 ms

    // Histogramma: 0.01 LU qadam, -70 ... +10 LUFS
    static constexpr float kHistMin = -70.0f, kHistStep = 0.01f;
    static constexpr int kHistBins = 8000;

    void designKWeighting();
    void designTruePeak();
    void resetAudioState() noexcept;
    void resetBackgroundState();
    void processHop (const HopMeasure&);
    void processFftFrame();
    void computeSnapshot();

    // ---- sozlamalar
    double sampleRate = 48000.0;
    int numCh = 2;
    int hopSize = 4800;
    int tpFactor = 4;
    int tpTaps = 12;
    std::vector<float> tpCoeffs;          // tpFactor * tpTaps

    // ---- audio oqimi holati
    Biquad kPre[kMaxChannels], kRlb[kMaxChannels];
    std::array<std::array<double, kMaxTapsPerPhase * 2>, kMaxChannels> tpHist {};
    int tpPos = 0;
    int hopCount = 0;
    double hopK[kMaxChannels] {}, hopRaw = 0;
    float hopSPeak = 0, hopTPeak = 0;
    int hopClipped = 0;
    double hopStartTime = -1;
    std::vector<float> monoScratch;

    std::atomic<bool> resetAudio { false }, resetBg { false };

    // ---- oqimlararo FIFO'lar (bitta yozuvchi, bitta o'quvchi)
    juce::AbstractFifo hopFifo { 512 };
    std::vector<HopMeasure> hopBuffer;
    juce::AbstractFifo monoFifo { 1 };
    std::vector<float> monoBuffer;

    // ---- fon oqimi holati
    juce::CriticalSection bgLock;
    std::array<double, kShortHops> recentK {};
    std::array<float, 4> recentTP {};
    int recentCount = 0, recentPos = 0;
    juce::int64 totalHops = 0;

    std::vector<double> integEnergy, integRaw;   // har bir bin bo'yicha energiya yig'indisi
    std::vector<juce::uint32> integCount;
    std::vector<juce::uint32> stCount;           // short-term (LRA uchun)
    double stEnergyAbove70 = 0; juce::uint32 stCountAbove70 = 0;

    std::vector<juce::uint32> peakHist;          // 100 ms true peak, -100..+12 dB, 0.05 dB
    std::vector<juce::uint32> floorHist;         // 100 ms RMS, -140..0 dB, 0.25 dB
    std::vector<juce::uint32> sibHist;           // -40..+40 dB, 0.1 dB

    float tpMax = 0, spMax = 0;
    juce::int64 clipTotal = 0;
    double timeFirst = -1, timeLast = -1;
    float momentaryLufs = kNoValue, shortTermLufs = kNoValue, recentTpDb = kNoValue;
    std::vector<Event> events;
    double lastEventTime[3] { -1e9, -1e9, -1e9 };

    // FFT
    int fftOrder = 12, fftSize = 4096;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frame, fftWork;
    std::vector<double> powerSum;      // uzoq muddatli quvvat (har bir FFT bin)
    juce::int64 powerFrames = 0;
    int frameFill = 0;
    double frameTime = -1;
    struct BandRange { int lo, hi; float nearestScale; int nearest; };
    std::array<BandRange, kNumBands> bandRanges {};
    std::array<float, kNumBands> liveBands {};
    int sibLo1 = 0, sibHi1 = 0, sibLo2 = 0, sibHi2 = 0;

    // natija
    mutable juce::SpinLock snapLock;
    Features snapshot;
};

//==============================================================================
template <typename Sample>
void Analyzer::pushAudio (const Sample* const* channels, int numChannels, int numSamples,
                          double hostTimeSec, bool hostTimeValid) noexcept
{
    if (resetAudio.exchange (false))
        resetAudioState();

    const int chans = juce::jmin (numChannels, numCh, kMaxChannels);
    if (chans <= 0 || numSamples <= 0)
        return;

    const int T = tpTaps;
    const int F = tpFactor;
    const double chanGain = 1.0; // L/R/mono: G = 1.0 (BS.1770)

    int done = 0;
    while (done < numSamples)
    {
        const int chunk = juce::jmin (numSamples - done, (int) monoScratch.size());

        for (int i = 0; i < chunk; ++i)
        {
            const int n = done + i;
            double mono = 0;

            if (hopCount == 0)
                hopStartTime = hostTimeValid ? hostTimeSec + (double) n / sampleRate : -1.0;

            for (int c = 0; c < chans; ++c)
            {
                const double x = (double) channels[c][n];
                mono += x;

                const double ax = std::abs (x);
                if (ax > hopSPeak) hopSPeak = (float) ax;
                if (ax >= 1.0) ++hopClipped;

                const double z = kRlb[c].process (kPre[c].process (x));
                hopK[c] += z * z;
                hopRaw += x * x;

                // True peak: polifaza interpolyatsiya
                auto& h = tpHist[(size_t) c];
                h[(size_t) tpPos] = x;
                h[(size_t) (tpPos + T)] = x;
                if (F == 1)
                {
                    if (ax > hopTPeak) hopTPeak = (float) ax;
                }
                else
                {
                    const double* hist = h.data() + tpPos + 1;   // eng eskidan eng yangigacha (T ta)
                    for (int p = 0; p < F; ++p)
                    {
                        const float* coeffs = tpCoeffs.data() + p * T;
                        double y = 0;
                        for (int k = 0; k < T; ++k)
                            y += coeffs[k] * hist[k];
                        const float ay = (float) std::abs (y);
                        if (ay > hopTPeak) hopTPeak = ay;
                    }
                }
            }

            tpPos = (tpPos + 1) % T;
            monoScratch[(size_t) i] = (float) (mono / (double) chans);

            if (++hopCount >= hopSize)
            {
                HopMeasure m;
                m.kEnergy = 0;
                for (int c = 0; c < chans; ++c)
                    m.kEnergy += chanGain * (hopK[c] / (double) hopSize);
                m.rawEnergy  = hopRaw / ((double) hopSize * chans);
                m.samplePeak = hopSPeak;
                m.truePeak   = juce::jmax (hopTPeak, hopSPeak);
                m.clipped    = hopClipped;
                m.time       = hopStartTime;

                const auto scope = hopFifo.write (1);
                if (scope.blockSize1 > 0)       hopBuffer[(size_t) scope.startIndex1] = m;
                else if (scope.blockSize2 > 0)  hopBuffer[(size_t) scope.startIndex2] = m;

                hopCount = 0;
                for (auto& v : hopK) v = 0;
                hopRaw = 0; hopSPeak = 0; hopTPeak = 0; hopClipped = 0;
            }
        }

        // mono signal FFT uchun
        const auto scope = monoFifo.write (chunk);
        if (scope.blockSize1 > 0)
            std::copy (monoScratch.begin(), monoScratch.begin() + scope.blockSize1, monoBuffer.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy (monoScratch.begin() + scope.blockSize1, monoScratch.begin() + scope.blockSize1 + scope.blockSize2,
                       monoBuffer.begin() + scope.startIndex2);

        done += chunk;
    }
}

} // namespace az
