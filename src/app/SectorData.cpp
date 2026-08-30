#include "SectorData.h"
#include "api/Client.h"
#include "api/v2/Maps.h"
#include "api/v2/Continents.h"
#include "api/core/Json.h"
#include "util/Json.h"   // Json::WriteAtomic (the ONE cache/settings writer)
#include "util/Coords.h"
#include "util/Geometry.h"

#include <fstream>
#include <imgui.h>          // ImGui::GetTime for the fallback retry backoff
#include <map>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Single-threaded: SectorAt/MapName are called from the render/compute path and the .Get callbacks are drained
// on the SAME main thread by api.Pump(), so the maps below need no locking (matches StaticData).
namespace
{
    struct Sector
    {
        std::string        name;
        std::vector<float> xy;        // interleaved continent-coord polygon (x0,y0,x1,y1,...)
        float              bbox[4] = { 0, 0, 0, 0 };  // {minX,minY,maxX,maxY} quick-reject
    };
    struct MapSectors
    {
        std::string         name;
        std::string         regionName;
        int                 continentId = 1;
        bool                hasRects = false;
        float               mapRect[4]  = { 0, 0, 0, 0 };   // {minX,minY,maxX,maxY}
        float               contRect[4] = { 0, 0, 0, 0 };   // {left,top,right,bottom}
        std::vector<Sector> sectors;
    };

    Api::Client*                    g_api = nullptr;
    std::string                     g_cacheFile;            // <cache>/sectors.json ("" = no runtime persistence)
    std::map<uint32_t, MapSectors>  g_maps;                 // mapId -> data (bundle + runtime-fetched)
    std::set<uint32_t>              g_runtime;              // maps filled by the runtime fallback (-> persisted to cache)
    std::set<uint32_t>              g_inflight;             // fallback fetch in progress (ask once)
    std::map<uint32_t, double>      g_retryAt;             // mapId -> earliest retry time after a failed fetch

