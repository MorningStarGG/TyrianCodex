#pragma once
#include "model/Dataset.h"   // Zone
#include <vector>
#include <string>
#include <algorithm>

// Region-grouping helpers shared by the guide viewer's "head here next" list, the Zones tab, and the
// Checklist region rows. A "region" is a set of zones sharing a RegionId / RegionName. Centralizes the
// label-fallback + min-level math that was copied across those three sites.
namespace Regions
{
    // The display label for ONE zone's region: its RegionName, else "Region <RegionId>".
    inline std::string Label(const Zone& z)
    {
        return !z.RegionName.empty() ? z.RegionName : ("Region " + std::to_string(z.RegionId));
    }

    // The display label for a GROUP of zones: the first non-empty RegionName, else the front zone's fallback.
    inline std::string Label(const std::vector<const Zone*>& zs)
    {
        for (const Zone* z : zs) if (!z->RegionName.empty()) return z->RegionName;
        return zs.empty() ? std::string("Region") : Label(*zs.front());
    }

    // The lowest MinLevel across a group (used to order regions by level).
    inline int MinLevel(const std::vector<const Zone*>& zs)
    {
        int m = 99999;
        for (const Zone* z : zs) m = std::min(m, z->MinLevel);
        return zs.empty() ? 0 : m;
    }
}
