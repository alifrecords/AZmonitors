#include "Profiles.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace az
{
namespace
{
    using Points = std::vector<std::pair<float, float>>;   // (Hz, dB)

    float interp (const Points& pts, float hz)
    {
        if (hz <= pts.front().first) return pts.front().second;
        if (hz >= pts.back().first)  return pts.back().second;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            if (hz <= pts[i].first)
            {
                const float l0 = std::log2 (pts[i - 1].first), l1 = std::log2 (pts[i].first);
                const float t = (std::log2 (hz) - l0) / (l1 - l0);
                return pts[i - 1].second + t * (pts[i].second - pts[i - 1].second);
            }
        }
        return pts.back().second;
    }

    Profile make (const juce::String& name, const Points& curve, float crestMin, float crestMax, float sibMax)
    {
        Profile p;
        p.name = name;
        p.crestMin = crestMin; p.crestMax = crestMax; p.sibMax = sibMax;

        double s = 0; int c = 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float fc = bandCentre (b);
            p.target[(size_t) b] = interp (curve, fc);
            if (fc >= 200.0f && fc <= 5000.0f) { s += p.target[(size_t) b]; ++c; }
            p.tol[(size_t) b] = (fc >= 150.0f && fc <= 8000.0f) ? 3.5f : (fc < 150.0f || fc <= 14000.0f ? 5.0f : 7.0f);
        }
        const float ref = (float) (s / c);
        for (auto& v : p.target) v -= ref;   // tahlilchi bilan bir xil normallashtirish
        return p;
    }
}

const std::vector<Profile>& Profiles::builtIns()
{
    static const std::vector<Profile> list = []
    {
        std::vector<Profile> v;
        v.push_back (make ("Pop - Lead Vocal",
            { {20,-45},{50,-36},{80,-22},{110,-10},{160,-3},{250,0},{400,0.5f},{600,-0.5f},{1000,-3},
              {2000,-6},{3200,-6.5f},{5000,-9},{8000,-12},{12000,-15},{16000,-21},{20000,-32} }, 9.0f, 13.0f, 4.0f));
        v.push_back (make ("Hip-Hop / Rap Vocal",
            { {20,-42},{50,-32},{80,-18},{110,-7},{160,-1},{250,1},{400,1},{600,0},{1000,-3},
              {2000,-6},{3200,-6},{5000,-9.5f},{8000,-13},{12000,-17},{16000,-23},{20000,-34} }, 8.0f, 12.0f, 5.0f));
        v.push_back (make ("R&B - Lead Vocal",
            { {20,-45},{50,-36},{80,-21},{110,-9},{160,-2.5f},{250,0},{400,0},{600,-1},{1000,-3.5f},
              {2000,-7},{3200,-7.5f},{5000,-9.5f},{8000,-11.5f},{12000,-14},{16000,-19},{20000,-30} }, 10.0f, 14.0f, 3.0f));
        v.push_back (make ("O'zbek estrada - Lead Vocal",
            { {20,-45},{50,-36},{80,-22},{110,-10},{160,-3.5f},{250,-0.5f},{400,0},{600,-0.5f},{1000,-2.5f},
              {2000,-5},{3200,-5},{5000,-8},{8000,-11},{12000,-14.5f},{16000,-20},{20000,-31} }, 9.0f, 13.0f, 4.0f));
        v.push_back (make ("Rok - Lead Vocal",
            { {20,-45},{50,-36},{80,-23},{110,-11},{160,-4},{250,0},{400,1},{600,0.5f},{1000,-1.5f},
              {2000,-4.5f},{3200,-5.5f},{5000,-9},{8000,-13},{12000,-17},{16000,-23},{20000,-34} }, 8.0f, 12.0f, 4.0f));
        return v;
    }();
    return list;
}

const std::vector<DeliveryStandard>& Profiles::standards()
{
    static const std::vector<DeliveryStandard> list {
        { "Streaming (Spotify/YouTube) -14 LUFS / -1 dBTP", -14.0f, -1.0f },
        { "Apple Music -16 LUFS / -1 dBTP",                 -16.0f, -1.0f },
        { "EBU R128 (TV/radio) -23 LUFS / -1 dBTP",         -23.0f, -1.0f },
        { "ATSC A/85 (AQSh TV) -24 LKFS / -2 dBTP",         -24.0f, -2.0f },
    };
    return list;
}

