#pragma once
#include "Common.h"

// GET /v1/map_names.json (anonymous, locale): map id -> name. Wiki: API:1/map_names.
namespace Api::V1
{
    class MapNamesEndpoint
    {
    public:
        explicit MapNamesEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<NameId>>)> cb) const { Fetch<std::vector<NameId>>(_c, "/v1/map_names.json", kV1StaticTtl, &ParseNameIdList, std::move(cb)); }
    private:
        Connection* _c;
    };
}
