#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "app/StaticData.h"
#include "ui/Gw2Ui.h"
#include "ui/SessionFormat.h"
#include <imgui.h>
#include <map>
#include <string>

// All owned currencies at a glance, named + ordered from the /v2/currencies catalog (StaticData::WarmCurrencies).
// (Was a hardcoded handful of 5; now the whole wallet.) /v2/account/wallet.
namespace
{
    void Row(int id, long long v, float w)
    {
        Gw2Ui::TextValueRow(SessionFmt::Label(id), id == 1 ? DashApi::Gold(v).c_str() : std::to_string(v).c_str(),
                            w, 18.f, 14.f);
    }
}

void DashW::Wallet(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Wallet, "wallet")) return;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveWallet) { Gw2Ui::Label("Loading wallet...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    std::map<int, long long> have;
    for (const AccountData::WalletEntry& e : m.wallet) if (e.value > 0) have[e.id] = e.value;

    int shown = 0;
    const std::vector<StaticData::CurrencyRef>& cat = StaticData::Currencies();
    if (!cat.empty())
    {
        for (const StaticData::CurrencyRef& c : cat)   // GW2 display order
        {
            auto it = have.find(c.id);
            if (it == have.end()) continue;
            Row(c.id, it->second, w);
            ++shown;
        }
    }
    else   // catalog still warming -> don't render blank; fall back to wallet order (gold labeled, others by id name)
    {
        for (const auto& kv : have) { Row(kv.first, kv.second, w); ++shown; }
    }

    if (shown == 0) Gw2Ui::Label("No currencies.", Gw2Ui::kTextDim, false, nullptr, 14.f);
    DashApi::Freshness(m.walletAt);
}
