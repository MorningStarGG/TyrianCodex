#include "Widgets.h"
#include "WidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "guide/CurrentChar.h"
#include "guide/ZoneCompletion.h"   // Guide::ZoneCompletion (shared count logic; also used by the Info Panel)
#include <imgui.h>
#include <string>

// Per-type completion bars for the active guided zone (waypoints/POIs/vistas/hero points/hearts).
void DashW::ZoneProgress(App& app, float w)
{
    const Zone& z = app.state.zone;
    if (!z.Loaded || z.Steps.empty())
    {
        Gw2Ui::Label("Not in a guided zone.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        return;
    }

    const std::string ch = app.state.currentChar;
    const Guide::ZoneCounts cc = Guide::ZoneCompletion(app.progress, ch, z);   // shared count (completed + confirmed)

    Gw2Ui::Label(z.Name.c_str(), Gw2Ui::Alpha(Gw2Ui::kGold, 245), false, nullptr, 16.f, 1.1f);
    ImGui::Spacing();

    const ImU32 fills[5] = { IM_COL32(130, 230, 150, 255), IM_COL32(190, 220, 255, 255), IM_COL32(140, 205, 255, 255),
                             IM_COL32(190, 150, 255, 255), IM_COL32(255, 207, 110, 255) };
    static const char* kTypeByIdx[5] = { "waypoint", "poi", "vista", "hero", "heart" };
    for (int k = 0; k < 5; ++k)
    {
        if (cc.kindTotal[k] == 0) continue;
        char lbl[48];
        std::snprintf(lbl, sizeof(lbl), "%s  %d/%d", DashUtil::TypeLabel(k), cc.kindDone[k], cc.kindTotal[k]);
        uint32_t icon = 0; Objective::TryIconAssetId(kTypeByIdx[k], icon);   // type icon beside the name
        Gw2Ui::ProgressBar((float)cc.kindDone[k] / (float)cc.kindTotal[k], lbl, w, 14.f, fills[k], icon);
        ImGui::Spacing();
    }
    if (cc.total > 0)
    {
        char tl[48]; std::snprintf(tl, sizeof(tl), "Zone total  %d/%d", cc.done, cc.total);
        Gw2Ui::ProgressBar((float)cc.done / (float)cc.total, tl, w, 16.f, IM_COL32(215, 168, 62, 255));
    }
}
