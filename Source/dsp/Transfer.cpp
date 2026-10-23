#include "Transfer.h"

namespace az
{
TransferAnalyzer::TransferAnalyzer()
{
    prepare (48000.0);
}

void TransferAnalyzer::prepare (double sr)
{
    const juce::ScopedLock sl (bgLock);
    sampleRate = juce::jmax (8000.0, sr);
    fftSize = juce::nextPowerOfTwo ((int) (sampleRate * 0.0853));
    fftOrder = (int) std::round (std::log2 ((double) fftSize));
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    frameIn.assign ((size_t) fftSize, 0.0f);
    frameOut.assign ((size_t) fftSize, 0.0f);
    workIn.assign ((size_t) fftSize * 2, 0.0f);
    workOut.assign ((size_t) fftSize * 2, 0.0f);
    frameFill = 0;

    delayBuf.assign ((size_t) sampleRate + 1, 0.0f);   // 1 s gacha plagin kechikishi
    delayPos = 0;

    const int cap = (int) (sampleRate * 1.5);
    fifoIn.assign ((size_t) cap, 0.0f);
    fifoOut.assign ((size_t) cap, 0.0f);
    fifo.setTotalSize (cap);
    fifo.reset();
    scratchIn.assign (8192, 0.0f);

    const int nb = fftSize / 2 + 1;
    sxxF.assign ((size_t) nb, 0.0); syyF.assign ((size_t) nb, 0.0); sxyF.assign ((size_t) nb, {});
    sxxL.assign ((size_t) nb, 0.0); syyL.assign ((size_t) nb, 0.0); sxyL.assign ((size_t) nb, {});
    framesL = framesF = 0;

    // Tez o'rtacha: ~0.6 s vaqt doimiysi (knob burilganda tez reaksiya)
    const double hopSec = (fftSize / 2) / sampleRate;
    alpha = 1.0 - std::exp (-hopSec / 0.6);

    const double binHz = sampleRate / fftSize;
    for (int k = 0; k < kNumBands; ++k)
    {
        const double fc = bandCentre (k);
        const double lo = fc * std::pow (2.0, -1.0 / 12.0), hi = fc * std::pow (2.0, 1.0 / 12.0);
        BandRange r;
        r.lo = juce::jlimit (1, nb - 1, (int) std::floor (lo / binHz));
        r.hi = juce::jlimit (1, nb - 1, (int) std::ceil (hi / binHz));
        if (hi >= sampleRate * 0.5) r.lo = r.hi = -1;
        bands[(size_t) k] = r;
    }

    specDelta.assign ((size_t) TransferSnapshot::kCols * kNumBands, 0.0f);
    specOut.assign ((size_t) TransferSnapshot::kCols * kNumBands, -120.0f);
    writeCol = 0;
    resetFlag = false;

    const juce::SpinLock::ScopedLockType s2 (snapLock);
    snapshot = TransferSnapshot();
}

void TransferAnalyzer::push (const float* in, const float* out, int n, int latency) noexcept
{
    const int dsize = (int) delayBuf.size();
    latency = juce::jlimit (0, dsize - 1, latency);
    int done = 0;
    while (done < n)
    {
        const int chunk = juce::jmin (n - done, (int) scratchIn.size());
        for (int i = 0; i < chunk; ++i)
        {
            delayBuf[(size_t) delayPos] = in[done + i];
            int r = delayPos - latency; if (r < 0) r += dsize;
            scratchIn[(size_t) i] = delayBuf[(size_t) r];
            if (++delayPos >= dsize) delayPos = 0;
        }
        const auto scope = fifo.write (chunk);
        pushedSamples += scope.blockSize1 + scope.blockSize2;
        if (scope.blockSize1 > 0)
        {
            std::copy (scratchIn.begin(), scratchIn.begin() + scope.blockSize1, fifoIn.begin() + scope.startIndex1);
            std::copy (out + done, out + done + scope.blockSize1, fifoOut.begin() + scope.startIndex1);
        }
        if (scope.blockSize2 > 0)
        {
            std::copy (scratchIn.begin() + scope.blockSize1, scratchIn.begin() + scope.blockSize1 + scope.blockSize2, fifoIn.begin() + scope.startIndex2);
            std::copy (out + done + scope.blockSize1, out + done + scope.blockSize1 + scope.blockSize2, fifoOut.begin() + scope.startIndex2);
        }
        done += chunk;
    }
}

bool TransferAnalyzer::runBackground()
{
    const juce::ScopedTryLock sl (bgLock);
    if (! sl.isLocked()) return false;

    if (resetFlag.exchange (false))
    {
        std::fill (sxxF.begin(), sxxF.end(), 0.0); std::fill (syyF.begin(), syyF.end(), 0.0); std::fill (sxyF.begin(), sxyF.end(), std::complex<double>());
        std::fill (sxxL.begin(), sxxL.end(), 0.0); std::fill (syyL.begin(), syyL.end(), 0.0); std::fill (sxyL.begin(), sxyL.end(), std::complex<double>());
        framesL = framesF = 0;
        analysedSamples += fifo.getNumReady();   // tashlab yuborilganlar ham "o'tgan" hisoblanadi
        fifo.reset();
        frameFill = 0;
        std::fill (specDelta.begin(), specDelta.end(), 0.0f);
        std::fill (specOut.begin(), specOut.end(), -120.0f);
    }

    bool any = false;
    for (;;)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) break;
        const int take = juce::jmin (fftSize - frameFill, ready);
        const auto scope = fifo.read (take);
        auto copyPart = [&] (int start, int size, int dst)
        {
            std::copy (fifoIn.begin() + start, fifoIn.begin() + start + size, frameIn.begin() + dst);
            std::copy (fifoOut.begin() + start, fifoOut.begin() + start + size, frameOut.begin() + dst);
        };
        if (scope.blockSize1 > 0) copyPart (scope.startIndex1, scope.blockSize1, frameFill);
        if (scope.blockSize2 > 0) copyPart (scope.startIndex2, scope.blockSize2, frameFill + scope.blockSize1);
        frameFill += take;
        analysedSamples += take;
        any = true;

