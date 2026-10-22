#include "PluginEditor.h"

using namespace az::ui;

namespace
{
    constexpr int kContentX = 76, kContentY = 84, kContentW = 1180, kContentH = 796;
    const char* kTabNames[] = { "Tahlil", "Rack \xc2\xb7 Zanjir", "Hisobot", "Sozlamalar" };
    const int kTabX[] = { 346, 440, 568, 662 }, kTabW[] = { 90, 124, 90, 108 };
}

//==============================================================================
AZRootView::AZRootView (AZMonitoringProcessor& p)
    : proc (p), chainPage (p), reportPage (p), settingsPage (p), probePage (p), browser (p)
{
    setSize (W, H);

    for (int i = 0; i < 4; ++i)
    {
        auto& t = tabs[i];
        t.setButtonText (juce::String::fromUTF8 (kTabNames[i]));
        t.setRadioGroupId (1001);
        t.setClickingTogglesState (true);
        t.getProperties().set ("noOutline", true);
        t.setColour (juce::TextButton::buttonColourId, strip);
        t.setColour (juce::TextButton::buttonOnColourId, border);
        t.setColour (juce::TextButton::textColourOffId, text2);
        t.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (t);
    }
    tabs[0].setToggleState (true, juce::dontSendNotification);

    fillChoices (profileBox, proc, "profile");
    lastProfileCount = profileBox.getNumItems();
    profileA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "profile", profileBox);
    addAndMakeVisible (profileBox);

    resetButton.getProperties().set ("bold", true);
    resetButton.setColour (juce::TextButton::buttonColourId, accent);
    resetButton.setColour (juce::TextButton::textColourOffId, bg);
    resetButton.setTooltip ("Shu guruhdagi monitor va barcha probe'larda tahlilni noldan boshlaydi");
    resetButton.onClick = [this] { proc.resetAnalysis(); };
    addAndMakeVisible (resetButton);

    for (auto* c : std::initializer_list<juce::Component*> { &analysisPage, &chainPage, &reportPage, &settingsPage, &probePage })
        addChildComponent (c);

    addChildComponent (browser);
    chainPage.onAddPlugin = [this] (int idx) { browser.open (idx, data); };
    browser.onAdded = [this] (juce::uint32 id) { chainPage.selectRackSlot (id); timerCallback(); };

    updateModeLayout();
    timerCallback();
    startTimerHz (8);
}

AZRootView::~AZRootView() { stopTimer(); }

void AZRootView::showPage (int i)
{
    currentPage = i;
    analysisPage.setVisible (i == 0);
    chainPage.setVisible (i == 1);
    reportPage.setVisible (i == 2);
    settingsPage.setVisible (i == 3);
    if (i == 3) settingsPage.refresh();
}

void AZRootView::updateModeLayout()
{
    const bool probe = proc.isProbe();
    lastMode = probe ? 1 : 0;
    for (auto& t : tabs) t.setVisible (! probe);
    profileBox.setVisible (! probe);
    probePage.setVisible (probe);
    if (probe) browser.setVisible (false);
    if (probe)
    {
        analysisPage.setVisible (false);
        chainPage.setVisible (false);
        reportPage.setVisible (false);
        settingsPage.setVisible (false);
    }
    else
        showPage (currentPage);
    repaint();
}

void AZRootView::timerCallback()
{
    if ((proc.isProbe() ? 1 : 0) != lastMode)
        updateModeLayout();

    data.profile = &proc.currentProfile();
    data.standard = &proc.currentStandard();
    data.master = proc.isMasterMode();
    data.group = proc.getGroupKey();
    data.track = proc.getTrackName();
    data.duplicateSlots.clear();

    if (proc.isProbe())
    {
        data.finalOut = proc.analyzer.getSnapshot();
        data.report = az::Report();
        data.monitorCount = az::ProbeRegistry::instance().countPoints (data.group, 0);
        probePage.setData (data);
    }
    else
    {
        data.report = proc.buildReport (data.finalOut, data.probeCount, data.duplicateSlots);
        if (analysisPage.isVisible()) analysisPage.setData (data);
        if (chainPage.isVisible())    chainPage.setData (data);
        if (reportPage.isVisible())   reportPage.setData (data);
        if (browser.isVisible())      browser.refresh (data);
    }
    repaint (0, 0, W, kContentY);
}

