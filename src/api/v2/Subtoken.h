#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Connection.h"
#include "../core/Request.h"
#include "../core/Result.h"
#include "../core/Permissions.h"
#include "../core/Json.h"

// GET /v2/createsubtoken (authenticated): mint a limited-lifetime subtoken from the current key, narrowed to a
// subset of `permissions`, an optional `expire` (ISO-8601), and optional URL allow-list. Returns { subtoken }.
// Wiki: API:2/createsubtoken.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Subtoken { std::string subtoken; nlohmann::json raw; };

    class CreateSubtokenEndpoint
    {
    public:
        explicit CreateSubtokenEndpoint(Connection* c) : _c(c) {}

        void Create(const std::vector<std::string>& permissions, const std::string& expire,
                    const std::vector<std::string>& urls, std::function<void(Result<Subtoken>)> cb) const
        {
            Request req;
            req.path = "/v2/createsubtoken";
            req.auth = true; req.hasScope = true; req.scope = TokenPermission::Account;
            req.cacheTtlSec = 0;   // a fresh subtoken each call
            if (!permissions.empty()) req.query.push_back({ "permissions", Join(permissions) });
            if (!expire.empty())      req.query.push_back({ "expire", expire });
            if (!urls.empty())        req.query.push_back({ "urls", Join(urls) });
            _c->Get<Subtoken>(std::move(req),
                [](const nlohmann::json& j) { Subtoken s; s.subtoken = Json::Str(j, "subtoken"); s.raw = j; return s; },
                std::move(cb));
        }

    private:
        static std::string Join(const std::vector<std::string>& v)
        {
            std::string out;
            for (size_t i = 0; i < v.size(); ++i) { if (i) out += ','; out += v[i]; }
            return out;
        }
        Connection* _c;
    };
}
