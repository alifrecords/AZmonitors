#include "RackViews.h"
#include "PluginProcessor.h"

namespace az::ui
{
namespace
{
    void drawTitle (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r)
    {
        g.setColour (text);
        g.setFont (sans (15.0f, true));
        g.drawText (t, r, juce::Justification::centredLeft);
    }

    void styleButton (juce::TextButton& b, bool primary = false)
    {
        if (primary)
        {
            b.getProperties().set ("bold", true);
            b.setColour (juce::TextButton::buttonColourId, accent);
            b.setColour (juce::TextButton::textColourOffId, bg);
        }
    }

    juce::String latencyText (int samples, double sr)
    {
        return juce::String (1000.0 * samples / juce::jmax (1.0, sr), 1) + " ms";
    }

    const az::StageResult* stageOf (const ViewData& d, int idx)
    {
        return idx >= 0 && idx < (int) d.report.stages.size() ? &d.report.stages[(size_t) idx] : nullptr;
    }
}

//==============================================================================
// RACK RO'YXATI
//==============================================================================
void RackList::setData (const ViewData& d, const std::vector<std::shared_ptr<RackSlot>>& s, const std::vector<RackRow>& r, int sel, int ds, double sampleRate)
{
    data = &d; slots = s; rows = r; selected = sel; delta = ds; sr = sampleRate > 0 ? sampleRate : 48000.0;
    setSize (getWidth(), juce::jmax (rowH, (int) rows.size() * rowH));
    repaint();
}

void RackList::paint (juce::Graphics& g)
{
    if (data == nullptr) return;
    const float w = (float) getWidth();

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& row = rows[(size_t) i];
        const float y = (float) (i * rowH);
        const auto* st = stageOf (*data, row.stageIndex);
        const RackSlot* slot = row.isRack && row.rackIndex < (int) slots.size() ? slots[(size_t) row.rackIndex].get() : nullptr;
        const bool byp = slot != nullptr && slot->bypass.load();
        const auto status = byp ? az::Status::NoData : (st != nullptr ? st->status : az::Status::NoData);

        auto box = juce::Rectangle<float> (0, y + 3, w, (float) rowH - 6);
        if (i == selected)
        {
            g.setColour (statusColour (status).withAlpha (0.10f));
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (statusColour (status).withAlpha (0.7f));
            g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
        }
        else
        {
            g.setColour (status == az::Status::Problem ? bad.withAlpha (0.06f) : juce::Colours::transparentBlack);
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (status == az::Status::Problem ? bad.withAlpha (0.45f) : border);
            g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
        }

        if (dragFrom >= 0 && dropTo == i && dropTo != dragFrom)
        {
            g.setColour (accent);
            g.fillRect (juce::Rectangle<float> (0, dropTo > dragFrom ? y + rowH - 2 : y, w, 2));
        }

        const float cy = y + rowH * 0.5f;
        if (row.isRack)
        {
            g.setColour (juce::Colour (0xff4a4f59));
            for (int k = -1; k <= 1; ++k) g.fillRect (juce::Rectangle<float> (10, cy + k * 5 - 1, 12, 2));
            g.setColour (text2);
            g.setFont (mono (12.0f));
            g.drawText ("R" + juce::String (row.rackIndex + 1), juce::Rectangle<float> (28, y, 30, (float) rowH), juce::Justification::centredLeft);

            const juce::Rectangle<float> power (62, cy - 15, 30, 30);
            g.setColour (inset);
            g.fillEllipse (power);
            g.setColour (border2);
            g.drawEllipse (power.reduced (0.5f), 1.0f);
            juce::Path p;
            p.addCentredArc (power.getCentreX(), power.getCentreY() + 1, 7, 7, 0, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            p.startNewSubPath (power.getCentreX(), power.getCentreY() - 8);
            p.lineTo (power.getCentreX(), power.getCentreY() - 1);
            g.setColour (byp ? juce::Colour (0xff4a4f59) : ok);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (info.withAlpha (0.15f));
            g.fillRoundedRectangle (juce::Rectangle<float> (12, cy - 10, 48, 20), 4.0f);
            g.setColour (info);
            g.setFont (sans (10.5f, true));
            g.drawText ("DAW", juce::Rectangle<float> (12, cy - 10, 48, 20), juce::Justification::centred);
            g.setColour (text3);
            g.setFont (mono (10.5f));
            g.drawText (st != nullptr ? st->slot : juce::String(), juce::Rectangle<float> (64, y, 40, (float) rowH), juce::Justification::centredLeft, true);
        }

        // Nomi va format
        const juce::String name = slot != nullptr ? slot->displayName() : (st != nullptr ? st->name : juce::String());
        g.setColour (byp ? text3 : text);
        g.setFont (sans (13.5f, true));
        const float nx = 104;
        const float nameW = juce::jmin ((float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 2.0f, w - nx - 260);
        g.drawText (name, juce::Rectangle<float> (nx, y + 10, nameW, 20), juce::Justification::centredLeft, true);
        if (slot != nullptr)
        {
            const auto fmt = slot->formatLabel();
            g.setFont (sans (9.5f, true));
            const float fw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), fmt) + 10;
            const juce::Rectangle<float> badge (nx + nameW + 8, y + 13, fw, 15);
            g.setColour (slot->plugin == nullptr ? bad.withAlpha (0.2f) : border);
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (slot->plugin == nullptr ? bad : text2);
            g.drawText (fmt, badge, juce::Justification::centred);
        }
        if (delta >= 0 && row.isRack && row.rackIndex == delta)
        {
            g.setColour (accent);
            g.setFont (sans (9.5f, true));
            g.drawText ("DELTA", juce::Rectangle<float> (w - 250, y + 13, 50, 15), juce::Justification::centredRight);
        }

        juce::String sub;
        if (byp) sub = "Bypass - tahlildan chiqarilgan";
        else if (slot != nullptr && slot->plugin == nullptr) sub = "Plagin bu kompyuterda topilmadi - holati saqlangan";
        else if (st != nullptr) sub = st->role + (st->valid ? "  |  " + st->effect : juce::String());
        else sub = "Tahlil kutilmoqda - trekni ijro eting";
        g.setColour (text3);
        g.setFont (sans (11.5f));
        g.drawText (sub, juce::Rectangle<float> (nx, y + 32, w - nx - 200, 18), juce::Justification::centredLeft, true);

        if (slot != nullptr)
        {
            g.setColour (text3);
            g.setFont (mono (11.0f));
            g.drawText (latencyText (slot->latency.load(), sr),
                        juce::Rectangle<float> (w - 196, y, 56, (float) rowH), juce::Justification::centredRight);
        }
        g.setColour (text);
        g.setFont (mono (13.0f));
        g.drawText (st != nullptr && st->valid && ! byp ? juce::String (st->score) : juce::String ("--"),
                    juce::Rectangle<float> (w - 132, y, 40, (float) rowH), juce::Justification::centredRight);
        drawChip (g, { w - 84, cy - 11, 78, 22 }, status, byp ? juce::String ("BYPASS") : juce::String());
    }
}

