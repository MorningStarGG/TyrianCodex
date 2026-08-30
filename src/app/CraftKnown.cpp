#include "app/CraftKnown.h"
#include "api/Client.h"
#include "api/v2/Account.h"
#include "api/v2/Characters.h"
#include "util/Json.h"   // Json::WriteAtomic (the ONE cache/settings writer)

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <vector>

// All state + every callback live on the main thread (results marshal back via api.Pump()), exactly like
// TpEligibility -- so no locking. Each set is replaced atomically only on a successful response (F10-safe).
namespace
{
    Api::Client* g_api = nullptr;
    std::string  g_cacheFile;

    std::unordered_set<int>                          g_account;   // account-wide unlocked recipe ids
    std::map<std::string, std::unordered_set<int>>   g_chars;     // character name -> recipe ids it can craft (sorted by name)

    bool      g_loaded = false;       // we have data to answer with (cache or fetched)
    bool      g_requested = false;    // Warm() already fired this session
    bool      g_settled = false;      // the login warm has finished (every request returned, ok or fail)
    int       g_pendingChars = 0;     // outstanding per-character fetches
    int       g_pendingChains = 0;    // outstanding top-level chains (account + characters)
    long long g_fetchedUtc = 0;
    uint64_t  g_version = 1;

    void ChainDone();   // fwd

    bool LoadCache()
    {
        if (g_cacheFile.empty()) return false;
        std::ifstream f(g_cacheFile, std::ios::binary);
        if (!f) return false;
        nlohmann::json j;
        try { f >> j; } catch (...) { return false; }
        if (!j.is_object() || j.value("schema", 0) != 1) return false;

        std::unordered_set<int> acct;
        if (auto a = j.find("account"); a != j.end() && a->is_array())
            for (const auto& e : *a) if (e.is_number_integer()) { const int id = e.get<int>(); if (id > 0) acct.insert(id); }

        std::map<std::string, std::unordered_set<int>> chars;
        if (auto c = j.find("chars"); c != j.end() && c->is_object())
            for (auto it = c->begin(); it != c->end(); ++it)
                if (it.value().is_array())
                {
                    std::unordered_set<int>& set = chars[it.key()];
                    for (const auto& e : it.value()) if (e.is_number_integer()) { const int id = e.get<int>(); if (id > 0) set.insert(id); }
                }

        if (acct.empty() && chars.empty()) return false;
        g_account = std::move(acct);
        g_chars = std::move(chars);
        g_fetchedUtc = j.value("fetchedUtc", (long long)0);
        g_loaded = true;
        ++g_version;
        return true;
    }

    void SaveCache()
    {
        if (g_cacheFile.empty() || !g_loaded || (g_account.empty() && g_chars.empty())) return;

        auto sortedVec = [](const std::unordered_set<int>& s) {
            std::vector<int> v(s.begin(), s.end()); std::sort(v.begin(), v.end()); return v;
        };

        nlohmann::json chars = nlohmann::json::object();
        for (const auto& kv : g_chars) chars[kv.first] = sortedVec(kv.second);

        nlohmann::json j;
        j["schema"] = 1;
        j["source"] = "/v2/account/recipes + /v2/characters/:id/recipes";
        j["fetchedUtc"] = g_fetchedUtc;
        j["account"] = sortedVec(g_account);
        j["chars"] = std::move(chars);

        try { Json::WriteAtomic(g_cacheFile, j.dump()); }
        catch (...) { /* best effort; keep any stale cache already on disk */ }
    }

    // One top-level chain (account, or the whole characters batch) finished. When both are done the warm is
    // settled -- persist once. (A failed/empty chain still counts, so the UI stops showing "loading".)
    void ChainDone()
    {
        if (--g_pendingChains > 0) return;
        g_pendingChains = 0;
        g_settled = true;
        g_fetchedUtc = (long long)std::time(nullptr);
        SaveCache();
    }
}

