#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "util/Projection.h"

struct TaskChip
{
    std::string Label;
    std::string Category;
    int Count = 0;
    std::vector<std::string> Examples;
    std::vector<std::string> Progress;
};

// A 2D continent-coordinate point (renown-heart area polygon vertex). Continent space, like Step::CX/CY.
struct Pt2
{
    float x = 0.f, y = 0.f;
};

// One objective (heart / poi / vista / hero / waypoint). `CX,CY` are 2D GW2 continent coords;
// `TrailIndex` points at the route point nearest it (we take the marker's height from there).
struct Step
{
    std::string Type;
    std::string Name;
    std::string StepId; // "{mapId}:{type}:{objectiveId}" - stable progress key (survives rebuilds)
    float CX = 0.f, CY = 0.f;
    int TrailIndex = -1;
    int Order = 0; // authored route order (steps are sorted by this on load)
    int RecommendedLevel = -1;
    std::string WikiText;            // prose summary (every objective)
    std::string WhatToDo;            // quick-actions: heart checklist -> "Tasks:", hero answers -> "Answer:"
    std::string ChatLink;            // this objective's chat link [&...] (waypoints) - travel: copy to clipboard
    std::string WaypointChatLink;    // nearest waypoint's chat link (on non-waypoint steps) - travel onward
    std::vector<TaskChip> TaskChips; // bundled wiki-derived heart task chips (offline, generated)
    std::vector<Pt2> Bounds;         // renown-heart area polygon (continent coords); empty for non-hearts / no data
};

// A step's slot in an alternate route: its order along that route + the nearest trail-point index.
struct RouteStepInfo
{
    int Order = 0;
    int TrailIndex = -1;
};

// One source-authored mount/glider marker on an open-world route.
struct RouteMarker
{
    float X = 0.f, Y = 0.f, Z = 0.f; // world position ([x, height, z], matches MumbleLink)
    int TrailIndex = -1;
    std::string Mount; // display name ("Springer", "Roller Beetle", "Glider")
    std::string Icon;  // bundled icon basename (data/textures/markers/mounts/<icon>.png)
};

struct TrailSectionRange
{
    int Start = 0; // inclusive index into a route Trail
    int End = 0;   // inclusive index into a route Trail
};

struct RouteTransitionLink
{
    std::string Label;
    std::string ChatLink;
};

// A source-authored waypoint transfer inside a route. Lady Elyssa uses these to split a route into
// efficient trail sections connected by teleports; they are not map-completion waypoint objectives.
struct RouteTransition
{
    std::string Id;
    std::string SourceBranch;
    std::string SourceType;
    std::string SourceFile;
    std::string Message;
    uint32_t MapId = 0;
    float X = 0.f, Y = 0.f, Z = 0.f; // author marker world position
    float CX = 0.f, CY = 0.f;        // same point in continent coords for map assist fallback
    int TrailIndex = -1;
    int SectionBefore = -1;
    int SectionAfter = -1;
    std::vector<RouteTransitionLink> Links;
};

// One route variant for a zone: its own trail polyline plus a per-stepId {order, trailIndex}
// map that re-sequences the shared steps along that trail.
struct RouteOverlay
{
    std::string Id;                             // "tekkit-foot", "lady-mount", ...
    std::string Label;                          // user-facing route label
    std::string Author;                         // "tekkit" / "lady"
    std::string Kind;                           // "foot" or "mount"
    std::string Source;                         // builder source id ("tekkit:barefoot")
    std::string SourceBranch;                   // original pack route branch
    bool Complete = true;                       // route has ordering metadata for every objective
    std::vector<Math::Vec3> Trail;              // [x, y, z] world points
    std::vector<TrailSectionRange> Sections;     // explicit route sections; empty = one continuous legacy route
    std::map<std::string, RouteStepInfo> Steps; // stepId -> {order, trailIndex}
    std::vector<RouteMarker> RouteMarkers;      // source-authored route-use markers for this route
    std::vector<RouteTransition> RouteTransitions; // source-authored waypoint transfers for this route
};