void RackList::mouseDown (const juce::MouseEvent& e)
{
    const int i = e.y / rowH;
    dragFrom = -1; dropTo = -1;
    if (i < 0 || i >= (int) rows.size()) return;
    const auto& row = rows[(size_t) i];
    if (row.isRack && e.x < 26) { dragFrom = i; dropTo = i; return; }
    if (row.isRack && e.x >= 60 && e.x <= 94)
    {
        if (onToggleBypass) onToggleBypass (row.rackIndex);
        return;
    }
    if (onSelect) onSelect (i);
}

void RackList::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    int i = juce::jlimit (0, (int) rows.size() - 1, e.y / rowH);
    while (i >= 0 && ! rows[(size_t) i].isRack) ++i;   // DAW qatorlari ustiga tashlab bo'lmaydi
    dropTo = juce::jlimit (0, (int) rows.size() - 1, i);
    repaint();
}

void RackList::mouseUp (const juce::MouseEvent&)
{
    if (dragFrom >= 0 && dropTo >= 0 && dropTo != dragFrom && onMove)
        onMove (rows[(size_t) dragFrom].rackIndex, rows[(size_t) dropTo].rackIndex);
    dragFrom = dropTo = -1;
    repaint();
}

//==============================================================================
// RACK SAHIFASI
//==============================================================================
RackPage::RackPage (AZMonitoringProcessor& p) : proc (p)
{
    addAndMakeVisible (listViewport);
    listViewport.setViewedComponent (&list, false);
    listViewport.setScrollBarsShown (true, false);
    listViewport.setScrollBarThickness (8);

    list.onSelect = [this] (int i)
    {
        if (i < 0 || i >= (int) rows.size()) return;
        if (rows[(size_t) i].isRack) { selectedId = rows[(size_t) i].slotId; selectedStage = -1; }
        else { selectedStage = rows[(size_t) i].stageIndex; selectedId = 0; }
        proposals.clear(); applied.clear(); message.clear();
        if (data) setData (*data);
    };
    list.onToggleBypass = [this] (int idx)
    {
        auto s = proc.rack.getSlots();
        if (idx >= 0 && idx < (int) s.size()) proc.rack.setBypass (idx, ! s[(size_t) idx]->bypass.load());
        if (data) setData (*data);
    };
    list.onMove = [this] (int from, int to)
    {
        proc.rack.move (from, to);
        proc.resetAnalysis();
        if (data) setData (*data);
    };

    addAndMakeVisible (addButton);
    addButton.getProperties().set ("noOutline", false);
    addButton.onClick = [this] { if (onAddPlugin) onAddPlugin (-1); };

    for (auto* b : { &savePreset, &loadPreset })
        addAndMakeVisible (b);
    savePreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Rack presetini saqlash",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("AZ Rack.azrack"), "*.azrack");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            message = proc.saveRackPreset (f.withFileExtension ("azrack")) ? "Preset saqlandi: " + f.getFileName() : juce::String ("Presetni saqlab bo'lmadi");
            repaint();
        });
    };
    loadPreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Rack presetini yuklash",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.azrack");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            const auto err = proc.loadRackPreset (f);
            message = err.isEmpty() ? "Preset yuklandi: " + f.getFileName() : "Ba'zi plaginlar topilmadi:\n" + err;
            selectedId = 0;
            repaint();
        });
    };

    styleButton (applyButton, true);
    for (auto* b : { &applyButton, &editorButton, &deltaButton, &bypassButton, &addAfterButton, &removeButton })
        addAndMakeVisible (b);
    deltaButton.setClickingTogglesState (false);

    applyButton.onClick = [this]
    {
        auto* row = currentRow();
        if (row == nullptr || ! row->isRack) return;
        auto s = proc.rack.getSlots();
        if (row->rackIndex >= (int) s.size() || s[(size_t) row->rackIndex]->plugin == nullptr) return;
        auto& inst = *s[(size_t) row->rackIndex]->plugin;

        if (! applied.empty())     // Qaytarish
        {
            az::Rack::apply (inst, applied, true);
            message = "Oldingi sozlamalar qaytarildi.";
            applied.clear(); proposals.clear();
        }
        else if (! proposals.empty() && proposalsFor == selectedId)   // Tasdiqlash
        {
            az::Rack::apply (inst, proposals);
            applied = proposals;
            proposals.clear();
            message = "Tavsiya qo'llandi. Natijani eshiting - kerak bo'lsa \"Qaytarish\" ni bosing. Yangi tahlil uchun trekni qayta ijro eting.";
            proc.resetAnalysis();
        }
        else
        {
            const auto* st = data != nullptr ? stageOf (*data, row->stageIndex) : nullptr;
            if (st == nullptr || ! st->valid) { message = "Avval trekni ijro eting - tavsiya tahlilga asoslanadi."; repaint(); return; }
            proposals = az::Rack::proposeFor (inst, *st, data->finalOut, *data->profile);
            proposalsFor = selectedId;
            message = proposals.empty()
                ? juce::String ("Bu plaginning parametrlari avtomatik tanilmadi yoki o'zgartirish shart emas. Tavsiyani plagin oynasida qo'lda qo'llang.")
                : juce::String ("Quyidagi o'zgarishlar qo'llanadi. Tasdiqlash uchun tugmani yana bir marta bosing.");
        }
        updateButtons();
        repaint();
    };
    editorButton.onClick = [this]
    {
        if (auto* row = currentRow(); row != nullptr && row->isRack) proc.rack.openEditor (row->rackIndex);
    };
    deltaButton.onClick = [this]
    {
        auto* row = currentRow();
        if (row == nullptr || ! row->isRack) return;
        proc.rack.setDelta (proc.rack.getDelta() == row->rackIndex ? -1 : row->rackIndex);
        updateButtons();
        if (data) setData (*data);
    };
    bypassButton.onClick = [this]
    {
        auto* row = currentRow();
        if (row == nullptr || ! row->isRack) return;
        list.onToggleBypass (row->rackIndex);
    };
    addAfterButton.onClick = [this]
    {
        auto* row = currentRow();
        if (onAddPlugin) onAddPlugin (row != nullptr && row->isRack ? row->rackIndex + 1 : -1);
    };
    removeButton.onClick = [this]
    {
        auto* row = currentRow();
        if (row == nullptr || ! row->isRack) return;
        proc.rack.remove (row->rackIndex);
        proc.updateLatency();
        proc.resetAnalysis();
        selectedId = 0;
        if (data) setData (*data);
    };
}

