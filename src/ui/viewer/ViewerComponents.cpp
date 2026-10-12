#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "render/objectives/ObjectiveIcons.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"
#include "ui/ZoneRow.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/Regions.h"
#include "guide/StepStatus.h"
#include "util/Draw.h"
#include "util/Textures.h"
#include "model/ObjectiveTypes.h"
#include "Shared.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Shared viewer primitives. Everything else moved into the per-concern modules that ViewerComponents.h
// (the umbrella header) re-includes, so the 3 consumers keep including just "ViewerComponents.h".

ImU32 WithAlpha(ImU32 col, int alpha)
{
    return Gw2Ui::Alpha(col, alpha);   // one implementation (the viewer keeps this short alias for brevity)
}

void DrawWrappedText(const char* text, float fs, ImU32 col, bool stroke, ImFont* font)
{
    Gw2Ui::LabelWrapped(text, col, stroke, font, fs);
}

void PaintTypeIconAt(ImDrawList* dl, const std::string& type, ImVec2 p, float size, bool medallion)
{
    Render::PaintObjectiveTypeIconAt(dl, type, p, size, medallion);
}