// One open-world zone dataset. The `trail` array is already in raw MumbleLink world coordinates
// (Y up, metres), so points feed straight to the projection; steps use 2D continent coords +
// the zone's rects (continent->world via Coords::ContinentToWorldXZ).
struct Zone
{
    bool Loaded = false;
    bool GeomLoaded = false; // the <slug>.trail.json (trail + legacy altRoute + routeVariants) has been lazy-loaded
    std::string Slug;        // key for the lazy trail side-file (data/zones/<slug>.trail.json)
    uint32_t MapId = 0;
    int ContinentId = 1;    // GW2 continent id: 1 = Tyria, 2 = Mists (Maguuma/Cantha are regions on 1); travel filters same-continent
    int Floor = 1;          // map floor for the tile server (Zones-tab thumbnail)
    int RegionId = 0;       // groups zones in the "head here next" / Recommend lists
    std::string RegionName; // e.g. "Kryta" (region header label)
    std::string Release;    // release key: "core"/"hot"/"pof"/"lws3"/... (Story tab grouping)
    std::string Name;
    int MinLevel = 0, MaxLevel = 0;
    std::vector<Math::Vec3> Trail; // the ACTIVE route's trail (Activate() swaps this)
    std::vector<TrailSectionRange> TrailSections; // the ACTIVE route's explicit trail sections
    std::vector<Step> Steps;       // the ACTIVE route's steps (Activate() swaps this)
    std::vector<RouteMarker> RouteMarkers; // the ACTIVE route's source-authored route-use markers
    std::vector<RouteTransition> RouteTransitions; // the ACTIVE route's authored waypoint transfers
    bool HasRects = false;
    float MapRect[4] = {};  // minX, minY, maxX, maxY
    float ContRect[4] = {}; // left, top, right, bottom
    bool HasSectorRect = false;
    float SectorRect[4] = {}; // union of named-sector bounds = tight playable extent (hover-map crop)

    // Route variants. Trail/Steps above are the ACTIVE route; Activate() swaps them between schema 5
    // source-aware routeVariants when present, or the legacy primary/altRoute pair when loading old data.
    std::string PrimaryKind = "foot"; // kind the as-loaded Trail/Steps represent
    bool PrimaryComplete = true;      // primary route has ordering metadata for every objective
    bool HasAlt = false;              // the other-kind variant exists
    RouteOverlay Alt;                 // that other variant (trail + per-stepId order/index)
    std::string ActiveKind;           // kind currently in Trail/Steps (set by Activate)
    std::string ActiveRouteId;        // route variant currently in Trail/Steps
    std::string ActiveRouteLabel;     // user-facing active route label
    std::string ActiveRouteAuthor;    // "tekkit" / "lady" / ""
    bool ActiveRouteFallback = false; // requested preference was unavailable on this map
    bool ActiveComplete = true;       // active route has ordering metadata for every objective
    std::vector<RouteOverlay> RouteVariants; // schema 5+: source-aware route variants
    // internal swap state (don't touch directly): captured primary + lazily-built alt steps.
    bool _primaryCaptured = false;
    std::vector<Math::Vec3> _primaryTrail;
    std::vector<TrailSectionRange> _primaryTrailSections;
    std::vector<Step> _primarySteps;
    std::vector<RouteMarker> _primaryRouteMarkers;
    std::vector<RouteTransition> _primaryRouteTransitions;
    std::vector<Step> _altSteps;
    bool _altBuilt = false;

    // Legacy path: make the requested kind ("foot"/"mount") active from primary/altRoute data.
    void Activate(const std::string &preferredKind);
    // Make the requested route preference active. The saved preference is not mutated; unavailable routes
    // fall back at runtime to another route variant for this map.
    void Activate(int routePreference);
};

// Parse data/zones/<slug>.json into `out`. Returns true if a trail loaded.
bool LoadZone(const std::string &path, Zone &out);

// Load every zone listed in <zonesDir>/index.json into `out`, keyed by mapId. Returns the count loaded.
// Each zone is loaded METADATA-ONLY (meta + steps); the heavy trail geometry stays on disk until LoadZoneTrail.
int LoadAllZones(const std::string &zonesDir, std::map<uint32_t, Zone> &out);

