#pragma once
#include <vector>
#include <cstddef>

// Header-only 2D polygon geometry. Added for the Zone Display sector test: which named sector (an area
// polygon in continent coords) contains the player's continent position. Mirrors the ray-cast test the
// Python builder already uses (gw2guide/zones.py _point_in_polygon). Vertices are stored INTERLEAVED
// (x0,y0,x1,y1,...) so a sector's bounds is one compact, cache-friendly float buffer.
namespace Geometry
{
    // Axis-aligned bounding-box quick-reject. b = {minX, minY, maxX, maxY}.
    inline bool InBBox(float px, float py, const float b[4])
    {
        return px >= b[0] && px <= b[2] && py >= b[1] && py <= b[3];
    }

    // Ray-cast (even-odd) point-in-polygon over an interleaved vertex list. vertCount = number of points
    // (xy has 2*vertCount floats). Returns false for degenerate (< 3) polygons.
    inline bool PointInPolygon(float px, float py, const float* xy, size_t vertCount)
    {
        if (!xy || vertCount < 3) return false;
        bool inside = false;
        for (size_t i = 0, j = vertCount - 1; i < vertCount; j = i++)
        {
            const float xi = xy[2 * i], yi = xy[2 * i + 1];
            const float xj = xy[2 * j], yj = xy[2 * j + 1];
            if (((yi > py) != (yj > py)) &&
                (px < (xj - xi) * (py - yi) / (yj - yi + 1e-12f) + xi))
                inside = !inside;
        }
        return inside;
    }

    inline bool PointInPolygon(float px, float py, const std::vector<float>& xy)
    {
        return PointInPolygon(px, py, xy.data(), xy.size() / 2);
    }
}
