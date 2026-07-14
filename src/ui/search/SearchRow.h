#pragma once
#include "ui/search/SearchModel.h"
#include <cstdint>
class App;

// Shared Atlas/search UI -- ONE implementation used by both the Atlas settings tab and the Search dashboard
// widget (and reused by the Zone Waypoints widget for its rows): the filter bar, the query runner, and the
// result row (icon + name + zone/region + distance pill + hover map thumbnail; click -> the unified travel-to).
namespace Search
{
    // Caller-owned filter state (the tab + each widget keep their own static instance).
    struct Filters
    {
        char text[64]  = "";
        std::string kind;     // map-KIND filter (Atlas row-1 chips): "" = all kinds; "Lounge" = the lounge TAG; else OpenWorld/City/...
        int  typeSel   = 0;   // 0 = All; 1..7 = a single entry kind (waypoint/poi/vista/hero/heart/zone/marker)
        int  gatherCat = 0;   // Farming kind: the row-2 material category (0 = All; Farm::kCategoryNames)
        int  regionSel = 0;   // 0 = All regions; else RegionOptions()[regionSel-1]
        int  levelMin  = 1;   // level-range slider (owning-zone band overlap); 1..80 = off
        int  levelMax  = 80;
        int  statusSel = 0;   // 0 = All, 1 = Recommended (in-band + incomplete), 2 = Incomplete
    };

    // The filter bar (full mode): search box -> row-1 map-KIND chips -> contextual row-2 objective chips ->
    // region dropdown -> level slider. Compact mode (dashboard widget) draws just the search box. Mutates `f`.
    void DrawFilterBar(App& app, Filters& f, bool compact);

    // Resolve `f` (type/region + the live player point) and run Search::Query -> result indices into Entries().
    // `slot` is the caller's private cache slot (see SearchModel::kSlots). `browse` -> region/zone order (grouped
    // browse view); false -> nearest-first / relevance (searches).
    const std::vector<int>& RunQuery(App& app, int slot, const Filters& f, bool browse = false);

    // One result row. compact = the dashboard look (smaller, no sub-label); very narrow compact rows collapse to
    // icon + ellipsized name, with hidden badges/copy surfaced in the tooltip/right-click copy.
    // showCopy adds a copy-waypoint hotspot when there is enough room for the normal compact row.
    // Click -> TravelToLocation / OpenZoneTravelChoice (the distance-gated card). Returns hovered.
    // hideRecommended: skip the green "Recommended" pill on zone rows (the Recommended-Zones widget lists ONLY
    // recommended zones, so the pill there is redundant -- it just wants the level pill).
    // tipMap: include the zone map (objective circled / zone's objectives pinned) in the hover tooltip. Atlas
    // always sets it; the dashboard widgets set it from their per-widget config.wMapTip* toggle.
    struct RowStyle
    {
        float height = 36.f;
        int stripe = -1;
        bool compact = false;
        bool showCopy = true;
        bool showFavorite = false;
        bool hideRecommended = false;
        bool tipMap = false;
    };
    bool DrawResultRow(App& app, const Entry& e, const RowStyle& style);
}
