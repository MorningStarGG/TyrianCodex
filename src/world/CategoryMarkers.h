#pragma once
#include "world/Markers.h"        // FrameCtx (via app/State.h), kMarkerMinPx/MaxPx, Config, GuideState
#include <functional>
#include <imgui.h>

// -----------------------------------------------------------------------------------------------------
// CategoryMarkers: the FARMING gathering-node overlay. Draws the active map's bundled gatherable nodes
// (State.activeGather, prepared once per map change) as in-world billboards + flat map/minimap pins, filtered
// by the shared per-map enabled-type set (Config::GatherShown). Reuses the Markers::DrawInstance billboard
// recipe (world coords, distance pre-cull, size/fade clamp, bundled icon or category dot). Independent of the
// guide route -- it shows on any map that has a farming dataset whenever Config.gatherShow is on.
// -----------------------------------------------------------------------------------------------------
namespace CategoryMarkers
{
    void DrawWorld(const FrameCtx& fc, const Config& cfg, const GuideState& st);   // 3D billboards
    // Flat map/minimap pins. `project` maps a continent coord -> screen (the MapRenderer lambda); bMin/bSize are
    // the surface bounds for off-screen culling.
    void DrawMap(const Config& cfg, const GuideState& st,
                 const std::function<ImVec2(ImVec2)>& project, ImVec2 bMin, ImVec2 bSize);
}
