#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "BulkEndpoint.h"
#include "../core/Json.h"

// GET /v2/logos (anonymous, day-cached): the expansion/feature logo image catalog. The id index is bare
// strings (e.g. "ArenaNet-Path-of-Fire-logo"); a detail carries the image url. Wiki: API:2/logos.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Logo { std::string id; std::string url; nlohmann::json raw; };

    inline Logo ParseLogo(const nlohmann::json& j)
    {
        Logo l;
        if (j.is_string()) l.id = j.get<std::string>();
        else { l.id = Json::Str(j, "id"); l.url = Json::Str(j, "url"); }
        l.raw = j;
        return l;
    }

    class LogosEndpoint : public BulkEndpoint<Logo, std::string>
    { public: explicit LogosEndpoint(Connection* c) : BulkEndpoint(c, "/v2/logos", &ParseLogo, kStaticTtlSec) {} };
}
