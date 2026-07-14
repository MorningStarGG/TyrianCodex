#include "ui/search/SearchModel.h"
#include "app/App.h"
#include "guide/Regions.h"
#include "guide/StepStatus.h"   // ZoneComplete (Recommended/Incomplete status filters)
#include "guide/FarmCategories.h" // Farm::CategoryMatch (the Farming kind's material sub-filter)
#include "util/Coords.h"        // WorldXZToContinent (gathering nodes are world coords)
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace
{
    std::string Lower(const std::string& s)
    {
        std::string o = s;
        std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return o;
    }

    std::string PrettyName(std::string s)   // "elder_wood" -> "Elder Wood" (gathering material display fallback)
    {
        for (char& c : s) if (c == '_') c = ' ';
        bool start = true;
        for (char& c : s) { if (start && c >= 'a' && c <= 'z') c = (char)(c - 32); start = (c == ' '); }
        return s;
    }

    // Split a lowercased query into its words (whitespace-separated), for multi-word "all words present" search.
    std::vector<std::string> Tokenize(const std::string& q)
    {
        std::vector<std::string> t; size_t i = 0;
        while (i < q.size())
        {
            while (i < q.size() && q[i] == ' ') ++i;
            size_t j = i; while (j < q.size() && q[j] != ' ') ++j;
            if (j > i) t.push_back(q.substr(i, j - i));
            i = j;
        }
        return t;
    }

    // Lower = better. exact name > name starts-with the whole query > all words in name (name leads with the
    // first word) > all words in name > matched only via zone/region.
    int Relevance(const std::string& name, const std::vector<std::string>& tokens, const std::string& full)
    {
        if (tokens.empty()) return 5;
        if (name == full)              return 0;
        if (name.rfind(full, 0) == 0)  return 1;
        bool allInName = true;
        for (const std::string& t : tokens) if (name.find(t) == std::string::npos) { allInName = false; break; }
        if (allInName && name.rfind(tokens[0], 0) == 0) return 2;
        if (allInName)                                  return 3;
        return 4;
    }

    const char* DefaultName(const std::string& type)
    {
        if (type == "waypoint") return "Waypoint";
        if (type == "poi")      return "Point of Interest";
        if (type == "vista")    return "Vista";
        if (type == "hero")     return "Hero Point";
        if (type == "heart")    return "Renown Heart";
        return "Location";
    }

    // The index. Render-thread only, so function-static-style globals are safe.
    std::vector<Search::Entry>     g_entries;
    std::vector<Search::RegionOpt> g_regions;
    size_t                         g_builtZoneCount = (size_t)-1;

    // Per-caller query cache (one slot each) -> filter/sort only re-run on a change, never every frame, and
    // independent callers can't invalidate each other.
    struct QCache
    {
        std::vector<int> filtered, sorted;
        std::string text; std::string kind; int gcat = 0; uint32_t type = 0; std::string region; uint32_t map = 0; size_t built = (size_t)-1;
        bool have = false; int cont = -1; float px = 1e30f, py = 1e30f; bool browse = false;
        int lvMin = 1, lvMax = 80, status = 0, lvl = -999; uint64_t ver = (uint64_t)-1;   // level slider + status-filter token
    };
    QCache g_q[Search::kSlots];

    // Cached "recommended zone ids" = in-band-for-your-level + incomplete (see Search::IsRecommendedZone).
    std::set<uint32_t> g_recIds; int g_recLevel = -999; std::string g_recChar; uint64_t g_recVer = (uint64_t)-1; size_t g_recCount = (size_t)-1;
}

bool Search::IsRecommendedZone(App& app, uint32_t mapId)
{
    const int      lvl = app.state.charLevel;
    const std::string& ch = app.state.currentChar;
    const uint64_t ver = app.progress.Version();
    const size_t   cnt = app.zones.size();
    if (lvl != g_recLevel || ch != g_recChar || ver != g_recVer || cnt != g_recCount)
    {
        g_recIds.clear();
        if (lvl > 0)
            for (const Zone* z : app.zoneManager.RecommendZones(lvl))   // in-band zones for this level
                if (z && !ZoneComplete(app.progress, ch, *z)) g_recIds.insert(z->MapId);
        g_recLevel = lvl; g_recChar = ch; g_recVer = ver; g_recCount = cnt;
    }
    return g_recIds.count(mapId) != 0;
}

