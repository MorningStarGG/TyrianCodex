#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/currencies (anonymous, day-cached): the wallet-currency catalog (id -> name/description/icon/order).
// Wiki: API:2/currencies.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Currency { int id = 0; std::string name; std::string description; std::string icon; int order = 0; nlohmann::json raw; };

    inline Currency ParseCurrency(const nlohmann::json& j)
    {
        Currency c;
        c.id          = Json::Int(j, "id");
        c.name        = Json::Str(j, "name");
        c.description = Json::Str(j, "description");
        c.icon        = Json::Str(j, "icon");
        c.order       = Json::Int(j, "order");
        c.raw         = j;
        return c;
    }

    class CurrenciesEndpoint : public BulkEndpoint<Currency>
    { public: explicit CurrenciesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/currencies", &ParseCurrency, kStaticTtlSec) {} };
}
