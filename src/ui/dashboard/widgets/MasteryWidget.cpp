#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <string>

// Mastery points + unlocked tracks (/v2/account/mastery/points + /v2/account/masteries).
void DashW::Mastery(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Progression, "progression")) return;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveMastery) { Gw2Ui::Label("Loading masteries...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    auto row = [&](const char* k, const std::string& v) {
        Gw2Ui::TextValueRow(k, v.c_str(), w, 18.f, 14.f);
    };
    char pts[32]; std::snprintf(pts, sizeof(pts), "%d / %d", m.mpSpent, m.mpEarned);
    row("Points (spent/earned)", pts);
    row("Tracks unlocked", std::to_string(m.masteriesUnlocked));
    DashApi::Freshness(m.masteryAt);
}
