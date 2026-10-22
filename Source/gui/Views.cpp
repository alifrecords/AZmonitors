#include "Views.h"
#include "PluginProcessor.h"

namespace az::ui
{
namespace
{
    const az::Check* findCheck (const ViewData& d, const char* prefix)
    {
        for (auto& c : d.report.checks)
            if (c.title.startsWith (prefix)) return &c;
        return nullptr;
    }

    juce::Colour colourFor (const ViewData& d, const char* prefix, juce::Colour fallback = text)
    {
        if (auto* c = findCheck (d, prefix))
            if (c->status != az::Status::Info && c->status != az::Status::NoData)
                return statusColour (c->status);
        return fallback;
    }

    float freqToX (float hz, juce::Rectangle<float> r)
    {
        return r.getX() + r.getWidth() * std::log10 (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / 3.0f;
    }

    void drawTitle (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r)
    {
        g.setColour (text);
        g.setFont (sans (15.0f, true));
        g.drawText (t, r, juce::Justification::centredLeft);
    }
}

void fillChoices (juce::ComboBox& box, AZMonitoringProcessor& p, const juce::String& id)
{
    box.clear (juce::dontSendNotification);
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
        box.addItemList (c->choices, 1);
}

//==============================================================================
// SPEKTR
void SpectrumView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    drawTitle (g, title, { 18, 12, b.getWidth() - 36, 24 });

    // Legenda
    {
        float x = b.getRight() - 18;
        auto item = [&] (const juce::String& s, juce::Colour c, bool band)
        {
            g.setFont (sans (12.0f));
            const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s);
            x -= tw;
            g.setColour (text2);
            g.drawText (s, juce::Rectangle<float> (x, 14, tw, 20), juce::Justification::centredLeft);
            x -= 22;
            g.setColour (band ? c.withAlpha (0.25f) : c);
            if (band) g.fillRect (x, 20.0f, 16.0f, 8.0f); else g.fillRect (x, 23.0f, 16.0f, 2.0f);
            x -= 16;
        };
        item ("Jonli", text2, false);
        item ("Sizning vokal (o'rtacha)", accent, false);
        item ("Etalon koridori", ok, true);
    }

    const auto plot = juce::Rectangle<float> (52, 48, b.getWidth() - 72, b.getHeight() - 82);
    const float dbTop = 12.0f, dbBottom = -36.0f;
    auto dbToY = [&] (float db) { return plot.getY() + plot.getHeight() * (dbTop - juce::jlimit (dbBottom, dbTop, db)) / (dbTop - dbBottom); };

    // To'r
    g.setColour (border);
    for (float db = 12; db >= -36; db -= 12)
    {
        g.drawHorizontalLine ((int) dbToY (db), plot.getX(), plot.getRight());
        g.setColour (text3);
        g.setFont (mono (10.0f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (14, dbToY (db) - 7, 32, 14), juce::Justification::centredRight);
        g.setColour (border);
    }
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (hz, plot);
        g.setColour (border);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (text3);
        g.setFont (mono (10.0f));
        g.drawText (az::fmtHz (hz).replace (" ", ""), juce::Rectangle<float> (x - 30, plot.getBottom() + 6, 60, 14), juce::Justification::centred);
    }

    if (data == nullptr || data->profile == nullptr)
        return;