void RackPage::rebuildRows()
{
    rows.clear();
    slots = proc.rack.getSlots();
    if (data == nullptr) return;

    std::map<int, int> stageForSlot;
    for (int i = 0; i < (int) data->report.stages.size(); ++i)
    {
        const auto& st = data->report.stages[(size_t) i];
        if (st.rackSlot >= 0) stageForSlot[st.rackSlot] = i;
        else rows.push_back ({ false, -1, i, 0 });
    }
    for (int k = 0; k < (int) slots.size(); ++k)
    {
        auto it = stageForSlot.find (k);
        rows.push_back ({ true, k, it != stageForSlot.end() ? it->second : -1, slots[(size_t) k]->id });
    }
}

int RackPage::selectedRow() const
{
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        if (selectedId != 0 && rows[(size_t) i].isRack && rows[(size_t) i].slotId == selectedId) return i;
        if (selectedId == 0 && selectedStage >= 0 && ! rows[(size_t) i].isRack && rows[(size_t) i].stageIndex == selectedStage) return i;
    }
    return -1;
}

const RackRow* RackPage::currentRow() const
{
    const int i = selectedRow();
    return i >= 0 ? &rows[(size_t) i] : nullptr;
}

void RackPage::setData (const ViewData& d)
{
    data = &d;
    rebuildRows();
    if (selectedRow() < 0)
    {
        // Avtomatik: eng yomon bahodagi qator, bo'lmasa birinchi rack sloti
        int best = -1, worst = 101;
        for (int i = 0; i < (int) rows.size(); ++i)
            if (auto* st = stageOf (d, rows[(size_t) i].stageIndex); st != nullptr && st->valid && st->score < worst) { worst = st->score; best = i; }
        if (best < 0 && ! rows.empty()) best = 0;
        if (best >= 0)
        {
            const auto& r = rows[(size_t) best];
            selectedId = r.isRack ? r.slotId : 0;
            selectedStage = r.isRack ? -1 : r.stageIndex;
        }
    }
    list.setData (d, slots, rows, selectedRow(), proc.rack.getDelta(), proc.getSampleRate());
    updateButtons();
    repaint();
}

