#pragma once
#include <cctype>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Connection.h"
#include "../core/Request.h"
#include "../core/Result.h"
#include "../core/Json.h"

// Shared plumbing + models for the v1 (legacy) client. v1 is FROZEN, so every endpoint is fully typed - no
// `Result<nlohmann::json>` accessors. Unlike v2 there is no `ids=` bulk form: a v1 call fetches the whole
// resource or one item via a `?param=`, and many responses are an object keyed by id (e.g. {"colors":{...}}).
// Reuses the same Connection/Http/Cache/RateLimiter pipeline; only the path (".json") + parse differ.
namespace Api::V1
{
    constexpr int kV1StaticTtl = 24 * 60 * 60;   // frozen reference data
    constexpr int kV1LiveTtl   = 30;             // events / wvw match state

    // v1 is inconsistently string-encoded (item_id "4", level "0", but event map_id 326 is a real number), so
    // every numeric field is read with these string-OR-number helpers.
    inline int IntS(const nlohmann::json& o, const char* key, int def = 0)
    {
        auto it = o.find(key);
        if (it == o.end()) return def;
        if (it->is_number_integer()) return it->get<int>();
        if (it->is_string()) { try { return std::stoi(it->get<std::string>()); } catch (...) {} }
        return def;
    }
    inline double DblS(const nlohmann::json& o, const char* key, double def = 0.0)
    {
        auto it = o.find(key);
        if (it == o.end()) return def;
        if (it->is_number()) return it->get<double>();
        if (it->is_string()) { try { return std::stod(it->get<std::string>()); } catch (...) {} }
        return def;
    }
    // A [x,y,z]-style numeric array (handles string-encoded numbers).
    inline std::vector<double> NumArray(const nlohmann::json& o, const char* key)
    {
        std::vector<double> out;
        auto it = o.find(key);
        if (it != o.end() && it->is_array())
            for (const auto& e : *it) { if (e.is_number()) out.push_back(e.get<double>()); else if (e.is_string()) { try { out.push_back(std::stod(e.get<std::string>())); } catch (...) {} } }
        return out;
    }

    // Tiny request helpers (no auth - v1 predates API keys; everything is anonymous).
    template <typename T>
    inline void Fetch(Connection* c, const std::string& path, int ttl, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb)
    {
        Request req; req.path = path; req.cacheTtlSec = ttl;
        c->Get<T>(std::move(req), parse, std::move(cb));
    }
    template <typename T>
    inline void FetchQ(Connection* c, const std::string& path, const std::string& qk, const std::string& qv, int ttl, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb)
    {
        Request req; req.path = path; req.query = { { qk, qv } }; req.cacheTtlSec = ttl;
        c->Get<T>(std::move(req), parse, std::move(cb));
    }

    // The item/skin detail sub-object is keyed by the lowercased `type` (e.g. type "Weapon" -> key "weapon").
    inline std::string Lower(std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; }

    // {id, name} row, shared by event_names / map_names / world_names / wvw/objective_names (bare arrays).
    struct NameId { std::string id; std::string name; nlohmann::json raw; };
    inline NameId ParseNameId(const nlohmann::json& j) { NameId n; n.id = Json::Str(j, "id"); n.name = Json::Str(j, "name"); n.raw = j; return n; }
    inline std::vector<NameId> ParseNameIdList(const nlohmann::json& j)
    {
        std::vector<NameId> out;
        if (j.is_array()) { out.reserve(j.size()); for (const auto& e : j) out.push_back(ParseNameId(e)); }
        return out;
    }

    // Extract a wrapped bare-id array: {"items":[..]} / {"recipes":[..]} / {"skins":[..]} -> vector<int>.
    // v1 is inconsistent (ids are ints in lists but strings in detail payloads), so accept both forms.
    inline std::vector<int> ParseWrappedIntIds(const nlohmann::json& j, const char* key)
    {
        std::vector<int> out;
        for (const auto& e : Json::Node(j, key))
        {
            if (e.is_number_integer()) out.push_back(e.get<int>());
            else if (e.is_string()) { try { out.push_back(std::stoi(e.get<std::string>())); } catch (...) {} }
        }
        return out;
    }
}
