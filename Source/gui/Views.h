#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

class AZMonitoringProcessor;

namespace az::ui
{
//==============================================================================
struct ViewData
{
    az::Features finalOut;
    az::Report report;
    int probeCount = 0, monitorCount = 0;
    juce::StringArray duplicateSlots;
    const az::Profile* profile = nullptr;
    const az::DeliveryStandard* standard = nullptr;
    bool master = false;
    juce::String group, track;
};

//==============================================================================
class SpectrumView final : public juce::Component
{
public:
    void setData (const ViewData& d) { data = &d; repaint(); }
    void setTitle (const juce::String& t) { title = t; }
    void paint (juce::Graphics&) override;
private:
    const ViewData* data = nullptr;
    juce::String title { "Spektral balans - etalon koridori bilan" };
};

class SummaryPanel final : public juce::Component
{
public:
    void setData (const ViewData& d) { data = &d; repaint(); }
    void paint (juce::Graphics&) override;
private:
    const ViewData* data = nullptr;
};

class ChecksList final : public juce::Component
{
public:
    void setData (const ViewData& d);
    void paint (juce::Graphics&) override;
private:
    const ViewData* data = nullptr;
    std::vector<float> rowHeights;
    float layoutRows (juce::Graphics* g);
};

class TimelinePanel final : public juce::Component
{
public:
    void setData (const ViewData& d) { data = &d; repaint(); }
    void paint (juce::Graphics&) override;
private:
    const ViewData* data = nullptr;
};

class AnalysisPage final : public juce::Component
{
public:
    AnalysisPage();
    void setData (const ViewData&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    SpectrumView spectrum;
    SummaryPanel summary;
    juce::Viewport checksViewport;
    ChecksList checks;
    TimelinePanel timeline;
};

//==============================================================================
class StageTable final : public juce::Component
{
public:
    std::function<void (int)> onSelect;
    void setData (const ViewData& d, int selectedIndex);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    const ViewData* data = nullptr;
    int selected = -1;
    static constexpr int rowH = 72;
};

class StageDetail final : public juce::Component
{
public:
    void setData (const ViewData& d, int selectedIndex) { data = &d; selected = selectedIndex; repaint(); }
    void paint (juce::Graphics&) override;
private:
    const ViewData* data = nullptr;
    int selected = -1;
};

class ChainPage final : public juce::Component
{
public:
    ChainPage();
    void setData (const ViewData&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    const ViewData* data = nullptr;
    juce::Viewport tableViewport;
    StageTable table;
    StageDetail detail;
    int selected = -1;
    bool userSelected = false;
};

//==============================================================================
class ReportPage final : public juce::Component
{
public:
    explicit ReportPage (AZMonitoringProcessor&);
    void setData (const ViewData&);
    void resized() override;
private:
    AZMonitoringProcessor& proc;
    juce::TextEditor editor;
    juce::TextButton copyButton { "Nusxalash" }, saveButton { "Faylga saqlash (.txt)" };
    juce::String text;
    juce::uint32 lastUpdate = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
class SettingsPage final : public juce::Component
{
public:
    explicit SettingsPage (AZMonitoringProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
private:
    AZMonitoringProcessor& proc;
    using ComboAttach = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    juce::ComboBox mode, group, source, standard;
    std::unique_ptr<ComboAttach> modeA, groupA, sourceA, standardA;
    juce::ToggleButton playOnly { "Faqat ijro (Play) paytida tahlil qilish" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playOnlyA;
    juce::TextButton makeProfile { juce::String::fromUTF8 ("Etalon treklarni tanlash…") };
    juce::String profileStatus;
    std::unique_ptr<juce::FileChooser> chooser;
    bool busy = false;
};

//==============================================================================
class ProbePage final : public juce::Component
{
public:
    explicit ProbePage (AZMonitoringProcessor&);
    void setData (const ViewData&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    AZMonitoringProcessor& proc;
    const ViewData* data = nullptr;
    using ComboAttach = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    juce::ComboBox mode, slot, group;
    std::unique_ptr<ComboAttach> modeA, slotA, groupA;
    juce::TextEditor label;
    SpectrumView spectrum;
};

void fillChoices (juce::ComboBox&, AZMonitoringProcessor&, const juce::String& paramID);

} // namespace az::ui
