#pragma once
#include "BulkEndpoint.h"
#include "Common.h"   // NamedList + ParseNamedList (shared id-only catalog row)

// GET /v2/worldbosses (anonymous, day-cached): the daily world-boss id catalog. Wiki: API:2/worldbosses.
namespace Api { class Connection; }
namespace Api::V2
{
    class WorldBossesEndpoint : public BulkEndpoint<NamedList, std::string>
    { public: explicit WorldBossesEndpoint(Connection* c) : BulkEndpoint(c, "/v2/worldbosses", &ParseNamedList, kStaticTtlSec) {} };
}
