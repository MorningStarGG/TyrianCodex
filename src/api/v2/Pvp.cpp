#include "Pvp.h"

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    PvpAmulet ParsePvpAmulet(const nlohmann::json& j)
    {
        PvpAmulet a;
        a.id   = Json::Int(j, "id");
        a.name = Json::Str(j, "name");
        a.icon = Json::Str(j, "icon");
        a.raw  = j;
        return a;
    }

    PvpRank ParsePvpRank(const nlohmann::json& j)
    {
        PvpRank r;
        r.id         = Json::Int(j, "id");
        r.finisherId = Json::Int(j, "finisher_id");
        r.name       = Json::Str(j, "name");
        r.icon       = Json::Str(j, "icon");
        r.minRank    = Json::Int(j, "min_rank");
        r.maxRank    = Json::Int(j, "max_rank");
        r.raw        = j;   // the `levels` tiers live in raw
        return r;
    }

    PvpHero ParsePvpHero(const nlohmann::json& j)
    {
        PvpHero h;
        h.id   = Json::Str(j, "id");
        h.name = Json::Str(j, "name");
        h.type = Json::Str(j, "type");
        h.raw  = j;
        return h;
    }

    PvpSeason ParsePvpSeason(const nlohmann::json& j)
    {
        PvpSeason s;
        s.id    = Json::Str(j, "id");
        s.name  = Json::Str(j, "name");
        s.start = Json::Str(j, "start");
        s.end   = Json::Str(j, "end");
        s.raw   = j;   // divisions / leaderboards config live in raw
        return s;
    }

    PvpGame ParsePvpGame(const nlohmann::json& j)
    {
        PvpGame g;
        g.id         = Json::Str(j, "id");
        g.mapId      = Json::Int(j, "map_id");
        g.result     = Json::Str(j, "result");
        g.profession = Json::Str(j, "profession");
        g.raw        = j;   // scores / rating_type / team live in raw
        return g;
    }

    static PvpStats ParsePvpStats(const nlohmann::json& j)
    {
        PvpStats s;
        s.rank       = Json::Int(j, "pvp_rank");
        s.rankPoints = Json::Int64(j, "pvp_rank_points");
        const nlohmann::json& a = Json::Node(j, "aggregate");
        s.wins = Json::Int(a, "wins"); s.losses = Json::Int(a, "losses"); s.victories = Json::Int(a, "victories");
        s.desertions = Json::Int(a, "desertions"); s.byes = Json::Int(a, "byes"); s.forfeits = Json::Int(a, "forfeits");
        s.raw = j;   // per-profession + ladder breakdowns
        return s;
    }
    static std::vector<PvpStanding> ParsePvpStandings(const nlohmann::json& j)
    {
        std::vector<PvpStanding> out;
        if (j.is_array()) for (const auto& e : j) { PvpStanding s; s.seasonId = Json::Str(e, "season_id"); s.raw = e; out.push_back(std::move(s)); }
        return out;
    }

    void PvpEndpoint::Stats(std::function<void(Result<PvpStats>)> cb) const
    {
        Request req;
        req.path = "/v2/pvp/stats";
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Pvp;
        req.cacheTtlSec = 5 * 60;
        _c->Get<PvpStats>(std::move(req), &ParsePvpStats, std::move(cb));
    }

    void PvpEndpoint::Standings(std::function<void(Result<std::vector<PvpStanding>>)> cb) const
    {
        Request req;
        req.path = "/v2/pvp/standings";
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Pvp;
        req.cacheTtlSec = 5 * 60;
        _c->Get<std::vector<PvpStanding>>(std::move(req), &ParsePvpStandings, std::move(cb));
    }

    static std::vector<std::string> ParsePvpBoardNames(const nlohmann::json& j)
    {
        std::vector<std::string> out;
        if (j.is_array()) for (const auto& e : j) if (e.is_string()) out.push_back(e.get<std::string>());
        return out;
    }
    static std::vector<PvpLeaderboardEntry> ParsePvpLeaderboard(const nlohmann::json& j)
    {
        std::vector<PvpLeaderboardEntry> out;
        if (j.is_array()) for (const auto& e : j)
        {
            PvpLeaderboardEntry ent; ent.name = Json::Str(e, "name"); ent.rank = Json::Int(e, "rank"); ent.date = Json::Str(e, "date");
            for (const auto& s : Json::Node(e, "scores")) if (s.is_object()) ent.scores.push_back({ Json::Str(s, "id"), Json::Int64(s, "value") });
            ent.raw = e; out.push_back(std::move(ent));
        }
        return out;
    }

    void PvpEndpoint::SeasonLeaderboards(const std::string& seasonId, std::function<void(Result<std::vector<std::string>>)> cb) const
    {
        Request req;
        req.path        = "/v2/pvp/seasons/" + seasonId + "/leaderboards";
        req.cacheTtlSec = 5 * 60;
        _c->Get<std::vector<std::string>>(std::move(req), &ParsePvpBoardNames, std::move(cb));
    }
    void PvpEndpoint::SeasonLeaderboard(const std::string& seasonId, const std::string& board, const std::string& region, std::function<void(Result<std::vector<PvpLeaderboardEntry>>)> cb) const
    {
        Request req;
        req.path        = "/v2/pvp/seasons/" + seasonId + "/leaderboards/" + board + "/" + region;
        req.cacheTtlSec = 5 * 60;
        _c->Get<std::vector<PvpLeaderboardEntry>>(std::move(req), &ParsePvpLeaderboard, std::move(cb));
    }
}
