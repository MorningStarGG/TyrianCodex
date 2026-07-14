#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Permissions.h"   // TokenPermission (the sub-resource scope)
#include "../core/Request.h"       // Request (MakeReq return type)
#include "../core/Result.h"
#include "Common.h"                // ItemSlot (bank / shared inventory)

// GET /v2/account/* (authenticated). Every sub-resource is typed (no bare-raw): the unlock lists are typed id
// vectors, the progression / wizard's-vault / storage shapes are small structs, and the item-bearing slots
// (bank / shared inventory / materials) reuse the shared ItemSlot. Each method documents its required scope;
// the client gates client-side and the server is the final word. Wiki: API:2/account.
namespace Api { class Connection; }
namespace Api::V2
{
    // /v2/account (base card).
    struct AccountInfo
    {
        std::string              id;
        std::string              name;
        int                      world = 0;
        std::string              created;
        std::vector<std::string> guilds;
        nlohmann::json           raw;     // access[], fractal_level, daily_ap, wvw, ... live in raw
    };

    // One /v2/account/achievements row: the player's progress on achievement `id`.
    struct AccountAchievement { int id = 0; bool done = false; int current = 0; int max = 0; int repeated = 0; nlohmann::json raw; };

    // One /v2/account/wallet row: currency `id` -> `value`.
    struct WalletEntry { int id = 0; int64_t value = 0; nlohmann::json raw; };

    // (CountedId {id,count} lives in Common.h - shared with guild storage.)

    // /v2/account/finishers row.
    struct AccountFinisher { int id = 0; bool permanent = false; int quantity = 0; nlohmann::json raw; };

    // /v2/account/home/cats row.
    struct AccountHomeCat { int id = 0; std::string hint; nlohmann::json raw; };

    // /v2/account/masteries row + /v2/account/mastery/points.
    struct AccountMastery     { int id = 0; int level = 0; nlohmann::json raw; };
    struct MasteryPointTotal  { std::string region; int spent = 0; int earned = 0; nlohmann::json raw; };
    struct AccountMasteryPoints { std::vector<MasteryPointTotal> totals; std::vector<int> unlocked; nlohmann::json raw; };

    // /v2/account/progression row ({id, value}; e.g. fractal agony resistance, luck).
    struct ProgressionRow { std::string id; int64_t value = 0; nlohmann::json raw; };

    // /v2/account/materials row.
    struct MaterialStack { int id = 0; int category = 0; int count = 0; std::string binding; nlohmann::json raw; };

    // /v2/account/buildstorage row (a stored build template; the deep build object stays in `raw`).
    struct BuildTemplate { std::string name; std::string profession; nlohmann::json raw; };

    // /v2/account/wizardsvault/{daily,weekly,special}.
    struct WizardsVaultObjective { int id = 0; std::string title; std::string track; int value = 0; int target = 0; int rewardItemId = 0; int rewardCount = 0; bool claimed = false; bool progressComplete = false; nlohmann::json raw; };
    struct WizardsVaultProgress  { int metaProgressCurrent = 0; int metaProgressComplete = 0; int metaRewardItemId = 0; bool metaRewardClaimed = false; std::vector<WizardsVaultObjective> objectives; nlohmann::json raw; };

    // /v2/account/wvw (the player's assigned team/guild) + /v2/account/wizardsvault/listings (purchase state).
    struct AccountWvw                 { int teamId = 0; nlohmann::json raw; };
    struct AccountWizardsVaultListing { int id = 0; int purchased = 0; nlohmann::json raw; };

    class AccountEndpoint
    {
    public:
        explicit AccountEndpoint(Connection* c) : _c(c) {}

        void Get(std::function<void(Result<AccountInfo>)> cb) const;                                  // account
        void Achievements(std::function<void(Result<std::vector<AccountAchievement>>)> cb) const;     // account + progression
        void Wallet(std::function<void(Result<std::vector<WalletEntry>>)> cb) const;                  // wallet

        // Inventory / storage (scope: inventories, except buildstorage = builds).
        void Bank(std::function<void(Result<std::vector<ItemSlot>>)> cb) const;
        void Materials(std::function<void(Result<std::vector<MaterialStack>>)> cb) const;
        void SharedInventory(std::function<void(Result<std::vector<ItemSlot>>)> cb) const;   // /v2/account/inventory
        void BuildStorage(std::function<void(Result<std::vector<BuildTemplate>>)> cb) const;

