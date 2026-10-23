#pragma once
#include "Views.h"
#include "rack/Rack.h"

namespace az::ui
{
//==============================================================================
// Rack ro'yxatidagi qator: DAW inserti (probe orqali) yoki AZ Rack sloti
struct RackRow
{
    bool isRack = false;
    int rackIndex = -1;          // rack slot indeksi
    int stageIndex = -1;         // report.stages ichidagi indeks (-1 = baholanmagan)
    juce::uint32 slotId = 0;
};

class RackList final : public juce::Component
{
public:
    std::function<void (int row)> onSelect;
    std::function<void (int rackIndex)> onToggleBypass;
    std::function<void (int from, int to)> onMove;

    void setData (const ViewData&, const std::vector<std::shared_ptr<RackSlot>>&, const std::vector<RackRow>&, int selectedRow, int deltaSlot, double sampleRate);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int rowH = 60;

private:
    const ViewData* data = nullptr;
    std::vector<std::shared_ptr<RackSlot>> slots;
    std::vector<RackRow> rows;
    int selected = -1, delta = -1;
    int dragFrom = -1, dropTo = -1;
    double sr = 48000.0;
};

//==============================================================================
class RackPage final : public juce::Component
{
public:
    explicit RackPage (AZMonitoringProcessor&);
    void setData (const ViewData&);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (int insertIndex)> onAddPlugin;
    void selectRackSlot (juce::uint32 id) { selectedId = id; selectedStage = -1; }
    void setView (int v) { view = juce::jlimit (0, 2, v); viewButtons[view].setToggleState (true, juce::dontSendNotification); updateButtons(); }

private:
    void rebuildRows();
    void updateButtons();
    int selectedRow() const;
    const RackRow* currentRow() const;

    AZMonitoringProcessor& proc;
    const ViewData* data = nullptr;
    std::vector<std::shared_ptr<RackSlot>> slots;
    std::vector<RackRow> rows;
    juce::uint32 selectedId = 0;     // rack slot tanlangan bo'lsa
    int selectedStage = -1;          // DAW qatori tanlangan bo'lsa

    juce::Viewport listViewport;
    RackList list;
    juce::TextButton addButton { "+  Plagin qo'shish (VST3 / AU)" };
    juce::TextButton savePreset { "Presetni saqlash" }, loadPreset { "Preset yuklash" };

    juce::TextButton applyButton { "Tavsiyani qo'llash" }, editorButton { "Plagin oynasi" },
                     deltaButton { "Delta tinglash" }, bypassButton { "Bypass" },
                     addAfterButton { "+ Bundan keyin qo'shish" }, removeButton { "Rack'dan olib tashlash" };

    juce::TextButton viewButtons[3], specModeButton;
    int view = 0, specMode = 0;

    std::vector<ParamProposal> proposals, applied;
    juce::uint32 proposalsFor = 0;
    juce::String message;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// Plagin qo'shish oynasi
class BrowserOverlay final : public juce::Component,
                             private juce::ListBoxModel
{
public:
    explicit BrowserOverlay (AZMonitoringProcessor&);
    void open (int insertIndex, const ViewData&);
    void refresh (const ViewData&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (juce::uint32 newSlotId)> onAdded;

private:
    int getNumRows() override { return filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { addRow (row); }
    void addRow (int row);
    void applyFilter();
    juce::Rectangle<int> modalBounds() const;

    AZMonitoringProcessor& proc;
    const ViewData* data = nullptr;
    int insertIndex = -1;
    juce::Array<juce::PluginDescription> all, filtered;
    juce::String category { "Hammasi" };
    juce::String recTitle, recText, recCategory;

    juce::TextEditor search;
    juce::TextButton scanButton { "Qayta skanerlash" }, closeButton { "Yopish" }, addButton { "Qo'shish" };
    juce::OwnedArray<juce::TextButton> catButtons;
    juce::ListBox listBox { "plugins", this };
    juce::String status;
    bool wasScanning = false;
};

} // namespace az::ui
