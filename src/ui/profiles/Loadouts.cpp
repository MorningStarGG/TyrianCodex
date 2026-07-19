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
    // A loadout: one chosen profile reference per family (empty name = "leave that family alone" on apply).
    struct LoadoutData { Profiles::ProfileRef fam[Loadouts::FamCount]; };
    static constexpr const char* kGlobalKey = "__global__";
    static constexpr const char* kPreLoginKey = "default";

    const char* ScopeName(Profiles::Scope scope)
    {
        return scope == Profiles::Scope::Global ? "global" : "character";
    }

    Profiles::Scope ScopeFromName(const std::string& s)
    {
        return s == "global" ? Profiles::Scope::Global : Profiles::Scope::Character;
    }

    nlohmann::json RefToJson(const Profiles::ProfileRef& ref)
    {
        if (ref.name.empty())
            return "";
        return nlohmann::json{ {"scope", ScopeName(ref.scope)}, {"name", ref.name} };
    }

    Profiles::ProfileRef RefFromJson(const nlohmann::json& j)
    {
        if (j.is_string())
            return Profiles::ProfileRef{ Profiles::Scope::Character, j.get<std::string>() };
        if (j.is_object())
            return Profiles::ProfileRef{ ScopeFromName(j.value("scope", std::string("character"))),
                                         j.value("name", std::string()) };
        return {};
    }

    std::string DisplayHostName(Profiles::IProfileHost& host, int i)
    {
        std::string name = host.NameAt(i);
        if (host.IsGlobalAt(i))
            name += "  [global]";
        return name;
    }

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
        struct CharSet
        {
            std::vector<Named> items;
            int active = 0;
            bool activeGlobal = false;
            int activeGlobalIndex = 0;
        };
        std::map<std::string, CharSet> byChar;
        std::string curKey;

        void Bind(App& a) { app = &a; const std::string& k = a.state.currentChar; curKey = k.empty() ? std::string("default") : k; }

        const CharSet* CurC() const { auto it = byChar.find(curKey); return it == byChar.end() ? nullptr : &it->second; }
        CharSet&       Cur()        { return byChar[curKey]; }

        LoadoutData Snapshot()
        {
            LoadoutData d;
            for (int f = 0; f < Loadouts::FamCount; ++f)
                if (Profiles::IProfileHost* h = FamHost(*app, f)) { const int a = h->Active(); if (a >= 0 && a < h->Count()) d.fam[f] = h->RefAt(a); }
            return d;
        }
        void ApplyData(const LoadoutData& d)
        {
            for (int f = 0; f < Loadouts::FamCount; ++f)
            {
                if (d.fam[f].name.empty()) continue;
                Profiles::IProfileHost* h = FamHost(*app, f);
                if (!h) continue;
                const int idx = h->FindRef(d.fam[f]);
                if (idx >= 0 && idx != h->Active())
                    h->SetActive(idx);
            }
        }
        // Capture the families' CURRENT selections into the active loadout (so changes made while a loadout is
        // active are remembered) -- called before switching away + on save, NOT on char-bind (the families own
        // their per-char state, so binding must not overwrite the loadout with a different char's selections).
        void SyncToActive()
        {
            if (!app) return;
            CharSet& cs = Cur();
            if (cs.activeGlobal)
            {
                CharSet& g = Global();
                if (cs.activeGlobalIndex >= 0 && cs.activeGlobalIndex < (int)g.items.size())
                    g.items[cs.activeGlobalIndex].data = SharedFamilyRefs(Snapshot());
            }
            else if (cs.active >= 0 && cs.active < (int)cs.items.size())
            {
                cs.items[cs.active].data = Snapshot();
            }
        }

        // ---- IProfileHost ----
        int Count() const override  { return LocalCount(CurC()) + GlobalCount(); }
        int Active() const override
        {
            const CharSet* cs = CurC();
            if (!cs) return 0;
            const int localN = LocalCount(cs);
            const int globalN = GlobalCount();
            if (cs->activeGlobal && globalN > 0)
                return localN + std::min(std::max(cs->activeGlobalIndex, 0), globalN - 1);
            return std::min(std::max(cs->active, 0), std::max(0, localN - 1));
        }
        std::string NameAt(int i) const override
        {
            bool global = false;
            const Named* n = Visible(i, &global);
            (void)global;
            return n ? n->name : std::string();
        }
        void SetActive(int i) override            // = apply this loadout
        {
            CharSet& cs = Cur();
            const int localN = (int)cs.items.size();
            const int globalN = GlobalCount();
            if (i < 0 || i >= localN + globalN) return;
            SyncToActive();                       // remember the current selections in the OUTGOING loadout first
            if (i < localN)
            {
                cs.activeGlobal = false;
                cs.active = i;
                ApplyData(cs.items[i].data);
            }
            else
            {
                cs.activeGlobal = true;
                cs.activeGlobalIndex = i - localN;
                ApplyData(Global().items[cs.activeGlobalIndex].data);
            }
        }
        void New(const std::string& name) override   // snapshot the current family selections
        {
            CharSet& cs = Cur();
            Named n; n.name = Unique(name, -1); n.data = Snapshot();
            cs.items.push_back(std::move(n));
            cs.activeGlobal = false;
            cs.active = (int)cs.items.size() - 1;     // already matches live -> no apply
        }
        void Rename(int i, const std::string& name) override
        {
            if (name.empty()) return;
            bool global = false;
            Named* n = VisibleMutable(i, &global);
            if (!n) return;
            n->name = global ? UniqueGlobal(name, GlobalIndexFromVisible(i)) : Unique(name, i);
        }
        void Duplicate(int i, const std::string& name) override
        {
            CharSet& cs = Cur();
            bool global = false;
            const Named* src = Visible(i, &global);
            if (!src) return;
            if (i == Active()) SyncToActive();
            Named n = *src;
            if (global)
            {
                CharSet& g = Global();
                n.name = UniqueGlobal(name, -1);
                g.items.push_back(std::move(n));
                cs.activeGlobal = true;
                cs.activeGlobalIndex = (int)g.items.size() - 1;
                ApplyData(g.items[cs.activeGlobalIndex].data);
            }
            else
            {
                n.name = Unique(name, -1);
                cs.items.push_back(std::move(n));
                cs.activeGlobal = false;
                cs.active = (int)cs.items.size() - 1;
                ApplyData(cs.items[cs.active].data);
            }
        }
        void Delete(int i) override
        {
            CharSet& cs = Cur();
            bool global = false;
            Named* target = VisibleMutable(i, &global);
            if (!target) return;
            const bool wasActive = (i == Active());
            if (global)
            {
                const int gi = GlobalIndexFromVisible(i);
                CharSet& g = Global();
                if (gi < 0 || gi >= (int)g.items.size()) return;
                g.items.erase(g.items.begin() + gi);
                const int remainingGlobal = (int)g.items.size();
                for (auto& kv : byChar)
                {
                    if (kv.first == kGlobalKey) continue;
                    CharSet& other = kv.second;
                    if (!other.activeGlobal) continue;
                    if (other.activeGlobalIndex == gi)
                    {
                        if (remainingGlobal > 0)
                        {
                            other.activeGlobal = true;
                            other.activeGlobalIndex = std::min(gi, remainingGlobal - 1);
                        }
                        else
                        {
                            other.activeGlobal = false;
                            other.active = std::min(std::max(other.active, 0), std::max(0, (int)other.items.size() - 1));
                        }
                    }
                    else if (other.activeGlobalIndex > gi)
                    {
                        --other.activeGlobalIndex;
                    }
                }
            }
            else
            {
                cs.items.erase(cs.items.begin() + i);
                if (cs.active >= (int)cs.items.size()) cs.active = (int)cs.items.size() - 1;
                if (cs.active < 0) cs.active = 0;
                if (wasActive)
                {
                    cs.activeGlobal = cs.items.empty() && GlobalCount() > 0;
                    cs.activeGlobalIndex = 0;
                }
            }
            if (wasActive)
                ApplyData(ActiveData());
        }
        std::string Suggest(const std::string& base) const override { return UniqueC(curKey, base, -1); }
        std::string CurrentChar() const override { return curKey; }

        std::vector<std::string> CharsWithProfiles() const override
        {
            std::vector<std::string> out;
            for (const auto& kv : byChar)
                if (kv.first != curKey && kv.first != kGlobalKey && kv.first != kPreLoginKey && !kv.second.items.empty()) out.push_back(kv.first);
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
            cs.items.push_back(std::move(n)); cs.activeGlobal = false; cs.active = (int)cs.items.size() - 1;
        }
        void CopyAllFrom(const std::string& srcChar) override
        {
            auto it = byChar.find(srcChar);
            if (it == byChar.end() || it->second.items.empty()) return;
            CharSet& cs = Cur();
            for (const Named& s : it->second.items) { Named n = s; n.name = Unique(n.name, -1); cs.items.push_back(std::move(n)); }
            cs.activeGlobal = false;
            cs.active = (int)cs.items.size() - 1;
        }

        bool IsGlobalAt(int i) const override
        {
            bool global = false;
            (void)Visible(i, &global);
            return global;
        }
        Profiles::ProfileRef RefAt(int i) const override
        {
            bool global = false;
            const Named* n = Visible(i, &global);
            return Profiles::ProfileRef{ global ? Profiles::Scope::Global : Profiles::Scope::Character, n ? n->name : std::string() };
        }
        int FindRef(const Profiles::ProfileRef& ref) const override
        {
            if (ref.name.empty()) return -1;
            if (ref.scope == Profiles::Scope::Global)
            {
                const CharSet* g = GlobalC();
                if (!g) return -1;
                for (int i = 0; i < (int)g->items.size(); ++i)
                    if (g->items[i].name == ref.name)
                        return LocalCount(CurC()) + i;
                return -1;
            }
            const CharSet* cs = CurC();
            if (!cs) return -1;
            for (int i = 0; i < (int)cs->items.size(); ++i)
                if (cs->items[i].name == ref.name)
                    return i;
            return -1;
        }
        bool CanMakeGlobal(int i) const override
        {
            bool global = false;
            return Visible(i, &global) && !global;
        }
        bool CanCopyToCharacter(int i) const override
        {
            bool global = false;
            return Visible(i, &global) && global;
        }
        void MakeGlobal(int i) override
        {
            CharSet& cs = Cur();
            bool global = false;
            const Named* src = Visible(i, &global);
            if (!src || global) return;
            if (i == Active()) SyncToActive();
            Named n = *src;
            n.name = UniqueGlobal(n.name, -1);
            n.data = SharedFamilyRefs(n.data);
            CharSet& g = Global();
            g.items.push_back(std::move(n));
            cs.activeGlobal = true;
            cs.activeGlobalIndex = (int)g.items.size() - 1;
            ApplyData(g.items[cs.activeGlobalIndex].data);
        }
        void CopyToCharacter(int i) override
        {
            CharSet& cs = Cur();
            bool global = false;
            const Named* src = Visible(i, &global);
            if (!src || !global) return;
            if (i == Active()) SyncToActive();
            Named n = *src;
            n.name = Unique(n.name, -1);
            n.data = CharacterFamilyRefs(n.data);
            cs.items.push_back(std::move(n));
            cs.activeGlobal = false;
            cs.active = (int)cs.items.size() - 1;
            ApplyData(cs.items[cs.active].data);
        }

    private:
        const CharSet* GlobalC() const
        {
            auto it = byChar.find(kGlobalKey);
            return it == byChar.end() ? nullptr : &it->second;
        }
        CharSet& Global() { return byChar[kGlobalKey]; }
        int LocalCount(const CharSet* cs) const { return cs ? (int)cs->items.size() : 0; }
        int GlobalCount() const { return LocalCount(GlobalC()); }
        int GlobalIndexFromVisible(int i) const
        {
            const int gi = i - LocalCount(CurC());
            return (gi >= 0 && gi < GlobalCount()) ? gi : -1;
        }
        const Named* Visible(int i, bool* global) const
        {
            if (global) *global = false;
            const CharSet* cs = CurC();
            const int localN = LocalCount(cs);
            if (cs && i >= 0 && i < localN)
                return &cs->items[i];
            const int gi = i - localN;
            const CharSet* g = GlobalC();
            if (g && gi >= 0 && gi < (int)g->items.size())
            {
                if (global) *global = true;
                return &g->items[gi];
            }
            return nullptr;
        }
        Named* VisibleMutable(int i, bool* global)
        {
            if (global) *global = false;
            CharSet& cs = Cur();
            const int localN = (int)cs.items.size();
            if (i >= 0 && i < localN)
                return &cs.items[i];
            const int gi = i - localN;
            CharSet& g = Global();
            if (gi >= 0 && gi < (int)g.items.size())
            {
                if (global) *global = true;
                return &g.items[gi];
            }
            return nullptr;
        }
        LoadoutData ActiveData() const
        {
            bool global = false;
            const Named* n = Visible(Active(), &global);
            (void)global;
            return n ? n->data : LoadoutData{};
        }
        LoadoutData SharedFamilyRefs(LoadoutData data)
        {
            if (!app) return data;
            for (int f = 0; f < Loadouts::FamCount; ++f)
            {
                if (data.fam[f].name.empty()) continue;
                Profiles::IProfileHost* h = FamHost(*app, f);
                if (!h) continue;
                int idx = h->FindRef(data.fam[f]);
                if (idx < 0) continue;
                if (!h->IsGlobalAt(idx))
                {
                    h->MakeGlobal(idx);
                    idx = h->Active();
                }
                if (idx >= 0 && idx < h->Count())
                    data.fam[f] = h->RefAt(idx);
            }
            return data;
        }
        LoadoutData CharacterFamilyRefs(LoadoutData data)
        {
            if (!app) return data;
            for (int f = 0; f < Loadouts::FamCount; ++f)
            {
                if (data.fam[f].name.empty()) continue;
                Profiles::IProfileHost* h = FamHost(*app, f);
                if (!h) continue;
                int idx = h->FindRef(data.fam[f]);
                if (idx < 0) continue;
                if (h->IsGlobalAt(idx))
                {
                    h->CopyToCharacter(idx);
                    idx = h->Active();
                }
                if (idx >= 0 && idx < h->Count())
                    data.fam[f] = h->RefAt(idx);
            }
            return data;
        }
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
        std::string UniqueGlobal(const std::string& base, int ignore) const { return UniqueC(kGlobalKey, base, ignore); }
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
        "Switching a loadout applies all four at once; 'New' snapshots your current selections. Use 'Make global' "
        "to create a shared loadout whose referenced shared profiles update across characters.");

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
        for (int i = 0; i < n; ++i) names.push_back(DisplayHostName(*h, i));
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
        if (kv.first == kGlobalKey)
            continue;
        nlohmann::json arr = nlohmann::json::array();
        for (const LoadoutHost::Named& n : kv.second.items)
        {
            nlohmann::json o = { {"name", n.name} };
            for (int f = 0; f < FamCount; ++f) o[kFamKey[f]] = RefToJson(n.data.fam[f]);
            arr.push_back(std::move(o));
        }
        nlohmann::json o = { {"active", kv.second.active}, {"loadouts", arr} };
        if (kv.second.activeGlobal)
        {
            auto git = g_host.byChar.find(kGlobalKey);
            if (git != g_host.byChar.end() && kv.second.activeGlobalIndex >= 0 && kv.second.activeGlobalIndex < (int)git->second.items.size())
            {
                o["activeScope"] = "global";
                o["activeName"] = git->second.items[kv.second.activeGlobalIndex].name;
            }
        }
        else if (kv.second.active >= 0 && kv.second.active < (int)kv.second.items.size())
        {
            o["activeScope"] = "character";
            o["activeName"] = kv.second.items[kv.second.active].name;
        }
        root[kv.first] = std::move(o);
    }
    j["loadoutsByChar"] = root;

    nlohmann::json global = nlohmann::json::array();
    auto git = g_host.byChar.find(kGlobalKey);
    if (git != g_host.byChar.end())
        for (const LoadoutHost::Named& n : git->second.items)
        {
            nlohmann::json o = { {"name", n.name} };
            for (int f = 0; f < FamCount; ++f) o[kFamKey[f]] = RefToJson(n.data.fam[f]);
            global.push_back(std::move(o));
        }
    j["loadoutsGlobal"] = { {"loadouts", std::move(global)} };
}

