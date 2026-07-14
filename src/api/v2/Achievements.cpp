#include "Achievements.h"

#include <string>

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    static constexpr int kStaticTtl = 24 * 60 * 60;   // the achievement catalog is static reference data

    Achievement ParseAchievement(const nlohmann::json& j)
    {
        Achievement a;
        a.id          = Json::Int(j, "id");
        a.name        = Json::Str(j, "name");
        a.description = Json::Str(j, "description");
        a.requirement = Json::Str(j, "requirement");
        a.icon        = Json::Str(j, "icon");
        const nlohmann::json& rw = Json::Node(j, "rewards");
        if (rw.is_array())
            for (const auto& r : rw)
            {
                AchievementReward rr;
                rr.type   = Json::Str(r, "type");
                rr.id     = Json::Int(r, "id");
                rr.count  = Json::Int(r, "count");
                rr.region = Json::Str(r, "region");
                a.rewards.push_back(std::move(rr));
            }
        a.raw = j;
        return a;
    }

    void AchievementsEndpoint::Get(int id, std::function<void(Result<Achievement>)> cb) const
    {
        Request req;
        req.path        = "/v2/achievements/" + std::to_string(id);
        req.cacheTtlSec = kStaticTtl;
        _c->Get<Achievement>(std::move(req), &ParseAchievement, std::move(cb));
    }

    void AchievementsEndpoint::Get(const std::vector<int>& ids, std::function<void(Result<std::vector<Achievement>>)> cb) const
    {
        Request req;
        req.path        = "/v2/achievements";
        req.cacheTtlSec = kStaticTtl;
        for (int id : ids) req.bulkIds.push_back(std::to_string(id));
        _c->Get<std::vector<Achievement>>(std::move(req),
            [](const nlohmann::json& j) {
                std::vector<Achievement> out;
                if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseAchievement(e)); }
                return out;
            },
            std::move(cb));
    }

    AchievementCategory ParseAchievementCategory(const nlohmann::json& j)
    {
        AchievementCategory c;
        c.id           = Json::Int(j, "id");
        c.name         = Json::Str(j, "name");
        c.description  = Json::Str(j, "description");
        c.order        = Json::Int(j, "order");
        c.icon         = Json::Str(j, "icon");
        c.achievements = Json::IntArray(j, "achievements");   // newer schema nests {id,...}; raw has the full form
        c.raw          = j;
        return c;
    }

    AchievementGroup ParseAchievementGroup(const nlohmann::json& j)
    {
        AchievementGroup g;
        g.id          = Json::Str(j, "id");
        g.name        = Json::Str(j, "name");
        g.description = Json::Str(j, "description");
        g.order       = Json::Int(j, "order");
        g.categories  = Json::IntArray(j, "categories");
        g.raw         = j;
        return g;
    }

    static DailyAchievement ParseDailyAch(const nlohmann::json& j)
    {
        DailyAchievement d; d.id = Json::Int(j, "id");
        const nlohmann::json& lv = Json::Node(j, "level"); d.levelMin = Json::Int(lv, "min"); d.levelMax = Json::Int(lv, "max");
        d.raw = j; return d;
    }
    static DailyAchievements ParseDailyAchievements(const nlohmann::json& j)
    {
        DailyAchievements d;
        auto bucket = [&](const char* key, std::vector<DailyAchievement>& out) {
            for (const auto& e : Json::Node(j, key)) if (e.is_object()) out.push_back(ParseDailyAch(e));
        };
        bucket("pve", d.pve); bucket("pvp", d.pvp); bucket("wvw", d.wvw); bucket("fractals", d.fractals); bucket("special", d.special);
        d.raw = j; return d;
    }

    void AchievementsEndpoint::Daily(std::function<void(Result<DailyAchievements>)> cb) const
    {
        Request req; req.path = "/v2/achievements/daily"; req.cacheTtlSec = 5 * 60;
        _c->Get<DailyAchievements>(std::move(req), &ParseDailyAchievements, std::move(cb));
    }
    void AchievementsEndpoint::DailyTomorrow(std::function<void(Result<DailyAchievements>)> cb) const
    {
        Request req; req.path = "/v2/achievements/daily/tomorrow"; req.cacheTtlSec = 5 * 60;
        _c->Get<DailyAchievements>(std::move(req), &ParseDailyAchievements, std::move(cb));
    }
}
