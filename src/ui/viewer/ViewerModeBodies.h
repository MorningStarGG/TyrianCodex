#pragma once
#include "app/App.h"
#include <imgui.h>
#include <cstdint>

// The non-Step mode bodies: the Complete/Recommend summary detail card, the Dungeon detail card, and the
// "head here next" recommended-zone list + the dungeon up-next list (both filter on the objective search).
// Defined in ui/viewer/ViewerModeBodies.cpp.
void DrawRecommendBody(App& app);          // the "head here next" list with loading/keyless/empty fallbacks
void DrawDungeonUpNextBody(App& app);      // the upcoming route instructions
// The "head here next" recommended-zone list (region-grouped, each row a Travel-here / [ active ]). Shared by
// the Complete + Recommend list area. Excludes excludeMapId + finished zones. Returns true if it drew rows.
bool DrawRecommendList(App& app, uint32_t excludeMapId);
// The dungeon DETAIL element: shared content body ("Do this" + dist/ETA chips + current instruction +
// Open-guide button) and the auto-height card around it. `compact` selects the look explicitly (compact viewer
// + dashboard true; rich viewer false) so the dashboard never depends on the global viewer style. The compact
// viewer draws its own fixed-height frame and calls DrawDungeonDetailBody directly (see ViewerStepCompact).
void DrawDungeonDetailBody(App& app, float fs, bool compact);
void DrawDungeonDetail(App& app, float fs, bool compact);
// The Complete/Recommend DETAIL summary card (green "Zone complete" + 100% bar, or "No guide route"), auto-height.
void DrawSummaryDetailRich(App& app, float fs);
// The EXACT inner content height of that summary card (icon row + name/map line + the "Complete" bar in Complete
// mode ONLY + the hint + divider + Open-guide button), WRAP-AWARE. Both viewers reserve their recommended-list
// area from this (then add their own frame padding), so the list shrinks to fit and the button never clips/
// pushes off. barHeight = the ProgressBar bar height the caller draws (compact 18, rich 20).
float SummaryDetailContentHeight(App& app, float fs, float contentW, float barHeight);
