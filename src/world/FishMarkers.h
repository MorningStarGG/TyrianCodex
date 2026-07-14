#pragma once
#include "world/Markers.h"        // FrameCtx, kMarkerMinPx/MaxPx, Config, GuideState
#include <functional>
#include <imgui.h>

// -----------------------------------------------------------------------------------------------------
// FishMarkers: the FISHING-hole overlay (mirrors world/CategoryMarkers for farming). Draws the active map's
// bundled fishing holes (State.activeFish, prepared once per map change) as in-world billboards + flat map/minimap
// pins, tinted by hole kind (FishHoles::Color) with the Fish glyph. State.fishWatchTypes (the hole kinds the
// FishingTab watchlist lights up, computed once/frame) dims holes NOT of a watched kind so the tracked fish's
// holes stand out -- empty means show all holes at full strength. Independent of the guide route; shows whenever
// Config.fishShow / fishOnMap / fishOnMinimap is on.
// -----------------------------------------------------------------------------------------------------
namespace FishMarkers
{
    void DrawWorld(const FrameCtx& fc, const Config& cfg, const GuideState& st);   // 3D billboards
    void DrawMap(const Config& cfg, const GuideState& st,
                 const std::function<ImVec2(ImVec2)>& project, ImVec2 bMin, ImVec2 bSize);
}