int Search::KindOrd(const std::string& t)
{
    if (t == "waypoint") return (int)Kind::Waypoint;
    if (t == "poi")      return (int)Kind::Poi;
    if (t == "vista")    return (int)Kind::Vista;
    if (t == "hero")     return (int)Kind::Hero;
    if (t == "heart")    return (int)Kind::Heart;
    if (t == "zone")     return (int)Kind::Zone;
    if (t == "marker")   return (int)Kind::Marker;
    return (int)Kind::Count;
}

void Search::EnsureIndex(App& app)
{
    const size_t token = app.zones.size() + app.mapIndex.size();
    if (token == g_builtZoneCount) return;   // built once; the zone + map-index sets only change at load
    g_entries.clear();
    g_regions.clear();

    std::map<uint32_t, const MapInfo*> mapByid;
    for (const MapInfo& mi : app.mapIndex) mapByid[mi.MapId] = &mi;
    auto kindFor = [&](uint32_t mid) -> std::string {
        auto it = mapByid.find(mid);
        return it != mapByid.end() ? it->second->Kind : std::string("OpenWorld");   // bundled zones not yet indexed -> open world
    };
    auto infoFor = [&](uint32_t mid) -> const MapInfo* {
        auto it = mapByid.find(mid);
        return it != mapByid.end() ? it->second : nullptr;
    };

    for (const auto& kv : app.zones)
    {
        const Zone& z = kv.second;
        const std::string region = Regions::Label(z);

        // The zone itself (search "Queensdale" -> travel to the zone).
        {
            Entry e;
            const MapInfo* mi = infoFor(z.MapId);
            e.type = "zone"; e.kind = mi ? mi->Kind : std::string("OpenWorld");
            e.lounge = mi && mi->Lounge; e.access = mi ? mi->Access : std::string();   // a bundled zone that's also a lounge (e.g. Wizard's Tower)
            if (mi) e.tags = mi->Tags;
            e.name = z.Name; e.zoneName = z.Name; e.regionLabel = region;
            e.regionId = z.RegionId; e.continentId = z.ContinentId; e.mapId = z.MapId;
            e.level = z.MinLevel; e.levelMax = z.MaxLevel; e.zone = &z; e.hasRects = z.HasRects;
            if (z.HasRects) { e.cx = (z.ContRect[0] + z.ContRect[2]) * 0.5f; e.cy = (z.ContRect[1] + z.ContRect[3]) * 0.5f; }
            e.nameLower = Lower(e.name);
            e.key = Lower(z.Name + " " + region);
            g_entries.push_back(std::move(e));
        }

        // Its objectives (the 5 world-objective kinds only).
        for (const Step& s : z.Steps)
        {
            if (KindOrd(s.Type) >= (int)Kind::Zone) continue;
            Entry e;
            e.type = s.Type; e.kind = kindFor(z.MapId);
            e.name = s.Name.empty() ? DefaultName(s.Type) : s.Name;
            e.zoneName = z.Name; e.regionLabel = region; e.regionId = z.RegionId;
            e.continentId = z.ContinentId; e.mapId = z.MapId;
            e.cx = s.CX; e.cy = s.CY; e.level = s.RecommendedLevel; e.levelMax = s.RecommendedLevel;
            e.stepId = s.StepId; e.chatLink = s.ChatLink; e.waypointLink = s.WaypointChatLink;
            e.zone = &z; e.hasRects = z.HasRects;
            e.nameLower = Lower(e.name);
            e.key = Lower(e.name + " " + z.Name + " " + region);
            g_entries.push_back(std::move(e));
        }

        if (std::none_of(g_regions.begin(), g_regions.end(), [&](const RegionOpt& r){ return r.label == region; }))
            g_regions.push_back({ z.RegionId, region });   // dedup by NAME: a region's /v2/maps id isn't unique
    }

    // Gathering nodes (farming): ONE grouped entry per (map, material) -- not per node -- so the list stays
    // usable (6000+ nodes would flood it). Representative position = the first node of that material on the map
    // (world coords -> continent via the zone rects). Only for maps that are bundled open-world zones (rects +
    // a valid &z for the level-band filter); the overlay/Farming tab still cover any non-zone maps.
    for (const auto& kv : app.farming)
    {
        const Instance& fm = kv.second;
        auto zit = app.zones.find(fm.MapId);
        if (zit == app.zones.end()) continue;
        const Zone& z = zit->second;
        const std::string region = Regions::Label(z);

        std::map<std::string, std::string> routeName;   // material -> exact route display ("Iron Ore")
        for (const InstanceRoute& rt : fm.Routes) if (!rt.Material.empty()) routeName[rt.Material] = rt.Name;

        struct Agg { int count = 0; float wx = 0, wz = 0; std::string icon, cat; std::set<std::string> mounts; bool anyFoot = false; };
        std::map<std::string, Agg> byMat;
        for (const FarmNode& n : fm.Nodes)
        {
            Agg& a = byMat[n.Material];
            if (a.count == 0) { a.wx = n.X; a.wz = n.Z; a.icon = n.Icon; a.cat = n.Category; }
            a.count++;
            if (n.Mount.empty()) a.anyFoot = true; else a.mounts.insert(n.Mount);
        }
        for (const auto& mc : byMat)
        {
            const Agg& a = mc.second;
            const auto rit = routeName.find(mc.first);
            const std::string nm = (rit != routeName.end()) ? rit->second : PrettyName(mc.first);
            char nbuf[112]; std::snprintf(nbuf, sizeof(nbuf), "%s (%d)", nm.c_str(), a.count);

            // Mount requirement: the full comma-joined list of distinct mounts some nodes of this material need
            // (SearchRow shows a short "Mounts" pill + lists these on hover). Every mount name also goes into the
            // search key, so "skyscale" finds them. markerAllMount = no foot-reachable node at all.
            std::string mountList, mountKey;
            for (const std::string& mn : a.mounts) { mountKey += " " + mn; if (!mountList.empty()) mountList += ", "; mountList += mn; }

            Entry e;
            e.type = "marker"; e.kind = kindFor(z.MapId); e.gatherCat = a.cat; e.name = nbuf; e.mount = mountList; e.markerAllMount = !a.mounts.empty() && !a.anyFoot;
            e.zoneName = z.Name; e.regionLabel = region; e.regionId = z.RegionId;
            e.continentId = z.ContinentId; e.mapId = z.MapId;
            e.level = -1; e.levelMax = -1; e.zone = &z; e.hasRects = z.HasRects;
            if (z.HasRects) Coords::WorldXZToContinent(a.wx, a.wz, z.ContRect, z.MapRect, e.cx, e.cy);
            e.iconBundled = a.icon;
            e.material = mc.first;
            e.markerHasRoute = routeName.find(mc.first) != routeName.end();
            e.nameLower = Lower(nm);
            e.key = Lower(nm + " " + z.Name + " " + region + " " + mc.first + " " + a.cat + mountKey + " gathering node");
            g_entries.push_back(std::move(e));
        }
    }

    // Every other "place" from the map index that ISN'T a bundled zone (festival / raid / fractal / lounge /
    // dungeon / story instance / home / WvW / PvP / tutorial) -> a searchable + browsable place row (no
    // objectives; travel/info). Open-world + cities are already covered above as full zones.
    for (const MapInfo& mi : app.mapIndex)
    {
        if (app.zones.count(mi.MapId)) continue;
        Entry e;
        const std::string regionLabel = mi.RegionName.empty() ? std::string("Other") : mi.RegionName;
        e.type = "zone"; e.kind = mi.Kind;
        e.lounge = mi.Lounge; e.access = mi.Access; e.tags = mi.Tags;
        e.chatLink = mi.ChatLink; e.waypointLink = mi.ChatLink;   // instanced content (fractals/raids): travel = copy this entrance WP
        e.name = mi.Name; e.zoneName = mi.Name;
        e.regionLabel = regionLabel;
        e.regionId = mi.RegionId; e.continentId = mi.ContinentId; e.mapId = mi.MapId;
        e.level = mi.MinLevel; e.levelMax = mi.MaxLevel; e.zone = nullptr; e.hasRects = mi.HasRect;
        for (int i = 0; i < 4; ++i) e.contRect[i] = mi.ContRect[i];   // hover thumbnail: non-zone places carry the rect
        e.tileMaxZoom = mi.TileMaxZoom; e.staticMap = mi.StaticMap;   // live tiles (maxZoom) vs a bundled static image
        if (mi.Cx != 0.f || mi.Cy != 0.f) { e.cx = mi.Cx; e.cy = mi.Cy; }   // explicit entrance coord (fractals/raids) wins
        else if (mi.HasRect) { e.cx = (mi.ContRect[0] + mi.ContRect[2]) * 0.5f; e.cy = (mi.ContRect[1] + mi.ContRect[3]) * 0.5f; }
        e.nameLower = Lower(e.name);
        e.key = Lower(mi.Name + " " + e.regionLabel + " " + mi.Kind);
        g_entries.push_back(std::move(e));
        if (std::none_of(g_regions.begin(), g_regions.end(), [&](const RegionOpt& r){ return r.label == regionLabel; }))
            g_regions.push_back({ mi.RegionId, regionLabel });   // local label (e was moved-from above) + dedup by NAME
    }

    // Tag "core" regions = the explorable Tyrian world (a region with an open-world or city map on continent 1) vs
    // the "Other" stuff (The Mists / WvW / PvP / Fractals / festivals). The region dropdown groups core first.
    std::set<std::string> coreReg;
    for (const Entry& e : g_entries)
        if (e.continentId == 1 && (e.kind == "OpenWorld" || e.kind == "City")) coreReg.insert(e.regionLabel);
    for (RegionOpt& r : g_regions) r.core = coreReg.count(r.label) > 0;
    std::sort(g_regions.begin(), g_regions.end(), [](const RegionOpt& a, const RegionOpt& b){
        if (a.core != b.core) return a.core;      // core (Tyrian) regions first, then "Other"
        return a.label < b.label;
    });
    g_builtZoneCount = token;   // each slot re-filters when its .built != this (zones + map-index places)
}

