#include "ConfigProfiles.h"
#include "app/App.h"
#include "ui/SettingsWindow.h"        // ApplyFontChoice (the one live side-effect of a General profile apply)
#include "ui/tabs/SettingsModel.h"    // Settings(), Setting, SKind, SEC_*
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
    using nlohmann::json;
    using ConfigProfiles::Owner;

    App* s_app = nullptr;
    bool g_init = false;

    // The 4 profile families, indexed by (int)Owner (General=0, Hud=1, Info=2, Dash=3). Global (4) has no family.
    struct Family
    {
        Profiles::PerCharProfiles<json> store;
        const char* storeKey = nullptr;
        json        defaultSlice;      // baked defaults, captured at Init() while Config is fresh
    };
    Family                   g_fam[4];
    ConfigProfiles::LayoutIO g_layout[4];
    bool                     g_hasLayout[4] = { false, false, false, false };

    int  Idx(Owner o)      { return (int)o; }
    bool IsFamily(Owner o) { return o != Owner::Global; }

    // --- Config slice <-> json: scalars via the Settings() table, structured fields via the registered LayoutIO ---
    json CaptureSlice(App& app, Owner owner)
    {
        json j = json::object();
        for (const Setting& s : Settings(app))
        {
            if (ConfigProfiles::OwnerOfSetting(s) != owner) continue;
            switch (s.kind)
            {
                case SKind::Bool:    j[s.key] = *s.vb; break;
                case SKind::Float:   j[s.key] = *s.vf; break;
                case SKind::Int:     j[s.key] = *s.vi; break;
                case SKind::Enum:    j[s.key] = *s.ve; break;
                case SKind::Keybind: j[s.key] = std::string(s.vk ? s.vk : ""); break;
                case SKind::String:  j[s.key] = std::string(s.vs ? s.vs : ""); break;
            }
        }
        if (IsFamily(owner) && g_hasLayout[Idx(owner)] && g_layout[Idx(owner)].capture)
            g_layout[Idx(owner)].capture(app, j);
        return j;
    }

    void ApplySlice(App& app, Owner owner, const json& j)
    {
        if (j.is_object())
            for (const Setting& s : Settings(app))
            {
                if (ConfigProfiles::OwnerOfSetting(s) != owner) continue;
                auto it = j.find(s.key);
                if (it == j.end()) continue;
                try
                {
                    switch (s.kind)
                    {
                        case SKind::Bool:    if (it->is_boolean())        *s.vb = it->get<bool>();  break;
                        case SKind::Float:   if (it->is_number())         *s.vf = it->get<float>(); break;
                        case SKind::Int:     if (it->is_number_integer()) *s.vi = it->get<int>();   break;
                        case SKind::Enum:    if (it->is_number_integer()) *s.ve = it->get<int>();   break;
                        case SKind::Keybind: if (it->is_string() && s.vk) std::snprintf(s.vk, s.kbuf, "%s", it->get<std::string>().c_str()); break;
                        case SKind::String:  if (it->is_string() && s.vs) std::snprintf(s.vs, s.sbuf, "%s", it->get<std::string>().c_str()); break;
                    }
                }
                catch (...) { /* malformed -> keep current */ }
            }
        if (IsFamily(owner) && g_hasLayout[Idx(owner)] && g_layout[Idx(owner)].apply)
            g_layout[Idx(owner)].apply(app, j);   // writes the structured fields + reconciles vs the catalog
        // Live side-effect so a profile switch takes effect immediately (matches the old GeneralProfiles::ApplyTo:
        // only the font is re-applied here). Keybinds re-register from Config in AddonLoad + on a keybind edit;
        // pathType re-applies on its own edit + the next zone activation -- neither is safe to force during the
        // initial load (this Apply runs inside LoadSettings, before the zone system is ready).
        if (owner == Owner::General)
            ApplyFontChoice(app);
    }

    void ConfigureFamily(Owner owner, const char* storeKey)
    {
        Family& f = g_fam[Idx(owner)];
        f.storeKey = storeKey;
        f.store.Configure(
            [owner]() { return s_app ? CaptureSlice(*s_app, owner) : json::object(); },
            [owner](const json& j) { if (s_app) ApplySlice(*s_app, owner, j); },
            [](const json& j) { return j; },   // toJson: the payload IS the slice json
            [](const json& j) { return j; },   // fromJson: identity
            [owner]() { return g_fam[Idx(owner)].defaultSlice; });   // baked defaults
    }

    constexpr Owner kFamilies[4] = { Owner::General, Owner::Hud, Owner::Info, Owner::Dash };
}

