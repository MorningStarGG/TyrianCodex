#pragma once
class App;

// Internal: the body-draw function for every dashboard widget, one per widgets/*.cpp. Only WidgetRegistry.cpp
// (which assembles the registry) and the defining .cpp need this -- the Dashboard always goes through
// DashWidgets()/FindDashWidget(). Add a new widget = add its function here + a registry row + its .cpp.
namespace DashW
{
    void Notifications(App &, float w);
    void ZoneHeader(App &, float w);
    void CurrentObjective(App &, float w);
    void ObjectiveList(App &, float w); // full scrollable objective list (the shared viewer panel: search + filters + Show-completed)
    void ModeSwitch(App &, float w);    // "Objectives / Farming": a viewer-style toggle that flips the shared farming run

    void ZoneProgress(App &, float w);
    void Events(App &, float w);
    void ResetTimers(App &, float w);
    void Character(App &, float w);
    void QuickControls(App &, float w);
    void TravelTarget(App &, float w);
    void RecommendedZones(App &, float w);
    void PersonalStory(App &, float w);
    void FavoriteWaypoints(App &, float w);
    void SessionStats(App &, float w);
    void SessionEarnings(App &, float w); // live current-session wallet/currency earnings (Session Tracker)
    void RouteArrow(App &, float w);
    void NearestWaypoints(App &, float w); // "Zone Waypoints": the active zone's unlocked waypoints, click-to-travel
    void Search(App &, float w);           // "Search": find any waypoint/POI/vista/hero/heart/zone, click-to-travel
    void Items(App &, float w);            // "Items": search the whole-game item catalogue, jump to the Items tab
    void QuickNotes(App &, float w);
    void WorldCompletion(App &, float w);
    void Calculator(App &, float w);
    void Loadouts(App &, float w);     // quick-switch the active loadout / per-family profile
    void CraftingCart(App &, float w); // the persistent Crafting Cart: projects + materials check-off
    void Wallet(App &, float w);
    void TodaysDailies(App &, float w);
    void TrackedAchievements(App &, float w);
    void Mastery(App &, float w);
    void TradingPost(App &, float w);
    void LoginRewards(App &, float w);
    void CharactersOverview(App &, float w);
    void Inventory(App &, float w);
    void AltEquipment(App &, float w);
    void AccountBank(App &, float w);

    // --- WvW (anonymous live match data) ---
    void WvwMatchup(App &, float w);    // the 3-world matchup scoreboard (score / VP / skirmish / PPT / K-D)
    void WvwCurrentMap(App &, float w); // the WvW map you're on (rendered map + owner-colored objectives + data)
    void WvwMaps(App &, float w);       // configurable per-map control (rendered maps + data, per-map mode)
}
