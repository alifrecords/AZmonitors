#pragma once
#include "Profiles.h"

namespace az
{
enum class Status { Ideal, Warn, Problem, Redundant, Info, NoData };

juce::String statusLabel (Status);

struct Check
{
    juce::String title, measured, target, advice;
    Status status = Status::NoData;
    int score = 0;
    float freqLo = 0, freqHi = 0;       // spektrdagi muammo zonasi (agar bor bo'lsa)
};

/** Ikki o'lchov nuqtasi orasidagi plagin(lar) bahosi */
struct StageResult
{
    juce::String slot, name, role, effect, advice;
    Status status = Status::NoData;
    int score = 0;
    bool valid = false;

    float gainChange = 0, crestBefore = 0, crestAfter = 0;
    float sibBefore = 0, sibAfter = 0, snrChange = 0;
    float levelIn = kNoValue, tpOut = kNoValue;
    std::array<float, kNumBands> bandDelta {};
};

/** Zanjirdagi o'lchov nuqtasi: IN probe, slot probe'lari va oxirida AZ Monitoring */
struct ChainPoint
{
    int slotIndex = 0;                  // 0 = IN, 1..10 = A..J, 11 = OUT (monitor)
    juce::String slot, name;
    Features f;
};

struct Report
{
    bool hasData = false;
    int score = 0;
    juce::String verdict;
    std::vector<Check> checks;
    int nIdeal = 0, nWarn = 0, nProblem = 0;

    std::vector<StageResult> stages;
    int chainScore = 0, stIdeal = 0, stWarn = 0, stProblem = 0, stRedundant = 0;
    juce::String chainHint;
};

Report diagnose (const Features& finalOut, const Profile&, const DeliveryStandard&, bool masterMode);
void   diagnoseChain (const std::vector<ChainPoint>& points, const Profile&, Report& report);
juce::String reportToText (const Report&, const Profile&, const DeliveryStandard&, bool masterMode, const juce::String& trackName);

juce::String fmtDb (float v, int decimals = 1, const char* unit = " dB");
juce::String fmtTime (float seconds);
juce::String fmtHz (float hz);

} // namespace az
