#pragma once
#include "dsp/Analyzer.h"
#include <memory>
#include <mutex>

namespace az
{
//==============================================================================
// Bir xil xost jarayonidagi barcha AZ Monitoring nusxalari (Probe va Monitor)
// shu registr orqali bir-birini ko'radi. Audio oqimi bu yerga tegmaydi —
// faqat fon/GUI oqimlari.
struct SharedPoint
{
    std::atomic<int>  mode { 0 };         // 0 = Monitor, 1 = Probe
    std::atomic<int>  slotIndex { 0 };    // 0 = IN, 1..10 = A..J
    std::atomic<bool> resetRequested { false };
    juce::uint32 instanceId = 0;

    void setText (const juce::String& groupKey, const juce::String& label)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        group = groupKey; name = label;
    }
    void getText (juce::String& groupKey, juce::String& label) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        groupKey = group; label = name;
    }
    void setFeatures (Features f)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        features = std::move (f);
    }
    Features getFeatures() const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return features;
    }

private:
    mutable juce::SpinLock lock;
    juce::String group, name;
    Features features;
};

class ProbeRegistry
{
public:
    void add (std::shared_ptr<SharedPoint>);
    void remove (const SharedPoint*);

    struct ProbeInfo
    {
        int slotIndex = 0;
        juce::String name;
        Features features;
        juce::uint32 instanceId = 0;
    };

    /** Shu guruhdagi Probe'lar (slot tartibida). duplicateSlots — bir slotda bir nechta probe. */
    std::vector<ProbeInfo> probesInGroup (const juce::String& group, juce::StringArray* duplicateSlots = nullptr) const;
    int countPoints (const juce::String& group, int mode) const;   // mode: 0 = Monitor, 1 = Probe
    void requestGroupReset (const juce::String& group);

    static juce::String slotName (int slotIndex);
    static ProbeRegistry& instance();

private:
    mutable std::mutex mutex;
    std::vector<std::weak_ptr<SharedPoint>> points;
};

} // namespace az
