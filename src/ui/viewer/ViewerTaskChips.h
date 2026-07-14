#pragma once
#include "app/App.h"
#include "model/Dataset.h"
#include <imgui.h>

// Heart-objective task chips/tiles: the renderer for the per-objective "task" pills (collect/kill/talk/...)
// with their procedural icons + the heart category accent, in styles (compact chip / medium / large tile).
// Defined in ui/viewer/ViewerTaskChips.cpp.
// `compact` selects the chip sizing explicitly (compact viewer + dashboard true; rich viewer false) so these
// never read the global viewer style.
void DrawHeartTaskChips(const App& app, const Step& s, float fs, ImU32 fallbackAccent, bool compact);
float EstimateTaskChipsHeight(const App& app, const Step& s, float fs, float contentW, bool compact);
