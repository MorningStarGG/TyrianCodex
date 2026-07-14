#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/dungeons (anonymous, day-cached): the dungeon catalog (string id -> path ids). Distinct from the
// guide's own Instance datasets. Wiki: API:2/dungeons.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Dungeon { std::string id; std::vector<std::string> paths; nlohmann::json raw; };

    inline Dungeon ParseDungeon(const nlohmann::json& j)
    {
        Dungeon d;
        d.id = Json::Str(j, "id");
        for (const auto& p : Json::Node(j, "paths"))   // [{id, type}, ...] -> the path ids
            if (p.is_object()) d.paths.push_back(Json::Str(p, "id"));
        d.raw = j;
        return d;
    }

    class DungeonsApiEndpoint : public BulkEndpoint<Dungeon, std::string>
    { public: explicit DungeonsApiEndpoint(Connection* c) : BulkEndpoint(c, "/v2/dungeons", &ParseDungeon, kStaticTtlSec) {} };
}
