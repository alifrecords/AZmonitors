#pragma once
#include "PluginProcessor.h"
#include "gui/Views.h"
#include "gui/RackViews.h"

//==============================================================================
// Asosiy oyna: 1280x900 mantiqiy o'lchamda chiziladi va masshtablanadi
class AZRootView final : public juce::Component,
                         private juce::Timer
{
public:
    explicit AZRootView (AZMonitoringProcessor&);
    ~AZRootView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void selectPage (int i) { if (i >= 0 && i < 4) { tabs[i].setToggleState (true, juce::dontSendNotification); showPage (i); timerCallback(); } }
    void showBrowser (int insertIndex) { browser.open (insertIndex, data); }

private:
    void timerCallback() override;
    void showPage (int);
    void updateModeLayout();

    AZMonitoringProcessor& proc;
    az::ui::ViewData data;

    juce::TextButton tabs[4];
    juce::ComboBox profileBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> profileA;
    juce::TextButton resetButton { "Qayta tahlil" };

    az::ui::AnalysisPage analysisPage;
    az::ui::RackPage chainPage;
    az::ui::ReportPage reportPage;
    az::ui::SettingsPage settingsPage;
    az::ui::ProbePage probePage;
    az::ui::BrowserOverlay browser;

    int currentPage = 0;
    int lastMode = -1;
    int lastProfileCount = 0;
};

class AZMonitoringEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AZMonitoringEditor (AZMonitoringProcessor&);
    ~AZMonitoringEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (az::ui::bg); }
    void selectPage (int i) { root.selectPage (i); }
    void showBrowser (int i) { root.showBrowser (i); }

private:
    az::ui::AZLookAndFeel lookAndFeel;
    AZRootView root;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AZMonitoringEditor)
};
