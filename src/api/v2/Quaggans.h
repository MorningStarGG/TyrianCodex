#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/quaggans (anonymous, day-cached): the quaggan image set (string id -> image url). Wiki: API:2/quaggans.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Quaggan { std::string id; std::string url; nlohmann::json raw; };

    inline Quaggan ParseQuaggan(const nlohmann::json& j)
    {
        Quaggan q;
        q.id  = Json::Str(j, "id");
        q.url = Json::Str(j, "url");
        q.raw = j;
        return q;
    }

    class QuaggansEndpoint : public BulkEndpoint<Quaggan, std::string>
    { public: explicit QuaggansEndpoint(Connection* c) : BulkEndpoint(c, "/v2/quaggans", &ParseQuaggan, kStaticTtlSec) {} };
}
