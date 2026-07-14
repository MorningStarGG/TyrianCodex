#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/raids (anonymous, day-cached): the raid catalog (string id -> wing ids). Wiki: API:2/raids.
namespace Api { class Connection; }
namespace Api::V2
{
    struct RaidWing { std::string id; std::vector<std::string> bosses; };   // wing id + its BOSS event ids (in order)
    struct Raid { std::string id; std::vector<std::string> wings; std::vector<RaidWing> wingDetail; nlohmann::json raw; };

    inline Raid ParseRaid(const nlohmann::json& j)
    {
        Raid r;
        r.id = Json::Str(j, "id");
        for (const auto& w : Json::Node(j, "wings"))   // [{id, events:[{id,type}]}, ...]
            if (w.is_object())
            {
                r.wings.push_back(Json::Str(w, "id"));
                RaidWing wd; wd.id = Json::Str(w, "id");
                for (const auto& e : Json::Node(w, "events"))
                    if (e.is_object() && Json::Str(e, "type") == "Boss")
                        wd.bosses.push_back(Json::Str(e, "id"));
                r.wingDetail.push_back(std::move(wd));
            }
        r.raw = j;
        return r;
    }

    class RaidsEndpoint : public BulkEndpoint<Raid, std::string>
    { public: explicit RaidsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/raids", &ParseRaid, kStaticTtlSec) {} };
}
