#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/mailcarriers (anonymous, day-cached): the mail-carrier catalog (id -> icon/order + unlock item ids).
// Wiki: API:2/mailcarriers.
namespace Api { class Connection; }
namespace Api::V2
{
    struct MailCarrier { int id = 0; std::string icon; int order = 0; std::vector<int> unlockItems; nlohmann::json raw; };

    inline MailCarrier ParseMailCarrier(const nlohmann::json& j)
    {
        MailCarrier m;
        m.id          = Json::Int(j, "id");
        m.icon        = Json::Str(j, "icon");
        m.order       = Json::Int(j, "order");
        m.unlockItems = Json::IntArray(j, "unlock_items");
        m.raw         = j;
        return m;
    }

    class MailCarriersEndpoint : public BulkEndpoint<MailCarrier>
    { public: explicit MailCarriersEndpoint(Connection* c) : BulkEndpoint(c, "/v2/mailcarriers", &ParseMailCarrier, kStaticTtlSec) {} };
}