        if (frameFill >= fftSize)
        {
            processFrame();
            const int half = fftSize / 2;
            std::copy (frameIn.begin() + half, frameIn.end(), frameIn.begin());
            std::copy (frameOut.begin() + half, frameOut.end(), frameOut.begin());
            frameFill = half;
        }
    }
    if (any) computeSnapshot();
    return any;
}

void TransferAnalyzer::processFrame()
{
    double eIn = 0;
    for (int i = 0; i < fftSize; ++i) eIn += (double) frameIn[(size_t) i] * frameIn[(size_t) i];
    if (10.0 * std::log10 (eIn / fftSize + 1.0e-30) < -65.0)
        return;   // sukut: o'lchamaymiz

    for (int i = 0; i < fftSize; ++i)
    {
        workIn[(size_t) i]  = frameIn[(size_t) i]  * window[(size_t) i];
        workOut[(size_t) i] = frameOut[(size_t) i] * window[(size_t) i];
    }
    std::fill (workIn.begin() + fftSize, workIn.end(), 0.0f);
    std::fill (workOut.begin() + fftSize, workOut.end(), 0.0f);
    fft->performRealOnlyForwardTransform (workIn.data(), true);
    fft->performRealOnlyForwardTransform (workOut.data(), true);

    const int nb = fftSize / 2 + 1;
    const double a = framesF == 0 ? 1.0 : alpha;
    for (int k = 0; k < nb; ++k)
    {
        const std::complex<double> X (workIn[(size_t) (2 * k)], workIn[(size_t) (2 * k + 1)]);
        const std::complex<double> Y (workOut[(size_t) (2 * k)], workOut[(size_t) (2 * k + 1)]);
        const double xx = std::norm (X), yy = std::norm (Y);
        const auto xy = std::conj (X) * Y;
        sxxL[(size_t) k] += xx; syyL[(size_t) k] += yy; sxyL[(size_t) k] += xy;
        sxxF[(size_t) k] += a * (xx - sxxF[(size_t) k]);
        syyF[(size_t) k] += a * (yy - syyF[(size_t) k]);
        sxyF[(size_t) k] += a * (xy - sxyF[(size_t) k]);
    }
    ++framesL; ++framesF; ++framesTotal;

    // Spektrogramma ustuni: shu freymdagi chiqish darajasi va o'zgarish (chiqish - kirish)
    const double norm = (double) fftSize * fftSize;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& r = bands[(size_t) b];
        const size_t idx = (size_t) writeCol * kNumBands + (size_t) b;
        if (r.lo < 0) { specDelta[idx] = 0; specOut[idx] = -120.0f; continue; }
        double pi = 0, po = 0;
        for (int k = r.lo; k <= r.hi; ++k)
        {
            pi += (double) workIn[(size_t) (2 * k)] * workIn[(size_t) (2 * k)] + (double) workIn[(size_t) (2 * k + 1)] * workIn[(size_t) (2 * k + 1)];
            po += (double) workOut[(size_t) (2 * k)] * workOut[(size_t) (2 * k)] + (double) workOut[(size_t) (2 * k + 1)] * workOut[(size_t) (2 * k + 1)];
        }
        const double inDb = 10.0 * std::log10 (pi / norm + 1.0e-20), outDb = 10.0 * std::log10 (po / norm + 1.0e-20);
        specOut[idx] = (float) outDb;
        specDelta[idx] = inDb > -90.0 ? (float) (outDb - inDb) : 0.0f;
    }
    writeCol = (writeCol + 1) % TransferSnapshot::kCols;
}

