#pragma once
#include "Profiles.h"
#include <string>

namespace mm
{
// Korreksiya sozlamalari (parametrlardan yig'iladi).
struct Settings
{
    int    source     = 0;      // sourceProfiles() indeksi; oxirgisidan keyingisi = o'z o'lchovingiz
    int    target     = 1;      // targetProfiles() indeksi
    double amount     = 1.0;    // 0..1 — emulyatsiya kuchi
    double bass       = 0.85;   // 0..1 — 250 Hz dan pastdagi korreksiya kuchi
    double maxBoostDb = 6.0;    // ko'tarishning yuqori chegarasi
    bool   minPhase   = false;  // false = linear phase (mastering), true = zero latency
    bool   autoGain   = true;   // balandlikni avtomatik tenglash

    bool operator== (const Settings& o) const
    {
        auto same = [] (double a, double b) { return std::abs (a - b) < 1.0e-9; };
        return source == o.source && target == o.target && same (amount, o.amount) && same (bass, o.bass)
            && same (maxBoostDb, o.maxBoostDb) && minPhase == o.minPhase && autoGain == o.autoGain;
    }
    bool operator!= (const Settings& o) const { return ! (*this == o); }
};

class CorrectionModel
{
public:
    std::vector<CurvePoint> custom;   // foydalanuvchi o'lchovi (REW va h.k.)

    double sourceDb (const Settings&, double hz) const;
    double targetDb (const Settings&, double hz) const;
    double rawCorrectionDb (const Settings&, double hz) const;
    double smoothedCorrectionDb (const Settings&, double hz) const;
    double gainCompDb (const Settings&) const;
    double correctionDb (const Settings&, double hz) const;

    // Mono FIR impuls javobi. Linear phase'da kechikish = length / 2.
    std::vector<float> buildImpulse (const Settings&, double sampleRate, int length) const;
};

int firLengthFor (double sampleRate);

// REW / ARTA / boshqa dasturlardan eksport qilingan "chastota  dB" matnini o'qiydi,
// 1/6 oktava silliqlaydi va 300 Hz–3 kHz bo'yicha 0 dB ga normallashtiradi.
std::vector<CurvePoint> parseMeasurementText (const std::string& text);
} // namespace mm
