#pragma once
#include "Common.h"

// GET /v1/skins.json (anonymous): the bare list of all skin ids ({"skins":[...]}). Detail via skin_details.
// Wiki: API:1/skins.
namespace Api::V1
{
    inline std::vector<int> ParseSkinIds(const nlohmann::json& j) { return ParseWrappedIntIds(j, "skins"); }

    class SkinsEndpoint
    {
    public:
        explicit SkinsEndpoint(Connection* c) : _c(c) {}
        void GetAll(std::function<void(Result<std::vector<int>>)> cb) const { Fetch<std::vector<int>>(_c, "/v1/skins.json", kV1StaticTtl, &ParseSkinIds, std::move(cb)); }
    private:
        Connection* _c;
    };
}
