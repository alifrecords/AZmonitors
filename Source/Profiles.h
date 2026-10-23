#pragma once
// Monitor Match — monitor chastota javobi profillari.
// Qiymatlar dB'da, o'rta chastota (~200 Hz – 2 kHz) atrofida 0 dB ga normallashtirilgan.
// DIQQAT: bu ishlab chiqaruvchilar e'lon qilgan grafiklar va mustaqil o'lchovlar asosida
// tuzilgan TAXMINIY egri chiziqlar. Ularni o'zgartirish uchun shu faylni tahrirlang.

#include <vector>
#include <cmath>
#include <algorithm>

namespace mm
{
constexpr double kPi = 3.14159265358979323846;

struct CurvePoint
{
    double hz;
    double db;
};

struct MonitorProfile
{
    const char* brand;
    const char* model;
    const char* note;
    std::vector<CurvePoint> points;
};

// Log-chastota o'qida silliq (kosinus) interpolyatsiya.
inline double interpolateDb (const std::vector<CurvePoint>& pts, double hz)
{
    if (pts.empty())               return 0.0;
    if (hz <= pts.front().hz)      return pts.front().db;
    if (hz >= pts.back().hz)       return pts.back().db;

    auto it = std::upper_bound (pts.begin(), pts.end(), hz,
                                [] (double f, const CurvePoint& p) { return f < p.hz; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double t = std::log (hz / a.hz) / std::log (b.hz / a.hz);
    const double s = 0.5 - 0.5 * std::cos (t * kPi);
    return a.db + (b.db - a.db) * s;
}

// Sizning monitorlaringiz (manba).
inline const std::vector<MonitorProfile>& sourceProfiles()
{
    static const std::vector<MonitorProfile> p {
        { "KRK", "Rokit 6 G4", "6\" woofer, bass ko'tarilgan",
          { {20,-30},{30,-18},{38,-9},{45,-3},{55,1.5},{70,2.5},{90,2.0},{120,0.8},{200,0},
            {350,-1.0},{600,-0.5},{1000,0},{2000,0.8},{3500,1.5},{5000,0.8},{8000,0.5},
            {12000,1.0},{16000,0},{20000,-2},{24000,-6} } },
        { "ADAM AUDIO", "A8X", "8.5\" woofer, X-ART tviter",
          { {20,-24},{28,-12},{33,-6},{38,-3},{50,1.0},{65,1.2},{100,0.5},{200,0},{500,0},
            {1000,-0.8},{2000,-0.3},{4000,0.5},{7000,1.5},{10000,2.0},{14000,2.0},{18000,1.0},
            {22000,-1},{24000,-2} } },
    };
    return p;
}

// Eshitmoqchi bo'lgan monitorlar (maqsad).
inline const std::vector<MonitorProfile>& targetProfiles()
{
    static const std::vector<MonitorProfile> p {
        { "FOCAL", "Shape 65", "Tiniq o'rta chastota",
          { {20,-24},{30,-14},{40,-6},{50,-2},{70,0},{150,0.3},{400,0},{1000,0.5},{1800,1.0},
            {3000,0.3},{5000,-0.5},{8000,0},{12000,0.5},{18000,-0.5},{22000,-3},{24000,-5} } },
        { "GENELEC", "8340A", "Eng tekis javob",
          { {20,-18},{30,-9},{38,-3},{45,-1},{60,0},{20000,0},{22000,-1},{24000,-2} } },
        { "YAMAHA", "HS8", "Halol, oldinga chiqqan o'rta",
          { {20,-26},{30,-14},{38,-6},{45,-2.5},{60,0},{100,0.5},{250,0},{700,-0.3},{1500,0.5},
            {2500,2.0},{3500,2.0},{5000,0.5},{8000,0},{12000,0.5},{16000,0},{20000,-2},{24000,-6} } },
        { "YAMAHA", "NS-10M", "Klassik, bass kam, o'rta qattiq",
          { {20,-36},{40,-20},{60,-11},{80,-6},{100,-4},{150,-1.5},{250,0},{500,0.5},{1000,1.5},
            {1500,2.5},{2000,2.0},{3000,0.5},{5000,0},{8000,-0.5},{12000,-1.0},{16000,-3},
            {20000,-7},{24000,-10} } },
        { "REFERENCE", "Flat", "Ideal tekis javob",
          { {20,0},{24000,0} } },
    };
    return p;
}
} // namespace mm
