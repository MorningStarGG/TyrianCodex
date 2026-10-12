#pragma once
#include "app/App.h"
#include "model/Dataset.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>

// Shared viewer primitives + UMBRELLA. The shared in-world guide-viewer drawing helpers used to all live in
// one ViewerComponents.cpp; they now split into per-concern modules under ui/viewer/. This header
// keeps the three primitives below AND re-includes every module header, so the consumers (GuideViewer.cpp,
// ViewerStepCompact.cpp, ViewerStepRich.cpp) keep including just "ViewerComponents.h" and see everything.

ImU32 WithAlpha(ImU32 col, int alpha);
void DrawWrappedText(const char* text, float fs, ImU32 col, bool stroke = false, ImFont* font = nullptr);
void PaintTypeIconAt(ImDrawList* dl, const std::string& type, ImVec2 p, float size, bool medallion);

// Fixed and auto-sized viewer detail frames use the framework's shared detail surface.
inline constexpr ImU32 kViewerCardBg = Gw2Ui::kDetailCardBg;
inline constexpr ImU32 kViewerCardBorder = Gw2Ui::kDetailCardBorder;

#include "ui/viewer/ViewerTaskChips.h"
#include "ui/viewer/ViewerActions.h"
#include "ui/viewer/ViewerHeader.h"
#include "ui/viewer/ViewerObjectiveList.h"
#include "ui/viewer/ViewerPanels.h"
#include "ui/viewer/ViewerModeBodies.h"