void RackPage::updateButtons()
{
    auto* row = currentRow();
    const bool isRack = row != nullptr && row->isRack;
    const bool loaded = isRack && row->rackIndex < (int) slots.size() && slots[(size_t) row->rackIndex]->plugin != nullptr;
    for (auto* b : { &applyButton, &editorButton, &deltaButton, &bypassButton, &removeButton })
        b->setVisible (isRack);
    applyButton.setEnabled (loaded);
    editorButton.setEnabled (loaded);
    deltaButton.setEnabled (loaded);

    applyButton.setButtonText (! applied.empty() ? juce::String ("Qaytarish")
                               : (! proposals.empty() && proposalsFor == selectedId) ? "Tasdiqlash (" + juce::String ((int) proposals.size()) + ")"
                               : juce::String ("Tavsiyani qo'llash"));
    const bool deltaOn = isRack && proc.rack.getDelta() == row->rackIndex;
    deltaButton.setButtonText (deltaOn ? "Delta: YONIQ" : "Delta tinglash");
    deltaButton.setColour (juce::TextButton::buttonColourId, deltaOn ? accent.withAlpha (0.25f) : field);
    const bool byp = isRack && row->rackIndex < (int) slots.size() && slots[(size_t) row->rackIndex]->bypass.load();
    bypassButton.setButtonText (byp ? "Yoqish" : "Bypass");
}

