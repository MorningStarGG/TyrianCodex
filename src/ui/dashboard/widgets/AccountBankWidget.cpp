#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>
#include <string>

// Shared bank + material storage fullness (/v2/account/bank + /v2/account/materials).
void DashW::AccountBank(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Inventories, "inventories")) return;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveBank && !m.haveMats) { Gw2Ui::Label("Loading bank...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    auto row = [&](const char* k, const std::string& v) {
        Gw2Ui::TextValueRow(k, v.c_str(), w, 18.f, 14.f);
    };
    if (m.haveBank)
    {
        char b[32]; std::snprintf(b, sizeof(b), "%d/%d slots", m.bankUsed, m.bankTotal);
        row("Bank", b);
        row("Items stored", std::to_string(m.bankItems));
    }
    if (m.haveMats)
    {
        row("Material types", std::to_string(m.matTypes));
        row("Materials total", std::to_string(m.matTotal));
    }
    DashApi::Freshness(m.bankAt > 0 ? m.bankAt : m.matsAt);
}
