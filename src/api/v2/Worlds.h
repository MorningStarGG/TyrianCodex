#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/worlds (anonymous, day-cached): the server/world list (id -> name + population tier). Wiki: API:2/worlds.
namespace Api { class Connection; }
namespace Api::V2
{
    struct World { int id = 0; std::string name; std::string population; nlohmann::json raw; };

    inline World ParseWorld(const nlohmann::json& j)
    {
        World w;
        w.id         = Json::Int(j, "id");
        w.name       = Json::Str(j, "name");
        w.population = Json::Str(j, "population");
        w.raw        = j;
        return w;
    }

    class WorldsEndpoint : public BulkEndpoint<World>
    { public: explicit WorldsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/worlds", &ParseWorld, kStaticTtlSec) {} };
}
