#pragma once
#include "app/App.h"
#include "model/Dataset.h"
#include <imgui.h>

// Shared viewer primitives + UMBRELLA. The shared in-world guide-viewer drawing helpers used to all live in
// one ViewerComponents.cpp; they now split into per-concern modules under ui/viewer/. This header
// keeps the three primitives below AND re-includes every module header, so the consumers (GuideViewer.cpp,
// ViewerStepCompact.cpp, ViewerStepRich.cpp) keep including just "ViewerComponents.h" and see everything.

ImU32 WithAlpha(ImU32 col, int alpha);
void DrawWrappedText(const char* text, float fs, ImU32 col, bool stroke = false, ImFont* font = nullptr);
void PaintTypeIconAt(ImDrawList* dl, const std::string& type, ImVec2 p, float size, bool medallion);

// Guide-viewer detail-card surface colors -- ONE source for all six card sites (the four compact fixed cards
// + the two rich auto-cards) so they stay identical and recolor in one place. (The two rich cards had drifted
// to 150/145 bg + two different borders; normalized here.)
inline constexpr ImU32 kViewerCardBg     = IM_COL32(3, 5, 5, 148);
inline constexpr ImU32 kViewerCardBorder = IM_COL32(136, 118, 82, 126);

#include "ui/viewer/ViewerTaskChips.h"
#include "ui/viewer/ViewerActions.h"
#include "ui/viewer/ViewerHeader.h"
#include "ui/viewer/ViewerObjectiveList.h"
#include "ui/viewer/ViewerPanels.h"
#include "ui/viewer/ViewerModeBodies.h"
