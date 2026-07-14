#pragma once
#include "Common.h"

// GET /v1/world_names.json (anonymous, locale): world (server) id -> name. Wiki: API:1/world_names.
namespace Api::V1
{
    class WorldNamesEndpoint
    {
    public:
        explicit WorldNamesEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<NameId>>)> cb) const { Fetch<std::vector<NameId>>(_c, "/v1/world_names.json", kV1StaticTtl, &ParseNameIdList, std::move(cb)); }
    private:
        Connection* _c;
    };
}
