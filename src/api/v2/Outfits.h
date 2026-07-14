#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/outfits (anonymous, day-cached): the outfit catalog (id -> name/icon + unlock item ids). Wiki: API:2/outfits.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Outfit { int id = 0; std::string name; std::string icon; std::vector<int> unlockItems; nlohmann::json raw; };

    inline Outfit ParseOutfit(const nlohmann::json& j)
    {
        Outfit o;
        o.id          = Json::Int(j, "id");
        o.name        = Json::Str(j, "name");
        o.icon        = Json::Str(j, "icon");
        o.unlockItems = Json::IntArray(j, "unlock_items");
        o.raw         = j;
        return o;
    }

    class OutfitsEndpoint : public BulkEndpoint<Outfit>
    { public: explicit OutfitsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/outfits", &ParseOutfit, kStaticTtlSec) {} };
}
