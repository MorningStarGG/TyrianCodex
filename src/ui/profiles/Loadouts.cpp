#include "Loadouts.h"
#include "ProfileBar.h"                  // Profiles::IProfileHost
#include "app/App.h"
#include "ui/profiles/ConfigProfiles.h"  // the 4 families' hosts (ONE universal mechanism)
#include "ui/Gw2Ui.h"                    // DrawProfileBar deps + Dropdown/Label/Divider for the per-family pickers
#include "ui/tabs/SettingsCommon.h"      // SettingsParagraph + SettingsText
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    // A loadout: one chosen profile NAME per family (empty = "leave that family alone" on apply).
    struct LoadoutData { std::string fam[Loadouts::FamCount]; };

    Profiles::IProfileHost* FamHost(App& app, int fam)
    {
        using O = ConfigProfiles::Owner;
        switch (fam)
        {
            case Loadouts::FamGeneral:   return &ConfigProfiles::Host(app, O::General);
            case Loadouts::FamDashboard: return &ConfigProfiles::Host(app, O::Dash);
            case Loadouts::FamInfoPanel: return &ConfigProfiles::Host(app, O::Info);
            case Loadouts::FamHud:       return &ConfigProfiles::Host(app, O::Hud);
            default:                     return nullptr;
        }
    }

    // Bespoke per-character collection of loadouts. SetActive APPLIES the bundle; New SNAPSHOTS the families'
    // current selections. No capture-on-bind (the families own their live state), so binding never corrupts.
    class LoadoutHost : public Profiles::IProfileHost
    {
    public:
        App* app = nullptr;
        struct Named   { std::string name; LoadoutData data; };
        struct CharSet { std::vector<Named> items; int active = 0; };
        std::map<std::string, CharSet> byChar;
        std::string curKey;

        void Bind(App& a) { app = &a; const std::string& k = a.state.currentChar; curKey = k.empty() ? std::string("default") : k; }

        const CharSet* CurC() const { auto it = byChar.find(curKey); return it == byChar.end() ? nullptr : &it->second; }
        CharSet&       Cur()        { return byChar[curKey]; }

        LoadoutData Snapshot()
        {
            LoadoutData d;
            for (int f = 0; f < Loadouts::FamCount; ++f)
                if (Profiles::IProfileHost* h = FamHost(*app, f)) { const int a = h->Active(); if (a >= 0 && a < h->Count()) d.fam[f] = h->NameAt(a); }
            return d;
        }
        void ApplyData(const LoadoutData& d)
        {
            for (int f = 0; f < Loadouts::FamCount; ++f)
            {
                if (d.fam[f].empty()) continue;
                Profiles::IProfileHost* h = FamHost(*app, f);
                if (!h) continue;
                for (int i = 0; i < h->Count(); ++i)
                    if (h->NameAt(i) == d.fam[f]) { if (i != h->Active()) h->SetActive(i); break; }
            }
        }
        // Capture the families' CURRENT selections into the active loadout (so changes made while a loadout is
        // active are remembered) -- called before switching away + on save, NOT on char-bind (the families own
        // their per-char state, so binding must not overwrite the loadout with a different char's selections).
        void SyncToActive()
        {
            if (!app) return;
            CharSet& cs = Cur();
            if (cs.active >= 0 && cs.active < (int)cs.items.size()) cs.items[cs.active].data = Snapshot();
        }

        // ---- IProfileHost ----
        int Count() const override  { const CharSet* cs = CurC(); return cs ? (int)cs->items.size() : 0; }
        int Active() const override { const CharSet* cs = CurC(); return cs ? cs->active : 0; }
        std::string NameAt(int i) const override { const CharSet* cs = CurC(); return (cs && i >= 0 && i < (int)cs->items.size()) ? cs->items[i].name : std::string(); }
        void SetActive(int i) override            // = apply this loadout
        {
            CharSet& cs = Cur();
            if (i < 0 || i >= (int)cs.items.size()) return;
            SyncToActive();                       // remember the current selections in the OUTGOING loadout first
            cs.active = i;
            ApplyData(cs.items[i].data);
        }
        void New(const std::string& name) override   // snapshot the current family selections
        {
            CharSet& cs = Cur();
            Named n; n.name = Unique(name, -1); n.data = Snapshot();
            cs.items.push_back(std::move(n));
            cs.active = (int)cs.items.size() - 1;     // already matches live -> no apply
        }
        void Rename(int i, const std::string& name) override { CharSet& cs = Cur(); if (i >= 0 && i < (int)cs.items.size() && !name.empty()) cs.items[i].name = Unique(name, i); }
        void Duplicate(int i, const std::string& name) override
        {
            CharSet& cs = Cur();
            if (i < 0 || i >= (int)cs.items.size()) return;
            Named n = cs.items[i]; n.name = Unique(name, -1);
            cs.items.push_back(std::move(n)); cs.active = (int)cs.items.size() - 1;   // same selection -> no apply needed
        }
        void Delete(int i) override
        {
            CharSet& cs = Cur();
            if (i < 0 || i >= (int)cs.items.size()) return;
            cs.items.erase(cs.items.begin() + i);
            if (cs.active >= (int)cs.items.size()) cs.active = (int)cs.items.size() - 1;
            if (cs.active < 0) cs.active = 0;
        }
        std::string Suggest(const std::string& base) const override { return UniqueC(curKey, base, -1); }
        std::string CurrentChar() const override { return curKey; }

        std::vector<std::string> CharsWithProfiles() const override
        {
            std::vector<std::string> out;
            for (const auto& kv : byChar)
                if (kv.first != curKey && kv.first != "default" && !kv.second.items.empty()) out.push_back(kv.first);
            std::sort(out.begin(), out.end());
            return out;
        }
        std::vector<std::string> ProfileNamesOf(const std::string& ch) const override
        {
            std::vector<std::string> out; auto it = byChar.find(ch);
            if (it != byChar.end()) for (const Named& n : it->second.items) out.push_back(n.name);
            return out;
        }
        void CopyFrom(const std::string& srcChar, int srcIdx) override
        {
            auto it = byChar.find(srcChar);
            if (it == byChar.end() || srcIdx < 0 || srcIdx >= (int)it->second.items.size()) return;
            CharSet& cs = Cur();
            Named n = it->second.items[srcIdx]; n.name = Unique(n.name, -1);
            cs.items.push_back(std::move(n)); cs.active = (int)cs.items.size() - 1;
        }
        void CopyAllFrom(const std::string& srcChar) override
        {
            auto it = byChar.find(srcChar);
            if (it == byChar.end() || it->second.items.empty()) return;
            CharSet& cs = Cur();
            for (const Named& s : it->second.items) { Named n = s; n.name = Unique(n.name, -1); cs.items.push_back(std::move(n)); }
            cs.active = (int)cs.items.size() - 1;
        }

    private:
        std::string UniqueC(const std::string& key, const std::string& base, int ignore) const
        {
            auto it = byChar.find(key);
            std::string root = base.empty() ? "Loadout" : base;
            auto taken = [&](const std::string& n) {
                if (it == byChar.end()) return false;
                for (int i = 0; i < (int)it->second.items.size(); ++i) if (i != ignore && it->second.items[i].name == n) return true;
                return false;
            };
            if (!taken(root)) return root;
            for (int n = 2; ; ++n) { std::string c = root + " " + std::to_string(n); if (!taken(c)) return c; }
        }
        std::string Unique(const std::string& base, int ignore) { return UniqueC(curKey, base, ignore); }
    };

    LoadoutHost g_host;

    // Deferred switch (applied at frame start by ProcessPending, never mid-render).
    enum class Pend { None, Loadout, Family };
    Pend g_pendKind = Pend::None;
    int  g_pendA = -1, g_pendB = -1;
}