    // Flatten one sector json (name + bounds polygon) onto `dst`.
    void ParseOneSector(const nlohmann::json& s, MapSectors& dst)
    {
        if (!s.is_object()) return;
        Sector sec;
        sec.name = Api::Json::Str(s, "name");
        const nlohmann::json& b = Api::Json::Node(s, "bounds");
        if (b.is_array())
        {
            sec.xy.reserve(b.size() * 2);
            for (const auto& p : b)
                if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number())
                { sec.xy.push_back((float)p[0].get<double>()); sec.xy.push_back((float)p[1].get<double>()); }
        }
        if (sec.name.empty() && sec.xy.size() < 6) return;   // unnamed + no usable polygon
        if (!sec.xy.empty())
        {
            float mnx = sec.xy[0], mny = sec.xy[1], mxx = sec.xy[0], mxy = sec.xy[1];
            for (size_t i = 0; i + 1 < sec.xy.size(); i += 2)
            {
                mnx = (std::min)(mnx, sec.xy[i]);   mxx = (std::max)(mxx, sec.xy[i]);
                mny = (std::min)(mny, sec.xy[i + 1]); mxy = (std::max)(mxy, sec.xy[i + 1]);
            }
            sec.bbox[0] = mnx; sec.bbox[1] = mny; sec.bbox[2] = mxx; sec.bbox[3] = mxy;
        }
        dst.sectors.push_back(std::move(sec));
    }

    // Sectors arrive as an ARRAY (our built dataset) or an OBJECT keyed by sector id (the raw API floor). Handle both.
    void ParseSectors(const nlohmann::json& node, MapSectors& dst)
    {
        if (node.is_array())
            for (const auto& s : node) ParseOneSector(s, dst);
        else if (node.is_object())
            for (auto it = node.begin(); it != node.end(); ++it) ParseOneSector(it.value(), dst);
    }

    // Read a flat [a,b,c,d] rect (the built dataset's mapRect/continentRect). Returns true if 4 numbers landed.
    bool ReadRect4(const nlohmann::json& node, float out[4])
    {
        if (!node.is_array() || node.size() < 4) return false;
        for (int i = 0; i < 4; ++i) { if (!node[i].is_number()) return false; out[i] = (float)node[i].get<double>(); }
        return true;
    }

    // Parse one built-dataset map entry into a MapSectors.
    MapSectors ParseBuiltMap(const nlohmann::json& m)
    {
        MapSectors d;
        d.name        = Api::Json::Str(m, "name");
        d.regionName  = Api::Json::Str(m, "regionName");
        d.continentId = Api::Json::Int(m, "continentId", 1);
        const bool rk = ReadRect4(Api::Json::Node(m, "mapRect"), d.mapRect);
        const bool rc = ReadRect4(Api::Json::Node(m, "continentRect"), d.contRect);
        d.hasRects = rk && rc;
        ParseSectors(Api::Json::Node(m, "sectors"), d);
        return d;
    }

    // Load a sectors.json (bundle or cache). `runtime` marks its maps for re-persistence (cache overlay only).
    void LoadFile(const std::string& path, bool runtime)
    {
        std::ifstream f(path);
        if (!f) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        const nlohmann::json& maps = (j.is_object() && j.contains("maps")) ? j["maps"] : nlohmann::json::object();
        if (!maps.is_object()) return;
        for (auto it = maps.begin(); it != maps.end(); ++it)
        {
            uint32_t mid = 0;
            try { mid = (uint32_t)std::stoul(it.key()); } catch (...) { continue; }
            if (mid == 0 || !it.value().is_object()) continue;
            g_maps[mid] = ParseBuiltMap(it.value());
            if (runtime) g_runtime.insert(mid);
        }
    }

    // Serialize one MapSectors back to the built-dataset shape (for the runtime cache file).
    nlohmann::json DumpMap(const MapSectors& d)
    {
        nlohmann::json m;
        m["name"] = d.name; m["regionName"] = d.regionName; m["continentId"] = d.continentId;
        if (d.hasRects)
        {
            m["mapRect"]       = { d.mapRect[0], d.mapRect[1], d.mapRect[2], d.mapRect[3] };
            m["continentRect"] = { d.contRect[0], d.contRect[1], d.contRect[2], d.contRect[3] };
        }
        nlohmann::json secs = nlohmann::json::array();
        for (const Sector& s : d.sectors)
        {
            nlohmann::json bounds = nlohmann::json::array();
            for (size_t i = 0; i + 1 < s.xy.size(); i += 2) bounds.push_back({ (int)s.xy[i], (int)s.xy[i + 1] });
            secs.push_back({ {"name", s.name}, {"bounds", bounds} });
        }
        m["sectors"] = secs;
        return m;
    }

    // Best-effort persist of the runtime-fetched maps to <cache>/sectors.json (called when a fallback lands).
    void SaveRuntimeCache()
    {
        if (g_cacheFile.empty() || g_runtime.empty()) return;
        nlohmann::json j; j["maps"] = nlohmann::json::object();
        for (uint32_t mid : g_runtime)
        {
            auto it = g_maps.find(mid);
            if (it != g_maps.end()) j["maps"][std::to_string(mid)] = DumpMap(it->second);
        }
        try { Json::WriteAtomic(g_cacheFile, j.dump()); } catch (...) { /* best effort */ }
    }
}

void SectorData::Init(Api::Client* api, const std::string& dataDir, const std::string& cacheDir)
{
    g_maps.clear(); g_runtime.clear(); g_inflight.clear(); g_retryAt.clear();
    g_api = api;
    g_cacheFile = cacheDir.empty() ? std::string() : (cacheDir + "\\sectors.json");
    if (!dataDir.empty()) LoadFile(dataDir + "\\sectors.json", /*runtime*/ false);
    if (!g_cacheFile.empty()) LoadFile(g_cacheFile, /*runtime*/ true);   // overlay any previously-fetched new maps
}

void SectorData::Shutdown()
{
    g_maps.clear(); g_runtime.clear(); g_inflight.clear(); g_retryAt.clear();
    g_cacheFile.clear(); g_api = nullptr;
}

bool SectorData::HasMap(uint32_t mapId)
{
    return mapId != 0 && g_maps.find(mapId) != g_maps.end();
}

const char* SectorData::MapName(uint32_t mapId)
{
    auto it = g_maps.find(mapId);
    if (it != g_maps.end()) return it->second.name.c_str();
    EnsureMap(mapId);
    return "";
}

