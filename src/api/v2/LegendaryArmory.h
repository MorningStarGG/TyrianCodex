#pragma once
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/legendaryarmory (anonymous, day-cached): the legendary-armory catalog (legendary item id -> how many
// copies may be equipped account-wide). Wiki: API:2/legendaryarmory.
namespace Api { class Connection; }
namespace Api::V2
{
    struct LegendaryArmoryItem { int id = 0; int maxCount = 0; nlohmann::json raw; };

    inline LegendaryArmoryItem ParseLegendaryArmoryItem(const nlohmann::json& j)
    {
        LegendaryArmoryItem l; l.id = Json::Int(j, "id"); l.maxCount = Json::Int(j, "max_count"); l.raw = j; return l;
    }

    class LegendaryArmoryEndpoint : public BulkEndpoint<LegendaryArmoryItem>
    { public: explicit LegendaryArmoryEndpoint(Connection* c) : BulkEndpoint(c, "/v2/legendaryarmory", &ParseLegendaryArmoryItem, kStaticTtlSec) {} };
}
