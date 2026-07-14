#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/finishers (anonymous, day-cached): the finisher catalog (id -> name/icon/order + unlock details +
// unlock item ids). Wiki: API:2/finishers.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Finisher { int id = 0; std::string name; std::string icon; int order = 0; std::string unlockDetails; std::vector<int> unlockItems; nlohmann::json raw; };

    inline Finisher ParseFinisher(const nlohmann::json& j)
    {
        Finisher f;
        f.id            = Json::Int(j, "id");
        f.name          = Json::Str(j, "name");
        f.icon          = Json::Str(j, "icon");
        f.order         = Json::Int(j, "order");
        f.unlockDetails = Json::Str(j, "unlock_details");
        f.unlockItems   = Json::IntArray(j, "unlock_items");
        f.raw           = j;
        return f;
    }

    class FinishersEndpoint : public BulkEndpoint<Finisher>
    { public: explicit FinishersEndpoint(Connection* c) : BulkEndpoint(c, "/v2/finishers", &ParseFinisher, kStaticTtlSec) {} };
}
