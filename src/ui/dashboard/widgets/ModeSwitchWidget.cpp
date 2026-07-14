#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerLayout.h"   // FarmingTabAvailable / FarmingMapHasData
#include <imgui.h>

// A narrow [ Objectives | Farming ] toggle, styled like the viewer's tab switch bar. It flips the ONE shared
// State.farmRunActive flag, so it stays in sync with the viewer's Farming chip + the tray, and switching it
// makes Current Objective / Upcoming / Route Arrow all follow. Side-by-side at full width; stacked in half-width.
// The Farming option is disabled (with a reason tooltip) on maps that have no gathering data.
void DashW::ModeSwitch(App& app, float w)
{
    GuideState& st = app.state;
    const float sc = Gw2Ui::TextScale();
    const bool farmAvail = FarmingTabAvailable(app);
    const bool farming   = farmAvail && st.farmRunActive;
    const float h = 28.f * sc, fs = 16.f, gap = 5.f * sc;
    const bool stack = w < 220.f * sc;             // half-width -> stack the two chips vertically (maxPerRow=1)

    // Farming is inert + a reason tooltip on maps with no gathering data (or while travelling / in a dungeon).
    Gw2Ui::ChipItem k[2] = {
        { "Objectives" },
        { "Farming", farmAvail ? nullptr : (FarmingMapHasData(app) ? "Not while travelling or in a dungeon"
                                                                   : "No gathering data on this map"),
          {}, 0.f, !farmAvail },
    };
    int sel = farming ? 1 : 0;
    if (Gw2Ui::ChipRow("modeswitch", k, 2, &sel, h, fs, gap, 100.f, /*wrap*/true, 0, stack ? 1 : 2))
    {
        st.farmRunActive = (sel == 1);   // the disabled Farming chip won't fire, so sel only flips on a valid pick
        app.settingsDirty = true;
    }
}