    // Muammo zonalari
    for (auto& c : data->report.checks)
    {
        if (c.freqHi <= 0 || (c.status != az::Status::Warn && c.status != az::Status::Problem)) continue;
        const float x0 = freqToX (c.freqLo, plot), x1 = freqToX (c.freqHi, plot);
        const auto col = statusColour (c.status);
        g.setColour (col.withAlpha (0.13f));
        g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));
        g.setColour (col.brighter (0.2f));
        g.setFont (mono (10.0f, true));
        auto name = c.title.upToFirstOccurrenceOf (" (", false, false).toUpperCase();
        g.drawText (name, juce::Rectangle<float> (x0 + 3, plot.getY() + 3, juce::jmax (60.0f, x1 - x0 + 60), 13), juce::Justification::centredLeft);
    }

    // Etalon koridori
    const auto& p = *data->profile;
    juce::Path corridor, upper;
    bool first = true;
    for (int k = 0; k < kNumBands; ++k)
    {
        const float x = freqToX (bandCentre (k), plot), y = dbToY (p.target[(size_t) k] + p.tol[(size_t) k]);
        if (first) { corridor.startNewSubPath (x, y); first = false; } else corridor.lineTo (x, y);
    }
    for (int k = kNumBands - 1; k >= 0; --k)
        corridor.lineTo (freqToX (bandCentre (k), plot), dbToY (p.target[(size_t) k] - p.tol[(size_t) k]));
    corridor.closeSubPath();
    g.setColour (ok.withAlpha (0.14f));
    g.fillPath (corridor);
    g.setColour (ok.withAlpha (0.7f));
    juce::Path dashed;
    const float dashes[] = { 3.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, corridor, dashes, 2);
    g.fillPath (dashed);

    const auto& f = data->finalOut;
    if (! f.spectrumValid)
    {
        g.setColour (text2);
        g.setFont (sans (14.0f));
        g.drawText ("Vokalni ijro eting - spektr to'planmoqda", plot, juce::Justification::centred);
        return;
    }

    auto curve = [&] (const std::array<float, kNumBands>& v)
    {
        juce::Path c; bool started = false;
        for (int k = 0; k < kNumBands; ++k)
        {
            if (v[(size_t) k] <= kNoValue + 1) continue;
            const float x = freqToX (bandCentre (k), plot), y = dbToY (v[(size_t) k]);
            if (! started) { c.startNewSubPath (x, y); started = true; } else c.lineTo (x, y);
        }
        return c;
    };
    g.setColour (text2.withAlpha (0.45f));
    g.strokePath (curve (f.bandLive), juce::PathStrokeType (1.0f));
    g.setColour (accent);
    g.strokePath (curve (f.bandRel), juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// BAHO VA O'LCHOVLAR
void SummaryPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    if (data == nullptr) return;
    const auto& r = data->report;
    const auto& f = data->finalOut;

    // Ball halqasi
    const juce::Rectangle<float> ring (22, 20, 96, 96);
    g.setColour (border);
    g.drawEllipse (ring.reduced (4), 8.0f);
    if (r.hasData)
    {
        juce::Path arc;
        const float a = juce::MathConstants<float>::twoPi * (float) r.score / 100.0f;
        arc.addCentredArc (ring.getCentreX(), ring.getCentreY(), 44, 44, 0, 0, a, true);
        const auto col = r.score >= 85 ? ok : r.score >= 60 ? accent : bad;
        g.setColour (col);
        g.strokePath (arc, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (text);
    g.setFont (sans (32.0f, true));
    g.drawText (r.hasData ? juce::String (r.score) : juce::String ("--"), ring, juce::Justification::centred);

    g.setColour (text2);
    g.setFont (sans (12.0f));
    g.drawText (data->master ? "Umumiy master bahosi" : "Umumiy vokal bahosi", juce::Rectangle<float> (136, 28, b.getWidth() - 150, 16), juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (sans (22.0f, true));
    g.drawText (r.verdict, juce::Rectangle<float> (136, 46, b.getWidth() - 150, 28), juce::Justification::centredLeft);
    g.setColour (text2);
    g.setFont (sans (12.0f));
    const auto counts = r.hasData ? juce::String (r.nProblem) + " ta muammo | " + juce::String (r.nWarn) + " ta diqqat | " + juce::String (r.nIdeal) + " ta ideal"
                                  : juce::String ("Trekni ijro eting (kamida 4 s vokal)");
    g.drawText (counts, juce::Rectangle<float> (136, 78, b.getWidth() - 150, 16), juce::Justification::centredLeft);

    g.setColour (border);
    g.drawHorizontalLine (134, 16, b.getWidth() - 16);

    struct M { juce::String label, value, sub; juce::Colour c; };
    const M ms[6] = {
        { "Integrated loudness", f.integrated > -70 ? juce::String (f.integrated, 1) + " LUFS" : "--",
          data->master && data->standard ? "nishon " + juce::String (data->standard->lufs, 0) : juce::String ("BS.1770-4"), colourFor (*data, "Loudness (") },
        { "Short-term / Momentary", f.shortTerm > -70 ? juce::String (f.shortTerm, 1) : juce::String ("--"),
          f.momentary > -70 ? "M " + juce::String (f.momentary, 1) : juce::String ("M --"), text },
        { "True Peak (max)", az::fmtDb (f.truePeakMax, 1, " dBTP"), juce::String ("BS.1770 oversampling"), colourFor (*data, "True Peak") },
        { "Loudness Range", f.lra > 0 ? juce::String (f.lra, 1) + " LU" : juce::String ("--"), "EBU Tech 3342", colourFor (*data, "Loudness Range") },
        { "Crest factor", f.crest > 0 ? juce::String (f.crest, 1) + " dB" : juce::String ("--"),
          data->profile ? juce::String (data->profile->crestMin, 0) + "-" + juce::String (data->profile->crestMax, 0) + " dB" : juce::String(), colourFor (*data, "Dinamika") },
        { "Shovqin foni", f.noiseFloor > kNoValue + 1 ? juce::String (juce::roundToInt (f.noiseFloor)) + " dBFS" : juce::String ("--"), "pauzalarda", colourFor (*data, "Shovqin") },
    };
    const float cw = (b.getWidth() - 48) / 2.0f;
    for (int i = 0; i < 6; ++i)
    {
        const float x = 18 + (float) (i % 2) * (cw + 12);
        const float y = 148 + (float) (i / 2) * 50;
        g.setColour (text2);
        g.setFont (sans (11.5f));
        g.drawText (ms[i].label, juce::Rectangle<float> (x, y, cw, 14), juce::Justification::centredLeft);
        g.setColour (ms[i].c);
        g.setFont (mono (17.0f, true));
        g.drawText (ms[i].value, juce::Rectangle<float> (x, y + 15, cw, 22), juce::Justification::centredLeft);
        g.setColour (text3);
        g.setFont (sans (10.5f));
        g.drawText (ms[i].sub, juce::Rectangle<float> (x, y + 36, cw, 12), juce::Justification::centredLeft);
    }
}

//==============================================================================
// TEKSHIRUVLAR RO'YXATI
void ChecksList::setData (const ViewData& d)
{
    data = &d;
    const float h = layoutRows (nullptr);
    setSize (getWidth(), juce::jmax (40, (int) std::ceil (h)));
    repaint();
}

float ChecksList::layoutRows (juce::Graphics* g)
{
    if (data == nullptr) return 0;
    const float w = (float) getWidth();
    float y = 0;
    rowHeights.clear();

    if (! data->report.hasData)
    {
        if (g != nullptr)
            drawWrapped (g, "Tahlil uchun vokal trekini ijro eting. AZ Monitoring vokal insert zanjirining eng oxirgi slotida turishi kerak. "
                            "Kamida 4 soniya faol signaldan keyin natijalar paydo bo'ladi; to'liq baho uchun butun qo'shiqni bir marta o'tkazing.",
                         { 4, 8, w - 8, 200 }, sans (14.0f), text2);
        return 120;
    }

    for (auto& c : data->report.checks)
    {
        const float adviceW = w - 104;
        const float ah = c.advice.isNotEmpty() ? drawWrapped (nullptr, c.advice, { 0, 0, adviceW, 1000 }, sans (13.0f), text2) : 0.0f;
        const float rh = 40 + (ah > 0 ? ah + 10 : 0);

        if (g != nullptr)
        {
            drawChip (*g, { 2, y + 9, 84, 22 }, c.status);
            g->setColour (text);
            g->setFont (sans (14.0f, true));
            g->drawText (c.title, juce::Rectangle<float> (100, y + 8, 250, 24), juce::Justification::centredLeft);
            g->setColour (c.status == az::Status::Info ? text : statusColour (c.status));
            g->setFont (mono (13.0f, true));
            g->drawText (c.measured, juce::Rectangle<float> (352, y + 8, 200, 24), juce::Justification::centredLeft);
            g->setColour (text3);
            g->setFont (sans (12.0f));
            g->drawText ("me'yor: " + c.target, juce::Rectangle<float> (556, y + 8, w - 556, 24), juce::Justification::centredLeft, true);
            if (ah > 0)
                drawWrapped (g, c.advice, { 100, y + 36, adviceW, ah + 4 }, sans (13.0f), text2);
            g->setColour (border);
            g->drawHorizontalLine ((int) (y + rh - 1), 0, w);
        }
        rowHeights.push_back (rh);
        y += rh;
    }
    return y;
}

void ChecksList::paint (juce::Graphics& g) { layoutRows (&g); }

//==============================================================================
// VAQT SHKALASI
void TimelinePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawPanel (g, b);
    drawTitle (g, "Trek bo'ylab muammo nuqtalari", { 18, 12, b.getWidth() - 36, 24 });
    if (data == nullptr) return;
    const auto& f = data->finalOut;

    const float t0 = (float) juce::jmax (0.0, f.timeFirst), t1 = (float) juce::jmax (f.timeLast, f.timeFirst + 1.0);
    g.setColour (text3);
    g.setFont (mono (11.0f));
    g.drawText (az::fmtTime (t0) + " - " + az::fmtTime (t1), juce::Rectangle<float> (18, 12, b.getWidth() - 36, 24), juce::Justification::centredRight);

    const juce::Rectangle<float> bar (18, 48, b.getWidth() - 36, 48);
    g.setColour (inset);
    g.fillRoundedRectangle (bar, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (bar, 6.0f, 1.0f);

    auto colourOf = [] (int type) { return type == Event::Sibilance ? warn : bad; };
    for (auto& e : f.events)
    {
        if (f.timeFirst < 0) break;
        const float x = bar.getX() + 4 + (bar.getWidth() - 8) * (e.time - t0) / juce::jmax (1.0f, t1 - t0);
        g.setColour (colourOf (e.type));
        g.fillRect (juce::Rectangle<float> (x - 1.5f, bar.getY() + 8, 3, bar.getHeight() - 16));
    }

    // Legenda
    float lx = 18;
    for (auto [label, c] : { std::pair<const char*, juce::Colour> { "True Peak > -1 dBTP", bad }, { "Sibilyant", warn }, { "Clip", bad } })
    {
        g.setColour (c);
        g.fillRect (lx, 110.0f, 8.0f, 8.0f);
        g.setColour (text2);
        g.setFont (sans (11.5f));
        g.drawText (label, juce::Rectangle<float> (lx + 12, 104, 140, 20), juce::Justification::centredLeft);
        lx += 12 + (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 18;
    }

    // Hodisalar ro'yxati (oxirgi 12 ta)
    g.setColour (border);
    g.drawHorizontalLine (134, 16, b.getWidth() - 16);
    float y = 146;
    const int n = (int) f.events.size();
    if (n == 0)
    {
        g.setColour (text3);
        g.setFont (sans (12.5f));
        g.drawText (f.timeFirst < 0 ? juce::String ("DAW vaqt ma'lumoti hali kelmadi") : juce::String ("Hozircha muammo nuqtasi yo'q"),
                    juce::Rectangle<float> (18, y, b.getWidth() - 36, 20), juce::Justification::centredLeft);
    }
    for (int i = juce::jmax (0, n - 12); i < n; ++i)
    {
        const auto& e = f.events[(size_t) i];
        const juce::String what = e.type == Event::Sibilance ? "Sibilyant" : e.type == Event::Clip ? "Clip (0 dBFS)" : "True Peak";
        g.setColour (text2);
        g.setFont (mono (12.5f));
        g.drawText (az::fmtTime (e.time), juce::Rectangle<float> (18, y, 60, 20), juce::Justification::centredLeft);
        g.setColour (colourOf (e.type));
        g.fillEllipse (82, y + 6, 8, 8);
        g.setColour (text);
        g.setFont (sans (12.5f));
        g.drawText (what, juce::Rectangle<float> (98, y, 150, 20), juce::Justification::centredLeft);
        g.setColour (text3);
        g.setFont (mono (12.0f));
        g.drawText (az::fmtDb (e.value), juce::Rectangle<float> (b.getWidth() - 118, y, 100, 20), juce::Justification::centredRight);
        y += 22;
    }

    g.setColour (text3);
    g.setFont (sans (11.0f));
    g.drawText ("Vaqtlar DAW vaqt shkalasi bo'yicha. Shu joyga borib, muammo bo'lagini solo qiling.",
                juce::Rectangle<float> (18, b.getHeight() - 30, b.getWidth() - 36, 18), juce::Justification::centredLeft, true);
}

//==============================================================================
AnalysisPage::AnalysisPage()
{
    addAndMakeVisible (spectrum);
    addAndMakeVisible (summary);
    addAndMakeVisible (checksViewport);
    checksViewport.setViewedComponent (&checks, false);
    checksViewport.setScrollBarsShown (true, false);
    checksViewport.setScrollBarThickness (8);
    addAndMakeVisible (timeline);
}

void AnalysisPage::setData (const ViewData& d)
{
    spectrum.setData (d);
    summary.setData (d);
    checks.setData (d);
    timeline.setData (d);
}

void AnalysisPage::paint (juce::Graphics& g)
{
    const auto r = checksViewport.getBounds().toFloat().expanded (16, 0).withTop (316).withBottom ((float) getHeight());
    drawPanel (g, r);
    drawTitle (g, "Tashxis - nima ideal, nima tuzatilishi kerak", { r.getX() + 18, r.getY() + 12, r.getWidth() - 36, 24 });
}

void AnalysisPage::resized()
{
    spectrum.setBounds (0, 0, 780, 300);
    summary.setBounds (796, 0, getWidth() - 796, 300);
    checksViewport.setBounds (16, 316 + 48, 780 - 32, getHeight() - 316 - 60);
    checks.setSize (checksViewport.getWidth() - 12, checks.getHeight());
    timeline.setBounds (796, 316, getWidth() - 796, getHeight() - 316);
}

//==============================================================================
// ZANJIR JADVALI
void StageTable::setData (const ViewData& d, int sel)
{
    data = &d;
    selected = sel;
    const int n = (int) d.report.stages.size();
    setSize (getWidth(), juce::jmax (200, n * rowH));
    repaint();
}

void StageTable::paint (juce::Graphics& g)
{
    if (data == nullptr) return;
    const auto& stages = data->report.stages;
    const float w = (float) getWidth();

    if (stages.empty())
    {
        drawWrapped (&g, data->report.chainHint.isNotEmpty() ? data->report.chainHint
                                                             : juce::String ("Zanjir hali o'lchanmagan."),
                     { 8, 12, w - 16, 60 }, sans (14.0f, true), text);
        drawWrapped (&g,
            U ("Har bir plaginni alohida baholash uchun:\n"
               "1. Vokal trekidagi har bir plagindan keyin AZ Monitoring qo'ying va Sozlamalar → Rejim = Probe qiling.\n"
               "2. Probe'da joyini tanlang (IN = zanjir boshi, keyin A, B, C …) va oldingi plagin nomini yozing.\n"
               "3. Eng oxirgi slotda AZ Monitoring Monitor rejimida qoladi.\n"
               "4. Trekni boshidan ijro eting va \"Qayta tahlil\" tugmasini bosing — har bir plagin nima qilgani shu yerda chiqadi.\n\n"
               "Probe signalni o'zgartirmaydi va kechikish qo'shmaydi. Pro Tools'da insertlar soni 10 ta — eng muhim plaginlar orasiga probe qo'ying."),
            { 8, 76, w - 16, 400 }, sans (13.5f), text2, 4.0f);
        return;
    }

    for (int i = 0; i < (int) stages.size(); ++i)
    {
        const auto& s = stages[(size_t) i];
        const float y = (float) (i * rowH);
        auto row = juce::Rectangle<float> (0, y + 3, w, (float) rowH - 6);
        if (i == selected)
        {
            g.setColour (statusColour (s.status).withAlpha (0.08f));
            g.fillRoundedRectangle (row, 8.0f);
            g.setColour (statusColour (s.status).withAlpha (0.55f));
            g.drawRoundedRectangle (row.reduced (0.5f), 8.0f, 1.0f);
        }
        else if (s.status == az::Status::Problem)
        {
            g.setColour (bad.withAlpha (0.05f));
            g.fillRoundedRectangle (row, 8.0f);
        }

        g.setColour (text2);
        g.setFont (mono (12.0f));
        g.drawText (s.slot, juce::Rectangle<float> (10, y, 84, (float) rowH), juce::Justification::centredLeft);

        g.setColour (text);
        g.setFont (sans (14.0f, true));
        g.drawText (s.name, juce::Rectangle<float> (98, y + 14, 196, 20), juce::Justification::centredLeft, true);
        g.setColour (text3);
        g.setFont (sans (12.0f));
        g.drawText (s.role, juce::Rectangle<float> (98, y + 36, 196, 18), juce::Justification::centredLeft, true);

        auto effect = s.effect;
        drawWrapped (&g, effect, { 302, y + 10, 222, (float) rowH - 14 }, sans (12.0f), juce::Colour (0xffd6d3cc));

        const juce::Rectangle<float> barR (534, y + rowH * 0.5f - 3, 68, 6);
        g.setColour (border);
        g.fillRoundedRectangle (barR, 3.0f);
        if (s.valid)
        {
            g.setColour (statusColour (s.status));
            g.fillRoundedRectangle (barR.withWidth (barR.getWidth() * (float) s.score / 100.0f), 3.0f);
        }
        g.setColour (text);
        g.setFont (mono (13.0f));
        g.drawText (s.valid ? juce::String (s.score) : juce::String ("--"), juce::Rectangle<float> (606, y, 30, (float) rowH), juce::Justification::centredRight);
        drawChip (g, { w - 82, y + rowH * 0.5f - 11, 78, 22 }, s.status);
    }
}

void StageTable::mouseUp (const juce::MouseEvent& e)
{
    if (data == nullptr) return;
    const int i = e.y / rowH;
    if (i >= 0 && i < (int) data->report.stages.size() && onSelect)
        onSelect (i);
}

//==============================================================================
void StageDetail::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (data == nullptr) { drawPanel (g, b); return; }
    const auto& stages = data->report.stages;
    const bool has = selected >= 0 && selected < (int) stages.size();
    drawPanel (g, b, has ? statusColour (stages[(size_t) selected].status).withAlpha (0.8f) : border);
    const float w = b.getWidth();

    if (! has)
    {
        drawTitle (g, "Plagin tafsiloti", { 18, 14, w - 36, 24 });
        drawWrapped (&g, "Jadvaldan plaginni tanlang: u signalga nima qilgani, me'yordan qayerda chiqqani va nima qilish kerakligi shu yerda chiqadi.",
                     { 18, 50, w - 36, 200 }, sans (13.5f), text2, 3.0f);
        return;
    }

    const auto& s = stages[(size_t) selected];
    g.setColour (text3);
    g.setFont (mono (11.0f));
    g.drawText (s.slot + U ("  ·  tanlangan"), juce::Rectangle<float> (18, 14, w - 120, 16), juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (sans (18.0f, true));
    g.drawText (s.name, juce::Rectangle<float> (18, 32, w - 36, 26), juce::Justification::centredLeft, true);
    g.setColour (text2);
    g.setFont (sans (12.5f));
    g.drawText ("Aniqlangan vazifa: " + s.role, juce::Rectangle<float> (18, 58, w - 36, 18), juce::Justification::centredLeft, true);
    drawChip (g, { w - 110, 14, 92, 22 }, s.status, s.valid ? statusLabel (s.status) + U (" · ") + juce::String (s.score) : statusLabel (s.status));

    if (! s.valid)
    {
        drawWrapped (&g, s.effect, { 18, 96, w - 36, 100 }, sans (13.5f), text2);
        return;
    }

    // Chastota o'zgarishi
    g.setColour (text2);
    g.setFont (sans (11.5f));
    g.drawText ("Chastota o'zgarishi (umumiy gain'siz), dB", juce::Rectangle<float> (18, 88, w - 36, 16), juce::Justification::centredLeft);
    const juce::Rectangle<float> plot (18, 108, w - 36, 104);
    g.setColour (inset);
    g.fillRoundedRectangle (plot, 4.0f);
    const float mid = plot.getCentreY();
    g.setColour (border2);
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    int first = bandForFreq (40.0f), last = bandForFreq (16000.0f);
    const float bw = plot.getWidth() / (float) (last - first + 1);
    for (int k = first; k <= last; ++k)
    {
        const float d = juce::jlimit (-12.0f, 12.0f, s.bandDelta[(size_t) k]);
        const float hgt = std::abs (d) / 12.0f * (plot.getHeight() * 0.5f - 4);
        const float x = plot.getX() + (float) (k - first) * bw;
        g.setColour (std::abs (d) < 1.0f ? text3.withAlpha (0.5f) : (d > 0 ? accent : info));
        g.fillRect (juce::Rectangle<float> (x + 1, d > 0 ? mid - hgt : mid, bw - 2, juce::jmax (1.0f, hgt)));
    }
    g.setColour (text3);
    g.setFont (mono (9.5f));
    for (float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = plot.getX() + (float) (bandForFreq (hz) - first) * bw;
        g.drawText (az::fmtHz (hz).replace (" ", ""), juce::Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 12), juce::Justification::centred);
    }
    g.drawText ("+12", juce::Rectangle<float> (plot.getRight() - 30, plot.getY() + 2, 28, 12), juce::Justification::centredRight);
    g.drawText ("-12", juce::Rectangle<float> (plot.getRight() - 30, plot.getBottom() - 14, 28, 12), juce::Justification::centredRight);

    // Raqamlar
    struct T { juce::String label, value; juce::Colour c; };
    const float crestD = s.crestAfter - s.crestBefore;
    const T tiles[4] = {
        { "Crest factor", juce::String (s.crestBefore, 1) + U (" → ") + juce::String (s.crestAfter, 1) + " dB",
          crestD <= -4.5f ? bad : crestD <= -3.0f ? warn : text },
        { "Daraja o'zgarishi", az::fmtDb (s.gainChange, 1, " LU"), text },
        { "Kirish (RMS)", az::fmtDb (s.levelIn, 1, " dBFS"), (s.levelIn > -10 || s.levelIn < -30) ? warn : text },
        { "Chiqish True Peak", az::fmtDb (s.tpOut, 1, " dBTP"), s.tpOut > 0 ? bad : text },
    };
    const float tw = (w - 36 - 10) / 2.0f;
    for (int i = 0; i < 4; ++i)
    {
        const juce::Rectangle<float> r (18 + (float) (i % 2) * (tw + 10), 234 + (float) (i / 2) * 62, tw, 54);
        g.setColour (inset);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (text2);
        g.setFont (sans (11.0f));
        g.drawText (tiles[i].label, r.reduced (10, 6).withHeight (14), juce::Justification::centredLeft);
        g.setColour (tiles[i].c);
        g.setFont (mono (15.0f, true));
        g.drawText (tiles[i].value, r.reduced (10, 6).withTrimmedTop (18), juce::Justification::centredLeft);
    }

    g.setColour (text2);
    g.setFont (sans (12.0f));
    g.drawText ("Sibilyant indeksi: " + az::fmtDb (s.sibBefore) + U (" → ") + az::fmtDb (s.sibAfter),
                juce::Rectangle<float> (18, 362, w - 36, 18), juce::Justification::centredLeft);

    // Tavsiya
    const auto advice = s.advice.isNotEmpty() ? s.advice : juce::String ("Bu bosqich me'yorda ishlayapti - o'zgartirish shart emas.");
    const juce::Rectangle<float> box (18, 390, w - 36, b.getHeight() - 390 - 60);
    g.setColour (inset);
    g.fillRoundedRectangle (box, 8.0f);
    drawWrapped (&g, advice, box.reduced (12), sans (13.0f), juce::Colour (0xffd6d3cc), 3.0f);

    juce::String foot = data->report.chainHint;
    if (! data->duplicateSlots.isEmpty())
        foot = "Bir slotda bir nechta probe: " + data->duplicateSlots.joinIntoString (", ") + ". " + foot;
    if (foot.isEmpty())
        foot = "Sozlamalar plagin ichidan o'qilmaydi - baho signalning o'zidan olinadi.";
    drawWrapped (&g, foot, { 18, b.getHeight() - 50, w - 36, 40 }, sans (11.5f), text3);
}

//==============================================================================
ChainPage::ChainPage()
{
    addAndMakeVisible (tableViewport);
    tableViewport.setViewedComponent (&table, false);
    tableViewport.setScrollBarsShown (true, false);
    tableViewport.setScrollBarThickness (8);
    addAndMakeVisible (detail);
    table.onSelect = [this] (int i) { selected = i; userSelected = true; if (data) setData (*data); };
}

void ChainPage::setData (const ViewData& d)
{
    data = &d;
    const int n = (int) d.report.stages.size();
    if (selected >= n) { selected = -1; userSelected = false; }
    if (! userSelected)
    {
        // Avtomatik: eng yomon bahodagi bosqich
        int worst = -1, worstScore = 101;
        for (int i = 0; i < n; ++i)
            if (d.report.stages[(size_t) i].valid && d.report.stages[(size_t) i].score < worstScore)
                { worst = i; worstScore = d.report.stages[(size_t) i].score; }
        selected = worst;
    }
    table.setData (d, selected);
    detail.setData (d, selected);
    repaint();
}

void ChainPage::paint (juce::Graphics& g)
{
    if (data == nullptr) return;
    const auto& r = data->report;
    struct Tile { juce::String label, value, note; juce::Colour c; };
    juce::StringArray idealSlots, badSlots, redSlots;
    for (auto& s : r.stages)
    {
        const auto tag = s.slot.fromLastOccurrenceOf (" ", false, false);
        if (s.status == az::Status::Ideal) idealSlots.add (tag);
        else if (s.status == az::Status::Warn || s.status == az::Status::Problem) badSlots.add (tag);
        else if (s.status == az::Status::Redundant) redSlots.add (s.name);
    }
    const bool any = r.chainScore > 0;
    const Tile tiles[5] = {
        { "Zanjir salomatligi", any ? juce::String (r.chainScore) + "/100" : juce::String ("--"),
          juce::String ((int) r.stages.size()) + " ta bosqich", any ? (r.chainScore >= 85 ? ok : r.chainScore >= 60 ? accent : bad) : text3 },
        { "Ideal ishlayotgan", juce::String (r.stIdeal), idealSlots.joinIntoString (", "), ok },
        { "Me'yori buzilgan", juce::String (r.stWarn + r.stProblem), badSlots.joinIntoString (", "), bad },
        { "Ortiqcha plagin", juce::String (r.stRedundant), redSlots.isEmpty() ? juce::String ("ta'siri yo'q plagin") : redSlots.joinIntoString (", "), redund },
        { "Ulangan probe'lar", juce::String (data->probeCount), "Guruh: " + data->group, data->probeCount > 0 ? ok : text3 },
    };
    const float tw = ((float) getWidth() - 40) / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        const juce::Rectangle<float> t ((float) i * (tw + 10), 0, tw, 88);
        drawPanel (g, t);
        g.setColour (text2);
        g.setFont (sans (11.5f));
        g.drawText (tiles[i].label, t.reduced (14, 10).withHeight (16), juce::Justification::centredLeft);
        g.setColour (tiles[i].c);
        g.setFont (mono (24.0f, true));
        g.drawText (tiles[i].value, juce::Rectangle<float> (t.getX() + 14, 30, tw - 28, 30), juce::Justification::centredLeft);
        g.setColour (text3);
        g.setFont (sans (11.0f));
        g.drawText (tiles[i].note, juce::Rectangle<float> (t.getX() + 14, 62, tw - 28, 16), juce::Justification::centredLeft, true);
    }

    const juce::Rectangle<float> tp (0, 104, 760, (float) getHeight() - 104);
    drawPanel (g, tp);
    g.setColour (text3);
    g.setFont (sans (11.5f));
    const float y = 118;
    g.drawText ("Bosqich", juce::Rectangle<float> (22, y, 80, 16), juce::Justification::centredLeft);
    g.drawText ("Plagin", juce::Rectangle<float> (110, y, 180, 16), juce::Justification::centredLeft);
    g.drawText (U ("Signalga nima qildi (oldin → keyin)"), juce::Rectangle<float> (314, y, 240, 16), juce::Justification::centredLeft);
    g.drawText ("Me'yor bahosi", juce::Rectangle<float> (546, y, 110, 16), juce::Justification::centredLeft);
    g.drawText ("Holat", juce::Rectangle<float> (672, y, 70, 16), juce::Justification::centredLeft);
    g.setColour (border);
    g.drawHorizontalLine (140, 12, 748);
}

void ChainPage::resized()
{
    tableViewport.setBounds (12, 146, 760 - 24, getHeight() - 146 - 10);
    table.setSize (tableViewport.getWidth() - 10, table.getHeight());
    detail.setBounds (776, 104, getWidth() - 776, getHeight() - 104);
}

//==============================================================================
ReportPage::ReportPage (AZMonitoringProcessor& p) : proc (p)
{
    editor.setMultiLine (true, true);
    editor.setReadOnly (true);
    editor.setScrollbarsShown (true);
    editor.setFont (mono (13.5f));
    editor.setColour (juce::TextEditor::backgroundColourId, panel);
    editor.setColour (juce::TextEditor::outlineColourId, border);
    addAndMakeVisible (editor);

    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (text); };
    saveButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Hisobotni saqlash",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("AZ Monitoring hisobot.txt"), "*.txt");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f != juce::File()) f.replaceWithText (text);
        });
    };
    addAndMakeVisible (copyButton);
    addAndMakeVisible (saveButton);
}

