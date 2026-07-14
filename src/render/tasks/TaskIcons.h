#pragma once
#include "model/Dataset.h"   // TaskChip (a heart objective's task pill)
#include <imgui.h>
#include <string>

// Procedural task-objective icon art (kill / collect / talk / ...), drawn from a heart task's category +
// keyword. Kept in render/ so viewer/dashboard heart task chips share one draw implementation.
ImU32 TaskChipAccent(const std::string& category, ImU32 fallback);                                   // category -> accent colour
void  DrawTaskIcon(ImDrawList* dl, ImVec2 c, float size, const TaskChip& chip, ImU32 accent, bool expanded);
