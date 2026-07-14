#pragma once
#include "app/App.h"

// The Farming tab body (the compact viewer's "Farming" tab + the rich viewer's "Farming Run" secondary panel).
// Built to match the step objective list, with TWO chip rows:
//   Row 1 -- category chips (single-select scope): All / Ore / Wood / Plant / Seafood / Misc -> Config.gatherCategory.
//   Row 2 -- material chips (multi-toggle enable, lit = shown): the materials within the selected category, each
//            toggling Config.GatherShown. This is the per-material control.
// Below them, a step-style progress list of the ACTIVE (in-category + enabled) materials -- nearest distance +
// gathered/total, the nearest highlighted, all-gathered dimmed (State.farmGathered). Scoped to the current map
// (State.activeGather). It drives nothing itself: the in-world arrow (FarmingGuidance) + the node overlay
// (CategoryMarkers) read Config.gatherCategory / GatherShown + State.farmGathered.
void DrawFarmingTab(App& app);
