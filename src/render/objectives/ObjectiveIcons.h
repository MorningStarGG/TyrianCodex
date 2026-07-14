#pragma once
#include <imgui.h>
#include <string>

namespace Render
{
    void PaintObjectiveTypeIconAt(ImDrawList* dl, const std::string& type, ImVec2 topLeft,
                                  float size, bool medallion);
}
