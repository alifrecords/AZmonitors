#pragma once
#include "dsp/Analyzer.h"

namespace az
{
//==============================================================================
// Janr etaloni: lead vokalning uzoq muddatli spektri (1/6 oktava, 200 Hz–5 kHz
// o'rtachasiga normallashtirilgan) va dinamika me'yorlari.
// Ichki etalonlar — boshlang'ich kalibrovka. Eng aniq natija uchun foydalanuvchi
// o'zi tanlagan professional treklardan "Maxsus etalon" yaratadi.
struct Profile
{
    juce::String name;
    std::array<float, kNumBands> target {};
    std::array<float, kNumBands> tol {};
    float crestMin = 9, crestMax = 13;      // dB (P99.5 peak - faol RMS)
    float lraMin = 2.5f, lraMax = 8;        // LU
    float sibMax = 4;                       // dB, sibilyant indeksining 97-persentili
    float noiseFloorMax = -60;              // dBFS
    bool  custom = false;
    int   numReferences = 0;
};

struct DeliveryStandard
{
    juce::String name;
    float lufs = -14.0f;
    float truePeakMax = -1.0f;
};

namespace Profiles
{
    /** Ichki janr etalonlari: Pop, Hip-Hop/Rap, R&B, O'zbek estrada, Rok */
    const std::vector<Profile>& builtIns();
    const std::vector<DeliveryStandard>& standards();

    /** Etalon treklar tahlilidan maxsus profil yaratadi (kamida 1 ta). */
    Profile fromReferences (const std::vector<Features>& refs, const juce::String& name);

    juce::File customProfileFile();
    bool saveCustom (const Profile&);
    bool loadCustom (Profile& out);

    /** Etalon audio fayllarni tahlil qiladi (WAV/AIFF/FLAC/MP3...). */
    std::vector<Features> analyseReferenceFiles (const juce::Array<juce::File>& files, juce::StringArray& errors);
}

} // namespace az
