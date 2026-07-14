#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// CraftingEngine: the bundled recipe catalog (data/recipes.json: official crafting + datawars2 Mystic Forge)
// PLUS the recursive buy-vs-craft cost engine behind the Trading Post Crafting Calculator. The catalog mirrors
// ItemCatalog -- a JSON seed parsed off-thread, indexed by OUTPUT item id, kept fresh by a /v2/build delta that
// tops up only NEW official crafting recipes into the writable cache (Mystic Forge recipes have no official id
// and only refresh when the seed is rebuilt). The engine reads live prices via TpPrices and is pure-ish: call
// ComputePlan() when the queue / toggles / prices change (NOT every frame -- see the F5 perf rule).
// -----------------------------------------------------------------------------------------------------
namespace CraftingEngine
{
    // A recipe producing `out` (`cnt` per craft) from `ing` = [(itemId, count)]. `id` is the official recipe id
    // (0 for Mystic Forge / non-API). type + disc are for the craft-step label ("Mystic Forge", "Tailor", ...).
    struct Recipe
    {
        int id = 0, out = 0, cnt = 1, rating = 0;
        std::vector<std::pair<int, int>> ing;
        std::string type;
        std::vector<std::string> disc;
    };

    void     Init(Api::Client* api, const std::string& dataDir, const std::string& cacheDir);
    void     Shutdown();
    void     Warm();          // build-id freshness check (after load); delta-fetches new official recipes
    bool     Ready();         // recipes loaded (seed/cache parsed + published)
    int      Count();
    uint64_t Version();       // bumps on publish/delta -- the UI's change token

    const std::vector<Recipe>* RecipesFor(int itemId);   // recipes producing itemId, or nullptr if none
    const Recipe* RecipeById(int recipeId);              // recipe by its official id (for known-recipe lists), or nullptr
    const std::vector<Recipe>& All();                    // every recipe (the Collections Recipes browser iterates all; empty until Ready)

    // ---- crafting plan ----
    struct Opts { bool useOwnMaterials = false; bool craftPrecursors = true; };

    struct ShopRow { int itemId = 0; long long qty = 0; int unitSell = 0; long long total = 0; bool priced = false; };
    struct StepRow { int outId = 0; int outCnt = 1; long long crafts = 0; std::string disc; std::vector<std::pair<int, int>> ing; };
    struct PlanNode
    {
        int itemId = 0; long long qty = 0; bool craft = false; long long unitCost = -1; int recipe = -1;
        std::vector<PlanNode> kids;
    };
    struct Plan
    {
        bool ok = false;
        bool pricesComplete = true;   // false while some ids are still unpriced (the UI re-plans as prices land)
        bool outputTradeable = false;
        PlanNode root;
        std::vector<ShopRow> shopping;   // base buys, aggregated (owned subtracted when opts.useOwnMaterials)
        std::vector<StepRow> steps;      // craft build order, deepest-first
        long long craftCost   = 0;       // cost to CRAFT `qty` (after the owned-materials discount)
        long long tpCost      = 0;       // cost to BUY `qty` outright (lowest sell), -1 if unbuyable
        long long revenueSell = 0;       // net from LISTING `qty` to sell (sum TpNet(sellUnit)), 0 if untradeable
        long long revenueBuy  = 0;       // net from INSTANT-selling `qty` to buy orders (sum TpNet(buyUnit))
    };

    // Plan crafting `qty` of `itemId`. Pulls live prices via TpPrices (Want+Get); `owned` = item id -> on-hand
    // count (materials/inventory) for the useOwnMaterials discount (pass an empty map to ignore). Cheap enough
    // to call on change; memoizes internally per call.
    Plan ComputePlan(int itemId, long long qty, const Opts& opts, const std::unordered_map<int, long long>& owned);
}
