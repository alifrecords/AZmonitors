#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/Diagnosis.h"

namespace az::ui
{
inline const juce::Colour bg      { 0xff121418 };
inline const juce::Colour strip   { 0xff0b0c0f };
inline const juce::Colour panel   { 0xff181b20 };
inline const juce::Colour inset   { 0xff121418 };
inline const juce::Colour field   { 0xff1c1f25 };
inline const juce::Colour border  { 0xff2a2e36 };
inline const juce::Colour border2 { 0xff343944 };
inline const juce::Colour text    { 0xffe8e6e1 };
inline const juce::Colour text2   { 0xffa9a59c };
inline const juce::Colour text3   { 0xff8d8a82 };
inline const juce::Colour accent  { 0xfff2a541 };
inline const juce::Colour ok      { 0xff3fc1a5 };
inline const juce::Colour warn    { 0xfff2a541 };
inline const juce::Colour bad     { 0xfff06a4b };
inline const juce::Colour redund  { 0xffb49cf2 };
inline const juce::Colour info    { 0xff5aa9f0 };

constexpr int W = 1280, H = 900;

inline juce::Colour statusColour (az::Status s)
{
    switch (s)
    {
        case az::Status::Ideal:     return ok;
        case az::Status::Warn:      return warn;
        case az::Status::Problem:   return bad;
        case az::Status::Redundant: return redund;
        case az::Status::Info:      return info;
        case az::Status::NoData:    return text3;
    }
    return text3;
}

inline juce::Font sans (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::String U (const char* utf8) { return juce::String::fromUTF8 (utf8); }

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour outline = border)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
}

inline void drawChip (juce::Graphics& g, juce::Rectangle<float> r, az::Status s, const juce::String& textOverride = {})
{
    const auto c = statusColour (s);
    g.setColour (c.withAlpha (0.15f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (c);
    g.setFont (sans (11.0f, true));
    g.drawText (textOverride.isNotEmpty() ? textOverride : az::statusLabel (s), r, juce::Justification::centred);
}

/** Matnni o'ralgan holda chizadi va balandligini qaytaradi */
inline float drawWrapped (juce::Graphics* g, const juce::String& s, juce::Rectangle<float> area, const juce::Font& f, juce::Colour c, float lineSpacing = 2.0f)
{
    juce::AttributedString as;
    as.setText (s);
    as.setFont (f);
    as.setColour (c);
    as.setLineSpacing (lineSpacing);
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (as, area.getWidth());
    if (g != nullptr) tl.draw (*g, area.withHeight (tl.getHeight()));
    return tl.getHeight();
}

//==============================================================================
class AZLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AZLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::ComboBox::backgroundColourId, field);
        setColour (juce::ComboBox::outlineColourId, border2);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, text2);
        setColour (juce::ComboBox::focusedOutlineColourId, accent);
        setColour (juce::PopupMenu::backgroundColourId, field);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, border2);
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::TextButton::buttonColourId, field);
        setColour (juce::TextButton::buttonOnColourId, border);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::TextEditor::backgroundColourId, inset);
        setColour (juce::TextEditor::outlineColourId, border2);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, text3);
        setColour (juce::ScrollBar::thumbColourId, border2);
        setColour (juce::Label::textColourId, text);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;
        if (down) c = c.brighter (0.1f);
        else if (highlighted) c = c.brighter (0.05f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 8.0f);
        if (! b.getProperties()["noOutline"])
        {
            g.setColour (border2);
            g.drawRoundedRectangle (r, 8.0f, 1.0f);
        }
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int) override { return sans (14.0f, (bool) b.getProperties()["bold"]); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return sans (14.0f); }
    juce::Font getPopupMenuFont() override { return sans (14.0f); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (box.hasKeyboardFocus (true) ? accent : border2);
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        juce::Path p;
        const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
        p.startNewSubPath (cx - 4, cy - 2); p.lineTo (cx, cy + 2); p.lineTo (cx + 4, cy - 2);
        g.setColour (text2);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};

} // namespace az::ui
