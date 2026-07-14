#pragma once
#include "Common.h"

// GET /v1/map_floor.json?continent_id=&floor= (anonymous, locale): one continent floor (texture dims +
// clamped view + the region->map->sector/poi tree). The deep `regions` tree is kept as a typed json field
// (superseded for our use by /v2/continents/:id/floors/:id). Wiki: API:1/map_floor.
namespace Api::V1
{
    struct MapFloor { std::vector<int> textureDims; nlohmann::json clampedView; nlohmann::json regions; nlohmann::json raw; };

    inline MapFloor ParseMapFloor(const nlohmann::json& j)
    {
        MapFloor f;
        f.textureDims = Json::IntArray(j, "texture_dims"); f.clampedView = Json::Node(j, "clamped_view"); f.regions = Json::Node(j, "regions"); f.raw = j;
        return f;
    }

    class MapFloorEndpoint
    {
    public:
        explicit MapFloorEndpoint(Connection* c) : _c(c) {}
        void Get(int continentId, int floor, std::function<void(Result<MapFloor>)> cb) const
        {
            Request req;
            req.path  = "/v1/map_floor.json";
            req.query = { { "continent_id", std::to_string(continentId) }, { "floor", std::to_string(floor) } };
            req.cacheTtlSec = kV1StaticTtl;
            _c->Get<MapFloor>(std::move(req), &ParseMapFloor, std::move(cb));
        }
    private:
        Connection* _c;
    };
}
