#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/skills (anonymous, day-cached): the skill catalog (id -> name/description/icon/type). Facts /
// traited_facts / categories stay in `raw`. Wiki: API:2/skills.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Skill { int id = 0; std::string name; std::string description; std::string icon; std::string type; nlohmann::json raw; };

    inline Skill ParseSkill(const nlohmann::json& j)
    {
        Skill s;
        s.id          = Json::Int(j, "id");
        s.name        = Json::Str(j, "name");
        s.description = Json::Str(j, "description");
        s.icon        = Json::Str(j, "icon");
        s.type        = Json::Str(j, "type");
        s.raw         = j;   // facts / traited_facts / categories live in raw
        return s;
    }

    class SkillsEndpoint : public BulkEndpoint<Skill>
    { public: explicit SkillsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/skills", &ParseSkill, kStaticTtlSec) {} };
}
