#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "guide/Progress.h" // ProgressStore

// On-screen marker pixel-size clamp (WorldMarkers.Min/MaxPixels). Shared by the world, dungeon and personal
// marker renderers.
inline constexpr float kMarkerMinPx = 18.f;
inline constexpr float kMarkerMaxPx = 110.f;

// -----------------------------------------------------------------------------------------------------
// Markers: in-world objective + dungeon marker billboards. The real gw2dat icon
// for each objective type (or the FvD sprite for a dungeon spot), continent/world coord -> screen, distance-
// faded and size-clamped; completed objectives drop out. Stateless renderers -- the frame camera (FrameCtx),
// config + runtime state (+ progress for completion) are passed by reference.
// -----------------------------------------------------------------------------------------------------
namespace Markers
{
    void DrawWorld(const FrameCtx &fc, const Config &cfg, const GuideState &st, ProgressStore &progress); // open-world objective icons
    void DrawInstance(const FrameCtx &fc, const Config &cfg, const GuideState &st);                       // dungeon route markers
}