const std::vector<Search::Entry>&     Search::Entries()       { return g_entries; }
const std::vector<Search::RegionOpt>& Search::RegionOptions() { return g_regions; }

const std::vector<int>& Search::Query(App& app, int slot, const char* textLower, uint32_t typeMask, const char* regionLabel,
                                      uint32_t mapId, bool havePlayer, int playerContinent, float playerCx, float playerCy,
                                      bool browse, int levelMin, int levelMax, int statusFilter, const char* kindFilter,
                                      int gatherCatSel)
{
    EnsureIndex(app);
    if (slot < 0 || slot >= kSlots) slot = 0;
    QCache& q = g_q[slot];
    const std::string text = textLower ? textLower : "";
    const std::vector<std::string> tokens = Tokenize(text);   // multi-word: every word must appear

    const int      curLvl    = app.state.charLevel;
    const uint64_t curVer    = app.progress.Version();
    const bool     lvlActive = (levelMin > 1 || levelMax < 80);
    const bool     statusDep = statusFilter != 0 && (curLvl != q.lvl || curVer != q.ver);
    const std::string kindF = kindFilter ? kindFilter : "";
    const std::string regionLbl = regionLabel ? regionLabel : "";   // region is keyed by NAME (regionId isn't unique)
    const bool filterChanged = text != q.text || kindF != q.kind || gatherCatSel != q.gcat || typeMask != q.type ||
                               regionLbl != q.region || mapId != q.map || q.built != g_builtZoneCount ||
                               levelMin != q.lvMin || levelMax != q.lvMax || statusFilter != q.status || statusDep;
    if (filterChanged)
    {
        q.filtered.clear();
        for (int i = 0; i < (int)g_entries.size(); ++i)
        {
            const Entry& e = g_entries[i];
            // "Lounge" is a TAG, not a kind: it spans kind=Lounge places PLUS zones/WvW maps that double as a lounge
            // (Wizard's Tower, Obsidian Sanctum). Other chips match the primary kind OR a cross-listing tag (Heart of
            // the Mists has kind=City + tag PvP, so it appears under both Cities and PvP).
            if (kindF == "Lounge") { if (!e.lounge) continue; }
            else if (!kindF.empty() && e.kind != kindF &&
                     std::find(e.tags.begin(), e.tags.end(), kindF) == e.tags.end()) continue;
            if (gatherCatSel && e.type == "marker" && !Farm::CategoryMatch(gatherCatSel, e.gatherCat)) continue;
            if (typeMask && !(typeMask & (1u << KindOrd(e.type)))) continue;
            if (!regionLbl.empty() && e.regionLabel != regionLbl) continue;
            if (mapId && e.mapId != mapId) continue;
            if (lvlActive)   // owning-zone level band must overlap the [levelMin, levelMax] slider
            {
                const Zone* z = e.zone;
                if (!z || z->MaxLevel < levelMin || z->MinLevel > levelMax) continue;
            }
            if (statusFilter == 1)      { if (!IsRecommendedZone(app, e.mapId)) continue; }   // Recommended = in-band + incomplete
            else if (statusFilter == 2) { const Zone* z = e.zone; if (!z || ZoneComplete(app.progress, app.state.currentChar, *z)) continue; }   // Incomplete
            bool allTokens = true;
            for (const std::string& t : tokens) if (e.key.find(t) == std::string::npos) { allTokens = false; break; }
            if (!allTokens) continue;
            q.filtered.push_back(i);
        }
        q.text = text; q.kind = kindF; q.gcat = gatherCatSel; q.type = typeMask; q.region = regionLbl; q.map = mapId; q.built = g_builtZoneCount;
        q.lvMin = levelMin; q.lvMax = levelMax; q.status = statusFilter; q.lvl = curLvl; q.ver = curVer;
        q.px = 1e30f;   // force a re-sort
    }

    // Re-sort on filter change, a continent change, or a coarse (~50 m) player move so the nearest-first order
    // tracks the player without re-sorting every frame.
    const float movedSq = (playerCx - q.px) * (playerCx - q.px) + (playerCy - q.py) * (playerCy - q.py);
    const bool  resort  = filterChanged || browse != q.browse || havePlayer != q.have || playerContinent != q.cont ||
                          (!browse && havePlayer && movedSq > 2000.f * 2000.f);
    if (resort)
    {
        q.sorted = q.filtered;
        std::sort(q.sorted.begin(), q.sorted.end(), [&](int ia, int ib)
        {
            const Entry& a = g_entries[ia]; const Entry& b = g_entries[ib];
            if (browse)   // browse view: continent -> region -> zone -> name (alphabetical, distance-independent)
            {
                if (a.continentId != b.continentId) return a.continentId < b.continentId;
                if (a.regionLabel != b.regionLabel) return a.regionLabel < b.regionLabel;
                if (a.zoneName    != b.zoneName)    return a.zoneName < b.zoneName;
                return a.name < b.name;
            }
            // search view: best name match first (no-op when the query is empty), then distance, then name
            const int ra = Relevance(a.nameLower, tokens, text), rb = Relevance(b.nameLower, tokens, text);
            if (ra != rb) return ra < rb;
            if (havePlayer)
            {
                const bool ca = a.continentId == playerContinent, cb = b.continentId == playerContinent;
                if (ca != cb) return ca;   // same-continent results first
                if (ca && cb)
                {
                    const float da = (a.cx - playerCx) * (a.cx - playerCx) + (a.cy - playerCy) * (a.cy - playerCy);
                    const float db = (b.cx - playerCx) * (b.cx - playerCx) + (b.cy - playerCy) * (b.cy - playerCy);
                    if (da != db) return da < db;
                }
            }
            if (a.zoneName != b.zoneName) return a.zoneName < b.zoneName;
            return a.name < b.name;
        });
        q.have = havePlayer; q.cont = playerContinent; q.px = playerCx; q.py = playerCy; q.browse = browse;
    }
    return q.sorted;
}
