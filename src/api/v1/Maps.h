#pragma once
#include "Common.h"

// GET /v1/maps.json (anonymous, locale): the map catalog ({"maps":{id:{...}}}), optionally one via ?map_id.
// The map/continent rects are kept as json fields (nested [[x,y],[x,y]] arrays). Wiki: API:1/maps.
namespace Api::V1
{
    struct Map
    {
        int id = 0; std::string name; int minLevel = 0; int maxLevel = 0; int defaultFloor = 0; std::string type;
        std::vector<int> floors; int regionId = 0; std::string regionName; int continentId = 0; std::string continentName;
        nlohmann::json mapRect; nlohmann::json continentRect; nlohmann::json raw;
    };

    inline std::vector<Map> ParseMaps(const nlohmann::json& j)
    {
        std::vector<Map> out;
        const nlohmann::json& maps = Json::Node(j, "maps");
        if (maps.is_object()) for (auto it = maps.begin(); it != maps.end(); ++it)
        {
            const nlohmann::json& m = it.value();
            Map k;
            try { k.id = std::stoi(it.key()); } catch (...) {}
            k.name = Json::Str(m, "map_name"); k.minLevel = IntS(m, "min_level"); k.maxLevel = IntS(m, "max_level");
            k.defaultFloor = IntS(m, "default_floor"); k.type = Json::Str(m, "type"); k.floors = Json::IntArray(m, "floors");
            k.regionId = IntS(m, "region_id"); k.regionName = Json::Str(m, "region_name");
            k.continentId = IntS(m, "continent_id"); k.continentName = Json::Str(m, "continent_name");
            k.mapRect = Json::Node(m, "map_rect"); k.continentRect = Json::Node(m, "continent_rect"); k.raw = m;
            out.push_back(std::move(k));
        }
        return out;
    }

    class MapsEndpoint
    {
    public:
        explicit MapsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<Map>>)> cb) const                 { Fetch<std::vector<Map>>(_c, "/v1/maps.json", kV1StaticTtl, &ParseMaps, std::move(cb)); }
        void ById(int mapId, std::function<void(Result<std::vector<Map>>)> cb) const         { FetchQ<std::vector<Map>>(_c, "/v1/maps.json", "map_id", std::to_string(mapId), kV1StaticTtl, &ParseMaps, std::move(cb)); }
    private:
        Connection* _c;
    };
}
