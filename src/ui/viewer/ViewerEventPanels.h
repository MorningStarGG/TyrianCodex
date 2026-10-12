#pragma once
#include "ui/Gw2Ui.h"

class App;

// ----------------------------------------------------------------------------------------------------
// ViewerEvents: the Timed Events / Event Chains card + the Nearby Event Areas card (data: app.eventTimers /
// app.eventAreas), shared so there is ONE implementation used by BOTH the Rich secondary panel and the Compact
// "Events" tab -- no duplicated row/sort/format code. The implementations (and all their static helpers) live
// in ViewerEventPanels.cpp; the caller chooses when to draw and whether it already supplies a frame.
// ----------------------------------------------------------------------------------------------------
namespace ViewerEvents
{
    // Timed Events (or Event Chains when nothing is on a schedule) for the active map. Returns rows drawn
    // (0 = nothing to show, no card drawn).
    int DrawTimers(App& app, float fs, Gw2Ui::SectionPresentation presentation = Gw2Ui::SectionPresentation::Card);

    // Nearby Event Areas, ranked by the player's live position. currentIndex gates to an active step (and is the
    // fallback ranking origin when MumbleLink is unavailable); maxRows caps the list. Returns rows drawn.
    int DrawAreas(App& app, int currentIndex, float fs, int maxRows,
                  Gw2Ui::SectionPresentation presentation = Gw2Ui::SectionPresentation::Card);

    // The Timers TAB body: a GLOBAL, all-maps event board with a List view (filter + sort) and a GW2-wiki-style
    // Timeline view. Reuses the same timer math + row renderer + category colours as DrawTimers (one impl).
    void DrawTimersTab(App& app);
}
