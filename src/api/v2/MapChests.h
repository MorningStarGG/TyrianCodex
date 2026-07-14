#pragma once
#include "BulkEndpoint.h"
#include "Common.h"   // NamedList + ParseNamedList (shared id-only catalog row)

// GET /v2/mapchests (anonymous, day-cached): the daily hero's-choice map-chest id catalog. Wiki: API:2/mapchests.
namespace Api { class Connection; }
namespace Api::V2
{
    class MapChestsEndpoint : public BulkEndpoint<NamedList, std::string>
    { public: explicit MapChestsEndpoint(Connection* c) : BulkEndpoint(c, "/v2/mapchests", &ParseNamedList, kStaticTtlSec) {} };
}