void CraftKnown::Init(Api::Client* api, const std::string& addonDir)
{
    g_api = api;
    g_account.clear();
    g_chars.clear();
    g_loaded = false;
    g_requested = false;
    g_settled = false;
    g_pendingChars = 0;
    g_pendingChains = 0;
    g_fetchedUtc = 0;
    ++g_version;

    g_cacheFile = addonDir.empty() ? std::string() : (addonDir + "\\craft_known.json");
    if (!addonDir.empty()) { std::error_code ec; std::filesystem::create_directories(addonDir, ec); }
    LoadCache();
}

void CraftKnown::Shutdown()
{
    g_api = nullptr;
    g_cacheFile.clear();
    g_account.clear();
    g_chars.clear();
    g_loaded = false;
    g_requested = false;
    g_settled = false;
    g_pendingChars = 0;
    g_pendingChains = 0;
    g_fetchedUtc = 0;
    ++g_version;
}

void CraftKnown::Warm()
{
    if (!g_api || g_requested || !g_api->HasKey()) return;   // no key -> keep any stale cache, fetch nothing
    g_requested = true;
    g_settled = false;
    g_pendingChains = 2;   // chain 1 = account recipes, chain 2 = the per-character batch

    // We do NOT gate on HasPermission: the key's scopes may not be resolved yet at login, and the server
    // validates the real key regardless. A 403/no-scope simply returns !ok and the chain still settles.

    // chain 1: account-wide unlocks (scope: unlocks)
    g_api->V2().Account().Recipes([](Api::Result<std::vector<int>> r) {
        if (r.ok)
        {
            std::unordered_set<int> next;
            for (int id : r.value) if (id > 0) next.insert(id);
            g_account = std::move(next);
            g_loaded = true;
            ++g_version;
        }
        ChainDone();
    });

    // chain 2: per-character recipes (scope: inventories; names from /v2/characters, scope: characters)
    g_api->V2().Characters().Names([](Api::Result<std::vector<std::string>> r) {
        if (!r.ok || r.value.empty()) { ChainDone(); return; }   // no characters / no scope -> chain settles now
        g_pendingChars = (int)r.value.size();
        for (const std::string& name : r.value)
            g_api->V2().Characters().Recipes(name, [name](Api::Result<std::vector<int>> rr) {
                if (rr.ok)
                {
                    std::unordered_set<int> next;
                    for (int id : rr.value) if (id > 0) next.insert(id);
                    g_chars[name] = std::move(next);
                    g_loaded = true;
                    ++g_version;
                }
                if (--g_pendingChars <= 0) { g_pendingChars = 0; ChainDone(); }   // whole batch done -> chain settles
            });
    });
}

bool      CraftKnown::IsReady()      { return g_loaded; }
bool      CraftKnown::HasFetched()   { return g_settled; }
bool      CraftKnown::IsRefreshing() { return g_requested && !g_settled; }
uint64_t  CraftKnown::Version()      { return g_version; }
long long CraftKnown::FetchedUtc()   { return g_fetchedUtc; }

std::vector<std::string> CraftKnown::CharacterNames()
{
    std::vector<std::string> out;
    out.reserve(g_chars.size());
    for (const auto& kv : g_chars) out.push_back(kv.first);   // g_chars is a std::map -> already sorted
    return out;
}

bool CraftKnown::CharKnows(const std::string& name, int recipeId)
{
    if (recipeId <= 0) return false;
    auto it = g_chars.find(name);
    return it != g_chars.end() && it->second.count(recipeId) != 0;
}

bool CraftKnown::KnownByAccount(int recipeId)
{
    return recipeId > 0 && g_account.count(recipeId) != 0;
}

std::vector<int> CraftKnown::KnownRecipeIds(const std::string& name)
{
    std::vector<int> out;
    auto it = g_chars.find(name);
    if (it == g_chars.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    return out;
}

std::vector<std::string> CraftKnown::CharsKnowing(int recipeId)
{
    std::vector<std::string> out;
    if (recipeId <= 0) return out;
    for (const auto& kv : g_chars)
        if (kv.second.count(recipeId) != 0) out.push_back(kv.first);   // sorted by name (std::map)
    return out;
}
