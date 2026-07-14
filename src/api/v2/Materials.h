#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/materials (anonymous, day-cached): the material-storage categories (id -> name + member item ids +
// order). Wiki: API:2/materials.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Material { int id = 0; std::string name; std::vector<int> items; int order = 0; nlohmann::json raw; };

    inline Material ParseMaterial(const nlohmann::json& j)
    {
        Material m;
        m.id    = Json::Int(j, "id");
        m.name  = Json::Str(j, "name");
        m.items = Json::IntArray(j, "items");
        m.order = Json::Int(j, "order");
        m.raw   = j;
        return m;
    }

    class MaterialsEndpoint : public BulkEndpoint<Material>
    { public: explicit MaterialsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/materials", &ParseMaterial, kStaticTtlSec) {} };
}
