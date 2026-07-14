#pragma once
#include "Common.h"

// GET /v1/wvw/{matches,match_details,objective_names}.json (anonymous): the legacy WvW match list, one match's
// detailed scores/map breakdown, and the objective id->name catalog. Wiki: API:1/wvw.
namespace Api::V1
{
    struct WvwMatch        { std::string matchId; int redWorldId = 0; int blueWorldId = 0; int greenWorldId = 0; std::string startTime; std::string endTime; nlohmann::json raw; };
    struct WvwMatchMap     { std::string type; std::vector<int> scores; nlohmann::json objectives; nlohmann::json bonuses; nlohmann::json raw; };
    struct WvwMatchDetails { std::string matchId; std::vector<int> scores; std::vector<WvwMatchMap> maps; nlohmann::json raw; };

    inline std::vector<WvwMatch> ParseWvwMatches(const nlohmann::json& j)
    {
        std::vector<WvwMatch> out;
        for (const auto& m : Json::Node(j, "wvw_matches"))
            if (m.is_object())
            {
                WvwMatch w; w.matchId = Json::Str(m, "wvw_match_id");
                w.redWorldId = IntS(m, "red_world_id"); w.blueWorldId = IntS(m, "blue_world_id"); w.greenWorldId = IntS(m, "green_world_id");
                w.startTime = Json::Str(m, "start_time"); w.endTime = Json::Str(m, "end_time"); w.raw = m;
                out.push_back(std::move(w));
            }
        return out;
    }
    inline WvwMatchDetails ParseWvwMatchDetails(const nlohmann::json& j)
    {
        WvwMatchDetails d; d.matchId = Json::Str(j, "match_id"); d.scores = Json::IntArray(j, "scores");
        for (const auto& m : Json::Node(j, "maps"))
            if (m.is_object()) { WvwMatchMap mm; mm.type = Json::Str(m, "type"); mm.scores = Json::IntArray(m, "scores"); mm.objectives = Json::Node(m, "objectives"); mm.bonuses = Json::Node(m, "bonuses"); mm.raw = m; d.maps.push_back(std::move(mm)); }
        d.raw = j;
        return d;
    }

    class WvwEndpoint
    {
    public:
        explicit WvwEndpoint(Connection* c) : _c(c) {}
        void Matches(std::function<void(Result<std::vector<WvwMatch>>)> cb) const                         { Fetch<std::vector<WvwMatch>>(_c, "/v1/wvw/matches.json", kV1LiveTtl, &ParseWvwMatches, std::move(cb)); }
        void MatchDetails(const std::string& matchId, std::function<void(Result<WvwMatchDetails>)> cb) const { FetchQ<WvwMatchDetails>(_c, "/v1/wvw/match_details.json", "match_id", matchId, kV1LiveTtl, &ParseWvwMatchDetails, std::move(cb)); }
        void ObjectiveNames(std::function<void(Result<std::vector<NameId>>)> cb) const                    { Fetch<std::vector<NameId>>(_c, "/v1/wvw/objective_names.json", kV1StaticTtl, &ParseNameIdList, std::move(cb)); }
    private:
        Connection* _c;
    };
}
