#include "PluginEditor.h"

namespace mmui
{
juce::Font font (float h, bool bold)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::TextButton::buttonColourId,   col::panel);
    setColour (juce::TextButton::buttonOnColourId, col::accent);
    setColour (juce::TextButton::textColourOffId,  col::text);
    setColour (juce::TextButton::textColourOnId,   col::bg);
    setColour (juce::ComboBox::backgroundColourId, col::panel);
    setColour (juce::ComboBox::outlineColourId,    col::line2);
    setColour (juce::ComboBox::textColourId,       col::text);
    setColour (juce::ComboBox::arrowColourId,      col::muted);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId,       col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, col::bg);
    setColour (juce::ToggleButton::textColourId,   col::text);
    setColour (juce::ToggleButton::tickColourId,   col::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, col::muted);
    setColour (juce::Label::textColourId,          col::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float start, float end, juce::Slider&)
{
    const auto r  = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    const auto c  = r.getCentre();
    const float angle = start + pos * (end - start);

    g.setColour (col::panel);
    g.fillEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c));
    g.setColour (juce::Colour (0xff2f343b));
    g.drawEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c), 2.0f);

    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius - 1.0f, radius - 1.0f, 0.0f, start, angle, true);
    g.setColour (col::accent);
    g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float px = c.x + (radius - 9.0f) * std::sin (angle);
    const float py = c.y - (radius - 9.0f) * std::cos (angle);
    g.setColour (col::text);
    g.drawLine (c.x, c.y, px, py, 2.5f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgc,
                                        bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgc;
    if (down) fill = fill.brighter (0.15f);
    else if (over) fill = fill.brighter (0.07f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (b.getToggleState() ? fill : col::line2);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

//==============================================================================
CardButton::CardButton (juce::String b, juce::String m, juce::String n, juce::Colour sel)
    : juce::Button (b + " " + m), brand (std::move (b)), model (std::move (m)), note (std::move (n)), selectedColour (sel)
{
    setClickingTogglesState (false);
}

void CardButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    auto fill = on ? selectedColour.withAlpha (0.12f).overlaidWith (col::panel.withAlpha (0.0f)) : col::panel;
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 10.0f);
    if (on) { g.setColour (selectedColour.withAlpha (0.12f)); g.fillRoundedRectangle (r, 10.0f); }
    if (over && ! on) { g.setColour (juce::Colours::white.withAlpha (down ? 0.06f : 0.03f)); g.fillRoundedRectangle (r, 10.0f); }
    juce::ignoreUnused (fill);
    g.setColour (on ? selectedColour : juce::Colour (0xff2f343b));
    g.drawRoundedRectangle (r, 10.0f, on ? 1.5f : 1.0f);

    auto inner = getLocalBounds().reduced (12, 10);
    g.setColour (brandColour);
    g.setFont (font (10.5f, true));
    g.drawText (brand, inner.removeFromTop (14), juce::Justification::topLeft, true);
    inner.removeFromTop (3);
    g.setColour (col::text);
    g.setFont (font (14.0f, true));
    g.drawText (model, inner.removeFromTop (18), juce::Justification::topLeft, true);
    inner.removeFromTop (3);
    g.setColour (col::muted);
    g.setFont (font (11.0f));
    g.drawFittedText (note, inner, juce::Justification::topLeft, 2, 1.0f);

    if (badge.isNotEmpty())
    {
        auto br = getLocalBounds().toFloat().removeFromTop (26.0f).removeFromRight (40.0f).reduced (6.0f, 5.0f);
        g.setColour (col::line2);
        g.fillRoundedRectangle (br, 4.0f);
        g.setColour (col::text);
        g.setFont (font (10.0f, true));
        g.drawText (badge, br, juce::Justification::centred);
    }
}

//==============================================================================
float CurveGraph::xFor (float hz, juce::Rectangle<float> r) const
{
    return r.getX() + r.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0));
}

float CurveGraph::yFor (float db, juce::Rectangle<float> r) const
{
    db = juce::jlimit (-14.0f, 14.0f, db);
    return r.getCentreY() - db / 12.0f * (r.getHeight() * 0.5f - 6.0f);
}

