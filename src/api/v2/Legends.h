#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/legends (anonymous, day-cached): revenant legends (string id -> swap/heal/elite skill ids + utilities).
// Wiki: API:2/legends.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Legend { std::string id; int swap = 0; int heal = 0; int elite = 0; std::vector<int> utilities; nlohmann::json raw; };

    inline Legend ParseLegend(const nlohmann::json& j)
    {
        Legend l;
        l.id        = Json::Str(j, "id");
        l.swap      = Json::Int(j, "swap");
        l.heal      = Json::Int(j, "heal");
        l.elite     = Json::Int(j, "elite");
        l.utilities = Json::IntArray(j, "utilities");
        l.raw       = j;
        return l;
    }

    class LegendsEndpoint : public BulkEndpoint<Legend, std::string>
    { public: explicit LegendsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/legends", &ParseLegend, kStaticTtlSec) {} };
}