void RackPage::paint (juce::Graphics& g)
{
    if (data == nullptr) return;
    const auto& r = data->report;
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;

    // Plitkalar
    juce::StringArray okNames, badNames;
    for (auto& row : rows)
        if (auto* st = stageOf (*data, row.stageIndex); st != nullptr && st->valid)
        {
            if (st->status == az::Status::Ideal) okNames.add (st->name);
            else if (st->status == az::Status::Warn || st->status == az::Status::Problem) badNames.add (st->name);
        }
    struct Tile { juce::String label, value, note; juce::Colour c; };
    const bool any = r.chainScore > 0;
    const Tile tiles[5] = {
        { "Zanjir salomatligi", any ? juce::String (r.chainScore) + "/100" : juce::String ("--"),
          juce::String ((int) slots.size()) + " ta plagin rack ichida", any ? (r.chainScore >= 85 ? ok : r.chainScore >= 60 ? accent : bad) : text3 },
        { "Ideal ishlayotgan", juce::String (okNames.size()), okNames.joinIntoString (", "), ok },
        { "Me'yori buzilgan", juce::String (badNames.size()), badNames.joinIntoString (", "), bad },
        { "Rack kechikishi", latencyText (proc.getLatencySamples(), sr), "PDC: DAW ga xabar qilinadi", text },
        { data->master ? "Master bahosi" : "Vokal bahosi", r.hasData ? juce::String (r.score) : juce::String ("--"),
          data->profile ? data->profile->name : juce::String(), r.hasData ? (r.score >= 85 ? ok : r.score >= 60 ? accent : bad) : text3 },
    };
    const float tw = ((float) getWidth() - 40) / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        const juce::Rectangle<float> t ((float) i * (tw + 10), 0, tw, 80);
        drawPanel (g, t);
        g.setColour (text2);  g.setFont (sans (11.5f));
        g.drawText (tiles[i].label, juce::Rectangle<float> (t.getX() + 14, 10, tw - 28, 16), juce::Justification::centredLeft);
        g.setColour (tiles[i].c); g.setFont (mono (21.0f, true));
        g.drawText (tiles[i].value, juce::Rectangle<float> (t.getX() + 14, 28, tw - 28, 26), juce::Justification::centredLeft);
        g.setColour (text3); g.setFont (sans (11.0f));
        g.drawText (tiles[i].note, juce::Rectangle<float> (t.getX() + 14, 56, tw - 28, 16), juce::Justification::centredLeft, true);
    }

    // Chap panel
    const juce::Rectangle<float> left (0, 96, 560, (float) getHeight() - 96);
    drawPanel (g, left);
    drawTitle (g, "AZ Rack - vokal zanjiri", { 18, 108, 300, 22 });
    g.setColour (text3); g.setFont (sans (11.0f));
    g.drawText ("Tartib: chapdagi tutqichdan sudrang", juce::Rectangle<float> (300, 108, 242, 22), juce::Justification::centredRight);

    auto pointLine = [&g] (float y, juce::Colour c, const juce::String& s)
    {
        g.setColour (c); g.fillEllipse (22, y + 5, 8, 8);
        g.setColour (text3); g.setFont (mono (11.0f));
        g.drawText (s, juce::Rectangle<float> (38, y, 500, 18), juce::Justification::centredLeft, true);
    };
    const auto in = proc.inputAnalyzer.getSnapshot();
    pointLine (136, ok, "KIRISH  |  " + az::fmtDb (in.rmsActive, 1, " dBFS RMS") + "  |  crest " + (in.crest > 0 ? juce::String (in.crest, 1) + " dB" : juce::String ("--")));
    pointLine (left.getBottom() - 70, accent, "CHIQISH  |  AZ Monitoring tahlili  |  True Peak " + az::fmtDb (data->finalOut.truePeakMax, 1, " dBTP"));

    if (rows.empty())
    {
        drawWrapped (&g, U ("Rack bo'sh. Plaginlaringizni shu yerga qo'shing — AZ Monitoring har birining nomini, parametrlarini va signalga qilgan "
                            "ta'sirini aniq ko'radi.\n\nDAW insertlaridagi plaginlar ham ko'rinishi uchun ulardan keyin AZ Monitoring'ni Probe rejimida qo'ying."),
                     { 22, 170, 516, 200 }, sans (13.0f), text2, 3.0f);
    }

    // O'ng panel — tafsilot
    const juce::Rectangle<float> right (576, 96, (float) getWidth() - 576, (float) getHeight() - 96);
    const auto* row = currentRow();
    const auto* st = row != nullptr ? stageOf (*data, row->stageIndex) : nullptr;
    const RackSlot* slot = row != nullptr && row->isRack && row->rackIndex < (int) slots.size() ? slots[(size_t) row->rackIndex].get() : nullptr;
    const auto status = slot != nullptr && slot->bypass.load() ? az::Status::NoData : (st != nullptr ? st->status : az::Status::NoData);
    drawPanel (g, right, row != nullptr ? statusColour (status).withAlpha (0.8f) : border);
    const float x0 = right.getX() + 18, wR = right.getWidth() - 36;

    if (row == nullptr)
    {
        drawTitle (g, "Plagin tafsiloti", { x0, 110, wR, 22 });
        drawWrapped (&g, "Chapdan plaginni tanlang.", { x0, 140, wR, 60 }, sans (13.0f), text2);
        return;
    }

    juce::String meta = row->isRack ? "R" + juce::String (row->rackIndex + 1) + U ("  ·  ") + (slot ? slot->formatLabel() : juce::String()) + U ("  ·  AZ Rack")
                                    : (st ? st->slot : juce::String()) + U ("  ·  DAW inserti (probe orqali)");
    g.setColour (text3); g.setFont (mono (11.0f));
    g.drawText (meta, juce::Rectangle<float> (x0, 110, wR - 120, 16), juce::Justification::centredLeft, true);
    g.setColour (text); g.setFont (sans (19.0f, true));
    g.drawText (slot ? slot->displayName() : (st ? st->name : juce::String()), juce::Rectangle<float> (x0, 128, wR - 110, 26), juce::Justification::centredLeft, true);
    g.setColour (text2); g.setFont (sans (12.5f));
    g.drawText ("Aniqlangan vazifa: " + (st ? st->role : juce::String ("tahlil kutilmoqda")), juce::Rectangle<float> (x0, 156, wR, 18), juce::Justification::centredLeft, true);
    drawChip (g, { right.getRight() - 120, 110, 102, 22 }, status,
              slot && slot->bypass.load() ? juce::String ("BYPASS") : (st && st->valid ? statusLabel (st->status) + U (" · ") + juce::String (st->score) : statusLabel (status)));

    float y = 186;
    if (st != nullptr && st->valid)
    {
        struct T { juce::String label, value; juce::Colour c; };
        const float crestD = st->crestAfter - st->crestBefore;
        const T tiles4[4] = {
            st->grRange >= 0 && st->role.contains ("Kompressor")
                ? T { "Gain reduction / ratio", juce::String (st->grRange, 1) + " dB  ~" + juce::String (st->effRatio, 1) + ":1", st->grRange >= 12 ? bad : st->grRange >= 8 ? warn : ok }
                : T { "Daraja o'zgarishi", az::fmtDb (st->gainChange, 1, " LU"), text },
            { "Crest", juce::String (st->crestBefore, 1) + U (" → ") + juce::String (st->crestAfter, 1), crestD <= -7 ? bad : crestD <= -5 ? warn : text },
            { "Kirish (RMS)", az::fmtDb (st->levelIn, 1, " dBFS"), (st->levelIn > -10 || st->levelIn < -30) ? warn : ok },
            { "Chiqish TP", az::fmtDb (st->tpOut, 1, " dBTP"), st->tpOut > 0 ? bad : text },
        };
        const float cw = (wR - 24) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<float> t (x0 + (float) i * (cw + 8), y, cw, 50);
            g.setColour (inset); g.fillRoundedRectangle (t, 8.0f);
            g.setColour (text2); g.setFont (sans (10.5f));
            g.drawText (tiles4[i].label, t.reduced (10, 6).withHeight (14), juce::Justification::centredLeft);
            g.setColour (tiles4[i].c); g.setFont (mono (13.5f, true));
            g.drawText (tiles4[i].value, t.reduced (10, 6).withTrimmedTop (18), juce::Justification::centredLeft, true);
        }
        y += 62;
    }

    // Parametrlar yoki tavsiyalar
    if (slot != nullptr && slot->plugin != nullptr)
    {
        const bool showProps = (! proposals.empty() && proposalsFor == selectedId) || ! applied.empty();
        g.setColour (text); g.setFont (sans (12.5f, true));
        g.drawText (showProps ? (applied.empty() ? "Tavsiya etilgan o'zgarishlar" : "Qo'llangan o'zgarishlar")
                              : "Plagin parametrlari - to'g'ridan-to'g'ri o'qildi",
                    juce::Rectangle<float> (x0, y, wR, 18), juce::Justification::centredLeft);
        y += 24;
        g.setColour (border); g.drawHorizontalLine ((int) y, x0, x0 + wR);
        y += 6;
        if (showProps)
        {
            for (auto& pr : (applied.empty() ? proposals : applied))
            {
                g.setColour (juce::Colour (0xffd6d3cc)); g.setFont (sans (12.5f));
                g.drawText (pr.name, juce::Rectangle<float> (x0, y, wR * 0.45f, 20), juce::Justification::centredLeft, true);
                g.setColour (warn); g.setFont (mono (12.5f));
                g.drawText (pr.fromText, juce::Rectangle<float> (x0 + wR * 0.45f, y, wR * 0.25f, 20), juce::Justification::centredLeft, true);
                g.setColour (ok);
                g.drawText (U ("→ ") + pr.toText, juce::Rectangle<float> (x0 + wR * 0.70f, y, wR * 0.30f, 20), juce::Justification::centredLeft, true);
                y += 22;
            }
        }
        else
        {
            const auto params = az::Rack::keyParameters (*slot->plugin, 8);
            const float colW = (wR - 16) / 2.0f;
            for (int i = 0; i < (int) params.size(); ++i)
            {
                const float px = x0 + (float) (i % 2) * (colW + 16), py = y + (float) (i / 2) * 22;
                g.setColour (text2); g.setFont (sans (12.0f));
                g.drawText (params[(size_t) i].name, juce::Rectangle<float> (px, py, colW * 0.55f, 20), juce::Justification::centredLeft, true);
                g.setColour (text); g.setFont (mono (12.0f));
                g.drawText (params[(size_t) i].value, juce::Rectangle<float> (px + colW * 0.55f, py, colW * 0.45f, 20), juce::Justification::centredRight, true);
            }
            y += (float) ((params.size() + 1) / 2) * 22;
        }
        y += 10;
    }
    else if (! row->isRack)
    {
        drawWrapped (&g, "Bu plagin DAW insertida turibdi, shuning uchun uning parametrlarini o'qib bo'lmaydi. Parametrlarni ko'rish va "
                         "tavsiyani avtomatik qo'llash uchun uni AZ Rack'ka qo'shing (faqat AAX formatidagi plaginlar bundan mustasno).",
                     { x0, y, wR, 60 }, sans (12.0f), text3, 2.0f);
        y += 64;
    }

    // Tavsiya matni
    const float buttonsTop = (float) getHeight() - 106;
    juce::String advice = message.isNotEmpty() ? message
                        : st != nullptr && st->valid ? (st->advice.isNotEmpty() ? st->advice : juce::String ("Bu plagin me'yorda ishlayapti - o'zgartirish shart emas."))
                        : st != nullptr ? st->effect : juce::String ("Tahlil uchun trekni ijro eting.");
    const juce::Rectangle<float> box (x0, y, wR, juce::jmax (40.0f, buttonsTop - y - 12));
    g.setColour (inset);
    g.fillRoundedRectangle (box, 8.0f);
    drawWrapped (&g, advice, box.reduced (12, 10), sans (12.5f), juce::Colour (0xffd6d3cc), 3.0f);
}

