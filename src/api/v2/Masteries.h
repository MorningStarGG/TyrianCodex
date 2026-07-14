#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/masteries (anonymous, day-cached): the mastery-track catalog (id -> name/requirement/order/region).
// The per-track `levels` array stays in `raw`. (Distinct from /v2/account/masteries, the player's progress.)
// Wiki: API:2/masteries.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Mastery { int id = 0; std::string name; std::string requirement; int order = 0; std::string region; std::string background; nlohmann::json raw; };

    inline Mastery ParseMastery(const nlohmann::json& j)
    {
        Mastery m;
        m.id          = Json::Int(j, "id");
        m.name        = Json::Str(j, "name");
        m.requirement = Json::Str(j, "requirement");
        m.order       = Json::Int(j, "order");
        m.region      = Json::Str(j, "region");
        m.background   = Json::Str(j, "background");
        m.raw         = j;   // the `levels` track lives in raw
        return m;
    }

    class MasteriesEndpoint : public BulkEndpoint<Mastery>
    { public: explicit MasteriesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/masteries", &ParseMastery, kStaticTtlSec) {} };
}
