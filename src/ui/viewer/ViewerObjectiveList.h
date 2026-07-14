#pragma once
#include "app/App.h"
#include "model/Dataset.h"
#include <imgui.h>
#include <string>
#include <vector>

// The objective checklist + its search/filter (the single owner of the viewer's objective search state).
// Defined in ui/viewer/ViewerObjectiveList.cpp.
bool ObjectiveMatchesRichFilter(const Step& s);                // does a step pass the current search + type filter
void DrawRichObjectiveTools(App& app, bool showChips = true);  // search box + Show-completed toggle; type chips when showChips
void DrawZoneObjectiveList(App& app, bool fillStep = true, float maxHeightOverride = 0.f, float exactTotalHeight = -1.f);
// One objective row (checkbox + icon + name + level pill + hover + click-to-target + right-click menu), shared by
// the Compact + Rich viewers and the objective widget. completed=true -> faded + ticked (untick = mark incomplete).
void DrawObjectiveRow(App& app, int stepIndex, float lfs, float iconSize, bool completed = false);
// The right-click menu for an objective row: "Open in Wiki" (named rows) + "Mark incomplete" (completed rows ONLY).
void ObjectiveRowMenu(App& app, const Step& s, bool hovered, bool completed);
// A self-contained scrollable objective list (tools chrome + active rows + a Completed group when toggled). The
// shared body the Compact viewer + the objective widget render; `height` 0 = fill the remaining content region.
void DrawObjectiveListPanel(App& app, float height = 0.f);
// The display order of ALL zone step indices (the SINGLE source of truth shared by the Compact + Rich lists):
// NEAREST modes -> sorted by distance from the player (nearest-first); TRAIL modes -> authored order rotated so
// the current objective leads. Callers apply their own visibility filter; row rendering stays per-style.
const std::vector<int>& OrderedObjectiveSteps(App& app);   // cached behind a change token (see .cpp); render-thread only
// The next `wantN` UPCOMING open-world objectives (the ordered list minus the current one and anything done) --
// the SINGLE source for the dashboard "Next objective" widget + the Info Panel "Upcoming" text. wantN<=0 = all.
std::vector<int> UpcomingObjectiveSteps(App& app, int wantN);
// True if `text` matches the current objective search box (so the Recommend / Dungeon list bodies can filter
// against the same search without touching the private filter state).
bool MatchesObjectiveSearch(const std::string& text);
