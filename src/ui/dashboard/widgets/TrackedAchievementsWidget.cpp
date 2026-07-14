#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <string>

// Account achievement summary: completed + in-progress counts (/v2/account/achievements).
void DashW::TrackedAchievements(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Progression, "progression")) return;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveAch) { Gw2Ui::Label("Loading achievements...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    auto row = [&](const char* k, const std::string& v) {
        Gw2Ui::TextValueRow(k, v.c_str(), w, 18.f, 14.f);
    };
    auto comma = [](long long n) { std::string s = std::to_string(n); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, ","); return s; };
    row("Points", m.haveTotalAp ? comma(m.totalAp) : std::string("..."));
    row("Completed", std::to_string(m.achDone));
    row("In progress", std::to_string(m.achInProgress));
    DashApi::Freshness(m.achAt);
}
