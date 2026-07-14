#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Result.h"

// GET /v2/achievements (by id)  (anonymous; static). The achievement CATALOG: name, icon (a
// render.guildwars2.com URL), description, requirement, and `rewards` (type/id/count). This is distinct from
// /v2/account/achievements (the player's PROGRESS, in Account.h). The Journal tab fetches the achievements an
// episode unlocks (mapped via our stories.json achievementIds) to draw its Achievements & Rewards block.
// Wiki: API:2/achievements.
namespace Api { class Connection; }
namespace Api::V2
{
    struct AchievementReward
    {
        std::string type;     // "Coins" / "Item" / "Mastery" / "Title"
        int         id    = 0;
        int         count = 0;
        std::string region;   // Mastery only
    };

    struct Achievement
    {
        int                           id = 0;
        std::string                   name;
        std::string                   description;
        std::string                   requirement;
        std::string                   icon;       // render.guildwars2.com URL (may be empty)
        std::vector<AchievementReward> rewards;
        nlohmann::json                raw;
    };

    // /v2/achievements/categories: a tab in the in-game Achievements panel (its `achievements` id list).
    struct AchievementCategory { int id = 0; std::string name; std::string description; int order = 0; std::string icon; std::vector<int> achievements; nlohmann::json raw; };
    // /v2/achievements/groups: a top-level group ("Heart of Thorns", "Story Journal", ...) of categories.
    struct AchievementGroup    { std::string id; std::string name; std::string description; int order = 0; std::vector<int> categories; nlohmann::json raw; };
    // /v2/achievements/daily[/tomorrow] (server-deprecated): the rotating daily set, bucketed by content type.
    struct DailyAchievement    { int id = 0; int levelMin = 0; int levelMax = 0; nlohmann::json raw; };
    struct DailyAchievements   { std::vector<DailyAchievement> pve, pvp, wvw, fractals, special; nlohmann::json raw; };

    Achievement         ParseAchievement(const nlohmann::json& j);
    AchievementCategory ParseAchievementCategory(const nlohmann::json& j);
    AchievementGroup    ParseAchievementGroup(const nlohmann::json& j);

    class AchievementCategoriesEndpoint : public BulkEndpoint<AchievementCategory>              { public: explicit AchievementCategoriesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/achievements/categories", &ParseAchievementCategory, kStaticTtlSec) {} };
    class AchievementGroupsEndpoint     : public BulkEndpoint<AchievementGroup, std::string>    { public: explicit AchievementGroupsEndpoint(Connection* c)     : BulkEndpoint(c, "/v2/achievements/groups",     &ParseAchievementGroup,    kStaticTtlSec) {} };

    class AchievementsEndpoint
    {
    public:
        explicit AchievementsEndpoint(Connection* c) : _c(c) {}
        void Get(int id, std::function<void(Result<Achievement>)> cb) const;
        void Get(const std::vector<int>& ids, std::function<void(Result<std::vector<Achievement>>)> cb) const;

        // Sub-resources (anonymous): the category / group catalogs + the rotating daily set.
        AchievementCategoriesEndpoint Categories() const { return AchievementCategoriesEndpoint(_c); }
        AchievementGroupsEndpoint     Groups()     const { return AchievementGroupsEndpoint(_c); }
        void Daily(std::function<void(Result<DailyAchievements>)> cb) const;           // /v2/achievements/daily (deprecated)
        void DailyTomorrow(std::function<void(Result<DailyAchievements>)> cb) const;   // /v2/achievements/daily/tomorrow (deprecated)

    private:
        Connection* _c;
    };
}
