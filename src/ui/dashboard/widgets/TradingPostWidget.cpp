#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>
#include <string>

// Trading Post: delivery box (gold + items to collect) + active order counts (/v2/commerce/*).
void DashW::TradingPost(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Tradingpost, "tradingpost")) return;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveTp) { Gw2Ui::Label("Loading trading post...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    auto row = [&](const char* k, const std::string& v) {
        Gw2Ui::TextValueRow(k, v.c_str(), w, 18.f, 14.f);
    };
    Gw2Ui::Label("Pick up", Gw2Ui::Alpha(Gw2Ui::kGold, 245), false, nullptr, 14.f, 1.0f);
    row("Coins", DashApi::Gold(m.tpCoins));
    row("Items", std::to_string(m.tpItems));
    ImGui::Spacing();
    Gw2Ui::Label("Active orders", Gw2Ui::Alpha(Gw2Ui::kGold, 245), false, nullptr, 14.f, 1.0f);
    row("Buy orders", std::to_string(m.tpBuys));
    row("Sell orders", std::to_string(m.tpSells));
    DashApi::Freshness(m.tpAt);
}
