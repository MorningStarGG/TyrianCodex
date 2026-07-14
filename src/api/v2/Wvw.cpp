#include "Wvw.h"

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    WvwAbility ParseWvwAbility(const nlohmann::json& j)
    {
        WvwAbility a;
        a.id          = Json::Int(j, "id");
        a.name        = Json::Str(j, "name");
        a.description = Json::Str(j, "description");
        a.icon        = Json::Str(j, "icon");
        a.raw         = j;   // per-rank effects live in raw
        return a;
    }

    WvwRank ParseWvwRank(const nlohmann::json& j)
    {
        WvwRank r;
        r.id      = Json::Int(j, "id");
        r.title   = Json::Str(j, "title");
        r.minRank = Json::Int(j, "min_rank");
        r.raw     = j;
        return r;
    }

    WvwUpgrade ParseWvwUpgrade(const nlohmann::json& j)
    {
        WvwUpgrade u;
        u.id  = Json::Int(j, "id");
        u.raw = j;   // the `tiers` (upgrade levels) live in raw
        return u;
    }

    WvwObjective ParseWvwObjective(const nlohmann::json& j)
    {
        WvwObjective o;
        o.id      = Json::Str(j, "id");
        o.name    = Json::Str(j, "name");
        o.type    = Json::Str(j, "type");
        o.mapType = Json::Str(j, "map_type");
        o.mapId   = Json::Int(j, "map_id");
        o.raw     = j;   // coord / label_coord / sector_id live in raw
        return o;
    }

    WvwMatch ParseWvwMatch(const nlohmann::json& j)
    {
        WvwMatch m;
        m.id  = Json::Str(j, "id");
        m.raw = j;   // scores / victory_points / worlds / maps live in raw
        return m;
    }

    static std::vector<std::string> ParseWvwStrList(const nlohmann::json& j)
    {
        std::vector<std::string> out;
        if (j.is_array()) for (const auto& e : j) if (e.is_string()) out.push_back(e.get<std::string>());
        return out;
    }
    static WvwTimer ParseWvwTimer(const nlohmann::json& j)
    {
        WvwTimer t; t.na = Json::Str(j, "na"); t.eu = Json::Str(j, "eu"); t.raw = j; return t;
    }
    // /v2/wvw/guilds/:region is an object { "<guild-id>": "<team-id>", ... } -> rows.
    static std::vector<WvwGuildClaim> ParseWvwGuildClaims(const nlohmann::json& j)
    {
        std::vector<WvwGuildClaim> out;
        if (j.is_object()) for (auto it = j.begin(); it != j.end(); ++it)
        {
            WvwGuildClaim c; c.guildId = it.key();
            c.team = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
            c.raw = it.value(); out.push_back(std::move(c));
        }
        return out;
    }

    void WvwTimersEndpoint::Index(std::function<void(Result<std::vector<std::string>>)> cb) const
    {
        Request req; req.path = "/v2/wvw/timers"; req.cacheTtlSec = 60;
        _c->Get<std::vector<std::string>>(std::move(req), &ParseWvwStrList, std::move(cb));
    }
    void WvwTimersEndpoint::Lockout(std::function<void(Result<WvwTimer>)> cb) const
    {
        Request req; req.path = "/v2/wvw/timers/lockout"; req.cacheTtlSec = 60;
        _c->Get<WvwTimer>(std::move(req), &ParseWvwTimer, std::move(cb));
    }
    void WvwTimersEndpoint::TeamAssignment(std::function<void(Result<WvwTimer>)> cb) const
    {
        Request req; req.path = "/v2/wvw/timers/teamAssignment"; req.cacheTtlSec = 60;
        _c->Get<WvwTimer>(std::move(req), &ParseWvwTimer, std::move(cb));
    }

    void WvwEndpoint::Guilds(std::function<void(Result<std::vector<std::string>>)> cb) const
    {
        Request req; req.path = "/v2/wvw/guilds"; req.cacheTtlSec = 60;
        _c->Get<std::vector<std::string>>(std::move(req), &ParseWvwStrList, std::move(cb));
    }
    void WvwEndpoint::Guilds(const std::string& region, std::function<void(Result<std::vector<WvwGuildClaim>>)> cb) const
    {
        Request req; req.path = "/v2/wvw/guilds/" + region; req.cacheTtlSec = 60;
        _c->Get<std::vector<WvwGuildClaim>>(std::move(req), &ParseWvwGuildClaims, std::move(cb));
    }

    static WvwMatchGuildStats ParseWvwMatchGuildStats(const nlohmann::json& j) { WvwMatchGuildStats g; g.id = Json::Str(j, "id"); g.raw = j; return g; }

    void WvwMatchesEndpoint::StatsByGuild(const std::string& matchId, const std::string& guildId, std::function<void(Result<WvwMatchGuildStats>)> cb) const
    {
        Request req; req.path = "/v2/wvw/matches/stats/" + matchId + "/guilds/" + guildId; req.cacheTtlSec = 60;
        _cc->Get<WvwMatchGuildStats>(std::move(req), &ParseWvwMatchGuildStats, std::move(cb));
    }
    void WvwMatchesEndpoint::TopKdr(const std::string& matchId, const std::string& team, std::function<void(Result<std::vector<std::string>>)> cb) const
    {
        Request req; req.path = "/v2/wvw/matches/stats/" + matchId + "/teams/" + team + "/top/kdr"; req.cacheTtlSec = 60;
        _cc->Get<std::vector<std::string>>(std::move(req), &ParseWvwStrList, std::move(cb));
    }
    void WvwMatchesEndpoint::TopKills(const std::string& matchId, const std::string& team, std::function<void(Result<std::vector<std::string>>)> cb) const
    {
        Request req; req.path = "/v2/wvw/matches/stats/" + matchId + "/teams/" + team + "/top/kills"; req.cacheTtlSec = 60;
        _cc->Get<std::vector<std::string>>(std::move(req), &ParseWvwStrList, std::move(cb));
    }
}
