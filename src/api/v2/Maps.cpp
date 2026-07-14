#include "Maps.h"

#include <string>

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    Map ParseMap(const nlohmann::json& j)
    {
        Map m;
        m.id           = Json::Int(j, "id");
        m.name         = Json::Str(j, "name");
        m.type         = Json::Str(j, "type");
        m.minLevel     = Json::Int(j, "min_level");
        m.maxLevel     = Json::Int(j, "max_level");
        m.defaultFloor = Json::Int(j, "default_floor");
        m.continentId  = Json::Int(j, "continent_id");
        m.regionId     = Json::Int(j, "region_id");
        m.regionName   = Json::Str(j, "region_name");
        m.raw          = j;
        return m;
    }

    void MapsEndpoint::Get(int id, std::function<void(Result<Map>)> cb) const
    {
        Request req;
        req.path        = "/v2/maps/" + std::to_string(id);
        req.auth        = false;                 // anonymous - works with no key
        req.cacheTtlSec = 24 * 60 * 60;          // static reference data
        _c->Get<Map>(std::move(req), &ParseMap, std::move(cb));
    }
}
