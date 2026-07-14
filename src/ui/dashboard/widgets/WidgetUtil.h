#pragma once
#include "Shared.h"
#include "model/Dataset.h"
#include "model/ObjectiveTypes.h"
#include "util/Draw.h"
#include "util/Coords.h"
#include <cmath>
#include <string>

// Small inline helpers shared by the dashboard widgets (kept header-only + tiny so widgets stay decoupled).
namespace DashUtil
{
    // A widget body is "narrow" below this width -> it should render its COMPACT layout (content left-grouped /
    // stacked and sized to FILL the given width, never stretched edge-to-edge, never fixed-px). Used uniformly
    // so every widget reads cleanly at half-width AND when the panel collapses to a single narrow column. Tunable.
    inline constexpr float kNarrowWidth = 320.f;
    inline bool Narrow(float w) { return w < kNarrowWidth; }

    // Type string -> 0..4 (waypoint/poi/vista/hero/heart) or -1.
    inline int KindIdx(const std::string& type)
    {
        const Objective::Info* i = Objective::Get(type);
        return i ? KindIndex(i->Kind) : -1;
    }

    // Player position in CONTINENT coords (from raw MumbleLink world XZ via the active zone's rects).
    // Returns false when not in a rect-mapped zone or the link is unavailable.
    inline bool PlayerContinent(const Zone& z, float& cx, float& cy)
    {
        if (!MumbleLink || !z.HasRects) return false;
        Coords::WorldXZToContinent(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z, z.ContRect, z.MapRect, cx, cy);
        return true;
    }

    inline float ContinentDist(float ax, float ay, float bx, float by)
    {
        const float dx = ax - bx, dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Continent units (inches) -> metres for display.
    inline float ContToMeters(float cont) { return cont / 39.3701f; }

    inline const char* TypeLabel(int kindIdx)
    {
        switch (kindIdx) { case 0: return "Waypoints"; case 1: return "POIs"; case 2: return "Vistas";
                           case 3: return "Hero Points"; case 4: return "Hearts"; default: return "Other"; }
    }
}
