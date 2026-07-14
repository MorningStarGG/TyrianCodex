#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// CraftKnown: disk-backed stale-while-revalidate cache of which crafting recipes the account / each
// character has learned. Mirrors TpEligibility -- loads craft_known.json from the addon root so the
// Crafting view can answer "does this character know it? / who knows it?" synchronously, then Warm()
// refreshes /v2/account/recipes + every character's /v2/characters/:id/recipes once per login and saves
// it back. F10-safe: a failed/empty response leaves the prior cache intact. Needs an API key + scopes
// (unlocks for account, inventories for characters); with no key it simply has no "known" data.
// -----------------------------------------------------------------------------------------------------
namespace CraftKnown
{
    void Init(Api::Client* api, const std::string& addonDir);
    void Shutdown();
    void Warm();          // login refresh (account + all characters); call once (e.g. from WarmCaches)

    bool     IsReady();        // any known data available (loaded from cache or fetched)
    bool     HasFetched();     // the login warm has completed (success OR failure) -- distinguishes "no recipes" from "still loading"
    bool     IsRefreshing();   // a warm is in flight
    uint64_t Version();        // UI change token -- bumps on load + as each account/character set lands
    long long FetchedUtc();

    std::vector<std::string> CharacterNames();                  // characters we have recipe data for (sorted)
    bool CharKnows(const std::string& name, int recipeId);      // that character can craft this recipe
    bool KnownByAccount(int recipeId);                          // account-wide unlock (recipe sheet)
    std::vector<int> KnownRecipeIds(const std::string& name);   // a character's known recipe ids (for the rail)
    std::vector<std::string> CharsKnowing(int recipeId);        // which characters can craft this recipe (sorted)
}
