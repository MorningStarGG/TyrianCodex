#pragma once
#include "model/Dataset.h"   // Zone
class App;

// A clickable recommended-zone row for the guide viewer's "head here next" list: the zone name + a right-aligned
// [ Lv x-y ][ Recommended ] pill pair (or an "Active" pill when it's the current travel destination), and it
// starts travel on click. A stateless immediate-mode VIEW of Travel::Controller (which owns the state). Returns
// whether the row is hovered. Display bits come from ZoneRowStyle.
struct ZoneRowStyle
{
    int   stripe       = -1;      // alternating-row index for RowBackground; -1 = none
    float height       = 34.f;
    float nameFs       = 20.f;    // zone-name label font px
    float actionFs     = 18.f;    // the Lv / Recommended / Active pill font px
    // Default TRUE: a click opens the Travel / Show target / Not now card -- the confirm path zone travel should
    // normally use. Set FALSE only for a bare app.travel.StartTravel (immediate on-foot routing, no card) -- e.g.
    // a future direct-travel surface. No caller passes false today; the path is kept reserved on purpose (the old
    // Zones tab used it). The safe default means a new caller can't accidentally skip the confirm card.
    bool  travelViaCard = true;
    // Default TRUE: show the green "Recommended" pill (every row of the level-band list is a recommendation).
    // The max-level story view groups by release (not level), so it sets this FALSE -- a "Recommended" pill
    // there would be misleading.
    bool  showRecommended = true;
    // Map-complete marker: when TRUE a green check is drawn in the status slot (the story view sets it per row
    // via ZoneComplete, since it lists mapped zones rather than hiding them).
    bool  mapped = false;
};

bool DrawZoneRow(App& app, const Zone* z, const ZoneRowStyle& style);   // returns hovered; click -> travel