void RackPage::resized()
{
    listViewport.setBounds (12, 160, 560 - 24, getHeight() - 160 - 150);
    list.setSize (listViewport.getWidth() - 10, list.getHeight());
    addButton.setBounds (18, getHeight() - 142, 524, 42);
    savePreset.setBounds (18, getHeight() - 46, 150, 34);
    loadPreset.setBounds (176, getHeight() - 46, 150, 34);

    const int x0 = 576 + 18, wR = getWidth() - 576 - 36;
    const int bw = (wR - 24) / 4;
    const int y1 = getHeight() - 106;
    applyButton.setBounds (x0, y1, bw, 42);
    editorButton.setBounds (x0 + (bw + 8), y1, bw, 42);
    deltaButton.setBounds (x0 + 2 * (bw + 8), y1, bw, 42);
    bypassButton.setBounds (x0 + 3 * (bw + 8), y1, bw, 42);
    addAfterButton.setBounds (x0, y1 + 52, (wR - 8) / 2, 36);
    removeButton.setBounds (x0 + (wR - 8) / 2 + 8, y1 + 52, (wR - 8) / 2, 36);
}

//==============================================================================
// PLAGIN QO'SHISH OYNASI
//==============================================================================
BrowserOverlay::BrowserOverlay (AZMonitoringProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty (U ("Qidirish: nomi yoki ishlab chiqaruvchi…"), text3);
    search.setFont (sans (14.0f));
    search.setIndents (12, 11);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    for (auto* name : { "Hammasi", "EQ", "Dinamika", "De-esser", "Saturatsiya", "Reverb / Delay", "UAD-2", "Boshqa" })
    {
        auto* b = catButtons.add (new juce::TextButton (name));
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2001);
        b->setColour (juce::TextButton::buttonOnColourId, border2);
        b->onClick = [this, b] { category = b->getButtonText(); applyFilter(); };
        addAndMakeVisible (b);
    }
    catButtons[0]->setToggleState (true, juce::dontSendNotification);

    scanButton.onClick = [this] { proc.getCatalog().startScan (proc.getCatalog().hasBeenScanned()); wasScanning = true; repaint(); };
    closeButton.onClick = [this] { setVisible (false); };
    styleButton (addButton, true);
    addButton.onClick = [this] { addRow (listBox.getSelectedRow()); };
    for (auto* b : { &scanButton, &closeButton, &addButton }) addAndMakeVisible (b);

    listBox.setRowHeight (46);
    listBox.setColour (juce::ListBox::backgroundColourId, panel);
    listBox.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (listBox);
}

juce::Rectangle<int> BrowserOverlay::modalBounds() const
{
    return juce::Rectangle<int> (0, 0, 960, 780).withCentre (getLocalBounds().getCentre());
}

