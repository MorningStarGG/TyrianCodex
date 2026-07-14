#pragma once
#include "Common.h"

// GET /v1/event_names.json (anonymous, locale; DEPRECATED upstream but still served): event id -> name.
// Wiki: API:1/event_names.
namespace Api::V1
{
    class EventNamesEndpoint
    {
    public:
        explicit EventNamesEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<NameId>>)> cb) const { Fetch<std::vector<NameId>>(_c, "/v1/event_names.json", kV1StaticTtl, &ParseNameIdList, std::move(cb)); }
    private:
        Connection* _c;
    };
}
