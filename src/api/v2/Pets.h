#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/pets (anonymous, day-cached): ranger pets (id -> name/description/icon). Wiki: API:2/pets.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Pet { int id = 0; std::string name; std::string description; std::string icon; nlohmann::json raw; };

    inline Pet ParsePet(const nlohmann::json& j)
    {
        Pet p;
        p.id          = Json::Int(j, "id");
        p.name        = Json::Str(j, "name");
        p.description = Json::Str(j, "description");
        p.icon        = Json::Str(j, "icon");
        p.raw         = j;
        return p;
    }

    class PetsEndpoint : public BulkEndpoint<Pet>
    { public: explicit PetsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/pets", &ParsePet, kStaticTtlSec) {} };
}
