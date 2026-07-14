#pragma once
#include "Common.h"

// GET /v1/recipe_details.json?recipe_id= (anonymous, locale; DEPRECATED but served): one crafting recipe,
// fully typed (output, disciplines, ingredient list). Wiki: API:1/recipe_details.
namespace Api::V1
{
    struct RecipeIngredient { int itemId = 0; int count = 0; };
    struct Recipe
    {
        int recipeId = 0; std::string type; int outputItemId = 0; int outputItemCount = 0; int minRating = 0; int timeToCraftMs = 0;
        std::vector<std::string> disciplines; std::vector<std::string> flags; std::vector<RecipeIngredient> ingredients; nlohmann::json raw;
    };

    inline Recipe ParseRecipe(const nlohmann::json& j)
    {
        Recipe r;
        r.recipeId = IntS(j, "recipe_id"); r.type = Json::Str(j, "type"); r.outputItemId = IntS(j, "output_item_id");
        r.outputItemCount = IntS(j, "output_item_count"); r.minRating = IntS(j, "min_rating"); r.timeToCraftMs = IntS(j, "time_to_craft_ms");
        r.disciplines = Json::StrArray(j, "disciplines"); r.flags = Json::StrArray(j, "flags");
        for (const auto& ing : Json::Node(j, "ingredients"))
            if (ing.is_object()) r.ingredients.push_back({ IntS(ing, "item_id"), IntS(ing, "count") });
        r.raw = j;
        return r;
    }

    class RecipeDetailsEndpoint
    {
    public:
        explicit RecipeDetailsEndpoint(Connection* c) : _c(c) {}
        void ById(int recipeId, std::function<void(Result<Recipe>)> cb) const { FetchQ<Recipe>(_c, "/v1/recipe_details.json", "recipe_id", std::to_string(recipeId), kV1StaticTtl, &ParseRecipe, std::move(cb)); }
    private:
        Connection* _c;
    };
}