Profile Profiles::fromReferences (const std::vector<Features>& refs, const juce::String& name)
{
    Profile p = builtIns().front();
    p.name = name;
    p.custom = true;

    std::vector<const Features*> ok;
    for (auto& r : refs)
        if (r.spectrumValid && r.integrated > -70.0f)
            ok.push_back (&r);
    p.numReferences = (int) ok.size();
    if (ok.empty())
        return p;

    auto meanStd = [&ok] (auto getter, float& mean, float& sd)
    {
        double s = 0, s2 = 0; int n = 0;
        for (auto* f : ok) { const double v = getter (*f); if (v <= kNoValue + 1) continue; s += v; s2 += v * v; ++n; }
        mean = n > 0 ? (float) (s / n) : 0.0f;
        sd = n > 1 ? (float) std::sqrt (juce::jmax (0.0, s2 / n - (s / n) * (s / n))) : 0.0f;
        return n;
    };

    for (int b = 0; b < kNumBands; ++b)
    {
        float m, sd;
        if (meanStd ([b] (const Features& f) { return (double) f.bandRel[(size_t) b]; }, m, sd) > 0)
        {
            p.target[(size_t) b] = m;
            const float fc = bandCentre (b);
            const float base = (fc >= 150.0f && fc <= 8000.0f) ? 2.5f : 4.0f;
            p.tol[(size_t) b] = juce::jlimit (base, 8.0f, juce::jmax (base, 1.5f * sd));
        }
    }

    float m, sd;
    if (meanStd ([] (const Features& f) { return (double) f.crest; }, m, sd) > 0)
    {
        const float half = juce::jmax (1.5f, 1.5f * sd);
        p.crestMin = m - half; p.crestMax = m + half;
    }
    if (meanStd ([] (const Features& f) { return (double) f.lra; }, m, sd) > 0)
    {
        const float half = juce::jmax (2.0f, 1.5f * sd);
        p.lraMin = juce::jmax (0.5f, m - half); p.lraMax = m + half;
    }
    if (meanStd ([] (const Features& f) { return (double) f.sibilanceP97; }, m, sd) > 0)
        p.sibMax = m + juce::jmax (1.5f, sd);
    if (meanStd ([] (const Features& f) { return (double) f.noiseFloor; }, m, sd) > 0)
        p.noiseFloorMax = juce::jmax (-60.0f, m + 6.0f);

    return p;
}

juce::File Profiles::customProfileFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Azimxodjayev Music").getChildFile ("AZ Monitoring").getChildFile ("custom_profile.json");
}

bool Profiles::saveCustom (const Profile& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", p.name);
    o->setProperty ("refs", p.numReferences);
    juce::Array<juce::var> tgt, tol;
    for (int b = 0; b < kNumBands; ++b) { tgt.add (p.target[(size_t) b]); tol.add (p.tol[(size_t) b]); }
    o->setProperty ("target", tgt);
    o->setProperty ("tol", tol);
    o->setProperty ("crestMin", p.crestMin);   o->setProperty ("crestMax", p.crestMax);
    o->setProperty ("lraMin", p.lraMin);       o->setProperty ("lraMax", p.lraMax);
    o->setProperty ("sibMax", p.sibMax);       o->setProperty ("noiseFloorMax", p.noiseFloorMax);

    const auto file = customProfileFile();
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (o)));
}

bool Profiles::loadCustom (Profile& p)
{
    const auto file = customProfileFile();
    if (! file.existsAsFile()) return false;
    const auto v = juce::JSON::parse (file);
    if (! v.isObject()) return false;

    const auto* tgt = v["target"].getArray();
    const auto* tol = v["tol"].getArray();
    if (tgt == nullptr || tol == nullptr || tgt->size() != kNumBands || tol->size() != kNumBands) return false;

    p = builtIns().front();
    p.custom = true;
    p.name = v["name"].toString();
    p.numReferences = (int) v["refs"];
    for (int b = 0; b < kNumBands; ++b) { p.target[(size_t) b] = (float) (*tgt)[b]; p.tol[(size_t) b] = (float) (*tol)[b]; }
    p.crestMin = (float) v["crestMin"]; p.crestMax = (float) v["crestMax"];
    p.lraMin = (float) v["lraMin"];     p.lraMax = (float) v["lraMax"];
    p.sibMax = (float) v["sibMax"];     p.noiseFloorMax = (float) v["noiseFloorMax"];
    return true;
}

std::vector<Features> Profiles::analyseReferenceFiles (const juce::Array<juce::File>& files, juce::StringArray& errors)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::vector<Features> out;

    for (auto& f : files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr) { errors.add (f.getFileName() + ": o'qib bo'lmadi"); continue; }
        if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 60 * 15)) { errors.add (f.getFileName() + ": 15 daqiqadan uzun"); continue; }

        juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), (int) reader->lengthInSamples);
        reader->read (&buf, 0, buf.getNumSamples(), 0, true, buf.getNumChannels() > 1);
        out.push_back (Analyzer::analyseBuffer (buf, reader->sampleRate));
    }
    return out;
}

} // namespace az