void TransferAnalyzer::computeSnapshot()
{
    TransferSnapshot s;
    s.valid = framesF >= 4;
    s.framesLong = (int) framesL;
    s.totalFrames = framesTotal;
    s.hopSeconds = (fftSize / 2) / sampleRate;
    s.analysedSamples = analysedSamples;
    s.sampleRate = sampleRate;
    s.longValid = framesL >= 60;   // ~2.5 s faol signal

    // Asosiy daraja: 200 Hz–5 kHz kirish energiyasi — undan 50 dB past polosalar baholanmaydi
    auto bandSum = [this] (const std::vector<double>& v, int b)
    {
        double sum = 0; const auto& r = bands[(size_t) b];
        for (int k = r.lo; k <= r.hi; ++k) sum += v[(size_t) k];
        return sum;
    };
    double refE = 0; int refN = 0;
    for (int b = 0; b < kNumBands; ++b)
        if (bands[(size_t) b].lo >= 0 && bandCentre (b) >= 200.0f && bandCentre (b) <= 5000.0f) { refE += bandSum (sxxL, b); ++refN; }
    const double refBand = refN > 0 ? refE / refN : 0.0;

    double cohSumL = 0, cohWL = 0, cohSumF = 0, cohWF = 0;
    const double norm = (double) fftSize * fftSize;
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& r = bands[(size_t) b];
        if (r.lo < 0) { s.usable[(size_t) b] = false; continue; }
        double xxL = 0, yyL = 0, xyAbsL = 0, cohNumL = 0;
        double xxF = 0, yyF = 0, xyAbsF = 0, cohNumF = 0;
        for (int k = r.lo; k <= r.hi; ++k)
        {
            const double ax = sxxL[(size_t) k], ay = syyL[(size_t) k], axy = std::abs (sxyL[(size_t) k]);
            xxL += ax; yyL += ay; xyAbsL += axy;
            if (ax > 0 && ay > 0) cohNumL += ax * (axy * axy / (ax * ay));
            const double fx = sxxF[(size_t) k], fy = syyF[(size_t) k], fxy = std::abs (sxyF[(size_t) k]);
            xxF += fx; yyF += fy; xyAbsF += fxy;
            if (fx > 0 && fy > 0) cohNumF += fx * (fxy * fxy / (fx * fy));
        }
        s.usable[(size_t) b] = refBand > 0 && xxL > refBand * 1.0e-5;
        s.hLong[(size_t) b] = xxL > 0 ? (float) (20.0 * std::log10 (xyAbsL / xxL + 1.0e-12)) : 0.0f;
        s.hFast[(size_t) b] = xxF > 0 ? (float) (20.0 * std::log10 (xyAbsF / xxF + 1.0e-12)) : 0.0f;
        s.cohLong[(size_t) b] = xxL > 0 ? (float) juce::jlimit (0.0, 1.0, cohNumL / xxL) : 0.0f;
        s.cohFast[(size_t) b] = xxF > 0 ? (float) juce::jlimit (0.0, 1.0, cohNumF / xxF) : 0.0f;
        s.inFast[(size_t) b]  = (float) (10.0 * std::log10 (xxF / norm + 1.0e-20));
        s.outFast[(size_t) b] = (float) (10.0 * std::log10 (yyF / norm + 1.0e-20));

        const float fc = bandCentre (b);
        if (s.usable[(size_t) b] && fc >= 200.0f && fc <= 8000.0f)
        {
            cohSumL += s.cohLong[(size_t) b] * xxL; cohWL += xxL;
            cohSumF += s.cohFast[(size_t) b] * xxF; cohWF += xxF;
        }
    }
    s.cohAvgLong = cohWL > 0 ? (float) (cohSumL / cohWL) : 1.0f;
    s.cohAvgFast = cohWF > 0 ? (float) (cohSumF / cohWF) : 1.0f;
    s.specDelta = specDelta;
    s.specOut = specOut;
    s.writeCol = writeCol;

    const juce::SpinLock::ScopedLockType sl (snapLock);
    snapshot = std::move (s);
}

TransferSnapshot TransferAnalyzer::getSnapshot() const
{
    const juce::SpinLock::ScopedLockType sl (snapLock);
    return snapshot;
}

} // namespace az
