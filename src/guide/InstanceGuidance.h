#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "guide/TrailFollower.h"
#include "world/GpsArrow.h"   // GpsArrow + TargetOverrideKind (via app/State.h)
#include "util/Projection.h"       // Math::Vec3
#include <functional>

// -----------------------------------------------------------------------------------------------------
// InstanceGuidance: dungeon spot-to-spot guidance (the dungeon counterpart of Guidance). Stateless -- it
// reads GuideState's inst/instStep/instSteps and the active trail, advances past reached instruction spots,
// aims the arrow along the trail to the current one, and draws it with a "Step N / M" caption. The arrow's
// smoothed-speed ETA comes from Guidance (speedEma); the arrow menu action returns through `dispatch`.
// -----------------------------------------------------------------------------------------------------
namespace InstanceGuidance
{
    // The authoritative per-frame localization of the player onto the instance route. It projects onto nearby
    // trail SEGMENTS, not just vertices, so vertical/down-wall trails remain sticky between recorded points.
    // A global re-acquire is still allowed for respawns/teleports, but only after local projection is clearly bad.
    int Localize(GuideState& st, const Math::Vec3& avatar);

    // Inclusive active section bounds for a route index. Empty section metadata means legacy continuous route.
    void TrailSectionBounds(const GuideState& st, int index, int& lo, int& hi);

    void Update(float W, float H, const Math::Vec3& avatar, const Math::Vec3& camFwd,
                Config& cfg, GuideState& st, bool& settingsDirty, Follow::TrailFollower& follower,
                GpsArrow& gpsArrow, float speedEma, TargetOverrideKind targetKind, const std::function<void(int)>& dispatch);
}