Profiles::IProfileHost& Loadouts::Host(App& app) { g_host.Bind(app); return g_host; }
int  Loadouts::Count(App& app)  { g_host.Bind(app); return g_host.Count(); }
int  Loadouts::Active(App& app) { g_host.Bind(app); return g_host.Active(); }
std::string Loadouts::NameAt(App& app, int i) { g_host.Bind(app); return g_host.NameAt(i); }
void Loadouts::ApplyIndex(App& app, int i)              { (void)app; g_pendKind = Pend::Loadout; g_pendA = i; }
void Loadouts::SetFamily(App& app, int fam, int prof)   { (void)app; g_pendKind = Pend::Family; g_pendA = fam; g_pendB = prof; }
void Loadouts::CycleNext(App& app)
{
    g_host.Bind(app);
    const int n = g_host.Count();
    if (n <= 0) return;
    g_pendKind = Pend::Loadout; g_pendA = (g_host.Active() + 1) % n;   // deferred
}
void Loadouts::ProcessPending(App& app)
{
    if (g_pendKind == Pend::None) return;
    g_host.Bind(app);
    if (g_pendKind == Pend::Loadout) g_host.SetActive(g_pendA);
    else if (g_pendKind == Pend::Family) { if (Profiles::IProfileHost* h = FamHost(app, g_pendA)) h->SetActive(g_pendB); g_host.SyncToActive(); }   // remember the area change in the active loadout
    g_pendKind = Pend::None;
    app.settingsDirty = true;
}

