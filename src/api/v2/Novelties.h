#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/novelties (anonymous, day-cached): the novelty catalog (id -> name/description/icon/slot + unlock
// item id). Wiki: API:2/novelties.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Novelty { int id = 0; std::string name; std::string description; std::string icon; std::string slot; std::vector<int> unlockItems; nlohmann::json raw; };

    inline Novelty ParseNovelty(const nlohmann::json& j)
    {
        Novelty n;
        n.id          = Json::Int(j, "id");
        n.name        = Json::Str(j, "name");
        n.description = Json::Str(j, "description");
        n.icon        = Json::Str(j, "icon");
        n.slot        = Json::Str(j, "slot");
        n.unlockItems = Json::IntArray(j, "unlock_item");   // note: singular key in the API
        n.raw         = j;
        return n;
    }

    class NoveltiesEndpoint : public BulkEndpoint<Novelty>
    { public: explicit NoveltiesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/novelties", &ParseNovelty, kStaticTtlSec) {} };
}
