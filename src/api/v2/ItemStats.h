#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/itemstats (anonymous, day-cached): the attribute-combination catalog (id -> name). The `attributes`
// multiplier table stays in `raw`. Wiki: API:2/itemstats.
namespace Api { class Connection; }
namespace Api::V2
{
    struct ItemStat { int id = 0; std::string name; nlohmann::json raw; };   // `attributes` table in raw

    inline ItemStat ParseItemStat(const nlohmann::json& j)
    {
        ItemStat s;
        s.id   = Json::Int(j, "id");
        s.name = Json::Str(j, "name");
        s.raw  = j;   // the `attributes` multiplier table lives in raw
        return s;
    }

    class ItemStatsEndpoint : public BulkEndpoint<ItemStat>
    { public: explicit ItemStatsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/itemstats", &ParseItemStat, kStaticTtlSec) {} };
}
