#include "guide/ZoneManager.h"
#include "util/Coords.h"   // WorldXZToContinent
#include "Shared.h"
#include "ui/dashboard/Notify.h"   // zone-entry notifications (was GUI_SendAlert)
#include <algorithm>

void ZoneManager::Init(GuideState* st, const Config* cfg, std::map<uint32_t, Zone>* zones,
                       std::map<uint32_t, Instance>* instances, Follow::TrailFollower* follower, GpsArrow* gpsArrow,
                       std::map<uint32_t, Instance>* farming, std::map<uint32_t, FishMap>* fishHoles)
{
    st_ = st; cfg_ = cfg; zones_ = zones; instances_ = instances; follower_ = follower; gpsArrow_ = gpsArrow;
    farming_ = farming; fishHoles_ = fishHoles;
}

void ZoneManager::SetDataDirs(const std::string& zonesDir, const std::string& instancesDir, const std::string& fishingDir)
{
    zonesDir_ = zonesDir; instancesDir_ = instancesDir; fishingDir_ = fishingDir;
}

void ZoneManager::EnsureActiveGather(uint32_t mapId)
{
    if (!st_ || mapId == st_->activeGatherMapId) return;     // rebuild only on map change
    st_->activeGatherMapId = mapId;
    st_->activeGather.clear();
    st_->farmGathered.clear();   // the cross-off holds the old map's node pointers -- a new map starts fresh
    if (!farming_) return;
    auto it = farming_->find(mapId);
    if (it == farming_->end()) return;

    // Continent rects (for the map/minimap pins) come from the bundled OPEN-WORLD zone of this map -- always
    // present for a real zone, and independent of whether a guide route is loaded. World coords project directly.
    const Zone* z = nullptr;
    if (zones_) { auto zi = zones_->find(mapId); if (zi != zones_->end() && zi->second.HasRects) z = &zi->second; }

    const Instance& fm = it->second;
    st_->activeGather.reserve(fm.Nodes.size());
    for (const FarmNode& nd : fm.Nodes)
    {
        GuideState::ActiveGatherNode a;
        a.X = nd.X; a.Y = nd.Y; a.Z = nd.Z; a.Node = &nd;
        if (z)
        {
            Coords::WorldXZToContinent(nd.X, nd.Z, z->ContRect, z->MapRect, a.CX, a.CY);
            a.HasCont = true;
        }
        st_->activeGather.push_back(a);
    }
}

void ZoneManager::EnsureActiveFishHoles(uint32_t mapId)
{
    if (!st_ || mapId == st_->activeFishMapId) return;       // rebuild only on map change
    st_->activeFishMapId = mapId;
    st_->activeFish.clear();
    st_->fishVisited.clear();    // the cross-off holds the old map's hole pointers -- a new map starts fresh
    if (!fishHoles_) return;
    auto it = fishHoles_->find(mapId);
    if (it == fishHoles_->end()) return;
    if (!it->second.GeomLoaded) LoadFishMapFull(fishingDir_, it->second);   // lazy: pull holes + rects in on entry

    // Continent rects are bundled WITH the fishing data (not read from a loaded zone), so even maps we don't ship
    // as guided zones still get map/minimap pins. World coords project directly for the in-world billboards.
    const FishMap& fm = it->second;
    st_->activeFish.reserve(fm.Holes.size());
    for (const FishHole& h : fm.Holes)
    {
        GuideState::ActiveFishHole a;
        a.X = h.X; a.Y = h.Y; a.Z = h.Z; a.Hole = &h;
        if (fm.HasRects)
        {
            Coords::WorldXZToContinent(h.X, h.Z, fm.ContRect, fm.MapRect, a.CX, a.CY);
            a.HasCont = true;
        }
        st_->activeFish.push_back(a);
    }
}

void ZoneManager::RebuildActiveTrail()
{
    std::vector<Follow::Vec2> poly;
    poly.reserve(st_->zone.Trail.size());
    st_->trailCont.clear();
    if (st_->zone.HasRects) st_->trailCont.reserve(st_->zone.Trail.size());
    for (const Math::Vec3& p : st_->zone.Trail)
    {
        poly.push_back({ p.x, p.z });
        if (st_->zone.HasRects)
        {
            float cx, cy;
            Coords::WorldXZToContinent(p.x, p.z, st_->zone.ContRect, st_->zone.MapRect, cx, cy);
            st_->trailCont.push_back(ImVec2(cx, cy));
        }
    }
    std::vector<std::pair<int, int>> sections;
    sections.reserve(st_->zone.TrailSections.size());
    for (const TrailSectionRange& s : st_->zone.TrailSections)
        sections.push_back({s.Start, s.End});
    follower_->SetTrail(std::move(poly), std::move(sections));
    follower_->Reset();
}

void ZoneManager::ApplyPathType()
{
    if (!st_->zone.Loaded) return;
    st_->zone.Activate(cfg_->pathType);
    RebuildActiveTrail();
    gpsArrow_->ResetEase();
}