// Lazy-load a zone's trail + route overlays from <zonesDir>/<z.Slug>.trail.json into an already-metadata-loaded Zone
// (sets z.GeomLoaded). Called on map entry. No-op-safe: returns true if the geometry is present after the call.
bool LoadZoneTrail(const std::string &zonesDir, Zone &z);

// -- Dungeons / instances (schemaVersion 4) ----------------------------------------------------------
// Unlike open-world zones, instance routes are already in WORLD coords (matching MumbleLink) - no
// continent transform. Markers carry the spot-to-spot instruction text + an icon/tint; markers with text
// drive the guidance, positional-only markers (no text) are just in-world icons.

// One dungeon instruction / positional marker (world coords). Text empty = positional-only.
struct InstanceMarker
{
    float X = 0.f, Y = 0.f, Z = 0.f; // world position ([x, height, z], matches MumbleLink)
    int TrailIndex = -1;             // nearest route-trail point (the arrow aims here)
    std::string Text;                // instruction ("Do this:"); empty = positional-only
    std::string Kind;                // e.g. "info"
    std::string Icon;                // icon id/name
    unsigned int Tint = 0xFFFFFFFFu; // 0xRRGGBBAA from the dataset "#RRGGBB"
    // Farming extensions (empty/0 for dungeons):
    std::string Material;     // canonical material/type ("iron")
    std::string ChatLink;     // waypoint chat code ("[&...]"), for copy
    std::string Role;         // "node" / "wp" / "start" / "end"
    std::string Mount;        // required mount display ("Skyscale"); "" = foot-reachable
    uint32_t IconAssetId = 0; // gw2dat asset id when no bundled icon
};

// One named route (a dungeon path, or a single-material farming trail): world-coord trail + ordered markers.
struct InstanceRoute
{
    std::string Name;
    std::vector<Math::Vec3> Trail; // [x, height, z] world points (feeds the follower + ribbon)
    std::vector<TrailSectionRange> Sections; // explicit .trl sections; empty means one continuous legacy section
    std::vector<InstanceMarker> Markers;
    // Farming extensions (empty for dungeons):
    std::string Category; // "ore"/"wood"/"plant"/"seafood"/"special"/"mixed"/"farmroute"
    std::string Material; // canonical material ("iron"); "" for theme routes
    std::string Source;   // pack the route came from
};

// A standalone gatherable node (farming overlay / combined-nearest run). World coords.
struct FarmNode
{
    float X = 0.f, Y = 0.f, Z = 0.f;
    std::string Material;     // canonical material/type ("iron")
    std::string Category;     // ore/wood/plant/seafood/special/misc/farmroute
    std::string Icon;         // bundled icon basename (data/textures/markers/gather/<icon>.png)
    std::string ChatLink;     // waypoint chat code, if this node is a waypoint
    std::string Mount;        // required mount display ("Skyscale"); "" = foot-reachable
    uint32_t IconAssetId = 0; // gw2dat asset id when no bundled icon
};

// One content map (a dungeon STORY/EXPLORABLE map, or a farming map): named routes + (farming) gatherable nodes.
struct Instance
{
    bool Loaded = false;
    bool GeomLoaded = false; // full per-slug file (route trails/markers) lazy-loaded in (dungeon/story)
    uint32_t MapId = 0;
    std::string Name;   // "Ascalonian Catacombs" / "Queensdale"
    std::string Mode;   // route behavior: "story" / "explorable" / "farming"
    std::string Family; // Content-tab scope TYPE: "dungeon" / "story" / "tutorial" / "home" ("" = farming)
    std::vector<InstanceRoute> Routes;
    std::string Release; // release key ("core"/"hot"/...) for the Story-tab expansion pill ("" = dungeon)
    // Farming extensions (empty for dungeons):
    std::string Slug;
    std::string RegionName;
    std::vector<FarmNode> Nodes; // all gatherables on this map (overlay + combined-nearest)
};

// Load every instance from <instancesDir>/index.json into `out` as METADATA STUBS (name/mode/family/release/region
// + route names, no trail geometry), keyed by mapId. The full per-slug file loads on entry via LoadInstanceFull.
int LoadAllInstances(const std::string &instancesDir, std::map<uint32_t, Instance> &out);

