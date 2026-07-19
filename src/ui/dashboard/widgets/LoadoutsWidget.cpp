#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/profiles/Loadouts.h"
#include "ui/profiles/ProfileBar.h"   // Profiles::IProfileHost
#include <imgui.h>
#include <string>
#include <vector>

namespace
{
    std::string DisplayName(Profiles::IProfileHost& host, int i)
    {
        std::string name = host.NameAt(i);
        if (host.IsGlobalAt(i))
            name += "  [global]";
        return name;
    }
}

// Loadouts quick-switcher: a compact mirror of SEC_LOADOUTS (minus the New/Rename/Delete bar -- that lives in
// Settings). Pick a whole loadout from the top dropdown to apply all four families at once, or set one family's
// active profile directly below. Every switch is DEFERRED (Loadouts::ApplyIndex / SetFamily) -- applying here
// would otherwise SetActive the Dashboard family mid-widget-render and mutate the layout being iterated; the
// queued switch runs at the next frame's Loadouts::ProcessPending(), before any family draws.
void DashW::Loadouts(App& app, float w)
{
    Profiles::IProfileHost& loadoutHost = Loadouts::Host(app);
    const int nL = loadoutHost.Count();

    Gw2Ui::Label("Active loadout", Gw2Ui::kTextSub, false, nullptr, 14.f);
    if (nL <= 0)
    {
        Gw2Ui::Label("No loadouts yet -- create one in Settings -> Loadouts.", Gw2Ui::kTextDim, false, nullptr, 14.f);
    }
    else
    {
        std::vector<std::string> names; names.reserve(nL);
        for (int i = 0; i < nL; ++i) names.push_back(DisplayName(loadoutHost, i));
        std::vector<const char*> cn; cn.reserve(nL);
        for (const std::string& s : names) cn.push_back(s.c_str());
        int sel = loadoutHost.Active();
        if (Gw2Ui::DropdownPx("##ldwidget", cn.data(), nL, &sel, w)) Loadouts::ApplyIndex(app, sel);
    }

    ImGui::Spacing();
    Gw2Ui::Divider(0.f);
    Gw2Ui::Label("Active profile per area", Gw2Ui::kTextSub, false, nullptr, 14.f);
    ImGui::Spacing();

    for (int f = 0; f < Loadouts::FamCount; ++f)
    {
        Profiles::IProfileHost* h = Loadouts::FamilyHost(app, f);
        if (!h) continue;
        Gw2Ui::Label(Loadouts::FamilyName(f), Gw2Ui::kTextDim, false, nullptr, 14.f);
        const int n = h->Count();
        if (n <= 0) { Gw2Ui::Label("   (no profiles)", Gw2Ui::kTextDim, false, nullptr, 14.f); continue; }
        std::vector<std::string> names; names.reserve(n);
        for (int i = 0; i < n; ++i) names.push_back(DisplayName(*h, i));
        std::vector<const char*> cn; cn.reserve(n);
        for (const std::string& s : names) cn.push_back(s.c_str());
        int active = h->Active();
        ImGui::PushID(f);
        if (Gw2Ui::DropdownPx("##fam", cn.data(), n, &active, w)) Loadouts::SetFamily(app, f, active);
        ImGui::PopID();
    }
}
