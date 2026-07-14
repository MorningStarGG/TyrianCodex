#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/homestead/{decorations(+/categories(+/:id)),glyphs} (anonymous, day-cached): the homestead catalogs
// - decorations (id -> name/description/icon + category ids + max placed), their categories, and crafting
// glyphs. (Distinct from /v2/account/homestead/*, the player's unlocks.) Wiki: API:2/homestead.
namespace Api { class Connection; }
namespace Api::V2
{
    struct HomesteadDecoration         { int id = 0; std::string name; std::string description; std::string icon; std::vector<int> categories; int maxCount = 0; nlohmann::json raw; };
    struct HomesteadDecorationCategory { int id = 0; std::string name; nlohmann::json raw; };
    struct HomesteadGlyph              { std::string id; int itemId = 0; std::string slot; nlohmann::json raw; };

    inline HomesteadDecoration ParseHomesteadDecoration(const nlohmann::json& j)
    {
        HomesteadDecoration d;
        d.id = Json::Int(j, "id"); d.name = Json::Str(j, "name"); d.description = Json::Str(j, "description");
        d.icon = Json::Str(j, "icon"); d.categories = Json::IntArray(j, "categories"); d.maxCount = Json::Int(j, "max_count"); d.raw = j;
        return d;
    }
    inline HomesteadDecorationCategory ParseHomesteadDecorationCategory(const nlohmann::json& j)
    { HomesteadDecorationCategory c; c.id = Json::Int(j, "id"); c.name = Json::Str(j, "name"); c.raw = j; return c; }
    inline HomesteadGlyph ParseHomesteadGlyph(const nlohmann::json& j)
    { HomesteadGlyph g; g.id = Json::Str(j, "id"); g.itemId = Json::Int(j, "item_id"); g.slot = Json::Str(j, "slot"); g.raw = j; return g; }

    class HomesteadDecorationCategoriesEndpoint : public BulkEndpoint<HomesteadDecorationCategory>
    { public: explicit HomesteadDecorationCategoriesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/homestead/decorations/categories", &ParseHomesteadDecorationCategory, kStaticTtlSec) {} };

    // /v2/homestead/decorations (+ the /categories(/:id) sub-resource via .Categories()).
    class HomesteadDecorationsEndpoint : public BulkEndpoint<HomesteadDecoration>
    {
    public:
        explicit HomesteadDecorationsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/homestead/decorations", &ParseHomesteadDecoration, kStaticTtlSec), _cc(c) {}
        HomesteadDecorationCategoriesEndpoint Categories() const { return HomesteadDecorationCategoriesEndpoint(_cc); }
    private:
        Connection* _cc;
    };

    class HomesteadGlyphsEndpoint : public BulkEndpoint<HomesteadGlyph, std::string>
    { public: explicit HomesteadGlyphsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/homestead/glyphs", &ParseHomesteadGlyph, kStaticTtlSec) {} };

    // /v2/homestead facade: .Decorations() (+ .Categories()) + .Glyphs().
    class HomesteadEndpoint
    {
    public:
        explicit HomesteadEndpoint(Connection* c) : _c(c) {}
        HomesteadDecorationsEndpoint Decorations() const { return HomesteadDecorationsEndpoint(_c); }
        HomesteadGlyphsEndpoint      Glyphs()      const { return HomesteadGlyphsEndpoint(_c); }
    private:
        Connection* _c;
    };
}
