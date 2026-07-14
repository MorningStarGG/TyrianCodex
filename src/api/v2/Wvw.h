#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"
#include "../core/Result.h"

// World-vs-World endpoints (all ANONYMOUS): Abilities, Ranks, Upgrades, Objectives (catalogs); Matches (live
// state by match id, with /overview, /scores, /stats sub-bulks); Guilds (per-region guild->team claim map);
// Timers (the next lockout / team-assignment instants per region). Deep score/stat trees stay in each row's
// `raw`. Wiki: API:2/wvw.
namespace Api { class Connection; }
namespace Api::V2
{
    struct WvwAbility      { int id = 0; std::string name; std::string description; std::string icon; nlohmann::json raw; };
    struct WvwRank         { int id = 0; std::string title; int minRank = 0; nlohmann::json raw; };
    struct WvwUpgrade      { int id = 0; nlohmann::json raw; };   // tiers in raw
    struct WvwObjective    { std::string id; std::string name; std::string type; std::string mapType; int mapId = 0; nlohmann::json raw; };
    struct WvwMatch        { std::string id; nlohmann::json raw; };   // scores/worlds/maps in raw
    struct WvwMatchOverview{ std::string id; nlohmann::json raw; };   // worlds / all_worlds / start_time in raw
    struct WvwMatchScores  { std::string id; nlohmann::json raw; };   // scores / victory_points / skirmishes in raw
    struct WvwMatchStats   { std::string id; nlohmann::json raw; };   // kills / deaths / per-map in raw
    struct WvwTimer        { std::string na; std::string eu; nlohmann::json raw; };   // /v2/wvw/timers/{lockout,teamAssignment}
    struct WvwGuildClaim   { std::string guildId; std::string team; nlohmann::json raw; };   // a row of /v2/wvw/guilds/:region
    struct WvwMatchGuildStats { std::string id; nlohmann::json raw; };   // a guild's stats in a match (id = guild id)

    WvwAbility   ParseWvwAbility(const nlohmann::json& j);
    WvwRank      ParseWvwRank(const nlohmann::json& j);
    WvwUpgrade   ParseWvwUpgrade(const nlohmann::json& j);
    WvwObjective ParseWvwObjective(const nlohmann::json& j);
    WvwMatch     ParseWvwMatch(const nlohmann::json& j);

    inline WvwMatchOverview ParseWvwMatchOverview(const nlohmann::json& j) { WvwMatchOverview m; m.id = Json::Str(j, "id"); m.raw = j; return m; }
    inline WvwMatchScores   ParseWvwMatchScores(const nlohmann::json& j)   { WvwMatchScores m;   m.id = Json::Str(j, "id"); m.raw = j; return m; }
    inline WvwMatchStats    ParseWvwMatchStats(const nlohmann::json& j)    { WvwMatchStats m;    m.id = Json::Str(j, "id"); m.raw = j; return m; }

    class WvwMatchOverviewEndpoint : public BulkEndpoint<WvwMatchOverview, std::string> { public: explicit WvwMatchOverviewEndpoint(Connection* c) : BulkEndpoint(c, "/v2/wvw/matches/overview", &ParseWvwMatchOverview, 30) {} };
    class WvwMatchScoresEndpoint   : public BulkEndpoint<WvwMatchScores, std::string>   { public: explicit WvwMatchScoresEndpoint(Connection* c)   : BulkEndpoint(c, "/v2/wvw/matches/scores",   &ParseWvwMatchScores,   30) {} };
    class WvwMatchStatsEndpoint    : public BulkEndpoint<WvwMatchStats, std::string>    { public: explicit WvwMatchStatsEndpoint(Connection* c)    : BulkEndpoint(c, "/v2/wvw/matches/stats",    &ParseWvwMatchStats,    30) {} };

    // /v2/wvw/matches (live, short cache) + the /overview, /scores, /stats sub-bulks.
    class WvwMatchesEndpoint : public BulkEndpoint<WvwMatch, std::string>
    {
    public:
        explicit WvwMatchesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/wvw/matches", &ParseWvwMatch, 30), _cc(c) {}
        WvwMatchOverviewEndpoint Overview() const { return WvwMatchOverviewEndpoint(_cc); }
        WvwMatchScoresEndpoint   Scores()   const { return WvwMatchScoresEndpoint(_cc); }
        WvwMatchStatsEndpoint    Stats()    const { return WvwMatchStatsEndpoint(_cc); }
        // Deep per-match stat sub-routes (/stats/:id/...).
        void StatsByGuild(const std::string& matchId, const std::string& guildId, std::function<void(Result<WvwMatchGuildStats>)> cb) const;   // /guilds/:guild_id
        void TopKdr(const std::string& matchId, const std::string& team, std::function<void(Result<std::vector<std::string>>)> cb) const;       // /teams/:team/top/kdr
        void TopKills(const std::string& matchId, const std::string& team, std::function<void(Result<std::vector<std::string>>)> cb) const;     // /teams/:team/top/kills
    private:
        Connection* _cc;
    };

    // /v2/wvw/timers: .Index() (region keys), .Lockout(), .TeamAssignment() (each {na, eu} instants).
    class WvwTimersEndpoint
    {
    public:
        explicit WvwTimersEndpoint(Connection* c) : _c(c) {}
        void Index(std::function<void(Result<std::vector<std::string>>)> cb) const;       // /v2/wvw/timers
        void Lockout(std::function<void(Result<WvwTimer>)> cb) const;                      // /v2/wvw/timers/lockout
        void TeamAssignment(std::function<void(Result<WvwTimer>)> cb) const;               // /v2/wvw/timers/teamAssignment
    private:
        Connection* _c;
    };

    class WvwEndpoint
    {
    public:
        explicit WvwEndpoint(Connection* c) : _c(c) {}
        BulkEndpoint<WvwAbility>                Abilities()  const { return BulkEndpoint<WvwAbility>(_c, "/v2/wvw/abilities", &ParseWvwAbility, kStaticTtlSec); }
        BulkEndpoint<WvwRank>                   Ranks()      const { return BulkEndpoint<WvwRank>(_c, "/v2/wvw/ranks",         &ParseWvwRank,    kStaticTtlSec); }
        BulkEndpoint<WvwUpgrade>                Upgrades()   const { return BulkEndpoint<WvwUpgrade>(_c, "/v2/wvw/upgrades",   &ParseWvwUpgrade, kStaticTtlSec); }
        BulkEndpoint<WvwObjective, std::string> Objectives() const { return BulkEndpoint<WvwObjective, std::string>(_c, "/v2/wvw/objectives", &ParseWvwObjective, kStaticTtlSec); }
        WvwMatchesEndpoint                      Matches()    const { return WvwMatchesEndpoint(_c); }
        WvwTimersEndpoint                       Timers()     const { return WvwTimersEndpoint(_c); }

        void Guilds(std::function<void(Result<std::vector<std::string>>)> cb) const;                                   // /v2/wvw/guilds (region index)
        void Guilds(const std::string& region, std::function<void(Result<std::vector<WvwGuildClaim>>)> cb) const;      // /v2/wvw/guilds/:region
    private:
        Connection* _c;
    };
}
