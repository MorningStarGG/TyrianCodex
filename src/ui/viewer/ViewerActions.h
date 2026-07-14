#pragma once
#include "app/App.h"
#include "model/Dataset.h"
#include <imgui.h>

// Viewer action buttons (Open guide / Map / Copy waypoint) + the target clear-X, with their glyphs.
// Defined in ui/viewer/ViewerActions.cpp.
void DrawObjectiveActionButtons(App& app, const Step& step, bool compact);
void DrawTravelActionButtons(App& app, bool compact);
// A single centered "Open guide" button for detail cards with no destination (Complete / Recommend /
// Dungeon) -- Map/Copy only apply to a real target, but Open guide is always available.
void DrawGuideOnlyButton(App& app, bool compact);
void DrawTargetClearX(App& app, TargetOverrideKind kind, ImVec2 topRight, float size = 22.f);
float EstimateObjectiveActionButtonsHeight(float contentW, bool compact, float vs);
