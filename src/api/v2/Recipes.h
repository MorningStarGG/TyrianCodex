#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Connection.h"
#include "../core/Request.h"
#include "../core/Result.h"
#include "../core/Json.h"

// GET /v2/recipes (anonymous, day-cached): the crafting-recipe catalog (id -> type/output/min rating +
// disciplines), plus the /v2/recipes/search index (recipe ids producing/consuming an item). `ingredients` /
// `flags` stay in `raw`. Wiki: API:2/recipes.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Recipe { int id = 0; std::string type; int outputItemId = 0; int outputItemCount = 0; int minRating = 0; std::vector<std::string> disciplines; nlohmann::json raw; };

    inline Recipe ParseRecipe(const nlohmann::json& j)
    {
        Recipe r;
        r.id              = Json::Int(j, "id");
        r.type            = Json::Str(j, "type");
        r.outputItemId    = Json::Int(j, "output_item_id");
        r.outputItemCount = Json::Int(j, "output_item_count");
        r.minRating       = Json::Int(j, "min_rating");
        r.disciplines     = Json::StrArray(j, "disciplines");
        r.raw             = j;   // `ingredients` / `flags` live in raw
        return r;
    }

    // The bulk pattern + the /recipes/search index (recipe ids producing/consuming an item).
    class RecipesEndpoint : public BulkEndpoint<Recipe>
    {
    public:
        explicit RecipesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/recipes", &ParseRecipe, kStaticTtlSec) {}
        void SearchByInput(int itemId, std::function<void(Result<std::vector<int>>)> cb) const  { Search("input",  itemId, std::move(cb)); }   // recipes that USE the item
        void SearchByOutput(int itemId, std::function<void(Result<std::vector<int>>)> cb) const { Search("output", itemId, std::move(cb)); }   // recipes that PRODUCE the item
    private:
        void Search(const char* key, int itemId, std::function<void(Result<std::vector<int>>)> cb) const
        {
            Request req;
            req.path        = "/v2/recipes/search";
            req.query       = { { key, std::to_string(itemId) } };
            req.cacheTtlSec = kStaticTtlSec;
            _c->Get<std::vector<int>>(std::move(req),
                [](const nlohmann::json& j) {   // the response is a bare array of recipe ids
                    std::vector<int> out;
                    if (j.is_array()) for (const auto& e : j) if (e.is_number_integer()) out.push_back(e.get<int>());
                    return out;
                },
                std::move(cb));
        }
    };
}
