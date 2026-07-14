#pragma once
#include <cstdint>
#include <string>
#include <vector>
class App;

// -----------------------------------------------------------------------------------------------------
// CraftCartSummary: the active Crafting Cart's computed rollup, cached behind a change token so EVERY surface
// (the Crafting view's external cart surfaces -- HUD button, Info Panel text, Dashboard widget) reads one shared
// result instead of each re-running the recursive buy-vs-craft plan. Recomputes only when the cart, the recipe
// catalog, or the craft opts change (throttled while live prices are still landing).
// -----------------------------------------------------------------------------------------------------
namespace CraftCartSummary
{
    struct Mat { int itemId = 0; long long qty = 0; bool priced = false; int unitSell = 0; long long total = 0; };
    struct Group { int outId = 0; long long outQty = 0; std::vector<Mat> mats; long long subtotal = 0; };  // one cart output

    struct Summary
    {
        std::string project;
        int itemCount = 0;                 // distinct outputs in the cart
        std::vector<Mat>   flat;           // materials aggregated across the cart, sorted by total desc
        std::vector<Group> byItem;         // materials grouped per cart output
        long long craftCost = 0;           // total cost to craft the cart (owned-subtracted when that opt is on)
        long long stillNeed = 0;           // priced, not-checked-off, still-short materials (the "to buy" total)
        bool pricesComplete = true;        // false while some prices are still being fetched
        bool ready = false;                // recipe catalog loaded + computed at least once
    };

    const Summary& Get(App& app);          // cached; recomputes behind a change token (call freely per frame)
}
