#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/professions (anonymous, day-cached): the playable professions (string id -> name/icon/code + spec ids).
// Weapons / skills / training trees stay in `raw`. Wiki: API:2/professions.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Profession { std::string id; std::string name; std::string icon; std::string iconBig; int code = 0; std::vector<int> specializations; nlohmann::json raw; };

    inline Profession ParseProfession(const nlohmann::json& j)
    {
        Profession p;
        p.id              = Json::Str(j, "id");
        p.name            = Json::Str(j, "name");
        p.icon            = Json::Str(j, "icon");
        p.iconBig         = Json::Str(j, "icon_big");
        p.code            = Json::Int(j, "code");
        p.specializations = Json::IntArray(j, "specializations");
        p.raw             = j;   // weapons / skills / training trees live in raw
        return p;
    }

    class ProfessionsEndpoint : public BulkEndpoint<Profession, std::string>
    { public: explicit ProfessionsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/professions", &ParseProfession, kStaticTtlSec) {} };
}
