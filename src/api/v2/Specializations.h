#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/specializations (anonymous, day-cached): profession specializations (id -> name/profession/elite +
// icon/background). Minor/major trait id arrays stay in `raw`. Wiki: API:2/specializations.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Specialization { int id = 0; std::string name; std::string profession; bool elite = false; std::string icon; std::string background; nlohmann::json raw; };

    inline Specialization ParseSpecialization(const nlohmann::json& j)
    {
        Specialization s;
        s.id         = Json::Int(j, "id");
        s.name       = Json::Str(j, "name");
        s.profession = Json::Str(j, "profession");
        s.elite      = Json::Bool(j, "elite");
        s.icon       = Json::Str(j, "icon");
        s.background  = Json::Str(j, "background");
        s.raw        = j;   // minor/major trait id arrays live in raw
        return s;
    }

    class SpecializationsEndpoint : public BulkEndpoint<Specialization>
    { public: explicit SpecializationsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/specializations", &ParseSpecialization, kStaticTtlSec) {} };
}