void ReportPage::setData (const ViewData& d)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (text.isNotEmpty() && now - lastUpdate < 2000) return;
    lastUpdate = now;
    if (d.profile == nullptr || d.standard == nullptr) return;
    const auto t = az::reportToText (d.report, *d.profile, *d.standard, d.master, d.track);
    if (t != text)
    {
        text = t;
        const auto caret = editor.getCaretPosition();
        editor.setText (text, false);
        editor.setCaretPosition (juce::jmin (caret, text.length()));
    }
}

void ReportPage::resized()
{
    editor.setBounds (0, 0, getWidth(), getHeight() - 58);
    copyButton.setBounds (0, getHeight() - 44, 150, 40);
    saveButton.setBounds (160, getHeight() - 44, 220, 40);
}

//==============================================================================
SettingsPage::SettingsPage (AZMonitoringProcessor& p) : proc (p)
{
    for (auto [box, id] : { std::pair<juce::ComboBox*, const char*> { &mode, "mode" }, { &group, "group" }, { &source, "source" }, { &standard, "standard" } })
    {
        fillChoices (*box, proc, id);
        addAndMakeVisible (*box);
    }
    modeA = std::make_unique<ComboAttach> (proc.apvts, "mode", mode);
    groupA = std::make_unique<ComboAttach> (proc.apvts, "group", group);
    sourceA = std::make_unique<ComboAttach> (proc.apvts, "source", source);
    standardA = std::make_unique<ComboAttach> (proc.apvts, "standard", standard);
    addAndMakeVisible (playOnly);
    playOnlyA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "playonly", playOnly);

    makeProfile.getProperties().set ("bold", true);
    makeProfile.setColour (juce::TextButton::buttonColourId, accent);
    makeProfile.setColour (juce::TextButton::textColourOffId, bg);
    makeProfile.onClick = [this]
    {
        if (busy) return;
        chooser = std::make_unique<juce::FileChooser> ("Etalon treklar (professional miks qilingan vokal yoki master)",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
        {
            const auto files = fc.getResults();
            if (files.isEmpty()) return;
            busy = true;
            profileStatus = juce::String (files.size()) + " ta trek tahlil qilinmoqda...";
            repaint();
            juce::Component::SafePointer<SettingsPage> safe (this);
            juce::Thread::launch ([safe, files]
            {
                juce::StringArray errors;
                auto feats = az::Profiles::analyseReferenceFiles (files, errors);
                auto profile = az::Profiles::fromReferences (feats, "Maxsus etalon (" + juce::String ((int) feats.size()) + " trek)");
                juce::MessageManager::callAsync ([safe, profile, errors]
                {
                    if (safe == nullptr) return;
                    safe->busy = false;
                    if (profile.numReferences == 0)
                        safe->profileStatus = "Xato: treklarni tahlil qilib bo'lmadi. " + errors.joinIntoString ("; ");
                    else
                    {
                        safe->proc.setCustomProfile (profile);
                        safe->profileStatus = "Tayyor: " + juce::String (profile.numReferences) + " ta trekdan etalon yaratildi va tanlandi."
                                              + (errors.isEmpty() ? juce::String() : " (" + errors.joinIntoString ("; ") + ")");
                    }
                    safe->repaint();
                });
            });
        });
    };
    addAndMakeVisible (makeProfile);
}

