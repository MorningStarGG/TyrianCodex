#pragma once
#include <functional>
#include <string>
#include <type_traits>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Connection.h"
#include "../core/Request.h"
#include "../core/Result.h"

namespace Api::V2
{
    constexpr int kStaticTtlSec = 24 * 60 * 60;   // reference/game-content data is static -> cache for a day

    // Reusable read-only, bulk-expanded endpoint - the v2 pattern shared by ~every game-content family, so a
    // new family is just a model + a ParseFn + a one-line subclass (instead of hand-rolling these four each
    // time, mirroring Gw2Sharp's bulk-expanded base):
    //   Get(id)   -> GET /v2/{base}/{id}        one resource
    //   Get(ids)  -> GET /v2/{base}?ids=a,b,c   many (the RequestSplitter chunks > 200 ids and merges)
    //   GetAll()  -> GET /v2/{base}?ids=all     every resource (only where the endpoint allows ids=all)
    //   GetIds()  -> GET /v2/{base}             the id index (the bare array of all ids)
    // T = the typed model; Id = the id type (int for most; std::string for professions / files / legends / ...).
    // Anonymous + statically cached by default; pass auth + scope for the account-bound bulk families.
    template <typename T, typename Id = int>
    class BulkEndpoint
    {
    public:
        using ParseFn = T (*)(const nlohmann::json&);

        BulkEndpoint(Connection* c, const char* base, ParseFn parse, int ttlSec,
                     bool auth = false, TokenPermission scope = TokenPermission::Account)
            : _c(c), _base(base), _parse(parse), _ttl(ttlSec), _auth(auth), _scope(scope) {}

        void Get(const Id& id, std::function<void(Result<T>)> cb) const
        {
            Request req = MakeBase();
            req.path = std::string(_base) + "/" + IdStr(id);
            _c->Get<T>(std::move(req), _parse, std::move(cb));
        }

        void Get(const std::vector<Id>& ids, std::function<void(Result<std::vector<T>>)> cb) const
        {
            Request req = MakeBase();
            req.path = _base;
            for (const Id& id : ids) req.bulkIds.push_back(IdStr(id));
            _c->Get<std::vector<T>>(std::move(req), ArrayParse(), std::move(cb));
        }

        void GetAll(std::function<void(Result<std::vector<T>>)> cb) const
        {
            Request req = MakeBase();
            req.path = _base;
            req.bulkIds.push_back("all");
            _c->Get<std::vector<T>>(std::move(req), ArrayParse(), std::move(cb));
        }

        void GetIds(std::function<void(Result<std::vector<Id>>)> cb) const
        {
            Request req = MakeBase();
            req.path = _base;
            _c->Get<std::vector<Id>>(std::move(req),
                [](const nlohmann::json& j) {
                    std::vector<Id> out;
                    if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseId(e)); }
                    return out;
                },
                std::move(cb));
        }

    protected:
        Request MakeBase() const
        {
            Request req;
            req.cacheTtlSec = _ttl;
            req.auth = _auth;
            if (_auth) { req.hasScope = true; req.scope = _scope; }
            return req;
        }

        static std::string IdStr(const Id& id)
        {
            if constexpr (std::is_same_v<Id, std::string>) return id;
            else                                           return std::to_string(id);
        }
        static Id ParseId(const nlohmann::json& j)
        {
            if constexpr (std::is_same_v<Id, std::string>) return j.is_string()  ? j.get<std::string>() : std::string();
            else                                           return j.is_number()  ? j.get<int>()         : 0;
        }
        std::function<std::vector<T>(const nlohmann::json&)> ArrayParse() const
        {
            ParseFn p = _parse;
            return [p](const nlohmann::json& j) {
                std::vector<T> out;
                if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(p(e)); }
                return out;
            };
        }

        Connection*     _c;
        const char*     _base;
        ParseFn         _parse;
        int             _ttl;
        bool            _auth;
        TokenPermission _scope;
    };
}