const char* SectorData::RegionName(uint32_t mapId)
{
    auto it = g_maps.find(mapId);
    if (it != g_maps.end()) return it->second.regionName.c_str();
    EnsureMap(mapId);
    return "";
}

std::string SectorData::SectorAt(uint32_t mapId, float worldX, float worldZ)
{
    auto it = g_maps.find(mapId);
    if (it == g_maps.end()) { EnsureMap(mapId); return ""; }
    const MapSectors& d = it->second;
    if (!d.hasRects || d.sectors.empty()) return "";

    float cx = 0.f, cy = 0.f;
    Coords::WorldXZToContinent(worldX, worldZ, d.contRect, d.mapRect, cx, cy);
    for (const Sector& s : d.sectors)
    {
        if (s.xy.size() < 6) continue;
        if (!Geometry::InBBox(cx, cy, s.bbox)) continue;
        if (Geometry::PointInPolygon(cx, cy, s.xy)) return s.name;
    }
    return "";   // between sectors / outside the playable extent -> no sector label
}

void SectorData::EnsureMap(uint32_t mapId)
{
    if (mapId == 0 || !g_api) return;
    if (g_maps.find(mapId) != g_maps.end() || g_inflight.count(mapId)) return;
    auto rt = g_retryAt.find(mapId);
    if (rt != g_retryAt.end() && ImGui::GetTime() < rt->second) return;   // backoff after a failure

    g_inflight.insert(mapId);
    g_api->V2().Maps().Get((int)mapId, [mapId](Api::Result<Api::V2::Map> r) {
        if (!r.ok)
        {
            g_inflight.erase(mapId);
            g_retryAt[mapId] = ImGui::GetTime() + 30.0;
            return;
        }
        MapSectors d;
        d.name        = r.value.name;
        d.regionName  = r.value.regionName;
        d.continentId = r.value.continentId;
        // rects live in the raw payload as [[a,b],[c,d]] -- flatten to our flat [a,b,c,d].
        auto rect2 = [](const nlohmann::json& n, float out[4]) -> bool {
            if (!n.is_array() || n.size() < 2 || !n[0].is_array() || !n[1].is_array()) return false;
            if (n[0].size() < 2 || n[1].size() < 2) return false;
            out[0] = (float)n[0][0].get<double>(); out[1] = (float)n[0][1].get<double>();
            out[2] = (float)n[1][0].get<double>(); out[3] = (float)n[1][1].get<double>();
            return true;
        };
        const bool rk = rect2(Api::Json::Node(r.value.raw, "map_rect"), d.mapRect);
        const bool rc = rect2(Api::Json::Node(r.value.raw, "continent_rect"), d.contRect);
        d.hasRects = rk && rc;

        const int contId = r.value.continentId;
        const int floor  = r.value.defaultFloor;
        const int region = r.value.regionId;

        // Store names/rects now so the popup can show region + zone immediately; then try ONE floor fetch for the
        // sector polygons (the rare fallback path -- a brand-new map before the next builder run).
        g_maps[mapId] = std::move(d);
        g_runtime.insert(mapId);
        g_inflight.erase(mapId);
        g_retryAt.erase(mapId);
        SaveRuntimeCache();

        if (!g_api || floor <= 0) return;
        g_api->V2().Continents().Floor(contId, floor, [mapId, region](Api::Result<nlohmann::json> fr) {
            if (!fr.ok) return;
            auto mit = g_maps.find(mapId);
            if (mit == g_maps.end()) return;
            const nlohmann::json& regions = Api::Json::Node(fr.value, "regions");
            if (!regions.is_object()) return;
            // Try the map's own region first, then scan (its reported region_id can be wrong).
            auto tryRegion = [&](const nlohmann::json& reg) -> bool {
                const nlohmann::json& maps = Api::Json::Node(reg, "maps");
                if (!maps.is_object() || !maps.contains(std::to_string(mapId))) return false;
                ParseSectors(Api::Json::Node(maps.at(std::to_string(mapId)), "sectors"), mit->second);
                return true;
            };
            bool found = false;
            if (region > 0 && regions.contains(std::to_string(region)))
                found = tryRegion(regions.at(std::to_string(region)));
            if (!found)
                for (auto it = regions.begin(); it != regions.end() && !found; ++it)
                    found = tryRegion(it.value());
            if (found) SaveRuntimeCache();
        });
    });
}