        // Progression (scope: progression).
        void Masteries(std::function<void(Result<std::vector<AccountMastery>>)> cb) const;
        void MasteryPoints(std::function<void(Result<AccountMasteryPoints>)> cb) const;       // /v2/account/mastery/points
        void Progression(std::function<void(Result<std::vector<ProgressionRow>>)> cb) const;
        void Luck(std::function<void(Result<int64_t>)> cb) const;
        void DailyCrafting(std::function<void(Result<std::vector<std::string>>)> cb) const;
        void Raids(std::function<void(Result<std::vector<std::string>>)> cb) const;
        void WorldBosses(std::function<void(Result<std::vector<std::string>>)> cb) const;
        void MapChests(std::function<void(Result<std::vector<std::string>>)> cb) const;
        void Dungeons(std::function<void(Result<std::vector<std::string>>)> cb) const;
        void HomeNodes(std::function<void(Result<std::vector<std::string>>)> cb) const;       // /v2/account/home/nodes

        // Unlocks (scope: unlocks; emotes = progression).
        void Dyes(std::function<void(Result<std::vector<int>>)> cb) const;
        void Skins(std::function<void(Result<std::vector<int>>)> cb) const;
        void Minis(std::function<void(Result<std::vector<int>>)> cb) const;
        void Outfits(std::function<void(Result<std::vector<int>>)> cb) const;
        void Gliders(std::function<void(Result<std::vector<int>>)> cb) const;
        void Finishers(std::function<void(Result<std::vector<AccountFinisher>>)> cb) const;
        void MailCarriers(std::function<void(Result<std::vector<int>>)> cb) const;
        void Novelties(std::function<void(Result<std::vector<int>>)> cb) const;
        void Titles(std::function<void(Result<std::vector<int>>)> cb) const;
        void Emotes(std::function<void(Result<std::vector<std::string>>)> cb) const;
        void Recipes(std::function<void(Result<std::vector<int>>)> cb) const;
        void MountSkins(std::function<void(Result<std::vector<int>>)> cb) const;              // /v2/account/mounts/skins
        void MountTypes(std::function<void(Result<std::vector<std::string>>)> cb) const;      // /v2/account/mounts/types
        void HomeCats(std::function<void(Result<std::vector<AccountHomeCat>>)> cb) const;     // /v2/account/home/cats
        void HomesteadDecorations(std::function<void(Result<std::vector<CountedId>>)> cb) const;
        void HomesteadGlyphs(std::function<void(Result<std::vector<std::string>>)> cb) const;
        void JadeBots(std::function<void(Result<std::vector<int>>)> cb) const;
        void Skiffs(std::function<void(Result<std::vector<int>>)> cb) const;
        void LegendaryArmory(std::function<void(Result<std::vector<CountedId>>)> cb) const;

        // Wizard's Vault (scope: account).
        void WizardsVaultDaily(std::function<void(Result<WizardsVaultProgress>)> cb) const;
        void WizardsVaultWeekly(std::function<void(Result<WizardsVaultProgress>)> cb) const;
        void WizardsVaultSpecial(std::function<void(Result<WizardsVaultProgress>)> cb) const;
        void WizardsVaultListings(std::function<void(Result<std::vector<AccountWizardsVaultListing>>)> cb) const;   // /v2/account/wizardsvault/listings

        // Competitive (scope: pvp / account).
        void PvpHeroes(std::function<void(Result<std::vector<std::string>>)> cb) const;   // /v2/account/pvp/heroes
        void Wvw(std::function<void(Result<AccountWvw>)> cb) const;                        // /v2/account/wvw

    private:
        // Build the authed, short-cached request for /v2/account/<sub> with the given scope.
        Request MakeReq(const char* sub, TokenPermission scope) const;
        // Type-erased helper: GET /v2/account/<sub> and parse with `parse` into T.
        template <typename T>
        void GetSub(const char* sub, TokenPermission scope, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb) const;
        Connection* _c;
    };
}
