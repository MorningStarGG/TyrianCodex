#include "Widgets.h"
#include "FarmWidgetUtil.h"              // DashFarm::DrawNodeRow (farming run takeover)
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerPanels.h"      // DrawCurrentObjectivePanel / DrawTravelTargetPanel (the viewer's detail cards)
#include "ui/viewer/ViewerModeBodies.h"  // DrawDungeonDetail
#include "ui/viewer/ViewerModel.h"       // DisplayStepIndex (the SAME current-step pick the viewer uses)
#include "ui/viewer/ViewerLayout.h"      // FarmingRunActive
#include "ui/tabs/MapThumbnail.h"        // ObjectiveTooltip (opt-in per-widget map-in-tooltip)
#include "guide/FarmingGuidance.h"       // NearestNodes
#include <imgui.h>

// The active objective shown as the SAME detail card the viewer uses -- and chosen the SAME way, so the two
// never disagree: a personal/cross-zone/chat-link target shows the travel card (not a blank), and the current
// step uses DisplayStepIndex (handles curStep == -1 in Travel mode / a done curStep). selfFramed: every branch
// draws its own card, so the dashboard doesn't wrap it. No duplicated detail-card logic -- it calls the viewer's.
void DashW::CurrentObjective(App& app, float w)
{
    const GuideState& st = app.state;
    constexpr float fs = 16.f;
    const ImU32 sub = IM_COL32(190, 182, 156, 255);

    // A gathering run takes over: show the nearest enabled node as the current objective.
    if (FarmingRunActive(app))
    {
        const std::vector<const FarmNode*> v = FarmingGuidance::NearestNodes(app, 1);
        if (v.empty())
        {
            Gw2Ui::Label("All nodes gathered for this run.", IM_COL32(150, 235, 150, 255), true, nullptr, 16.f);
            Gw2Ui::Label("They respawn over time.", Gw2Ui::kTextSub, false, nullptr, 14.f);
            if (ImGui::Button("Reset run", ImVec2(120.f, 0.f))) app.state.farmGathered.clear();
        }
        else DashFarm::DrawNodeRow(app, v[0], w, /*big*/ true);
        return;
    }

    // The dashboard ALWAYS renders these elements in compact form (compact=true), independent of the viewer's
    // style setting -- so a Rich viewer (or None) never changes the dashboard's look.
    if (st.inDungeon) { DrawDungeonDetail(app, fs, /*compact*/ true); return; }

    // Personal target / "Travel here" / waypoint chat-link target -> the viewer's travel card (was vanishing).
    if (app.travel.Active()) { DrawTravelTargetPanel(app, fs, sub, /*compact*/ true); return; }

    const int total = (int)st.zone.Steps.size();
    const int disp  = DisplayStepIndex(app, total);
    if (disp < 0 || disp >= total)
    {
        Gw2Ui::Label(st.zone.Loaded ? "All objectives complete here." : "No active objective.",
                     Gw2Ui::kTextDim, false, nullptr, 16.f);
        return;
    }
    const Step& s = st.zone.Steps[disp];
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    DrawCurrentObjectivePanel(app, s, fs, sub, /*compact*/ true);
    // Opt-in (default off): hovering the card shows the zone map with this objective circled.
    if (app.config.wMapTipCurrent && ImGui::IsMouseHoveringRect(p0, ImVec2(p0.x + w, ImGui::GetCursorScreenPos().y)))
        ObjectiveTooltip(s.Name.c_str(), s.Type.c_str(), &s, &st.zone, s.CX, s.CY, /*withMap*/ true);
}