void AZRootView::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // Chap tomondagi brend chizig'i
    g.setColour (strip);
    g.fillRect (0, 0, 52, H);
    g.setColour (border);
    g.drawVerticalLine (52, 0.0f, (float) H);

    g.setColour (accent);
    g.fillRoundedRectangle (13.0f, 22.0f, 26.0f, 26.0f, 6.0f);
    g.setColour (bg);
    g.setFont (sans (13.0f, true));
    g.drawText ("AZ", juce::Rectangle<float> (13, 22, 26, 26), juce::Justification::centred);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, 26.0f, 470.0f));
        g.setColour (text2);
        g.setFont (sans (16.0f, true).withExtraKerningFactor (0.35f));
        g.drawText ("AZIMXODJAYEV MUSIC", juce::Rectangle<float> (26.0f - 300.0f, 460.0f, 600.0f, 22.0f), juce::Justification::centred);
    }

    const bool probe = proc.isProbe();
    g.setColour (probe ? accent : ok);
    g.fillEllipse (23.0f, (float) H - 34, 6.0f, 6.0f);

    // Sarlavha
    g.setColour (text);
    g.setFont (sans (30.0f, true).withExtraKerningFactor (0.03f));
    g.drawText ("AZ MONITORING", juce::Rectangle<float> (kContentX, 20, 260, 48), juce::Justification::centredLeft);

    if (probe)
    {
        g.setColour (accent.withAlpha (0.15f));
        g.fillRoundedRectangle (juce::Rectangle<float> (346, 30, 220, 30), 15.0f);
        g.setColour (accent);
        g.setFont (sans (13.0f, true));
        g.drawText ("PROBE  |  " + juce::String (az::ProbeRegistry::slotName (proc.getSlotIndex())) + " nuqtasi",
                    juce::Rectangle<float> (346, 30, 220, 30), juce::Justification::centred);
    }
    else
    {
        g.setColour (strip);
        g.fillRoundedRectangle (juce::Rectangle<float> (342, 24, 432, 42), 9.0f);
        g.setColour (border);
        g.drawRoundedRectangle (juce::Rectangle<float> (342.5f, 24.5f, 431, 41), 9.0f, 1.0f);
    }

    // Holat satri
    g.setColour (text3);
    g.setFont (sans (11.5f));
    juce::String status = probe ? "Guruh: " + data.group
                                : juce::String (data.probeCount) + " probe | " + (data.track.isNotEmpty() ? data.track : data.group);
    g.drawText (status, juce::Rectangle<float> (kContentX + kContentW - 260, 2, 260, 18), juce::Justification::centredRight, true);
}

void AZRootView::resized()
{
    for (int i = 0; i < 4; ++i)
        tabs[i].setBounds (kTabX[i], 28, kTabW[i], 34);
    profileBox.setBounds (kContentX + kContentW - 250 - 140, 26, 250, 38);
    resetButton.setBounds (kContentX + kContentW - 130, 26, 130, 38);

    const juce::Rectangle<int> content (kContentX, kContentY, kContentW, kContentH);
    analysisPage.setBounds (content);
    chainPage.setBounds (content);
    reportPage.setBounds (content);
    settingsPage.setBounds (content);
    probePage.setBounds (content);
    browser.setBounds (53, 0, W - 53, H);
}

//==============================================================================
AZMonitoringEditor::AZMonitoringEditor (AZMonitoringProcessor& p)
    : AudioProcessorEditor (&p), root (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (root);
    setResizable (true, true);
    setResizeLimits (W / 2, H / 2, W * 3 / 2, H * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) W / (double) H);
    setSize (W * 7 / 8, H * 7 / 8);
}

AZMonitoringEditor::~AZMonitoringEditor()
{
    setLookAndFeel (nullptr);
}

void AZMonitoringEditor::resized()
{
    const float s = (float) getWidth() / (float) W;
    root.setTransform (juce::AffineTransform::scale (s));
    root.setBounds (0, 0, W, H);
}
