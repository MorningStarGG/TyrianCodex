#pragma once
#include <nlohmann/json_fwd.hpp>
#include <set>
#include <string>
class App;
namespace Profiles { struct IProfileHost; }

// -----------------------------------------------------------------------------------------------------
// Loadouts: a "meta profile" -- a named bundle remembering WHICH profile is active in each of the four
// families (General / Dashboard / Info Panel / HUD). Loadouts and the profiles they reference can be either
// character-local or shared/global. Applying a loadout sets all four families at once, for a one-click
// "switch my whole setup" (e.g. Leveling / Combat / AFK). It is NOT a PerCharProfiles consumer: its "live
// state" (the families' active profiles) is owned by the families and can drift independently, so it uses a
// bespoke IProfileHost where New() snapshots the current family selections and SetActive() APPLIES the bundle.
// Apply is MANUAL (UI / keybind) -- loadouts do not auto-apply on character switch (the families restore their
// own active profile as before, whether local or shared).
// -----------------------------------------------------------------------------------------------------
namespace Loadouts
{
    Profiles::IProfileHost& Host(App& app);   // the loadout collection, bound to the current char (for DrawProfileBar)
    void DrawSettings(App& app);              // SEC_LOADOUTS body: loadout bar + per-family active-profile pickers

    // Switches are DEFERRED to the next frame's ProcessPending() -- a family SetActive re-applies that family's
    // live state, which must NOT happen mid-render of that family (e.g. applying from the Dashboard's own widget
    // would mutate the layout it's iterating). ProcessPending() runs once at frame start, before any family draws.
    void        ProcessPending(App& app);     // call once per frame, FIRST (before the families render)
    void        ApplyIndex(App& app, int i);  // queue: apply loadout i (set all four families)
    void        CycleNext(App& app);          // queue: apply the next loadout (keybind)
    void        SetFamily(App& app, int fam, int profileIdx);   // queue: switch one family's active profile
    int         Count(App& app);
    int         Active(App& app);
    std::string NameAt(App& app, int i);

    // Per-family direct switch (for the submenus / widget): fam 0=General, 1=Dashboard, 2=Info Panel, 3=HUD.
    enum { FamGeneral, FamDashboard, FamInfoPanel, FamHud, FamCount };
    const char*             FamilyName(int fam);
    Profiles::IProfileHost* FamilyHost(App& app, int fam);   // list / switch that family's profiles directly

    void Serialize(App& app, nlohmann::json& j);             // -> loadoutsByChar + loadoutsGlobal
    void Deserialize(App& app, const nlohmann::json& j);

    // -- per-character maintenance (rename detector + Diagnostics cleanup); persisted by the next SaveSettings --
    void RenameChar(App& app, const std::string& from, const std::string& to);   // move the char's loadout bundle
    void PurgeChar(App& app, const std::string& name);
    void CollectCharNames(App& app, std::set<std::string>& out);
}