namespace ConfigProfiles
{
    Owner OwnerOf(int section)
    {
        switch (section)
        {
            case SEC_HUD:       return Owner::Hud;
            case SEC_INFO:      return Owner::Info;
            case SEC_DASHBOARD: return Owner::Dash;
            // Global (not profiled): API key, Diagnostics, Wiki, the dashboard widget-content toggles, Loadouts.
            case SEC_WIDGETS: case SEC_API: case SEC_DIAG: case SEC_WIKI: case SEC_LOADOUTS:
                return Owner::Global;
            default:            return Owner::General;   // guide/arrow/panel/routing/trail/maptrail/markers/notify/zonedisplay/keys
        }
    }

    Owner OwnerOfSetting(const Setting& s)
    {
        // A few settings are GLOBAL (same for every profile) despite living in profiled sections.
        static const std::set<std::string> kGlobal = {
            "dashEnabled", "dashLocked", "dashAutoHideHoverOff", "dashAutoHideClickOff", "uiScale" };
        if (s.key && kGlobal.count(s.key)) return Owner::Global;
        return OwnerOf(s.section);
    }

    void RegisterLayout(Owner owner, LayoutIO io)
    {
        if (!IsFamily(owner)) return;
        g_layout[Idx(owner)]    = std::move(io);
        g_hasLayout[Idx(owner)] = true;
    }

    void Init(App& app)
    {
        if (g_init) return;
        s_app = &app;
        // The structured Config fields start empty; seed them to their catalog defaults so the captured default
        // slice below is the FULL baked default (Config.h scalar defaults + the default layouts). Config is fresh
        // here (Init runs before LoadSettings), so this is a clean snapshot.
        for (Owner o : kFamilies)
            if (g_hasLayout[Idx(o)] && g_layout[Idx(o)].seedDefault) g_layout[Idx(o)].seedDefault(app);

        static const struct { Owner o; const char* key; } kCfg[] = {
            { Owner::General, "settingsProfilesByChar" },
            { Owner::Hud,     "hudProfilesByChar" },
            { Owner::Info,    "infoProfilesByChar" },
            { Owner::Dash,    "dashProfilesByChar" },
        };
        for (const auto& e : kCfg)
        {
            g_fam[Idx(e.o)].defaultSlice = CaptureSlice(app, e.o);
            ConfigureFamily(e.o, e.key);
        }
        g_init = true;
    }

    void TickAll(App& app)
    {
        s_app = &app;
        if (!g_init) Init(app);
        for (Owner o : kFamilies) g_fam[Idx(o)].store.BindCharacter(app.state.currentChar);
    }

    void SerializeAll(App& app, nlohmann::json& j)
    {
        s_app = &app;
        if (!g_init) Init(app);
        for (Owner o : kFamilies)
        {
            g_fam[Idx(o)].store.BindCharacter(app.state.currentChar);
            g_fam[Idx(o)].store.Serialize(j, g_fam[Idx(o)].storeKey);   // Serialize() folds live -> active first
        }
    }

    void DeserializeAll(App& app, const nlohmann::json& j)
    {
        s_app = &app;
        if (!g_init) Init(app);
        for (Owner o : kFamilies)
        {
            g_fam[Idx(o)].store.Deserialize(j, g_fam[Idx(o)].storeKey);
            g_fam[Idx(o)].store.BindCharacter(app.state.currentChar);   // applies this char's active profile into Config
        }
    }

    Profiles::IProfileHost& Host(App& app, Owner owner)
    {
        s_app = &app;
        if (!g_init) Init(app);
        const Owner o = IsFamily(owner) ? owner : Owner::General;
        g_fam[Idx(o)].store.BindCharacter(app.state.currentChar);
        return g_fam[Idx(o)].store;
    }
}
