#pragma once
#include "guide/GuidanceSnapshot.h"
#include <imgui.h>

namespace Render
{
    // The direct-objective glow's look/behavior. Each surface (in-world arrow, route-arrow widget, Info Panel
    // datatext) fills this from ITS OWN settings, so the chevron / ping / timing are independent per surface.
    struct DirectIndicatorStyle
    {
        bool chevron   = true;    // a mini chevron riding the arc, apex at the objective
        bool ping      = false;   // flash periodically instead of a constant glow
        int  everySecs = 5;       // ping cadence (full on+off cycle), seconds
        int  showSecs  = 2;       // how long each ping stays visible, seconds
    };

    // showDirect: also draw the Hybrid direct-objective glow when the snapshot carries a direct bearing
    // (guidance.hasDirect). The glow rides the dial's outer rim and scales with `size`.
    void DrawRouteArrow(ImDrawList* dl, ImVec2 center, float size, const GuidanceSnapshot& guidance,
                        bool showDirect = false, const DirectIndicatorStyle& style = {});

    // The Hybrid direct-objective glow: a glowing arc (+ optional chevron, + optional ping) centered on
    // `center`, at screen rotation `directRot` (0 = the arrow's "up"), on a ring of radius `ringR`. The arc
    // thickness + chevron scale with `ringR`, so it tracks the arrow's size. Shared by all arrow surfaces.
    void DrawDirectIndicator(ImDrawList* dl, ImVec2 center, float ringR, float directRot,
                             const DirectIndicatorStyle& style);
}
