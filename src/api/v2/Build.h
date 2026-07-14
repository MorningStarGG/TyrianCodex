#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include "../core/Connection.h"
#include "../core/Request.h"
#include "../core/Result.h"
#include "../core/Json.h"
#include "BulkEndpoint.h"   // kStaticTtlSec (not used here, but keeps the v2 include shape uniform)

// GET /v2/build (anonymous): the current game build id (singleton, not bulk-expanded). Changes on a game
// patch, so a short cache. Wiki: API:2/build.
namespace Api { class Connection; }
namespace Api::V2
{
    class BuildEndpoint
    {
    public:
        explicit BuildEndpoint(Connection* c) : _c(c) {}
        void Get(std::function<void(Result<int>)> cb) const
        {
            Request req;
            req.path        = "/v2/build";
            req.cacheTtlSec = 5 * 60;   // the build id changes on a game patch; a short cache is plenty
            _c->Get<int>(std::move(req), [](const nlohmann::json& j) { return Json::Int(j, "id"); }, std::move(cb));
        }
    private:
        Connection* _c;
    };
}
