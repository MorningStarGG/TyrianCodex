#pragma once
#include "Common.h"

// GET /v1/recipes.json (anonymous; DEPRECATED but served): the bare list of all recipe ids ({"recipes":[...]}).
// Detail via recipe_details. Wiki: API:1/recipes.
namespace Api::V1
{
    inline std::vector<int> ParseRecipeIds(const nlohmann::json& j) { return ParseWrappedIntIds(j, "recipes"); }

    class RecipesEndpoint
    {
    public:
        explicit RecipesEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<int>>)> cb) const { Fetch<std::vector<int>>(_c, "/v1/recipes.json", kV1StaticTtl, &ParseRecipeIds, std::move(cb)); }
    private:
        Connection* _c;
    };
}
