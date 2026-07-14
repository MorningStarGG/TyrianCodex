#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/races (anonymous, day-cached): the playable races (string id -> name + racial skill ids). Wiki: API:2/races.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Race { std::string id; std::string name; std::vector<int> skills; nlohmann::json raw; };

    inline Race ParseRace(const nlohmann::json& j)
    {
        Race r;
        r.id     = Json::Str(j, "id");
        r.name   = Json::Str(j, "name");
        r.skills = Json::IntArray(j, "skills");
        r.raw    = j;
        return r;
    }

    class RacesEndpoint : public BulkEndpoint<Race, std::string>
    { public: explicit RacesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/races", &ParseRace, kStaticTtlSec) {} };
}