void SettingsPage::refresh() { repaint(); }

void SettingsPage::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const juce::Rectangle<float> left (0, 0, 580, (float) getHeight()), right (596, 0, w - 596, (float) getHeight());
    drawPanel (g, left);
    drawPanel (g, right);

    drawTitle (g, "Rejim va o'lchov", { 22, 16, 500, 24 });
    g.setFont (sans (13.0f));
    g.setColour (text2);
    const char* labels[] = { "Rejim", "Guruh", "Manba", "Standart" };
    for (int i = 0; i < 4; ++i)
        g.drawText (labels[i], juce::Rectangle<float> (22, 58 + (float) i * 52, 120, 40), juce::Justification::centredLeft);

    drawWrapped (&g,
        U ("Monitor — vokal insert zanjirining eng OXIRGI slotiga qo'yiladi. Umumiy tashxis va zanjir bahosini ko'rsatadi.\n"
           "Probe — har bir plagindan keyin qo'yiladigan o'lchov nuqtasi. Signalni o'zgartirmaydi, kechikish qo'shmaydi.\n\n"
           "Guruh: probe'lar va monitor bir guruhda bo'lishi kerak. \"Avto\" — trek nomi bo'yicha (Cubase, Logic, Pro Tools, Studio One nomni beradi). "
           "Xost trek nomini bermasa, hammasida bir xil \"Guruh N\" tanlang.\n\n"
           "Manba: \"Vokal treki\" — vokal spektri va dinamikasi janr etaloniga solishtiriladi. \"Master\" — master bus'da: "
           "loudness (LUFS), True Peak va PLR tanlangan standartga solishtiriladi.\n\n"
           "Barcha namuna chastotalari (44.1 – 384 kHz) va 64-bit (double) ishlov qo'llab-quvvatlanadi."),
        { 22, 330, 536, 440 }, sans (13.0f), text2, 3.0f);

    drawTitle (g, U ("Maxsus etalon — o'z standartingiz"), { 618, 16, w - 640, 24 });
    drawWrapped (&g,
        "Ichki janr etalonlari boshlang'ich kalibrovka. Eng aniq tashxis uchun o'zingiz namuna deb bilgan 3-10 ta professional trekni "
        "(alohida vokal stem yoki acapella - vokal rejimi uchun; tayyor master - master rejimi uchun) tanlang. "
        "AZ Monitoring ularning spektri, dinamikasi va sibilyant darajasini o'lchab, o'rtacha + ruxsat etilgan og'ish bilan etalon yaratadi.",
        { 618, 50, w - 640, 120 }, sans (13.0f), text2, 3.0f);

    g.setColour (text);
    g.setFont (sans (13.0f));
    juce::String current = proc.hasCustomProfile() ? "Saqlangan etalon: mavjud (Janr etaloni ro'yxatining oxirida)"
                                                   : "Saqlangan etalon: hali yo'q";
    g.drawText (current, juce::Rectangle<float> (618, 236, w - 640, 20), juce::Justification::centredLeft);
    if (profileStatus.isNotEmpty())
        drawWrapped (&g, profileStatus, { 618, 262, w - 640, 60 }, sans (13.0f, true), busy ? accent : ok);

    g.setColour (border);
    g.drawHorizontalLine (334, 612, w - 16);
    drawTitle (g, "DAW mosligi", { 618, 346, w - 640, 24 });
    drawWrapped (&g,
        U ("• Cubase / Nuendo, Studio One, Reaper, FL Studio, Ableton Live — VST3\n"
           "• Logic Pro, GarageBand, Ableton Live (Mac) — AU\n"
           "• Pro Tools (Studio / Ultimate, HD Native, HDX) — AAX Native\n"
           "• UAD-2 / UAD Spark plaginlari bilan: AZ Monitoring CPU'da ishlaydi va UAD DSP'ni band qilmaydi. "
           "Probe'lar UAD plagini chiqishini o'lchaydi va analog emulyatsiyalar uchun kirish darajasini (-18 dBFS ≈ 0 VU) tekshiradi.\n"
           "• Bir trekdagi barcha nusxalar bitta formatda bo'lsin (masalan, hammasi VST3). "
           "Plaginlarni alohida jarayonda ishlatadigan rejimda (bridge/sandbox) probe'lar monitorni ko'rmaydi."),
        { 618, 380, w - 640, 400 }, sans (13.0f), text2, 4.0f);
}

