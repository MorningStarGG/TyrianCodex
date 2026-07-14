#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Result.h"

// Structured PvP endpoints. Amulets, Ranks, Heroes, Seasons are ANONYMOUS bulk catalogs; Stats, Standings,
// and Games are AUTHENTICATED (pvp scope) - the player's own record. Detail payloads (amulet attributes,
// rank tiers, season leaderboard config, per-game scores) stay in `raw`. Wiki: API:2/pvp.
namespace Api { class Connection; }
namespace Api::V2
{
    struct PvpAmulet { int id = 0; std::string name; std::string icon; nlohmann::json raw; };   // `attributes` in raw
    struct PvpRank   { int id = 0; int finisherId = 0; std::string name; std::string icon; int minRank = 0; int maxRank = 0; nlohmann::json raw; };
    struct PvpHero   { std::string id; std::string name; std::string type; nlohmann::json raw; };
    struct PvpSeason { std::string id; std::string name; std::string start; std::string end; nlohmann::json raw; };
    struct PvpGame   { std::string id; int mapId = 0; std::string result; std::string profession; nlohmann::json raw; };
    // /v2/pvp/stats: the player's win/loss aggregate (the per-profession + ladder breakdowns stay in `raw`).
    struct PvpStats    { int rank = 0; int64_t rankPoints = 0; int wins = 0; int losses = 0; int victories = 0; int desertions = 0; int byes = 0; int forfeits = 0; nlohmann::json raw; };
    // /v2/pvp/standings row (the per-season current/best ladder objects stay in `raw`).
    struct PvpStanding { std::string seasonId; nlohmann::json raw; };
    // /v2/pvp/seasons/:id/leaderboards/:board/:region entry.
    struct PvpLeaderboardScore { std::string id; int64_t value = 0; };
    struct PvpLeaderboardEntry { std::string name; int rank = 0; std::string date; std::vector<PvpLeaderboardScore> scores; nlohmann::json raw; };

    PvpAmulet ParsePvpAmulet(const nlohmann::json& j);
    PvpRank   ParsePvpRank(const nlohmann::json& j);
    PvpHero   ParsePvpHero(const nlohmann::json& j);
    PvpSeason ParsePvpSeason(const nlohmann::json& j);
    PvpGame   ParsePvpGame(const nlohmann::json& j);

    class PvpEndpoint
    {
    public:
        explicit PvpEndpoint(Connection* c) : _c(c) {}

        // Anonymous catalogs.
        BulkEndpoint<PvpAmulet>            Amulets() const { return BulkEndpoint<PvpAmulet>(_c, "/v2/pvp/amulets", &ParsePvpAmulet, kStaticTtlSec); }
        BulkEndpoint<PvpRank>              Ranks()   const { return BulkEndpoint<PvpRank>(_c, "/v2/pvp/ranks",     &ParsePvpRank,   kStaticTtlSec); }
        BulkEndpoint<PvpHero, std::string> Heroes()  const { return BulkEndpoint<PvpHero, std::string>(_c, "/v2/pvp/heroes", &ParsePvpHero, kStaticTtlSec); }
        BulkEndpoint<PvpSeason, std::string> Seasons() const { return BulkEndpoint<PvpSeason, std::string>(_c, "/v2/pvp/seasons", &ParsePvpSeason, kStaticTtlSec); }

        // Authenticated (pvp scope): the player's record.
        BulkEndpoint<PvpGame, std::string> Games() const { return BulkEndpoint<PvpGame, std::string>(_c, "/v2/pvp/games", &ParsePvpGame, kCacheNever, true, TokenPermission::Pvp); }
        void Stats(std::function<void(Result<PvpStats>)> cb) const;                       // /v2/pvp/stats
        void Standings(std::function<void(Result<std::vector<PvpStanding>>)> cb) const;   // /v2/pvp/standings

        // Season leaderboards (anonymous): the board-name list, then a board's regional ladder entries.
        void SeasonLeaderboards(const std::string& seasonId, std::function<void(Result<std::vector<std::string>>)> cb) const;                                   // /v2/pvp/seasons/:id/leaderboards
        void SeasonLeaderboard(const std::string& seasonId, const std::string& board, const std::string& region, std::function<void(Result<std::vector<PvpLeaderboardEntry>>)> cb) const;  // .../:board/:region

    private:
        Connection* _c;
    };
}
