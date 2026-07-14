#pragma once
#include "BulkEndpoint.h"
#include "Common.h"   // NamedList + ParseNamedList (shared id-only catalog row)

// GET /v2/dailycrafting (anonymous, day-cached): the daily time-gated crafting id catalog. Wiki: API:2/dailycrafting.
namespace Api { class Connection; }
namespace Api::V2
{
    class DailyCraftingEndpoint : public BulkEndpoint<NamedList, std::string>
    { public: explicit DailyCraftingEndpoint(Connection* c) : BulkEndpoint(c, "/v2/dailycrafting", &ParseNamedList, kStaticTtlSec) {} };
}
