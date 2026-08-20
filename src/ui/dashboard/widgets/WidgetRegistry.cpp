#include "Widget.h"
#include "Widgets.h"
#include <cstring>

// The ONE dashboard widget registry. Add a widget here (with its body fn from Widgets.h) and it automatically
// appears in the dashboard + the Dashboard settings list. Order here is the DEFAULT order; the user's saved
// layout (order/show/span) overrides it at runtime. defaultOn keeps the out-of-box panel clean.
const std::vector<DashWidget>& DashWidgets()
{
    static const std::vector<DashWidget> r = {
        // Each widget carries its own default: defaultOn (shown), defaultSpan (1 full / 2 half), defaultOrder (order).
        // key                  title                  icon canHalf defaultOn span order  draw
        { "notifications",      "Notifications",          0, true,   true, 1, 7,  &DashW::Notifications },
        { "zoneHeader",         "Zone Header",            0, true,   true, 1, 1, &DashW::ZoneHeader, /*selfFramed*/ true, /*defaultHover*/ true },
        { "currentObjective",   "Current Objective",      0, true,   true, 1, 4,  &DashW::CurrentObjective, /*selfFramed*/ true },
        { "objectiveList",      "Objectives",             0, true,   true, 1, 3, &DashW::ObjectiveList },
        { "modeSwitch",         "Objectives / Farming",   0, true,   true, 1, 2, &DashW::ModeSwitch },
        { "zoneProgress",       "Zone Progress",          0, true,   true, 1, 8,  &DashW::ZoneProgress },
        { "events",             "Events",                 0, true,   true, 1, 6,  &DashW::Events, /*selfFramed*/ true },  // ViewerEvents draws its own cards -> don't wrap (a frame inside a frame)
        { "resetTimers",        "Resets & Clock",         0, true,   false, 1, 0,  &DashW::ResetTimers },
        { "travelTarget",       "Travel Target",          0, true,   false, 1, 0, &DashW::TravelTarget },
        { "recommendedZones",   "Recommended Zones",      0, true,   true, 1, 9, &DashW::RecommendedZones, /*selfFramed*/ true },  // draws its own card(s); at 80 the story card would sit inside a second frame otherwise
        { "character",          "Character",              0, true,   false, 1, 0, &DashW::Character },
        { "sessionStats",       "Session Stats",          0, true,   false, 1, 0, &DashW::SessionStats },
        { "sessionEarnings",    "Session Earnings",       0, true,   false, 1, 0, &DashW::SessionEarnings },
        { "worldCompletion",    "World Completion",       0, true,   false, 1, 0, &DashW::WorldCompletion },
        { "nearestWaypoints",   "Zone Waypoints",         0, true,   true, 1, 10, &DashW::NearestWaypoints },
        { "search",             "Search",                 0, true,   true, 1, 18, &DashW::Search },
        { "items",              "Items",                  0, true,   true, 1, 17, &DashW::Items },
        { "routeArrow",         "Route Arrow",            0, true,   true, 1, 5, &DashW::RouteArrow },
        { "favoriteWaypoints",  "Favorite Waypoints",     0, true,   false, 1, 0, &DashW::FavoriteWaypoints },
        { "personalStory",      "Personal Story",         0, true,   true, 2, 14, &DashW::PersonalStory },
        { "quickControls",      "Quick Controls",         0, true,   true, 1, 11, &DashW::QuickControls },
        { "quickNotes",         "Quick Notes",            0, true,   false, 1, 0, &DashW::QuickNotes },
        { "calculator",         "Calculator",             0, true,   false, 1, 0, &DashW::Calculator },
        { "loadouts",           "Loadouts",               0, true,   false, 1, 0, &DashW::Loadouts },
        { "craftCart",          "Crafting Cart",          0, true,   false, 1, 0, &DashW::CraftingCart },
        // --- API-backed (need a key + scopes; read through AccountData) ---
        { "wallet",             "Wallet",                 0, true,   true, 1, 12, &DashW::Wallet },
        { "todaysDailies",      "Today's Dailies",        0, true,   false, 1, 0, &DashW::TodaysDailies },
        { "trackedAch",         "Achievements",           0, true,   false, 1, 0, &DashW::TrackedAchievements },
        { "mastery",            "Mastery",                0, true,   false, 1, 0, &DashW::Mastery },
        { "tradingPost",        "Trading Post",           0, true,   true, 2, 13, &DashW::TradingPost },
        { "loginRewards",       "Account / Login",        0, true,   false, 1, 0, &DashW::LoginRewards },
        { "charactersOverview", "Characters",             0, true,   true, 1, 15, &DashW::CharactersOverview },
        { "inventory",          "Inventory",              0, true,   false, 1, 0, &DashW::Inventory },
        { "altEquipment",       "Alt Equipment",          0, true,   true, 1, 16, &DashW::AltEquipment },
        { "accountBank",        "Account Bank",           0, true,   false, 1, 0, &DashW::AccountBank },
        { "wvwMatchup",         "WvW Matchup",            0, true,   true, 1, 19, &DashW::WvwMatchup },
        { "wvwCurrentMap",      "WvW Current Map",        0, false,  false, 1, 0, &DashW::WvwCurrentMap },
        { "wvwMaps",            "WvW Maps",               0, false,  false, 1, 0, &DashW::WvwMaps },
    };
    return r;
}

const DashWidget* FindDashWidget(const char* key)
{
    if (!key) return nullptr;
    for (const DashWidget& w : DashWidgets())
        if (std::strcmp(w.key, key) == 0) return &w;
    return nullptr;
}