void Loadouts::Deserialize(App& app, const nlohmann::json& j)
{
    static const char* kFamKey[FamCount] = { "general", "dashboard", "infopanel", "hud" };
    g_host.byChar.clear();
    auto readLoadouts = [&](const nlohmann::json& arr) {
        std::vector<LoadoutHost::Named> out;
        if (!arr.is_array()) return out;
        for (const auto& e : arr)
        {
            if (!e.is_object()) continue;
            LoadoutHost::Named n; n.name = e.value("name", std::string("Loadout"));
            for (int f = 0; f < FamCount; ++f)
                if (auto fit = e.find(kFamKey[f]); fit != e.end())
                    n.data.fam[f] = RefFromJson(*fit);
            out.push_back(std::move(n));
        }
        return out;
    };

    if (auto git = j.find("loadoutsGlobal"); git != j.end())
    {
        const nlohmann::json& go = *git;
        std::vector<LoadoutHost::Named> items = go.is_array()
            ? readLoadouts(go)
            : (go.is_object() ? readLoadouts(go.value("loadouts", nlohmann::json::array())) : std::vector<LoadoutHost::Named>{});
        if (!items.empty())
            g_host.byChar[kGlobalKey].items = std::move(items);
    }

    struct PendingActive { std::string key; std::string scope; std::string name; };
    std::vector<PendingActive> pending;
    auto it = j.find("loadoutsByChar");
    if (it != j.end() && it->is_object())
        for (auto el = it->begin(); el != it->end(); ++el)
        {
            const nlohmann::json& o = el.value();
            if (!o.is_object()) continue;
            LoadoutHost::CharSet cs; cs.active = o.value("active", 0);
            cs.items = readLoadouts(o.value("loadouts", nlohmann::json::array()));
            if (cs.active < 0) cs.active = 0;
            if (cs.active >= (int)cs.items.size()) cs.active = cs.items.empty() ? 0 : (int)cs.items.size() - 1;
            const std::string keyName = el.key();
            if (keyName == kGlobalKey)
            {
                if (g_host.byChar[kGlobalKey].items.empty())
                    g_host.byChar[kGlobalKey].items = std::move(cs.items); // legacy reserved-bucket support
            }
            else
            {
                if (o.contains("activeScope") && o["activeScope"].is_string() &&
                    o.contains("activeName") && o["activeName"].is_string())
                    pending.push_back({ keyName, o["activeScope"].get<std::string>(), o["activeName"].get<std::string>() });
                g_host.byChar[keyName] = std::move(cs);
            }
        }
    for (const PendingActive& p : pending)
    {
        LoadoutHost::CharSet& cs = g_host.byChar[p.key];
        if (p.scope == "global")
        {
            auto git2 = g_host.byChar.find(kGlobalKey);
            if (git2 != g_host.byChar.end())
                for (int i = 0; i < (int)git2->second.items.size(); ++i)
                    if (git2->second.items[i].name == p.name)
                    {
                        cs.activeGlobal = true;
                        cs.activeGlobalIndex = i;
                        break;
                    }
        }
        else
        {
            for (int i = 0; i < (int)cs.items.size(); ++i)
                if (cs.items[i].name == p.name)
                {
                    cs.activeGlobal = false;
                    cs.active = i;
                    break;
                }
        }
    }
    g_host.Bind(app);   // bind; loadouts do NOT auto-apply (families restore their own active on load)
}

void Loadouts::RenameChar(App& app, const std::string& from, const std::string& to)
{
    (void)app;
    if (from.empty() || to.empty() || from == to) return;
    if (from == kGlobalKey || to == kGlobalKey) return;
    auto itF = g_host.byChar.find(from);
    if (itF == g_host.byChar.end()) return;
    g_host.byChar[to] = std::move(itF->second);   // REPLACE: the renamed character's loadouts win
    g_host.byChar.erase(itF);
    if (g_host.curKey == from) g_host.curKey = to;
}

void Loadouts::PurgeChar(App& app, const std::string& name)
{
    (void)app;
    if (name == g_host.curKey || name == kGlobalKey) return;   // never purge the active character or shared loadouts
    g_host.byChar.erase(name);
}

void Loadouts::CollectCharNames(App& app, std::set<std::string>& out)
{
    (void)app;
    for (const auto& kv : g_host.byChar)
        if (kv.first != kPreLoginKey && kv.first != kGlobalKey) out.insert(kv.first);
}
