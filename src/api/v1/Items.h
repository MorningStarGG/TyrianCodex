#pragma once
#include "Common.h"

// GET /v1/items.json (anonymous): the bare list of all item ids ({"items":[...]}). Detail via item_details.
// Wiki: API:1/items.
namespace Api::V1
{
    inline std::vector<int> ParseItemIds(const nlohmann::json& j) { return ParseWrappedIntIds(j, "items"); }

    class ItemsEndpoint
    {
    public:
        explicit ItemsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<int>>)> cb) const { Fetch<std::vector<int>>(_c, "/v1/items.json", kV1StaticTtl, &ParseItemIds, std::move(cb)); }
    private:
        Connection* _c;
    };
}
