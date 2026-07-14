#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerLayout.h"          // FarmingRunActive
#include "ui/viewer/ViewerFarming.h"         // DrawFarmingTab (the shared Farming-tab panel: chips + node list)
#include "ui/viewer/ViewerObjectiveList.h"   // DrawObjectiveListPanel (the shared objective-list panel)
#include <imgui.h>

// The dashboard "Objectives" widget: the full, scrollable objective list -- the SAME shared panel the compact
// viewer uses (search + type filters + Show-completed toggle + tick / right-click to complete / mark-incomplete),
// so the widget and the viewer render identically and never drift (incl. recommending the next zone when the
// current one is finished). A gathering run or a dungeon takes over with its own list, mirroring the viewer.
void DashW::ObjectiveList(App& app, float w)
{
    const GuideState& st = app.state;
    const int wantN = 5;   // farming / dungeon takeover: how many upcoming rows to list (the objective list itself is full + scrollable)

    // A gathering run takes over: show the full Farming tab -- the category + material chip rows + the nearest-first
    // node list -- the SAME shared panel (DrawFarmingTab) the viewer's Farming tab uses, so the chips/filters are
    // available here too. Neutralize the dashboard text-scale + bound it in a scroll child (like the objective list)
    // so it renders at the viewer's size, not blown up.
    if (FarmingRunActive(app))
    {
        Gw2Ui::PushTextScale(1.0f);
        ImGui::BeginChild("##farmpanel", ImVec2(0.f, 340.f), false);
        DrawFarmingTab(app);
        ImGui::EndChild();
        Gw2Ui::PopTextScale();
        return;
    }

    if (st.inDungeon)
    {
        int shown = 0;
        for (int idx = st.instStep + 1; idx < (int)st.instSteps.size() && shown < wantN; ++idx)
        {
            if (!st.instSteps[idx]) continue;
            const char* txt = st.instSteps[idx]->Text.c_str();
            const float h = Gw2Ui::MeasureWrappedHeight(txt, 16.f, w);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(w, h));
            Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + h), txt,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(232, 226, 206, 255), false, nullptr, 16.f, w);
            ++shown;
            if (shown < wantN) ImGui::Dummy(ImVec2(w, 5.f));
        }
        if (shown == 0) Gw2Ui::Label("End of route.", Gw2Ui::kTextDim, false, nullptr, 16.f);
        return;
    }

    // Open world: the full, scrollable objective list (recommends the next zone when finished, like the viewer).
    DrawObjectiveListPanel(app, 340.f);
}
