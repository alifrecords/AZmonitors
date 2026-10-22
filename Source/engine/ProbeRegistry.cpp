#include "ProbeRegistry.h"

namespace az
{
ProbeRegistry& ProbeRegistry::instance()
{
    static ProbeRegistry reg;
    return reg;
}

juce::String ProbeRegistry::slotName (int i)
{
    if (i <= 0)  return "IN";
    if (i >= 11) return "OUT";
    return juce::String::charToString ((juce::juce_wchar) ('A' + i - 1));
}

void ProbeRegistry::add (std::shared_ptr<SharedPoint> p)
{
    std::lock_guard<std::mutex> lg (mutex);
    points.push_back (p);
}

void ProbeRegistry::remove (const SharedPoint* p)
{
    std::lock_guard<std::mutex> lg (mutex);
    points.erase (std::remove_if (points.begin(), points.end(), [p] (const std::weak_ptr<SharedPoint>& w)
    {
        auto s = w.lock();
        return s == nullptr || s.get() == p;
    }), points.end());
}

std::vector<ProbeRegistry::ProbeInfo> ProbeRegistry::probesInGroup (const juce::String& group, juce::StringArray* dups) const
{
    std::vector<std::shared_ptr<SharedPoint>> live;
    {
        std::lock_guard<std::mutex> lg (mutex);
        for (auto& w : points)
            if (auto s = w.lock())
                live.push_back (s);
    }

    std::vector<ProbeInfo> out;
    for (auto& s : live)
    {
        if (s->mode.load() != 1) continue;
        juce::String g, n;
        s->getText (g, n);
        if (g != group) continue;

        ProbeInfo info;
        info.slotIndex = s->slotIndex.load();
        info.name = n;
        info.instanceId = s->instanceId;
        info.features = s->getFeatures();
        out.push_back (std::move (info));
    }

    std::sort (out.begin(), out.end(), [] (const ProbeInfo& a, const ProbeInfo& b)
    {
        return a.slotIndex != b.slotIndex ? a.slotIndex < b.slotIndex : a.instanceId < b.instanceId;
    });

    // Bir slotga bir nechta probe qo'yilgan bo'lsa — oxirgisini qoldiramiz
    std::vector<ProbeInfo> unique;
    for (auto& p : out)
    {
        if (! unique.empty() && unique.back().slotIndex == p.slotIndex)
        {
            if (dups != nullptr) dups->addIfNotAlreadyThere (slotName (p.slotIndex));
            unique.back() = std::move (p);
        }
        else unique.push_back (std::move (p));
    }
    return unique;
}

int ProbeRegistry::countPoints (const juce::String& group, int mode) const
{
    std::lock_guard<std::mutex> lg (mutex);
    int n = 0;
    for (auto& w : points)
        if (auto s = w.lock())
        {
            if (s->mode.load() != mode) continue;
            juce::String g, nm;
            s->getText (g, nm);
            if (g == group) ++n;
        }
    return n;
}

void ProbeRegistry::requestGroupReset (const juce::String& group)
{
    std::lock_guard<std::mutex> lg (mutex);
    for (auto& w : points)
        if (auto s = w.lock())
        {
            juce::String g, n;
            s->getText (g, n);
            if (g == group) s->resetRequested = true;
        }
}

} // namespace az
