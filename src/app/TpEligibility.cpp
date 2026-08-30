#include "app/TpEligibility.h"
#include "api/Client.h"
#include "api/v2/Commerce.h"
#include "util/Json.h"   // Json::WriteAtomic (the ONE cache/settings writer)

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <vector>

namespace
{
    Api::Client* g_api = nullptr;
    std::string  g_cacheFile;
    std::unordered_set<int> g_ids;
    long long    g_fetchedUtc = 0;
    bool         g_loaded = false;
    bool         g_requested = false;
    bool         g_refreshing = false;
    uint64_t     g_version = 1;

    constexpr int kMinSaneIds = 20000;

    bool BuildSet(const std::vector<int>& ids, std::unordered_set<int>& out)
    {
        out.clear();
        out.reserve(ids.size());
        for (int id : ids)
        {
            if (id <= 0) return false;
            out.insert(id);
        }
        return (int)out.size() >= kMinSaneIds;
    }

    bool LoadCache()
    {
        if (g_cacheFile.empty()) return false;
        std::ifstream f(g_cacheFile, std::ios::binary);
        if (!f) return false;
        nlohmann::json j;
        try { f >> j; } catch (...) { return false; }
        if (!j.is_object() || j.value("schema", 0) != 1 || !j.contains("ids") || !j["ids"].is_array())
            return false;

        std::vector<int> ids;
        ids.reserve(j["ids"].size());
        for (const auto& e : j["ids"])
        {
            if (!e.is_number_integer()) return false;
            ids.push_back(e.get<int>());
        }

        std::unordered_set<int> next;
        if (!BuildSet(ids, next)) return false;
        g_ids = std::move(next);
        g_fetchedUtc = j.value("fetchedUtc", (long long)0);
        g_loaded = true;
        ++g_version;
        return true;
    }

    void SaveCache()
    {
        if (g_cacheFile.empty() || !g_loaded || (int)g_ids.size() < kMinSaneIds) return;

        std::vector<int> ids;
        ids.reserve(g_ids.size());
        for (int id : g_ids) ids.push_back(id);
        std::sort(ids.begin(), ids.end());

        nlohmann::json j;
        j["schema"] = 1;
        j["source"] = "/v2/commerce/prices";
        j["fetchedUtc"] = g_fetchedUtc;
        j["ids"] = ids;

        try { Json::WriteAtomic(g_cacheFile, j.dump()); }
        catch (...) { /* best effort; keep any stale cache already on disk */ }
    }
}

void TpEligibility::Init(Api::Client* api, const std::string& addonDir)
{
    g_api = api;
    g_ids.clear();
    g_fetchedUtc = 0;
    g_loaded = false;
    g_requested = false;
    g_refreshing = false;
    ++g_version;

    g_cacheFile = addonDir.empty() ? std::string() : (addonDir + "\\tp_ids.json");
    if (!addonDir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(addonDir, ec);
    }
    LoadCache();
}

void TpEligibility::Shutdown()
{
    g_api = nullptr;
    g_cacheFile.clear();
    g_ids.clear();
    g_fetchedUtc = 0;
    g_loaded = false;
    g_requested = false;
    g_refreshing = false;
    ++g_version;
}

void TpEligibility::Warm()
{
    if (!g_api || g_requested || g_refreshing) return;
    g_requested = true;
    g_refreshing = true;
    g_api->V2().Commerce().Prices().GetIds([](Api::Result<std::vector<int>> r) {
        g_refreshing = false;
        if (!r.ok) return;

        std::unordered_set<int> next;
        if (!BuildSet(r.value, next)) return;

        g_ids = std::move(next);
        g_fetchedUtc = (long long)std::time(nullptr);
        g_loaded = true;
        ++g_version;
        SaveCache();
    });
}

bool TpEligibility::IsReady() { return g_loaded; }
bool TpEligibility::IsRefreshing() { return g_refreshing; }
bool TpEligibility::IsTradeable(int itemId) { return itemId > 0 && g_loaded && g_ids.find(itemId) != g_ids.end(); }
int TpEligibility::Count() { return (int)g_ids.size(); }
long long TpEligibility::FetchedUtc() { return g_fetchedUtc; }
uint64_t TpEligibility::Version() { return g_version; }
