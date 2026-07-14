#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/colors (anonymous, day-cached): the dye palette (id -> name + base rgb). The per-material tints
// (cloth/leather/metal/fur brightness/contrast/hue) stay in `raw`. Wiki: API:2/colors.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Color { int id = 0; std::string name; std::vector<int> baseRgb; nlohmann::json raw; };

    inline Color ParseColor(const nlohmann::json& j)
    {
        Color c;
        c.id      = Json::Int(j, "id");
        c.name    = Json::Str(j, "name");
        c.baseRgb = Json::IntArray(j, "base_rgb");
        c.raw     = j;   // cloth/leather/metal/fur material tints live in raw
        return c;
    }

    class ColorsEndpoint : public BulkEndpoint<Color>
    { public: explicit ColorsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/colors", &ParseColor, kStaticTtlSec) {} };
}
