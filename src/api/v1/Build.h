#pragma once
#include "Common.h"

// GET /v1/build.json (anonymous): the current game build id. Wiki: API:1/build.
namespace Api::V1
{
    inline int ParseBuild(const nlohmann::json& j) { return Json::Int(j, "build_id"); }

    class BuildEndpoint
    {
    public:
        explicit BuildEndpoint(Connection* c) : _c(c) {}
        void Get(std::function<void(Result<int>)> cb) const { Fetch<int>(_c, "/v1/build.json", 5 * 60, &ParseBuild, std::move(cb)); }
    private:
        Connection* _c;
    };
}