void ZoneManager::SwitchZone(uint32_t mapId)
{
    if (mapId == st_->activeMapId) return;
    st_->activeMapId = mapId;
    st_->manualTarget.clear();
    gpsArrow_->ResetEase();
    st_->inDungeon = false; st_->inst = nullptr;            // an open-world map, not a dungeon
    st_->instTrailSections.clear();
    st_->instTrailReacquireFrames = 0;

    auto it = zones_->find(mapId);
    if (it == zones_->end())
    {
        st_->zone = Zone{};                              // uncovered map -> no active guide
        st_->trailCont.clear();
        follower_->SetTrail(std::vector<Follow::Vec2>{});
        follower_->Reset();
        return;
    }
    if (!it->second.GeomLoaded) LoadZoneTrail(zonesDir_, it->second);   // lazy: pull the trail side-file in on entry
    st_->zone = it->second;                              // activate this zone
    st_->zone.Activate(cfg_->pathType);                  // pick the requested route variant before building
    RebuildActiveTrail();                             // follower (x,z) poly + continent poly for the map

    // "Announce zone on entry" (GuideModule.Zones.cs toast): a coalescing Zone notification (gated by the
    // Notifications > Type: Zone toggle inside Notify::Push, so rapid zone hops show one updating toast).
    {
        char lvl[48];
        std::snprintf(lvl, sizeof(lvl), "Level %d-%d", st_->zone.MinLevel, st_->zone.MaxLevel);
        Notify::Push(Notify::Kind::Zone, "Now guiding: " + st_->zone.Name, lvl);
    }
}

void ZoneManager::ActivateInstanceRoute(int idx)
{
    st_->instSteps.clear();
    st_->instStep = 0;
    st_->instTrailAnchor = -1;   // new route -> re-localize from scratch (Localize does a global acquire)
    st_->instTrailReacquireFrames = 0;
    gpsArrow_->ResetEase();
    if (!st_->inst || st_->inst->Routes.empty())
    {
        st_->zone = Zone{}; st_->trailCont.clear(); st_->instTrailSections.clear();
        follower_->SetTrail(std::vector<Follow::Vec2>{}); follower_->Reset();
        return;
    }
    st_->instRouteIdx = std::clamp(idx, 0, (int)st_->inst->Routes.size() - 1);
    st_->instRouteChoice[st_->inst->MapId] = st_->instRouteIdx;
    const InstanceRoute& r = st_->inst->Routes[st_->instRouteIdx];

    // Load the route trail into st_->zone (world coords; HasRects=false -> no continent transform / map trail).
    st_->zone = Zone{};
    st_->zone.Loaded   = true;
    st_->zone.MapId    = st_->inst->MapId;
    st_->zone.Name     = st_->inst->Name;
    st_->zone.HasRects = false;
    st_->zone.Trail    = r.Trail;
    st_->instTrailSections = r.Sections;
    st_->trailCont.clear();                              // dungeons aren't on the continent map
    std::vector<Follow::Vec2> poly; poly.reserve(r.Trail.size());
    for (const Math::Vec3& p : r.Trail) poly.push_back({ p.x, p.z });
    std::vector<std::pair<int, int>> sections;
    sections.reserve(r.Sections.size());
    for (const TrailSectionRange& s : r.Sections)
        sections.push_back({ s.Start, s.End });
    follower_->SetTrail(std::move(poly), std::move(sections));
    follower_->Reset();

    for (const InstanceMarker& m : r.Markers) if (!m.Text.empty()) st_->instSteps.push_back(&m);
}

void ZoneManager::SwitchInstance(uint32_t mapId)
{
    if (mapId == st_->activeMapId) return;
    st_->activeMapId = mapId;
    st_->manualTarget.clear();
    st_->inDungeon = true;
    auto it = instances_->find(mapId);
    if (it != instances_->end() && !it->second.GeomLoaded)
        LoadInstanceFull(instancesDir_, it->second);     // lazy: pull the full route geometry in on entry
    st_->inst = (it != instances_->end()) ? &it->second : nullptr;
    ActivateInstanceRoute(st_->instRouteChoice.count(mapId) ? st_->instRouteChoice[mapId] : 0);

    if (st_->inst)
    {
        const char* rn = (st_->instRouteIdx < (int)st_->inst->Routes.size()) ? st_->inst->Routes[st_->instRouteIdx].Name.c_str() : "";
        Notify::Push(Notify::Kind::Zone, "Now guiding: " + st_->inst->Name, rn ? rn : "");
    }
}

const std::vector<const Zone*>& ZoneManager::RecommendZones(int level) const
{
    const size_t count = zones_ ? zones_->size() : 0;
    if (level == recLevel_ && count == recCount_) return recCache_;   // unchanged -> reuse (no rebuild/sort/alloc)

    recCache_.clear();
    if (zones_)
        for (const auto& kv : (*zones_))
            // In-band only: your level sits WITHIN the zone's range (level-appropriate). level<=0 (no character
            // level yet) -> all zones, lowest-first. Completion is filtered by callers (ZoneComplete).
            if (level <= 0 || (kv.second.MinLevel <= level && level <= kv.second.MaxLevel)) recCache_.push_back(&kv.second);

    if (level <= 0)
        std::sort(recCache_.begin(), recCache_.end(), [](const Zone* a, const Zone* b) {
            if (a->MinLevel != b->MinLevel) return a->MinLevel < b->MinLevel;
            if (a->MaxLevel != b->MaxLevel) return a->MaxLevel < b->MaxLevel;
            return a->Name < b->Name;
        });
    else
        std::sort(recCache_.begin(), recCache_.end(), [level](const Zone* a, const Zone* b) {
            const bool aIn = level <= a->MaxLevel, bIn = level <= b->MaxLevel;
            if (aIn != bIn)                 return aIn;                  // in-band first
            if (a->MaxLevel != b->MaxLevel) return a->MaxLevel > b->MaxLevel;  // then highest band
            return a->Name < b->Name;
        });
    recLevel_ = level; recCount_ = count;
    return recCache_;
}