void CurveGraph::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::graphBg);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);

    auto plot = b.reduced (12.0f);
    plot.removeFromTop (22.0f);      // legenda
    plot.removeFromLeft (30.0f);
    plot.removeFromBottom (18.0f);
    plot.removeFromRight (8.0f);

    // Legenda
    {
        auto lg = b.reduced (14.0f, 10.0f).removeFromTop (16.0f);
        g.setFont (font (11.0f));
        auto item = [&] (juce::Colour c, const juce::String& s, bool dashed)
        {
            auto sw = lg.removeFromLeft (16.0f);
            g.setColour (c);
            if (dashed)
            {
                for (float xx = sw.getX(); xx < sw.getRight(); xx += 6.0f)
                    g.fillRect (xx, sw.getCentreY() - 1.0f, 3.5f, 2.0f);
            }
            else g.fillRect (sw.getX(), sw.getCentreY() - 1.5f, sw.getWidth(), 3.0f);
            lg.removeFromLeft (6.0f);
            g.setColour (juce::Colour (0xffc9cdd2));
            const float tw = juce::GlyphArrangement::getStringWidth (font (11.0f), s) + 4.0f;
            g.drawText (s, lg.removeFromLeft (tw), juce::Justification::centredLeft);
            lg.removeFromLeft (18.0f);
        };
        item (col::blue, srcName, false);
        item (col::accent, tgtName, false);
        item (col::text, bypassed ? "Korreksiya (o'chiq)" : "Korreksiya", true);
    }

    // Setka
    g.setFont (font (9.5f));
    for (int db : { 12, 6, 0, -6, -12 })
    {
        const float y = yFor ((float) db, plot);
        g.setColour (db == 0 ? col::line2 : juce::Colour (0xff262a30));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (juce::Colour (0xff8a9096));
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (plot.getX() - 34.0f, y - 7.0f, 28.0f, 14.0f),
                    juce::Justification::centredRight);
    }
    const std::pair<float, const char*> fl[] = { {20,"20"},{50,"50"},{100,"100"},{200,"200"},{500,"500"},
                                                 {1000,"1k"},{2000,"2k"},{5000,"5k"},{10000,"10k"},{20000,"20k"} };
    for (auto& [hz, label] : fl)
    {
        const float x = xFor (hz, plot);
        g.setColour (juce::Colour (0xff262a30));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (juce::Colour (0xff8a9096));
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 13.0f), juce::Justification::centred);
    }

    auto makePath = [&] (const std::vector<float>& v)
    {
        juce::Path p;
        for (size_t i = 0; i < v.size() && i < freqs.size(); ++i)
        {
            const float x = xFor (freqs[i], plot), y = yFor (v[i], plot);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setColour (col::blue);
    g.strokePath (makePath (src), juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
    g.setColour (col::accent);
    g.strokePath (makePath (tgt), juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));

    juce::Path dashed;
    const float dashes[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.6f).createDashedStroke (dashed, makePath (corr), dashes, 2);
    g.setColour (col::text.withAlpha (bypassed ? 0.35f : 1.0f));
    g.fillPath (dashed);
    g.restoreState();
}

//==============================================================================
void Meter::push (float l, float r)
{
    level[0] = juce::jmax (l, level[0] * 0.86f);
    level[1] = juce::jmax (r, level[1] * 0.86f);
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    auto bars = b.removeFromLeft (34);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = bars.removeFromLeft (14).toFloat();
        bars.removeFromLeft (6);
        g.setColour (col::panel);
        g.fillRoundedRectangle (bar, 3.0f);
        const float db = juce::Decibels::gainToDecibels (level[ch], -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fillR = bar.withTop (bar.getBottom() - bar.getHeight() * frac);
        g.setColour (db > -1.0f ? juce::Colour (0xffeb5757) : db > -6.0f ? juce::Colour (0xfff2c94c) : col::green);
        g.fillRoundedRectangle (fillR, 3.0f);
    }
    const float pk = juce::jmax (level[0], level[1]);
    const float db = juce::Decibels::gainToDecibels (pk, -60.0f);
    g.setColour (col::muted);
    g.setFont (font (11.0f));
    g.drawText (db <= -59.9f ? "-inf dBFS" : juce::String (db, 1) + " dBFS", b.removeFromBottom (16).withTrimmedLeft (6),
                juce::Justification::bottomLeft);
}

//==============================================================================
MainView::MainView (MonitorMatchProcessor& p) : proc (p), apvts (p.apvts)
{

    // Manba monitorlar
    const auto& srcP = mm::sourceProfiles();
    for (size_t i = 0; i < srcP.size(); ++i)
        sourceCards.add (new CardButton (srcP[i].brand, srcP[i].model, srcP[i].note, col::blue));
    sourceCards.add (new CardButton ("O'Z O'LCHOVIM", "Yuklanmagan", "REW / ARTA eksport fayli", col::blue));
    for (int i = 0; i < sourceCards.size(); ++i)
    {
        addAndMakeVisible (sourceCards[i]);
        sourceCards[i]->onClick = [this, i]
        {
            if (i == MonitorMatchProcessor::customSourceIndex && ! proc.hasMeasurement()) { chooseMeasurement(); return; }
            setChoice ("source", i);
        };
    }

    // Maqsad monitorlar
    const auto& tP = mm::targetProfiles();
    const juce::Colour brandCols[] = { juce::Colour (0xffe57373), col::muted, juce::Colour (0xffc9cdd2),
                                       juce::Colour (0xffc9cdd2), col::green };
    for (size_t i = 0; i < tP.size(); ++i)
    {
        auto* c = targetCards.add (new CardButton (tP[i].brand, tP[i].model, tP[i].note, col::accent));
        c->brandColour = brandCols[juce::jmin<size_t> (i, 4)];
        addAndMakeVisible (c);
        c->onClick = [this, i]
        {
            const bool b = apvts.getRawParameterValue ("abSlot")->load() > 0.5f;
            setChoice (b ? "targetB" : "targetA", (int) i);
        };
    }

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { chooseMeasurement(); };

    auto setupLabel = [this] (juce::Label& l, float size, juce::Colour c, bool bold = false)
    {
        l.setFont (font (size, bold));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (l);
    };
    setupLabel (measurementLabel, 11.0f, col::muted);
    setupLabel (infoLabel, 11.0f, col::muted);
    infoLabel.setText ("Korreksiya = Target egri chizig'i - Sizning monitoringiz.\n\n"
                       "Aniq natija uchun monitorlaringizni xonada mikrofon bilan o'lchab (REW, bepul), "
                       "\"Export measurement as text\" faylini yuklang. Unda xona akustikasi ham to'g'rilanadi.",
                       juce::dontSendNotification);
    setupLabel (abHint, 11.0f, col::muted);
    setupLabel (warnLabel, 11.0f, juce::Colour (0xfff5c79a));
    warnLabel.setText ("Faqat monitoring uchun. Cubase'da Control Room insertiga qo'ying - eksportga tushmaydi.",
                       juce::dontSendNotification);
    setupLabel (statusLeft, 11.0f, col::muted);
    setupLabel (statusRight, 11.0f, col::muted);
    statusLeft.setJustificationType (juce::Justification::centredLeft);
    statusRight.setJustificationType (juce::Justification::centredRight);

    // Header
    for (auto* b : { &slotA, &slotB })
    {
        addAndMakeVisible (b);
        b->setColour (juce::TextButton::buttonOnColourId, col::line2);
        b->setColour (juce::TextButton::textColourOnId, col::text);
        b->setColour (juce::TextButton::textColourOffId, col::muted);
    }
    slotA.setTooltip ("A slot");
    slotA.onClick = [this] { setBool ("abSlot", false); };
    slotB.onClick = [this] { setBool ("abSlot", true); };

    addAndMakeVisible (modeBox);
    modeBox.addItemList ({ "Linear phase (mastering)", "Zero latency (aranjirovka)" }, 1);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "mode", modeBox);

    addAndMakeVisible (bypassButton);
    bypassButton.onClick = [this]
    {
        setBool ("bypass", ! (apvts.getRawParameterValue ("bypass")->load() > 0.5f));
    };

    addAndMakeVisible (autoGainToggle);
    autoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "autoGain", autoGainToggle);

    // Knoblar
    const char* ids[]    = { "amount", "bass", "maxBoost", "output" };
    const char* titles[] = { "Emulyatsiya", "Bass korreksiya", "Maks. boost", "Chiqish" };
    for (int i = 0; i < 4; ++i)
    {
        auto& k = knobs[i];
        addAndMakeVisible (k.slider);
        k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        sliderAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, ids[i], k.slider));
        k.title.setText (titles[i], juce::dontSendNotification);
        setupLabel (k.title, 12.0f, col::muted);
        setupLabel (k.value, 16.0f, col::text, true);
        k.slider.onValueChange = [&k] { k.value.setText (k.slider.getTextFromValue (k.slider.getValue()), juce::dontSendNotification); };
        k.slider.onValueChange();
    }
    knobs[2].slider.setTooltip ("Dinamik chiqara olmaydigan chastotalarni haddan tashqari ko'tarmaslik chegarasi");

    addAndMakeVisible (graph);
    addAndMakeVisible (meter);

    setSize (MonitorMatchEditor::baseW, MonitorMatchEditor::baseH);
    resized();
    refresh();
    startTimerHz (30);
}