void SettingsPage::resized()
{
    mode.setBounds (150, 60, 400, 36);
    group.setBounds (150, 112, 400, 36);
    source.setBounds (150, 164, 400, 36);
    standard.setBounds (150, 216, 400, 36);
    playOnly.setBounds (18, 270, 540, 30);
    makeProfile.setBounds (618, 182, 260, 40);
}

//==============================================================================
ProbePage::ProbePage (AZMonitoringProcessor& p) : proc (p)
{
    for (auto [box, id] : { std::pair<juce::ComboBox*, const char*> { &mode, "mode" }, { &slot, "slot" }, { &group, "group" } })
    {
        fillChoices (*box, proc, id);
        addAndMakeVisible (*box);
    }
    modeA = std::make_unique<ComboAttach> (proc.apvts, "mode", mode);
    slotA = std::make_unique<ComboAttach> (proc.apvts, "slot", slot);
    groupA = std::make_unique<ComboAttach> (proc.apvts, "group", group);

    label.setFont (sans (15.0f));
    label.setTextToShowWhenEmpty ("masalan: UAD LA-2A, EQ3 7-Band, Pro-Q", text3);
    label.setText (proc.getPluginLabel(), false);
    label.setIndents (10, 8);
    label.onTextChange = [this] { proc.setPluginLabel (label.getText()); };
    addAndMakeVisible (label);

    spectrum.setTitle ("Shu nuqtadagi spektr");
    addAndMakeVisible (spectrum);
}

