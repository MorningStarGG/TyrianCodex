#pragma once
#include "Common.h"

// GET /v1/colors.json (anonymous, locale): the dye palette ({"colors":{id:{...}}}), each with base rgb + the
// per-material (cloth/leather/metal/fur) tint transform. Wiki: API:1/colors.
namespace Api::V1
{
    struct ColorMaterial { int brightness = 0; double contrast = 0; int hue = 0; double saturation = 0; double lightness = 0; std::vector<int> rgb; nlohmann::json raw; };
    struct Color         { int id = 0; std::string name; std::vector<int> baseRgb; ColorMaterial cloth, leather, metal, fur; int item = 0; std::vector<int> categories; nlohmann::json raw; };

    inline ColorMaterial ParseColorMaterial(const nlohmann::json& j)
    {
        ColorMaterial m;
        if (!j.is_object()) return m;
        m.brightness = IntS(j, "brightness"); m.contrast = DblS(j, "contrast"); m.hue = IntS(j, "hue");
        m.saturation = DblS(j, "saturation"); m.lightness = DblS(j, "lightness"); m.rgb = Json::IntArray(j, "rgb"); m.raw = j;
        return m;
    }
    inline std::vector<Color> ParseColors(const nlohmann::json& j)
    {
        std::vector<Color> out;
        const nlohmann::json& colors = Json::Node(j, "colors");
        if (colors.is_object()) for (auto it = colors.begin(); it != colors.end(); ++it)
        {
            const nlohmann::json& c = it.value();
            Color col;
            try { col.id = std::stoi(it.key()); } catch (...) {}
            col.name = Json::Str(c, "name"); col.baseRgb = Json::IntArray(c, "base_rgb");
            col.cloth = ParseColorMaterial(Json::Node(c, "cloth")); col.leather = ParseColorMaterial(Json::Node(c, "leather"));
            col.metal = ParseColorMaterial(Json::Node(c, "metal")); col.fur = ParseColorMaterial(Json::Node(c, "fur"));
            col.item = IntS(c, "item"); col.categories = Json::IntArray(c, "categories"); col.raw = c;
            out.push_back(std::move(col));
        }
        return out;
    }

    class ColorsEndpoint
    {
    public:
        explicit ColorsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<Color>>)> cb) const { Fetch<std::vector<Color>>(_c, "/v1/colors.json", kV1StaticTtl, &ParseColors, std::move(cb)); }
    private:
        Connection* _c;
    };
}
