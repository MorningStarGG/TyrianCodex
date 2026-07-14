#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "model/Dataset.h" // Zone, Instance
#include "guide/TrailFollower.h"
#include "world/GpsArrow.h" // GpsArrow
#include <cstdint>
#include <map>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// ZoneManager: owns the active-route lifecycle. Switches the active dataset on
// map change (open-world zone or dungeon), activates the foot/mount path variant, rebuilds the follower +
// continent trail, and recommends zones for a level. Init'd (Travel::Controller-style) with pointers to the
// runtime state, config, the bundled datasets, the follower, and the arrow (whose ease it re-snaps on a
// switch). Operates mostly on those references; its only owned state is the RecommendZones cache and the
// dataset directory paths.
// -----------------------------------------------------------------------------------------------------
class ZoneManager
{
public:
    void Init(GuideState *st, const Config *cfg, std::map<uint32_t, Zone> *zones,
              std::map<uint32_t, Instance> *instances, Follow::TrailFollower *follower, GpsArrow *gpsArrow,
              std::map<uint32_t, Instance> *farming = nullptr,
              std::map<uint32_t, FishMap> *fishHoles = nullptr);

    // The dataset dirs used to lazy-load the heavy geometry a stub map defers (zone trail / instance routes / fishing
    // holes) the first time that map is entered. Set once at AddonLoad, before the first Render.
    void SetDataDirs(const std::string &zonesDir, const std::string &instancesDir, const std::string &fishingDir);

    void SwitchZone(uint32_t mapId);     // activate the open-world zone for mapId (no-op if unchanged)
    void SwitchInstance(uint32_t mapId); // enter a dungeon map (pick the remembered / first path)
    void ActivateInstanceRoute(int idx); // switch the active dungeon path
    void ApplyPathType();                // re-activate the current zone for the foot/mount setting

    // Prepare the active map's farming nodes (world + continent coords) for the overlay + combined run. No-op
    // unless mapId changed; clears when the map has no bundled farming dataset. Cheap to call every frame.
    void EnsureActiveGather(uint32_t mapId);

    // Prepare the active map's fishing holes (world + continent coords) for the hole overlay + nearest-hole run.
    // Continent coords come from the bundled FishMap rects, so even non-zone fishing maps get map/minimap pins.
    void EnsureActiveFishHoles(uint32_t mapId);

    // zones at/below the level (or all, lowest-first). Cached behind (level, zone-count) -> recomputed only when
    // the character's level changes (callers hit this every frame); returns a stable reference, no per-frame copy.
    const std::vector<const Zone *> &RecommendZones(int level) const;

private:
    void RebuildActiveTrail(); // follower (x,z) poly + continent poly from the active trail

    mutable int recLevel_ = -2;                  // cache token: last level RecommendZones built for
    mutable size_t recCount_ = (size_t)-1;       // cache token: zone-map size (datasets load incrementally)
    mutable std::vector<const Zone *> recCache_; // cached recommendation list

    std::string zonesDir_, instancesDir_, fishingDir_; // for lazy geometry loads on map entry

    GuideState *st_ = nullptr;
    const Config *cfg_ = nullptr;
    std::map<uint32_t, Zone> *zones_ = nullptr;
    std::map<uint32_t, Instance> *instances_ = nullptr;
    std::map<uint32_t, Instance> *farming_ = nullptr;
    std::map<uint32_t, FishMap> *fishHoles_ = nullptr;
    Follow::TrailFollower *follower_ = nullptr;
    GpsArrow *gpsArrow_ = nullptr;
};