void ProbePage::setData (const ViewData& d)
{
    data = &d;
    spectrum.setData (d);
    repaint();
}

void ProbePage::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const juce::Rectangle<float> left (0, 0, 580, (float) getHeight());
    drawPanel (g, left);
    drawTitle (g, U ("AZ Probe — o'lchov nuqtasi"), { 22, 16, 500, 24 });

    g.setFont (sans (13.0f));
    g.setColour (text2);
    const char* labels[] = { "Rejim", "Joyi", "Oldingi plagin", "Guruh" };
    for (int i = 0; i < 4; ++i)
        g.drawText (labels[i], juce::Rectangle<float> (22, 58 + (float) i * 52, 120, 40), juce::Justification::centredLeft);

    if (data != nullptr)
    {
        const bool found = data->monitorCount > 0;
        const juce::Rectangle<float> st (22, 270, 536, 52);
        g.setColour ((found ? ok : warn).withAlpha (0.12f));
        g.fillRoundedRectangle (st, 8.0f);
        g.setColour (found ? ok : warn);
        g.fillEllipse (38, 291, 10, 10);
        g.setFont (sans (14.0f, true));
        g.drawText (found ? "Monitor topildi - \"" + data->group + "\" guruhida"
                          : "Monitor topilmadi - shu trekning oxirgi slotiga Monitor rejimida AZ Monitoring qo'ying",
                    st.withTrimmedLeft (40).reduced (0, 4), juce::Justification::centredLeft, true);
    }

    drawWrapped (&g,
        U ("Bu nusxa signalni o'zgartirmaydi: kirish = chiqish, kechikish 0.\n\n"
           "Joylashtirish: probe'ni o'lchanayotgan plagindan KEYINGI slotga qo'ying va \"Joyi\"da shu plagin harfini tanlang. "
           "Zanjir boshida (birinchi plagindan oldin) bitta probe \"IN\" bo'lishi kerak — shunda birinchi plagin ham baholanadi.\n\n"
           "Masalan Pro Tools: A = IN probe, B = EQ, C = probe (B), D = Kompressor, E = probe (D), … J = AZ Monitoring (Monitor).\n\n"
           "Natijalar Monitor oynasining \"Zanjir\" sahifasida chiqadi."),
        { 22, 340, 536, 420 }, sans (13.0f), text2, 3.0f);

    // O'ng tomonda jonli o'lchovlar
    const juce::Rectangle<float> right (596, 0, w - 596, 300);
    drawPanel (g, right);
    drawTitle (g, "Jonli o'lchov", { 618, 16, 300, 24 });
    if (data == nullptr) return;
    const auto& f = data->finalOut;
    struct M { const char* label; juce::String value; };
    const M ms[6] = {
        { "Momentary", f.momentary > -70 ? juce::String (f.momentary, 1) + " LUFS" : juce::String ("--") },
        { "Integrated", f.integrated > -70 ? juce::String (f.integrated, 1) + " LUFS" : juce::String ("--") },
        { "True Peak (max)", az::fmtDb (f.truePeakMax, 1, " dBTP") },
        { "Crest factor", f.crest > 0 ? juce::String (f.crest, 1) + " dB" : juce::String ("--") },
        { "RMS (faol)", az::fmtDb (f.rmsActive, 1, " dBFS") },
        { "Tahlil qilindi", az::fmtTime ((float) f.secondsAnalysed) },
    };
    const float cw = (right.getWidth() - 48) / 3.0f;
    for (int i = 0; i < 6; ++i)
    {
        const float x = right.getX() + 18 + (float) (i % 3) * (cw + 6);
        const float y = 64 + (float) (i / 3) * 104;
        g.setColour (inset);
        g.fillRoundedRectangle (juce::Rectangle<float> (x, y, cw, 92), 8.0f);
        g.setColour (text2);
        g.setFont (sans (12.0f));
        g.drawText (ms[i].label, juce::Rectangle<float> (x + 12, y + 12, cw - 24, 16), juce::Justification::centredLeft);
        g.setColour (text);
        g.setFont (mono (20.0f, true));
        g.drawText (ms[i].value, juce::Rectangle<float> (x + 12, y + 40, cw - 24, 30), juce::Justification::centredLeft);
    }
}

void ProbePage::resized()
{
    mode.setBounds (160, 60, 398, 36);
    slot.setBounds (160, 112, 398, 36);
    label.setBounds (160, 164, 398, 36);
    group.setBounds (160, 216, 398, 36);
    spectrum.setBounds (596, 316, getWidth() - 596, getHeight() - 316);
}

} // namespace az::ui
