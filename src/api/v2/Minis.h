#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/minis (anonymous, day-cached): the miniature catalog (id -> name/icon/order + the item that unlocks it).
// Wiki: API:2/minis.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Mini { int id = 0; std::string name; std::string icon; int order = 0; int itemId = 0; nlohmann::json raw; };

    inline Mini ParseMini(const nlohmann::json& j)
    {
        Mini m;
        m.id     = Json::Int(j, "id");
        m.name   = Json::Str(j, "name");
        m.icon   = Json::Str(j, "icon");
        m.order  = Json::Int(j, "order");
        m.itemId = Json::Int(j, "item_id");
        m.raw    = j;
        return m;
    }

    class MinisEndpoint : public BulkEndpoint<Mini>
    { public: explicit MinisEndpoint(Connection* c) : BulkEndpoint(c, "/v2/minis", &ParseMini, kStaticTtlSec) {} };
}
