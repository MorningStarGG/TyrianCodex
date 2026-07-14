#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>

// Wizard's Vault daily + weekly objectives (replaces the removed /v2/achievements/daily system).
// Reads the shared AccountData (wvDaily/wvWeekly) -- /v2/account/wizardsvault/{daily,weekly}, account scope.
void DashW::TodaysDailies(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Account, "wizard's vault")) return;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveWvDaily && !m.haveWvWeekly) { Gw2Ui::Label("Loading Wizard's Vault...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }
    if (m.wvUnavailable) { DashApi::Note("Wizard's Vault not unlocked yet (requires a level-80 character)."); return; }

    auto section = [&](const char* title, const std::vector<AccountData::WvObjective>& objs, bool have)
    {
        Gw2Ui::Label(title, Gw2Ui::kGold, false, nullptr, 14.f, 1.0f);
        if (!have) { Gw2Ui::Label("Loading...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }
        if (objs.empty()) { Gw2Ui::Label("No objectives.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }
        for (const AccountData::WvObjective& o : objs)
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = std::max(18.f, Gw2Ui::MeasureWrappedHeight(o.title.c_str(), 14.f, w - 58.f));
            ImGui::Dummy(ImVec2(w, h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const Gw2Ui::RowHotspot row{ p, w, h, false, false };
            const bool done = o.max > 0 && o.cur >= o.max;
            dl->AddCircleFilled(ImVec2(p.x + 4.f, p.y + 9.f), 2.f, done ? IM_COL32(150, 210, 140, 255) : IM_COL32(240, 150, 70, 255));
            Gw2Ui::RowLabel(dl, row, 12.f, 46.f, o.title.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                            done ? IM_COL32(160, 180, 150, 255) : IM_COL32(220, 214, 196, 255), false, nullptr, 14.f, w - 58.f);
            char prog[16]; std::snprintf(prog, sizeof(prog), "%d/%d", o.cur, o.max);
            Gw2Ui::RowLabel(dl, row, 0.f, 0.f, prog, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
        }
    };
    section("Daily", m.wvDaily, m.haveWvDaily);
    ImGui::Dummy(ImVec2(0.f, 4.f));
    section("Weekly", m.wvWeekly, m.haveWvWeekly);
    DashApi::Freshness(m.wvDailyAt);
}
