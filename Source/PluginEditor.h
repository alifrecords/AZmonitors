#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace mmui
{
namespace col
{
    const juce::Colour bg      { 0xff15171a };
    const juce::Colour header  { 0xff1b1e22 };
    const juce::Colour panel   { 0xff1e2126 };
    const juce::Colour line    { 0xff2a2e35 };
    const juce::Colour line2   { 0xff3a3f47 };
    const juce::Colour text    { 0xffe8e6e1 };
    const juce::Colour muted   { 0xff9aa0a6 };
    const juce::Colour accent  { 0xfff2994a };
    const juce::Colour blue    { 0xff5aa9e6 };
    const juce::Colour green   { 0xff6fcf97 };
    const juce::Colour graphBg { 0xff101215 };
}

juce::Font font (float h, bool bold = false);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.0f, true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
};

// Monitor kartasi (brend / model / izoh).
class CardButton : public juce::Button
{
public:
    CardButton (juce::String brand, juce::String model, juce::String note, juce::Colour selected);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::String brand, model, note;
    juce::Colour selectedColour, brandColour { col::muted };
    juce::String badge;   // "A" / "B" belgisi
};

class CurveGraph : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    std::vector<float> freqs, src, tgt, corr;
    juce::String srcName, tgtName;
    bool bypassed = false;
private:
    float xFor (float hz, juce::Rectangle<float> r) const;
    float yFor (float db, juce::Rectangle<float> r) const;
};

class Meter : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void push (float peakL, float peakR);
private:
    float level[2] {};
};

class MainView : public juce::Component, private juce::Timer
{
public:
    explicit MainView (MonitorMatchProcessor&);
    ~MainView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void setChoice (const juce::String& id, int index);
    void setBool (const juce::String& id, bool v);
    void chooseMeasurement();

    MonitorMatchProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;

    juce::OwnedArray<CardButton> sourceCards, targetCards;
    juce::TextButton loadButton { "O'lchov faylini yuklash (REW .txt)" };
    juce::Label measurementLabel, infoLabel, abHint, warnLabel, statusLeft, statusRight;
    juce::TextButton slotA { "A" }, slotB { "B" }, bypassButton { "ACTIVE" };
    juce::ComboBox modeBox;
    juce::ToggleButton autoGainToggle { "Auto-gain" };

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label title, value;
    };
    Knob knobs[4];

    CurveGraph graph;
    Meter meter;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAtt;

    juce::String lastError;
};
} // namespace mmui

class MonitorMatchEditor : public juce::AudioProcessorEditor
{
public:
    explicit MonitorMatchEditor (MonitorMatchProcessor&);
    ~MonitorMatchEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (mmui::col::bg); }

    static constexpr int baseW = 1120, baseH = 680;

private:
    mmui::LookAndFeel lnf;
    mmui::MainView view;
};