MainView::~MainView() { stopTimer(); }

void MainView::setChoice (const juce::String& id, int index)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void MainView::setBool (const juce::String& id, bool v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (v ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void MainView::chooseMeasurement()
{
    chooser = std::make_unique<juce::FileChooser> ("O'lchov faylini tanlang (REW: File > Export > Measurement as text)",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.txt;*.csv;*.frd");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              juce::String err;
                              lastError = proc.loadMeasurementFile (f, err) ? juce::String() : err;
                              refresh();
                          });
}

void MainView::timerCallback()
{
    refresh();
    meter.push (proc.takePeak (0), proc.takePeak (1));
}

void MainView::refresh()
{
    const auto s = proc.currentSettings();
    const bool slotB_ = apvts.getRawParameterValue ("abSlot")->load() > 0.5f;
    const int tA = juce::roundToInt (apvts.getRawParameterValue ("targetA")->load());
    const int tB = juce::roundToInt (apvts.getRawParameterValue ("targetB")->load());
    const bool bypass = apvts.getRawParameterValue ("bypass")->load() > 0.5f;

    for (int i = 0; i < sourceCards.size(); ++i)
        sourceCards[i]->setToggleState (i == s.source, juce::dontSendNotification);

    auto* custom = sourceCards[MonitorMatchProcessor::customSourceIndex];
    const auto mName = proc.hasMeasurement() ? proc.getMeasurementName() : juce::String ("Yuklanmagan");
    if (custom->model != mName) { custom->model = mName; custom->repaint(); }

    for (int i = 0; i < targetCards.size(); ++i)
    {
        auto* c = targetCards[i];
        c->setToggleState (i == s.target, juce::dontSendNotification);
        juce::String badge = (i == tA ? "A" : "");
        if (i == tB) badge += (badge.isEmpty() ? "B" : " B");
        if (c->badge != badge) { c->badge = badge; c->repaint(); }
    }

    slotA.setToggleState (! slotB_, juce::dontSendNotification);
    slotB.setToggleState (slotB_, juce::dontSendNotification);

    bypassButton.setButtonText (bypass ? "BYPASS" : "ACTIVE");
    bypassButton.setColour (juce::TextButton::buttonColourId, bypass ? col::panel : col::accent);
    bypassButton.setColour (juce::TextButton::textColourOffId, bypass ? col::muted : col::bg);

    measurementLabel.setText (lastError.isNotEmpty() ? "Xato: " + lastError
                                                     : proc.hasMeasurement() ? "Yuklangan: " + proc.getMeasurementName()
                                                                             : "O'lchov fayli yuklanmagan",
                              juce::dontSendNotification);

    const auto& tP = mm::targetProfiles();
    auto tName = [&] (int i) { i = juce::jlimit (0, (int) tP.size() - 1, i); return juce::String (tP[(size_t) i].brand) + " " + tP[(size_t) i].model; };
    abHint.setText ("A: " + tName (tA) + "      B: " + tName (tB) + "      Faol: " + (slotB_ ? "B" : "A")
                        + "  (kartani bossangiz, faol slotga yoziladi)",
                    juce::dontSendNotification);

    // Grafik
    juce::String srcName = s.source < (int) mm::sourceProfiles().size()
                               ? juce::String (mm::sourceProfiles()[(size_t) s.source].brand) + " " + mm::sourceProfiles()[(size_t) s.source].model
                               : (proc.hasMeasurement() ? "O'lchov: " + proc.getMeasurementName() : juce::String ("O'lchov yo'q (tekis)"));
    proc.computeDisplay (graph.freqs, graph.src, graph.tgt, graph.corr);
    graph.srcName = srcName;
    graph.tgtName = tName (s.target);
    graph.bypassed = bypass;
    graph.repaint();

    const double sr = proc.getCurrentSampleRate();
    statusLeft.setText (bypass ? "O'chirilgan - asl " + srcName + " ovozi"
                               : srcName + "  ->  " + tName (s.target) + "   |   korreksiya faol"
                                     + (s.autoGain ? "   |   auto-gain " + juce::String (proc.getAutoGainDb(), 1) + " dB" : juce::String()),
                        juce::dontSendNotification);
    statusRight.setText ("Latency " + juce::String (proc.getLatencySamples()) + " smp   |   "
                             + (sr > 0 ? juce::String (sr / 1000.0, 1) + " kHz" : juce::String ("-")) + "   |   "
                             + (s.minPhase ? "Zero latency" : "Linear phase"),
                         juce::dontSendNotification);
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // Header
    g.setColour (col::header);
    g.fillRect (0, 0, getWidth(), 56);
    g.setColour (col::line);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());

    {
        juce::Path icon;
        icon.addRoundedRectangle (29.0f, 15.0f, 16.0f, 26.0f, 3.0f);
        icon.addEllipse (32.5f, 27.5f, 9.0f, 9.0f);
        icon.addEllipse (35.0f, 18.5f, 4.0f, 4.0f);
        g.setColour (col::accent);
        g.strokePath (icon, juce::PathStrokeType (2.0f));
    }
    g.setColour (col::text);
    g.setFont (font (16.0f, true));
    g.drawText ("MONITOR MATCH", 58, 12, 300, 18, juce::Justification::centredLeft);
    g.setColour (col::muted);
    g.setFont (font (10.5f));
    g.drawText ("AZIMXODJAYEV MUSIC  |  MONITOR EMULATOR", 58, 31, 360, 14, juce::Justification::centredLeft);

    // Ustun chiziqlari
    g.setColour (col::line);
    g.drawVerticalLine (260, 57.0f, 644.0f);
    g.drawVerticalLine (900, 57.0f, 644.0f);

    // Bo'lim sarlavhalari
    g.setColour (col::muted);
    g.setFont (font (11.0f, true));
    g.drawText ("1.  SIZNING MONITORINGIZ", 20, 72, 230, 16, juce::Justification::centredLeft);
    g.drawText ("2.  CHASTOTA JAVOBI", 284, 72, 300, 16, juce::Justification::centredLeft);
    g.drawText ("3.  QAYSI MONITORDA ESHITMOQCHISIZ", 284, 416, 400, 16, juce::Justification::centredLeft);
    g.drawText ("4.  BOSHQARUV", 920, 72, 180, 16, juce::Justification::centredLeft);
    g.drawText ("Chiqish darajasi", 920, 448, 180, 16, juce::Justification::centredLeft);

    // Ogohlantirish fon
    g.setColour (juce::Colour (0xff2a1f14));
    g.fillRoundedRectangle (916.0f, 556.0f, 188.0f, 74.0f, 8.0f);
    g.setColour (juce::Colour (0xff5c3b1a));
    g.drawRoundedRectangle (916.5f, 556.5f, 187.0f, 73.0f, 8.0f, 1.0f);

    // Status bar
    g.setColour (col::header);
    g.fillRect (0, 644, getWidth(), 36);
    g.setColour (col::line);
    g.drawHorizontalLine (644, 0.0f, (float) getWidth());
}

