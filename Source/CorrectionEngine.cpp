#include "CorrectionEngine.h"
#include <complex>
#include <sstream>
#include <cstdlib>

namespace mm
{
namespace
{
using cd = std::complex<double>;

void fft (std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / (double) len * (inverse ? 1.0 : -1.0);
        for (size_t i = 0; i < n; i += len)
            for (size_t j = 0; j < len / 2; ++j)
            {
                const cd w (std::cos (ang * (double) j), std::sin (ang * (double) j));
                const cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
            }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
} // namespace

int firLengthFor (double sr)
{
    // 44.1/48 kHz da ~170 ms: 30 Hz gacha aniqlik (xato < 0.6 dB).
    if (sr <= 50000.0)  return 16384;
    if (sr <= 100000.0) return 32768;
    return 65536;
}

double CorrectionModel::sourceDb (const Settings& s, double hz) const
{
    const auto& src = sourceProfiles();
    if (s.source >= 0 && s.source < (int) src.size())
        return interpolateDb (src[(size_t) s.source].points, hz);
    return custom.empty() ? 0.0 : interpolateDb (custom, hz);
}

double CorrectionModel::targetDb (const Settings& s, double hz) const
{
    const auto& t = targetProfiles();
    const int i = std::clamp (s.target, 0, (int) t.size() - 1);
    return interpolateDb (t[(size_t) i].points, hz);
}

double CorrectionModel::rawCorrectionDb (const Settings& s, double hz) const
{
    const double src = sourceDb (s, hz);
    const double tgt = targetDb (s, hz);
    double c = tgt - src;

    // Bass korreksiyasini alohida boshqarish (250 Hz atrofida silliq o'tish).
    const double wb = 1.0 / (1.0 + std::pow (hz / 250.0, 4.0));
    c *= s.amount * ((1.0 - wb) + wb * s.bass);

    // Himoya: dinamik o'zi chiqara olmaydigan joyda ko'tarmaymiz
    // (manba -6 dB dan pastda boost kamayadi, -12 dB da butunlay yo'q).
    const double protect = std::clamp ((src + 12.0) / 6.0, 0.0, 1.0);
    c = std::min (c, s.maxBoostDb * protect);
    c = std::max (c, -18.0);

    if (hz < 20.0)    c *= std::pow (hz / 20.0, 2.0);
    if (hz > 20000.0) c *= std::max (0.0, 1.0 - (hz - 20000.0) / 4000.0);
    return c;
}

double CorrectionModel::gainCompDb (const Settings& s) const
{
    // 100 Hz – 8 kHz oralig'idagi o'rtacha korreksiyani qaytarib olamiz,
    // shunda emulyatsiya yoqilganda ovoz balandroq/pastroq bo'lib "aldamaydi".
    constexpr int n = 64;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double hz = 100.0 * std::pow (80.0, (double) i / (n - 1));
        sum += rawCorrectionDb (s, hz);
    }
    return -sum / n;
}

double CorrectionModel::smoothedCorrectionDb (const Settings& s, double hz) const
{
    // 1/6 oktava silliqlash: keskin burilishlar FIR'da aniq takrorlanadi va quloqqa tabiiy.
    double sum = 0.0;
    for (int k = -4; k <= 4; ++k)
        sum += rawCorrectionDb (s, hz * std::pow (2.0, k / 48.0));
    return sum / 9.0;
}

double CorrectionModel::correctionDb (const Settings& s, double hz) const
{
    return smoothedCorrectionDb (s, hz) + (s.autoGain ? gainCompDb (s) : 0.0);
}

std::vector<float> CorrectionModel::buildImpulse (const Settings& s, double sr, int N) const
{
    std::vector<float> out ((size_t) N, 0.0f);
    const double comp = s.autoGain ? gainCompDb (s) : 0.0;
    auto magAt = [&] (double f) { return dbToGain (smoothedCorrectionDb (s, std::max (f, 1.0)) + comp); };

    if (! s.minPhase)
    {
        // Linear phase: nol fazali spektr -> IFFT -> markazga siljitish -> Hann oyna.
        std::vector<cd> X ((size_t) N);
        for (int k = 0; k <= N / 2; ++k)
        {
            const double m = magAt ((double) k * sr / N);
            X[(size_t) k] = m;
            if (k > 0 && k < N / 2) X[(size_t) (N - k)] = m;
        }
        fft (X, true);
        for (int n = 0; n < N; ++n)
        {
            const double x = X[(size_t) ((n + N / 2) % N)].real();
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * n / (N - 1));
            out[(size_t) n] = (float) (x * w);
        }
    }
    else
    {
        // Minimum phase (kepstr usuli) — kechikishsiz.
        const int M = N * (N >= 32768 ? 2 : 4);
        std::vector<cd> X ((size_t) M);
        for (int k = 0; k <= M / 2; ++k)
        {
            const double lm = std::log (std::max (magAt ((double) k * sr / M), 1.0e-6));
            X[(size_t) k] = lm;
            if (k > 0 && k < M / 2) X[(size_t) (M - k)] = lm;
        }
        fft (X, true);                         // real kepstr
        for (int n = 1; n < M / 2; ++n) X[(size_t) n] *= 2.0;
        for (int n = M / 2 + 1; n < M; ++n) X[(size_t) n] = 0.0;
        fft (X, false);
        for (auto& v : X) v = std::exp (v);
        fft (X, true);
        for (int n = 0; n < N; ++n)
        {
            double w = 1.0;
            if (n >= N / 2) w = 0.5 + 0.5 * std::cos (kPi * (n - N / 2) / (double) (N / 2));
            out[(size_t) n] = (float) (X[(size_t) n].real() * w);
        }
    }
    return out;
}

