#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/gliders (anonymous, day-cached): the glider-skin catalog (id -> name/description/icon/order + unlock
// item ids). Wiki: API:2/gliders.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Glider { int id = 0; std::string name; std::string description; std::string icon; int order = 0; std::vector<int> unlockItems; nlohmann::json raw; };

    inline Glider ParseGlider(const nlohmann::json& j)
    {
        Glider g;
        g.id          = Json::Int(j, "id");
        g.name        = Json::Str(j, "name");
        g.description = Json::Str(j, "description");
        g.icon        = Json::Str(j, "icon");
        g.order       = Json::Int(j, "order");
        g.unlockItems = Json::IntArray(j, "unlock_items");
        g.raw         = j;
        return g;
    }

    class GlidersEndpoint : public BulkEndpoint<Glider>
    { public: explicit GlidersEndpoint(Connection* c) : BulkEndpoint(c, "/v2/gliders", &ParseGlider, kStaticTtlSec) {} };
}
