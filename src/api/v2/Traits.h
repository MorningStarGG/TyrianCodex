#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/traits (anonymous, day-cached): the trait catalog (id -> name/description/icon + specialization/tier/
// slot). Facts live in `raw`. Wiki: API:2/traits.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Trait { int id = 0; std::string name; std::string description; std::string icon; int specialization = 0; int tier = 0; std::string slot; nlohmann::json raw; };

    inline Trait ParseTrait(const nlohmann::json& j)
    {
        Trait t;
        t.id             = Json::Int(j, "id");
        t.name           = Json::Str(j, "name");
        t.description    = Json::Str(j, "description");
        t.icon           = Json::Str(j, "icon");
        t.specialization = Json::Int(j, "specialization");
        t.tier           = Json::Int(j, "tier");
        t.slot           = Json::Str(j, "slot");
        t.raw            = j;
        return t;
    }

    class TraitsEndpoint : public BulkEndpoint<Trait>
    { public: explicit TraitsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/traits", &ParseTrait, kStaticTtlSec) {} };
}