std::vector<CurvePoint> parseMeasurementText (const std::string& text)
{
    std::vector<CurvePoint> raw;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        if (line.empty() || line[0] == '*' || line[0] == '#' || line[0] == ';') continue;
        for (auto& ch : line) if (ch == ';' || ch == '\t') ch = ' ';

        // "100, 85.2, -10" yoki "100 85.2" kabi formatlar (o'nlik ajratuvchi — nuqta)
        std::string l2 = line;
        for (auto& ch : l2) if (ch == ',') ch = ' ';

        const char* p = l2.c_str();
        char* end = nullptr;
        const double f = std::strtod (p, &end);
        if (end == p) continue;
        p = end;
        const double d = std::strtod (p, &end);
        if (end == p) continue;
        if (f > 5.0 && f < 48000.0 && std::isfinite (d))
            raw.push_back ({ f, d });
    }
    if (raw.size() < 10) return {};
    std::sort (raw.begin(), raw.end(), [] (auto& a, auto& b) { return a.hz < b.hz; });

    // 1/6 oktava silliqlash, 1/24 oktava qadamli setkada.
    std::vector<CurvePoint> out;
    const double fLo = std::max (raw.front().hz, 15.0);
    const double fHi = std::min (raw.back().hz, 24000.0);
    const double halfBand = std::pow (2.0, 1.0 / 12.0);
    for (double f = fLo; f <= fHi; f *= std::pow (2.0, 1.0 / 24.0))
    {
        double sum = 0.0; int cnt = 0;
        for (auto& r : raw)
            if (r.hz >= f / halfBand && r.hz <= f * halfBand) { sum += r.db; ++cnt; }
        out.push_back ({ f, cnt > 0 ? sum / cnt : interpolateDb (raw, f) });
    }
    if (out.size() < 4) return {};

    double ref = 0.0; int n = 0;
    for (auto& p : out) if (p.hz >= 300.0 && p.hz <= 3000.0) { ref += p.db; ++n; }
    ref = n > 0 ? ref / n : 0.0;
    for (auto& p : out) p.db -= ref;
    return out;
}
} // namespace mm
