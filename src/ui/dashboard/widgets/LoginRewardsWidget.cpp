#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <ctime>
#include <cstdio>
#include <string>

// Account glance + daily login: account name, total playtime, and the daily reset countdown (when the next
// login reward becomes available). /v2/account + daily-reset clock math.
void DashW::LoginRewards(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Account, "account")) return;
    const AccountData::Model& m = AccountData::Get();

    auto row = [&](const char* k, const std::string& v) {
        Gw2Ui::TextValueRow(k, v.c_str(), w, 18.f, 14.f);
    };

    if (m.haveAccount && !m.accountName.empty())
        Gw2Ui::Label(m.accountName.c_str(), IM_COL32(255, 244, 207, 255), false, nullptr, 16.f, 1.1f);
    if (m.haveAccount && m.age > 0)
    {
        const long h = m.age / 3600;
        char b[32]; std::snprintf(b, sizeof(b), "%ldd %ldh", h / 24, h % 24);
        row("Playtime", b);
    }

    // daily login reset (00:00 UTC)
    const std::time_t now = std::time(nullptr);
    std::tm g{};
#if defined(_WIN32)
    gmtime_s(&g, &now);
#else
    g = *std::gmtime(&now);
#endif
    const long toDaily = 86400L - (g.tm_hour * 3600L + g.tm_min * 60 + g.tm_sec);
    char d[16]; std::snprintf(d, sizeof(d), "%ld:%02ld:%02ld", toDaily / 3600, (toDaily % 3600) / 60, toDaily % 60);
    row("Next daily login", d);
    DashApi::Freshness(m.accountAt);
}