// Lazy-load a stub instance's full per-slug file (<instancesDir>/<inst.Slug>.json -> route trails + markers) in
// place (sets inst.GeomLoaded, preserves the stub's Slug). Called on entering the instance. Returns success.
bool LoadInstanceFull(const std::string &instancesDir, Instance &inst);

// Load every farming map listed in <farmingDir>/index.json into `out`, keyed by mapId. Returns the count.
// Reuses the Instance struct (routes + nodes); a farming Instance has Mode "farming".
int LoadAllFarming(const std::string &farmingDir, std::map<uint32_t, Instance> &out);

// One in-world fishing hole: raw Y-up MumbleLink world coords (project directly) +
// the hole kind (the mmm.fishing.holes.<type> suffix, matched to a fish's "hole" metadata at runtime).
struct FishHole
{
    float X = 0.f, Y = 0.f, Z = 0.f;
    std::string Type; // "lake" / "river" / "offshore" / "coastal" / ...
};

// One map's fishing holes + the continent/map rects (bundled so even maps we don't ship as zones get map pins).
struct FishMap
{
    bool GeomLoaded = false; // full per-slug file (holes + rects) lazy-loaded in
    uint32_t MapId = 0;
    std::string Slug, Name, RegionName;
    float ContRect[4] = {}; // continent_rect, flattened [x1,y1,x2,y2]
    float MapRect[4] = {};  // map_rect, flattened
    bool HasRects = false;
    std::vector<FishHole> Holes;
};

// Load every fishing map from <fishingDir>/index.json into `out` as METADATA STUBS (mapId/name/slug, no holes),
// keyed by mapId. The full per-slug file (holes + rects) loads on entry via LoadFishMapFull.
int LoadAllFishing(const std::string &fishingDir, std::map<uint32_t, FishMap> &out);

// Lazy-load a stub fishing map's full per-slug file (<fishingDir>/<fm.Slug>.json -> holes + rects) in place
// (sets fm.GeomLoaded, preserves the stub's Slug). Called on entering the map. Returns success.
bool LoadFishMapFull(const std::string &fishingDir, FishMap &fm);

// One entry of data/maps/index.json: a lightweight record of EVERY real place (open world, cities, hubs, lounges,
// dungeons, fractals, raids, strikes, story-with-trail, home, guild halls, festival, WvW, PvP, tutorial) for the
// Atlas all-maps browse (Kind -> Region -> Zone) + search + travel. `Kind` is the classification; `HasGuide` is
// filled at load time (true when a zone OR instance dataset exists for the map id).
struct MapInfo
{
    uint32_t MapId = 0;
    std::string Name;
    std::string Kind;
    int ContinentId = 0;
    int RegionId = 0;
    std::string RegionName;
    int MinLevel = 0, MaxLevel = 0;
    float ContRect[4] = {0.f, 0.f, 0.f, 0.f}; // continent rect [left, top, right, bottom]
    bool HasRect = false;
    bool HasGuide = false;         // a zone/instance dataset exists (set at runtime)
    bool Lounge = false;           // tagged a lounge (incl. zones/WvW that double as a lounge)
    std::string Access;            // lounge access: "premium" / "earned" / "free" / "" (none)
    std::vector<std::string> Tags; // extra kind chips this map also lists under (City+PvP, ...)
    std::string ChatLink;          // travel waypoint chat code for instanced content (fractals/raids)
    float Cx = 0.f, Cy = 0.f;      // entrance continent coords (nav-arrow target for the chat code)
    bool HasTile = false;          // hover map = live tiles (this continent's floor-1 world map)
    int TileMaxZoom = 7;           // tile-server max zoom for the continent (7 cont1 / 6 cont2)
    std::string StaticMap;         // hover map = a bundled static image (maps.pack), keyed by slug
};

// Load data/maps/index.json into `out` (static metadata for the Atlas all-maps browse). Returns the count.
int LoadMapIndex(const std::string &path, std::vector<MapInfo> &out);
