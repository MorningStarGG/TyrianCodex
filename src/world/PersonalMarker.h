#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "guide/Travel.h"         // Travel::Controller
#include "guide/TrailFollower.h"  // Follow::TrailFollower

// -----------------------------------------------------------------------------------------------------
// PersonalMarker: the in-world billboard for the Alt+click PERSONAL travel target -- an orange waypoint pin
// (matching its orange map pin, drawn via util/Draw DrawPersonalPin) at the clicked spot, height taken from
// the nearest route point. Only when the target is inside the active zone; always visible (no distance fade)
// but size-clamped. Stateless; needs travel (the target position) + follower (nearest-trail height) on top
// of the frame camera + config + state.
// -----------------------------------------------------------------------------------------------------
namespace PersonalMarker
{
    void Draw(const FrameCtx& fc, const Config& cfg, const GuideState& st,
              Travel::Controller& travel, Follow::TrailFollower& follower);
}
