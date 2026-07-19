#pragma once
#include "ProfileStore.h"     // Profiles::PerCharProfiles + IProfileHost
#include <nlohmann/json.hpp>
#include <functional>

class App;
struct Setting;                // ui/tabs/SettingsModel.h

// -----------------------------------------------------------------------------------------------------
// ConfigProfiles: the ONE universal profile mechanism, with character-local and shared/global profiles.
// Every setting lives in Config; a
// "profile" is just a snapshot of a SLICE of Config, chosen by an Owner. The four families (General / HUD /
// Info / Dashboard) are the same code, differing only by which Config sections they own -- this replaces the
// bespoke HudPayload/InfoPayload/DashPayload systems and their four hand-written capture/apply/toJson/fromJson.
//
// Scalars come from the Settings() table (so they are automatically searchable + tooltipped + rendered by the
// model). The few STRUCTURED fields (the Ordered<> layouts + the Info textOpts map) are handled by a per-owner
// LayoutIO the surface registers (the only bit ConfigProfiles can't do generically).
//
// Global (owner == Global) settings are NOT profiled -- they persist flat in settings.json (API key, Diagnostics,
// Wiki, widget-content toggles, the global Dashboard toggles, and all the internal UI-state fields).
// -----------------------------------------------------------------------------------------------------
namespace ConfigProfiles
{
    enum class Owner { General, Hud, Info, Dash, Global };

    Owner OwnerOf(int section);              // a Settings section -> its owning family (Global = not profiled)
    Owner OwnerOfSetting(const Setting& s);  // per-setting owner (a few Dashboard toggles are Global despite SEC_DASHBOARD)

    // A surface registers how to (de)serialize + default-seed its STRUCTURED Config fields for one owner.
    struct LayoutIO
    {
        std::function<void(App&, nlohmann::json&)>       capture;      // config structured fields -> j (into the slice)
        std::function<void(App&, const nlohmann::json&)> apply;        // slice j -> config structured fields, then reconcile vs catalog
        std::function<void(App&)>                        seedDefault;  // config structured fields <- catalog defaults (for the baked m_default)
    };
    void RegisterLayout(Owner, LayoutIO);    // surfaces call this BEFORE Init()

    // Lifecycle (entry.cpp). Call the surfaces' register fns, then Init, then LoadSettings (which calls
    // DeserializeAll), then TickAll every frame.
    void Init(App&);                                   // configure the 4 families + capture the baked default slices
    void TickAll(App&);                                // bind all 4 families to the current character (cheap no-op unless changed)
    void SerializeAll(App&, nlohmann::json&);          // write the 4 families' character-local + shared profiles
    void DeserializeAll(App&, const nlohmann::json&);  // read them + bind the current character (applies its active profile)

    Profiles::IProfileHost& Host(App&, Owner);         // for Loadouts + DrawProfileBar
}