const char* Loadouts::FamilyName(int fam)
{
    switch (fam) { case FamGeneral: return "General"; case FamDashboard: return "Dashboard"; case FamInfoPanel: return "Info Panel"; case FamHud: return "HUD"; default: return "?"; }
}
Profiles::IProfileHost* Loadouts::FamilyHost(App& app, int fam) { return FamHost(app, fam); }

void Loadouts::DrawSettings(App& app)
{
    Profiles::DrawProfileBar(app, Host(app), "loadoutprof",
        "A loadout bundles which profile is active in each area below (General / Dashboard / Info Panel / HUD). "
        "Switching a loadout applies all four at once; 'New' snapshots your current selections. Saved per "
        "character; import one from another character above.");

    Gw2Ui::Label("Active profile per area", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("loadout-areas");
    SettingsParagraph("Set each area's active profile directly, or build the combination you want and create a "
                      "loadout above to remember it.", IM_COL32(168, 158, 136, 255));
    ImGui::Spacing();

    for (int f = 0; f < FamCount; ++f)
    {
        Profiles::IProfileHost* h = FamilyHost(app, f);
        if (!h) continue;
        Gw2Ui::Label(FamilyName(f), Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
        const int n = h->Count();
        if (n <= 0) { Gw2Ui::Label("   (no profiles)", Gw2Ui::kTextDim, false, nullptr, 14.f); continue; }
        std::vector<std::string> names; names.reserve(n);
        for (int i = 0; i < n; ++i) names.push_back(h->NameAt(i));
        std::vector<const char*> cnames; cnames.reserve(n);
        for (const std::string& s : names) cnames.push_back(s.c_str());
        int active = h->Active();
        ImGui::PushID(f);
        if (Gw2Ui::Dropdown("##fam", cnames.data(), n, &active, 240.f)) Loadouts::SetFamily(app, f, active);
        ImGui::PopID();
    }
    Gw2Ui::EndCard();
}

void Loadouts::Serialize(App& app, nlohmann::json& j)
{
    g_host.Bind(app);
    g_host.SyncToActive();   // fold the current family selections into the active loadout before writing
    static const char* kFamKey[FamCount] = { "general", "dashboard", "infopanel", "hud" };
    nlohmann::json root = nlohmann::json::object();
    for (const auto& kv : g_host.byChar)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const LoadoutHost::Named& n : kv.second.items)
        {
            nlohmann::json o = { {"name", n.name} };
            for (int f = 0; f < FamCount; ++f) o[kFamKey[f]] = n.data.fam[f];
            arr.push_back(std::move(o));
        }
        root[kv.first] = { {"active", kv.second.active}, {"loadouts", arr} };
    }
    j["loadoutsByChar"] = root;
}

void Loadouts::Deserialize(App& app, const nlohmann::json& j)
{
    static const char* kFamKey[FamCount] = { "general", "dashboard", "infopanel", "hud" };
    g_host.byChar.clear();
    auto it = j.find("loadoutsByChar");
    if (it != j.end() && it->is_object())
        for (auto el = it->begin(); el != it->end(); ++el)
        {
            const nlohmann::json& o = el.value();
            if (!o.is_object()) continue;
            LoadoutHost::CharSet cs; cs.active = o.value("active", 0);
            if (o.contains("loadouts") && o["loadouts"].is_array())
                for (const auto& e : o["loadouts"])
                {
                    LoadoutHost::Named n; n.name = e.value("name", std::string("Loadout"));
                    for (int f = 0; f < FamCount; ++f) n.data.fam[f] = e.value(kFamKey[f], std::string());
                    cs.items.push_back(std::move(n));
                }
            if (cs.active < 0) cs.active = 0;
            if (cs.active >= (int)cs.items.size()) cs.active = cs.items.empty() ? 0 : (int)cs.items.size() - 1;
            g_host.byChar[el.key()] = std::move(cs);
        }
    g_host.Bind(app);   // bind; loadouts do NOT auto-apply (families restore their own active on load)
}

void Loadouts::RenameChar(App& app, const std::string& from, const std::string& to)
{
    (void)app;
    if (from.empty() || to.empty() || from == to) return;
    auto itF = g_host.byChar.find(from);
    if (itF == g_host.byChar.end()) return;
    g_host.byChar[to] = std::move(itF->second);   // REPLACE: the renamed character's loadouts win
    g_host.byChar.erase(itF);
    if (g_host.curKey == from) g_host.curKey = to;
}

void Loadouts::PurgeChar(App& app, const std::string& name)
{
    (void)app;
    if (name == g_host.curKey) return;   // never purge the active character's loadouts
    g_host.byChar.erase(name);
}

void Loadouts::CollectCharNames(App& app, std::set<std::string>& out)
{
    (void)app;
    for (const auto& kv : g_host.byChar)
        if (kv.first != "default") out.insert(kv.first);
}
