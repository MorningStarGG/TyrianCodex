#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "app/SessionTracker.h"
#include "app/StaticData.h"
#include "ui/Gw2Ui.h"
#include "ui/SessionFormat.h"
#include <imgui.h>
#include <map>

// Live current-session EARNINGS (wallet/currency deltas) -- the money sibling of SessionStats. The shared
// SessionTracker baselines the wallet at login and accrues deltas as it repolls (gentle 300s TTL); this widget
// just renders them. Gold is always shown; other currencies appear once they change. Reset re-baselines now.
void DashW::SessionEarnings(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Wallet, "wallet")) return;
    const ::SessionEarnings& se = app.state.sessionEarnings;   // ::-qualified: the fn name DashW::SessionEarnings shadows the struct
    if (!se.active)   { Gw2Ui::Label("Waiting for character.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }
    if (!se.haveBase) { Gw2Ui::Label("Baselining wallet...",  Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    Gw2Ui::TextValueRow("Session time", SessionFmt::Clock(SessionTracker::DurationSec(app)).c_str(), w, 18.f, 14.f);

    const std::map<int, long long> deltas = SessionTracker::CurrentDeltas(app);

    // Gold always (even at +0c), then every other currency that moved this session, in GW2 display order.
    auto g = deltas.find(1);
    Gw2Ui::TextValueRow("Gold", SessionFmt::Signed(1, g != deltas.end() ? g->second : 0).c_str(), w, 18.f, 14.f);

    int extra = 0;
    for (const StaticData::CurrencyRef& c : StaticData::Currencies())
    {
        if (c.id == 1) continue;
        auto it = deltas.find(c.id);
        if (it == deltas.end()) continue;   // unchanged this session
        Gw2Ui::TextValueRow(SessionFmt::Label(c.id), SessionFmt::Signed(c.id, it->second).c_str(), w, 18.f, 14.f);
        if (++extra >= 8) break;            // keep the widget compact; the Sessions tab shows everything
    }
    if (deltas.empty())
        Gw2Ui::Label("No earnings yet this session.", Gw2Ui::kTextDim, false, nullptr, 14.f);

    ImGui::Spacing();
    if (Gw2Ui::ActionButton("Reset", w, 22.f, Gw2Ui::ActionButtonVariant::Normal, "Re-baseline now (zero the session deltas)"))
        SessionTracker::Reset(app);
    DashApi::Freshness(AccountData::Get().walletAt);
}
