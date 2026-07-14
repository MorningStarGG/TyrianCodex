#pragma once
#include <cstdint>
#include <string>
#include <vector>
class App;
struct Zone;

// -----------------------------------------------------------------------------------------------------
// Search index ("Atlas" database). A flat, cached list of every world entity we already hold in memory --
// each zone's waypoints / POIs / vistas / hero points / hearts, PLUS the zones themselves -- with filter +
// sort. Built ONCE from app.zones (LoadAllZones loads them all at startup) and rebuilt only when the zone set
// changes. Pure data (no ImGui): the Atlas tab + the Search dashboard widget both query it; the shared row
// renderer (SearchRow) draws an Entry. No per-frame churn -- Query caches behind a filter/sort change token.
// -----------------------------------------------------------------------------------------------------
namespace Search
{
    // The kinds the index covers; the typeMask in Query() is a bitfield of (1u << Ord). Order MATCHES the
    // type-filter chip row (waypoint/poi/vista/hero/heart) with Zone appended.
    enum class Kind : int { Waypoint = 0, Poi, Vista, Hero, Heart, Zone, Marker, Count };
    int  KindOrd(const std::string& type);   // type string -> 0..6 (Count for unknown). Marker is APPENDED
                                             // (after Zone) so existing ordinals/typeMask bits are unchanged.

    struct Entry
    {
        std::string type;        // "waypoint"/"poi"/"vista"/"hero"/"heart"/"zone" (PaintTypeIconAt + Objective::ColorOf)
        std::string name;        // display name (fallback labels for unnamed objectives)
        std::string zoneName;    // owning zone's name
        std::string regionLabel; // region display label (Regions::ZoneRegionLabel)
        int         regionId    = 0;
        int         continentId = 1;
        std::string kind;        // map kind (OpenWorld/City/Lounge/Dungeon/Fractal/Raid/Strike/Story/Home/
                                 // GuildHall/Festival/WvW/PvP/Tutorial); objectives inherit their zone's kind
        bool        lounge      = false;    // tagged a lounge (kind==Lounge, or a zone/WvW map that doubles as one)
        std::string access;      // lounge access tag: "premium" / "earned" / "free" / "" (non-lounge)
        std::vector<std::string> tags;      // extra kind chips this place also lists under (Heart of the Mists: City + PvP)
        uint32_t    mapId       = 0;
        float       cx = 0.f, cy = 0.f;     // continent coords (zone center for a Zone entry)
        int         level       = -1;       // objective recommended level / a zone's MIN level
        int         levelMax    = -1;       // a zone's MAX level (== level for objectives) -> "Lv x-y" pill
        std::string stepId;      // objective stepId ("" for a zone)
        std::string chatLink;    // a waypoint's own chat link
        std::string waypointLink;// nearest waypoint's chat link (non-waypoint objectives)
        const Zone* zone = nullptr;         // owning zone (hover thumbnail + cross-zone travel)
        bool        hasRects = false;
        float       contRect[4] = {};       // continent rect for the hover thumbnail (non-zone places carry it directly)
        int         tileMaxZoom = 7;        // tile-server max zoom for live-tile thumbnails (7 cont1 / 6 cont2)
        std::string staticMap;              // bundled static map slug (maps.pack) for instanced content with no live tiles
        std::string key;         // lowercased name + zone + region (precomputed for fast substring filtering)
        std::string nameLower;   // lowercased name (for relevance ranking of text searches)
        std::string iconBundled; // for "marker" (gathering) entries: bundled icon basename (data/textures/markers/gather)
        std::string material;    // for "marker" entries: the canonical material key ("iron") -- the GatherShown filter key
        std::string gatherCat;   // for "marker" entries: the node category ("ore"/"wood"/...) -- Farm::CategoryMatch filter
        std::string mount;       // for "marker" entries: comma-joined distinct mounts some nodes need ("Springer, Skyscale"); "" = all foot
        bool        markerAllMount = false;   // every node of this material on the map needs a mount (vs only some)
        bool        markerHasRoute = false;   // a hand-made single-material route exists (enables "Follow this route")
    };

    struct RegionOpt { int id; std::string label; bool core = false; };   // core = an explorable Tyrian region (vs Mists/WvW/PvP/festival "Other")

    // Rebuild the index from app.zones if the zone set changed (cheap no-op otherwise). Call before Entries()/Query().
    void EnsureIndex(App& app);
    const std::vector<Entry>&     Entries();
    const std::vector<RegionOpt>& RegionOptions();   // distinct regions, sorted by label (for the region dropdown)

    // Filter + sort -> indices into Entries(). typeMask: (1u<<Ord) bits to include (0 = all kinds). regionLabel ""
    // = all regions (keyed by NAME -- a region's /v2/maps id isn't unique). mapId 0 = all. When havePlayer, entries on the player's continent come first nearest-first (others
    // after, by zone/name); otherwise by zone then name. Cached behind a change token so it re-filters/re-sorts
    // only when the inputs change (or the player moves a coarse step), never every frame. `slot` (0..kSlots-1)
    // gives each independent caller (Atlas tab / Search widget / Zone Waypoints widget) its own cache so they
    // can't thrash each other's when rendered in the same frame.
    constexpr int kSlots = 4;
    // `browse` true -> order by continent / region / zone / name (for the grouped browse view); false -> nearest
    // first when havePlayer, else zone/name (for searches + the waypoint widget).
    // levelMin/levelMax (1..80): keep only entries whose OWNING ZONE's level band overlaps [min,max] (full 1-80
    // = off). statusFilter: 0 = all, 1 = Recommended (zone is in-band for the character's level AND incomplete),
    // 2 = Incomplete (zone not complete for the character). Both cache behind the level/progress change token.
    const std::vector<int>& Query(App& app, int slot, const char* textLower, uint32_t typeMask, const char* regionLabel,
                                  uint32_t mapId, bool havePlayer, int playerContinent, float playerCx, float playerCy,
                                  bool browse = false, int levelMin = 1, int levelMax = 80, int statusFilter = 0,
                                  const char* kindFilter = "",   // "" = all map kinds; else keep entries of this kind
                                  int gatherCatSel = 0);         // Farm category for "marker" entries (0 = all)

    // Is this zone "recommended" right now -- in-band for the character's level (RecommendZones) AND not yet
    // complete for the character. Cached behind level / character / progress-version. Empty (false) with no
    // character level (no API key). Used by the green "Recommended" pill + the Recommended status filter.
    bool IsRecommendedZone(App& app, uint32_t mapId);
}