void BrowserOverlay::open (int index, const ViewData& d)
{
    insertIndex = index;
    data = &d;
    status.clear();
    all = proc.getCatalog().getEffects();
    applyFilter();
    refresh (d);
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void BrowserOverlay::refresh (const ViewData& d)
{
    data = &d;
    auto& cat = proc.getCatalog();
    if (wasScanning && ! cat.isScanning())
    {
        wasScanning = false;
        all = cat.getEffects();
        applyFilter();
    }
    if (cat.isScanning()) wasScanning = true;

    // AZ tavsiyasi: tashxisga ko'ra zanjirga nima kerak
    recTitle.clear(); recText.clear(); recCategory.clear();
    for (auto& c : d.report.checks)
    {
        if (c.status != az::Status::Warn && c.status != az::Status::Problem) continue;
        if (c.title.startsWith ("Sibilyant")) { recCategory = "De-esser"; recTitle = "Bu zanjirga de-esser kerak"; recText = "Sibilyant etalondan baland. Kompressorlardan keyin 5-9 kHz split-band de-esser qo'ying."; break; }
        if (c.title.startsWith ("Dinamika") && c.advice.contains ("suzib")) { recCategory = "Dinamika"; recTitle = "Kompressor kerak"; recText = "Vokal dinamikasi juda keng. Yumshoq (opto) kompressor qo'shing: 3:1, 3-6 dB gain reduction."; break; }
        if (c.freqHi > 0 && c.title.startsWith ("Past")) { recCategory = "EQ"; recTitle = "High-pass EQ kerak"; recText = "100 Hz dan pastda ortiqcha energiya. Zanjir boshiga EQ qo'yib HPF 80-100 Hz ni yoqing."; break; }
        if (c.freqHi > 0) { recCategory = "EQ"; recTitle = "Tembr uchun EQ kerak"; recText = c.title + ": " + c.advice; break; }
    }
    repaint();
}

void BrowserOverlay::applyFilter()
{
    filtered.clear();
    const auto q = search.getText().trim().toLowerCase();
    for (auto& d : all)
    {
        if (q.isNotEmpty() && ! (d.name + " " + d.manufacturerName).toLowerCase().contains (q)) continue;
        if (category == "UAD-2") { if (! az::PluginCatalog::isUad (d)) continue; }
        else if (category != "Hammasi" && az::PluginCatalog::categoryOf (d) != category) continue;
        filtered.add (d);
    }
    listBox.updateContent();
    listBox.repaint();
}

void BrowserOverlay::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= filtered.size()) return;
    const auto& d = filtered.getReference (row);
    if (selected)
    {
        g.setColour (ok.withAlpha (0.10f));
        g.fillRoundedRectangle (juce::Rectangle<float> (0, 1, (float) w, (float) h - 2), 8.0f);
    }
    g.setColour (text); g.setFont (sans (13.5f, true));
    g.drawText (d.name, juce::Rectangle<int> (12, 4, w - 330, 20), juce::Justification::centredLeft, true);
    g.setColour (text3); g.setFont (sans (11.5f));
    g.drawText (az::PluginCatalog::categoryOf (d) + (az::PluginCatalog::isUad (d) ? U (" · UAD-2 DSP") : juce::String()),
                juce::Rectangle<int> (12, 24, w - 330, 18), juce::Justification::centredLeft, true);
    g.setColour (text2); g.setFont (sans (12.0f));
    g.drawText (d.manufacturerName, juce::Rectangle<int> (w - 310, 0, 190, h), juce::Justification::centredLeft, true);
    g.setColour (text2); g.setFont (sans (10.5f, true));
    g.drawText (d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName, juce::Rectangle<int> (w - 110, 0, 100, h), juce::Justification::centredLeft);
}

void BrowserOverlay::addRow (int row)
{
    if (row < 0 || row >= filtered.size()) { status = "Ro'yxatdan plaginni tanlang."; repaint(); return; }
    const auto d = filtered[row];
    status = d.name + " yuklanmoqda...";
    repaint();
    const auto err = proc.addPlugin (d, insertIndex);
    if (err.isNotEmpty()) { status = "Xato: " + err; repaint(); return; }
    auto slots = proc.rack.getSlots();
    const int idx = insertIndex < 0 ? (int) slots.size() - 1 : juce::jlimit (0, (int) slots.size() - 1, insertIndex);
    setVisible (false);
    if (onAdded && idx >= 0) onAdded (slots[(size_t) idx]->id);
}

void BrowserOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (! modalBounds().contains (e.getPosition())) setVisible (false);
}

void BrowserOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    const auto m = modalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (m, 14.0f);
    g.setColour (border2);
    g.drawRoundedRectangle (m.reduced (0.5f), 14.0f, 1.0f);

    g.setColour (text); g.setFont (sans (18.0f, true));
    const auto where = insertIndex < 0 ? juce::String ("rack oxiriga") : "R" + juce::String (insertIndex) + " dan keyin";
    g.drawText ("Plagin qo'shish - " + where, juce::Rectangle<float> (m.getX() + 22, m.getY() + 18, 600, 24), juce::Justification::centredLeft);
    auto& cat = proc.getCatalog();
    g.setColour (text3); g.setFont (sans (12.0f));
    g.drawText ("Kompyuteringizdagi VST3 / AU plaginlar: " + juce::String (all.size()) + " ta  |  ko'rsatilmoqda: " + juce::String (filtered.size()),
                juce::Rectangle<float> (m.getX() + 22, m.getY() + 44, 600, 18), juce::Justification::centredLeft);

    const auto lb = listBox.getBounds().toFloat();
    if (cat.isScanning())
    {
        g.setColour (inset); g.fillRoundedRectangle (lb, 8.0f);
        g.setColour (text); g.setFont (sans (15.0f, true));
        g.drawText ("Plaginlar skanerlanmoqda...", lb.withHeight (60).withY (lb.getCentreY() - 50), juce::Justification::centred);
        const juce::Rectangle<float> bar (lb.getCentreX() - 200, lb.getCentreY() + 10, 400, 6);
        g.setColour (border); g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * cat.getProgress()), 3.0f);
        g.setColour (text3); g.setFont (sans (12.0f));
        g.drawText (cat.getStatus(), bar.withY (bar.getBottom() + 10).withHeight (18).expanded (100, 0), juce::Justification::centred, true);
    }
    else if (all.isEmpty())
    {
        g.setColour (inset); g.fillRoundedRectangle (lb, 8.0f);
        drawWrapped (&g, cat.hasBeenScanned()
                         ? juce::String ("Effekt plaginlari topilmadi. Plaginlar standart papkalarda o'rnatilganini tekshiring va qayta skanerlang.")
                         : U ("Plaginlar hali skanerlanmagan.\n\n\"Qayta skanerlash\" tugmasini bosing — AZ Monitoring kompyuteringizdagi VST3/AU plaginlarni bir marta "
                              "ro'yxatga oladi (bir necha daqiqa). Agar biror plagin skanerlash paytida DAW'ni yopib yuborsa, keyingi safar u o'tkazib yuboriladi."),
                     lb.reduced (40, 60), sans (14.0f), text2, 4.0f);
    }

    // O'ng panel: AZ tavsiyasi va eslatma
    const float rx = m.getRight() - 22 - 270;
    float ry = lb.getY();
    if (recTitle.isNotEmpty())
    {
        juce::StringArray have;
        for (auto& d : all)
            if (az::PluginCatalog::categoryOf (d) == recCategory && have.size() < 3) have.addIfNotAlreadyThere (d.name);
        const juce::String body = recText + (have.isEmpty() ? juce::String() : "\n\nSizda bor: " + have.joinIntoString (", "));
        const float h = drawWrapped (nullptr, body, { 0, 0, 246, 1000 }, sans (12.0f), text2) + 64;
        const juce::Rectangle<float> card (rx, ry, 270, h);
        g.setColour (inset); g.fillRoundedRectangle (card, 10.0f);
        g.setColour (ok.withAlpha (0.5f)); g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (ok); g.setFont (sans (11.0f, true));
        g.drawText ("AZ TAVSIYASI", juce::Rectangle<float> (rx + 12, ry + 10, 246, 14), juce::Justification::centredLeft);
        g.setColour (text); g.setFont (sans (13.5f, true));
        g.drawText (recTitle, juce::Rectangle<float> (rx + 12, ry + 26, 246, 18), juce::Justification::centredLeft, true);
        drawWrapped (&g, body, { rx + 12, ry + 48, 246, h }, sans (12.0f), text2);
        ry += h + 12;
    }
    const juce::String note = U ("Faqat AAX formatidagi plaginlar (masalan Avid Dyn3, EQ3) boshqa plagin ichiga yuklanmaydi — ularni Pro Tools insertida qoldiring "
                                 "va oldiga/keyiniga Probe qo'ying. UAD-2 plaginlari VST3/AU versiyasi orqali rack'da DSP kartangizda ishlaydi.");
    const float nh = drawWrapped (nullptr, note, { 0, 0, 246, 1000 }, sans (11.5f), text2) + 40;
    g.setColour (inset); g.fillRoundedRectangle (juce::Rectangle<float> (rx, ry, 270, nh), 10.0f);
    g.setColour (text); g.setFont (sans (12.5f, true));
    g.drawText ("Eslatma", juce::Rectangle<float> (rx + 12, ry + 10, 246, 16), juce::Justification::centredLeft);
    drawWrapped (&g, note, { rx + 12, ry + 30, 246, nh }, sans (11.5f), text2);

    if (status.isNotEmpty())
    {
        g.setColour (status.startsWith ("Xato") ? bad : text2);
        g.setFont (sans (12.5f));
        g.drawText (status, juce::Rectangle<float> (m.getX() + 22, m.getBottom() - 46, 560, 30), juce::Justification::centredLeft, true);
    }
}

void BrowserOverlay::resized()
{
    const auto m = modalBounds();
    closeButton.setBounds (m.getRight() - 22 - 90, m.getY() + 18, 90, 38);
    search.setBounds (m.getX() + 22, m.getY() + 76, m.getWidth() - 44 - 170, 42);
    scanButton.setBounds (m.getRight() - 22 - 160, m.getY() + 76, 160, 42);
    int x = m.getX() + 22;
    for (auto* b : catButtons)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (sans (13.0f), b->getButtonText()) + 32;
        b->setBounds (x, m.getY() + 130, w, 34);
        x += w + 6;
    }
    listBox.setBounds (m.getX() + 22, m.getY() + 178, m.getWidth() - 44 - 290, m.getHeight() - 178 - 70);
    addButton.setBounds (m.getRight() - 22 - 160, m.getBottom() - 56, 160, 40);
}

} // namespace az::ui
