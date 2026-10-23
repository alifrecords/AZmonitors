#pragma once
#include "Analyzer.h"
#include <complex>

namespace az
{
//==============================================================================
// Plaginning haqiqiy chastota javobini o'lchaydi: kirish va chiqish signali
// bir vaqtda mavjud (AZ Rack ichida), shuning uchun
//     H(f) = |Sxy| / Sxx       — plagin har bir chastotada qancha ko'targan/kesgan
//     coherence = |Sxy|^2 / (Sxx * Syy) — o'zgarish chiziqlimi (EQ ≈ 1) yoki
//                                         nochiziqli/vaqtga bog'liq (saturatsiya, kompressor < 1)
// Tez o'rtacha (~0.6 s) real vaqtda knob burilishini ko'rsatadi; uzoq o'rtacha tashxis uchun.
struct TransferSnapshot
{
    static constexpr int kCols = 160;              // spektrogramma tarixi (~7 s)

    bool valid = false;                            // tez o'rtacha tayyor
    bool longValid = false;                        // uzoq o'rtacha tashxis uchun yetarli
    int framesLong = 0;
    juce::int64 totalFrames = 0;                   // hech qachon nolga tushmaydi (reaksiya o'lchovi uchun)
    double hopSeconds = 0.04;
    juce::int64 analysedSamples = 0;               // fon oqimi qayta ishlagan namunalar (monoton)
    double sampleRate = 48000.0;
    std::array<float, kNumBands> hFast {}, hLong {};        // dB
    std::array<float, kNumBands> cohFast {}, cohLong {};    // 0..1
    std::array<float, kNumBands> inFast {}, outFast {};     // dB (nisbiy)
    std::array<bool,  kNumBands> usable {};                 // kirishda energiya bor polosalar
    float cohAvgLong = 1.0f, cohAvgFast = 1.0f;              // 200 Hz–8 kHz o'rtachasi

    std::vector<float> specDelta, specOut;          // kCols x kNumBands, ustunlar halqa bo'yicha
    int writeCol = 0;
};

class TransferAnalyzer
{
public:
    TransferAnalyzer();

    void prepare (double sampleRate);
    void requestReset() noexcept { resetFlag = true; }

    /** Audio oqimi: kirish (plagindan oldin) va chiqish (keyin), mono. latency — plagin kechikishi. */
    void push (const float* in, const float* out, int n, int latency) noexcept;

    /** Fon oqimi */
    bool runBackground();

    TransferSnapshot getSnapshot() const;

    /** Audio oqimi shu paytgacha yuborgan namunalar (monoton) */
    juce::int64 getPushedSamples() const noexcept { return pushedSamples.load(); }

private:
    void processFrame();
    void computeSnapshot();

    double sampleRate = 48000.0;
    int fftOrder = 12, fftSize = 4096;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, frameIn, frameOut, workIn, workOut;
    int frameFill = 0;

    // Kirishni plagin kechikishiga moslash (audio oqimi)
    std::vector<float> delayBuf;
    int delayPos = 0;

    juce::AbstractFifo fifo { 1 };
    std::vector<float> fifoIn, fifoOut;
    std::vector<float> scratchIn;

    std::atomic<bool> resetFlag { false };
    std::atomic<juce::int64> pushedSamples { 0 };
    juce::int64 analysedSamples = 0;
    juce::CriticalSection bgLock;

    // Spektral yig'indilar (har bir FFT bin)
    std::vector<double> sxxF, syyF, sxxL, syyL;
    std::vector<std::complex<double>> sxyF, sxyL;
    juce::int64 framesL = 0, framesF = 0, framesTotal = 0;
    double alpha = 0.1;

    struct BandRange { int lo, hi; };
    std::array<BandRange, kNumBands> bands {};

    std::vector<float> specDelta, specOut;
    int writeCol = 0;

    mutable juce::SpinLock snapLock;
    TransferSnapshot snapshot;
};

} // namespace az
