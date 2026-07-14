#pragma once
#include <algorithm>
#include <cmath>

namespace Coords
{
    // Continent-rect helpers (rect = {left, top, right, bottom} continent coords). Used by the travel
    // system to test which zone a point is in / clamp a far point to this zone's border.
    inline float RectDistance(float cx, float cy, const float r[4])
    {
        const float minX = (std::min)(r[0], r[2]), maxX = (std::max)(r[0], r[2]);
        const float minY = (std::min)(r[1], r[3]), maxY = (std::max)(r[1], r[3]);
        const float dx = (std::max)((std::max)(minX - cx, cx - maxX), 0.f);
        const float dy = (std::max)((std::max)(minY - cy, cy - maxY), 0.f);
        return std::sqrt(dx * dx + dy * dy);
    }
    inline void ClampToRect(float cx, float cy, const float r[4], float &ox, float &oy)
    {
        const float minX = (std::min)(r[0], r[2]), maxX = (std::max)(r[0], r[2]);
        const float minY = (std::min)(r[1], r[3]), maxY = (std::max)(r[1], r[3]);
        ox = std::clamp(cx, minX, maxX);
        oy = std::clamp(cy, minY, maxY);
    }

    // GW2 continent (2D map) coordinate -> raw MumbleLink world HORIZONTAL plane (metres). Height isn't
    // in 2D data, so the caller supplies it (we use the nearest trail point's height). Ported from the builder's
    // geometry.py (the two MUST stay in sync). Validated statically against the dataset trail: continent [43728, 28590] ->
    // (-419.5, 440.2), which matches trail[34] = (-419.2, _, 439.9). X/Z are not negated or swapped (Y follows the map-rect convention).
    // contRect = {left, top, right, bottom}; mapRect = {minX, minY, maxX, maxY}.
    inline void ContinentToWorldXZ(float cx, float cy, const float contRect[4], const float mapRect[4],
                                   float &outX, float &outZ)
    {
        const float cl = contRect[0], ct = contRect[1], cr = contRect[2], cb = contRect[3];
        const float mMinX = mapRect[0], mMinY = mapRect[1], mMaxX = mapRect[2], mMaxY = mapRect[3];

        const float fracX = (cr != cl) ? (cx - cl) / (cr - cl) : 0.f;
        const float fracY = (cb != ct) ? (cy - ct) / (cb - ct) : 0.f; // 0 at continent-top, 1 at bottom

        const float mx = mMinX + fracX * (mMaxX - mMinX);
        const float my = mMaxY - fracY * (mMaxY - mMinY); // continent-top (north) -> map maxY

        const float InchesPerMeter = 39.3701f;
        outX = mx / InchesPerMeter;
        outZ = my / InchesPerMeter;
    }

    // Inverse of ContinentToWorldXZ: raw MumbleLink world HORIZONTAL plane (metres) -> GW2 continent (2D
    // map) coordinate. Used to draw the world-coord trail on the in-game map/compass, which work in the
    // continent coords MumbleLink's MapCenter uses.
    inline void WorldXZToContinent(float worldX, float worldZ, const float contRect[4], const float mapRect[4],
                                   float &outCX, float &outCY)
    {
        const float cl = contRect[0], ct = contRect[1], cr = contRect[2], cb = contRect[3];
        const float mMinX = mapRect[0], mMinY = mapRect[1], mMaxX = mapRect[2], mMaxY = mapRect[3];

        const float InchesPerMeter = 39.3701f;
        const float mx = worldX * InchesPerMeter;
        const float my = worldZ * InchesPerMeter;

        const float fracX = (mMaxX != mMinX) ? (mx - mMinX) / (mMaxX - mMinX) : 0.f;
        const float fracY = (mMaxY != mMinY) ? (mMaxY - my) / (mMaxY - mMinY) : 0.f; // inverse of the map-Y flip

        outCX = cl + fracX * (cr - cl);
        outCY = ct + fracY * (cb - ct);
    }
}
