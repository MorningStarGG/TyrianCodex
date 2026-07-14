#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/titles (anonymous, day-cached): the title catalog (id -> name + the achievement ids that grant it).
// Wiki: API:2/titles.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Title { int id = 0; std::string name; std::vector<int> achievements; nlohmann::json raw; };

    inline Title ParseTitle(const nlohmann::json& j)
    {
        Title t;
        t.id   = Json::Int(j, "id");
        t.name = Json::Str(j, "name");
        // Titles expose either a single `achievement` or an `achievements` array depending on age.
        t.achievements = Json::IntArray(j, "achievements");
        if (t.achievements.empty() && j.contains("achievement")) t.achievements.push_back(Json::Int(j, "achievement"));
        t.raw = j;
        return t;
    }

    class TitlesEndpoint : public BulkEndpoint<Title>
    { public: explicit TitlesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/titles", &ParseTitle, kStaticTtlSec) {} };
}
