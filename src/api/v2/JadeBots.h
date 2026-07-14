#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/jadebots (anonymous, day-cached): the jade-bot skin catalog (id -> name/description + unlock item).
// Wiki: API:2/jadebots.
namespace Api { class Connection; }
namespace Api::V2
{
    struct JadeBot { int id = 0; std::string name; std::string description; int unlockItem = 0; nlohmann::json raw; };

    inline JadeBot ParseJadeBot(const nlohmann::json& j)
    {
        JadeBot b;
        b.id = Json::Int(j, "id"); b.name = Json::Str(j, "name"); b.description = Json::Str(j, "description");
        b.unlockItem = Json::Int(j, "unlock_item"); b.raw = j;
        return b;
    }

    class JadeBotsEndpoint : public BulkEndpoint<JadeBot>
    { public: explicit JadeBotsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/jadebots", &ParseJadeBot, kStaticTtlSec) {} };
}
