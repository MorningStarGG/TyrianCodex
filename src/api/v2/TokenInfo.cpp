#include "TokenInfo.h"

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    TokenInfo ParseTokenInfo(const nlohmann::json& j)
    {
        TokenInfo ti;
        ti.id   = Json::Str(j, "id");
        ti.name = Json::Str(j, "name");
        for (const std::string& s : Json::StrArray(j, "permissions"))
            ti.permissions.push_back(ParsePermission(s));
        ti.valid = true;
        return ti;
    }

    void TokenInfoEndpoint::Get(std::function<void(Result<TokenInfo>)> cb) const
    {
        Request req;
        req.path        = "/v2/tokeninfo";
        req.auth        = true;          // needs a key, but no particular scope
        req.cacheTtlSec = kCacheNever;   // always resolve fresh for the current key
        _c->Get<TokenInfo>(std::move(req), &ParseTokenInfo, std::move(cb));
    }
}
