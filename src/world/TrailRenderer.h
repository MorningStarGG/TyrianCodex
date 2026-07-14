#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "util/Projection.h" // Math::Vec3

// -----------------------------------------------------------------------------------------------------
// TrailRenderer: draws the in-world ground-hugging footprint ribbon along the active route trail
// left/right vertex pairs per trail point, textured quads tinted by the trail color,
// distance-faded from the player and brightened ahead / dimmed behind. Owns its per-frame diagnostics
// (on-screen point count + nearest trail point/distance) that the Diagnostics tab + routing-debug readout
// display. Self-contained: the frame camera (FrameCtx), config + runtime state are passed by reference.
// -----------------------------------------------------------------------------------------------------
class TrailRenderer
{
public:
    // visLo/visHi clip which trail points render (inclusive corridor):
    // Trail mode + dungeons pass [0, INT_MAX] (whole ribbon); Hybrid/Direct pass [entrance, objective] so the
    // ribbon only "kicks in" from the approach-distance entrance to the current objective. Distance fade still
    // applies. visLo is FRACTIONAL (e.g. 142.6) so the corridor starts mid-segment and grows smoothly as the
    // approach distance changes, instead of snapping vertex-to-vertex.
    void Draw(const FrameCtx &fc, const Config &cfg, const GuideState &st, float visLo, int visHi);

    // Per-frame diagnostics (read by the Diagnostics tab + the routing-debug readout)
    int OnScreen() const { return onScreen_; }
    float NearestDist() const { return nearestDist_; }

private:
    int onScreen_ = 0;        // trail segment-quads emitted to the GPU buffer last frame (rough diagnostic)
    Math::Vec3 nearest_;      // the closest trail point to the player (horizontal)
    float nearestDist_ = 0.f; // distance to that closest point
};