void MainView::resized()
{
    // Header
    slotA.setBounds (712, 12, 40, 32);
    slotB.setBounds (758, 12, 40, 32);
    modeBox.setBounds (808, 12, 200, 32);
    bypassButton.setBounds (1018, 12, 84, 32);

    // Chap ustun
    int y = 98;
    for (auto* c : sourceCards) { c->setBounds (20, y, 220, 68); y += 76; }
    loadButton.setBounds (20, y + 2, 220, 32);
    measurementLabel.setBounds (18, y + 38, 226, 30);
    infoLabel.setBounds (18, y + 74, 226, 160);

    // Markaz
    graph.setBounds (284, 96, 592, 308);
    const int cardW = (592 - 4 * 10) / 5;
    for (int i = 0; i < targetCards.size(); ++i)
        targetCards[i]->setBounds (284 + i * (cardW + 10), 440, cardW, 100);
    abHint.setBounds (282, 552, 600, 40);

    // O'ng ustun
    for (int i = 0; i < 4; ++i)
    {
        const int ky = 98 + i * 70;
        knobs[i].slider.setBounds (920, ky, 56, 56);
        knobs[i].title.setBounds (984, ky + 8, 120, 16);
        knobs[i].value.setBounds (984, ky + 26, 120, 22);
    }
    autoGainToggle.setBounds (918, 382, 180, 28);
    meter.setBounds (920, 470, 180, 76);
    warnLabel.setBounds (922, 562, 178, 64);

    // Status
    statusLeft.setBounds (20, 648, 640, 28);
    statusRight.setBounds (660, 648, 440, 28);
}
} // namespace mmui

//==============================================================================
MonitorMatchEditor::MonitorMatchEditor (MonitorMatchProcessor& p)
    : AudioProcessorEditor (p), view (p)
{
    view.setLookAndFeel (&lnf);
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
}

MonitorMatchEditor::~MonitorMatchEditor()
{
    view.setLookAndFeel (nullptr);
}

void MonitorMatchEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseW;
    view.setBounds (0, 0, baseW, baseH);
    view.setTransform (juce::AffineTransform::scale (scale));
}
