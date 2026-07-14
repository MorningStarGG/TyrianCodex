#include "app/MarketData.h"
#include "api/core/Http.h"
#include "api/core/Json.h"   // Api::Json null-safe readers

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_set>

namespace
{
    namespace fs = std::filesystem;

    std::mutex g_mtx;
    std::string g_cacheFile;
    std::vector<MarketData::Item> g_items;
    long long g_fetchedUtc = 0;
    bool g_loaded = false;
    bool g_requested = false;
    bool g_refreshing = false;
    bool g_stop = false;
    uint64_t g_version = 1;
    long long g_nextRetryUtc = 0;
    std::string g_lastError;
    std::thread g_worker;

    constexpr const char* kEndpoint = "https://api.datawars2.ie/gw2/v1/items/json";
    constexpr int kMinSaneRows = 20000;
    constexpr int kMinSaneActiveRows = 10000;
    constexpr long long kRetrySeconds = 60;

    bool ReadFile(const std::string& path, std::string& out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !out.empty();
    }

    bool NonNegativeInt(const nlohmann::json& j, const char* key, int& out, bool missingOk = false)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null())
        {
            if (!missingOk) return false;
            out = 0;
            return true;
        }
        if (!it->is_number_integer()) return false;
        const long long v = it->get<long long>();
        if (v < 0 || v > (long long)std::numeric_limits<int>::max()) return false;
        out = (int)v;
        return true;
    }

    bool ParseItemsJson(const nlohmann::json& arr, std::vector<MarketData::Item>& out)
    {
        if (!arr.is_array()) return false;
        out.clear();
        out.reserve(arr.size());
        std::unordered_set<int> seen;
        seen.reserve(arr.size());
        int activeRows = 0;

        for (const auto& e : arr)
        {
            if (!e.is_object()) return false;
            MarketData::Item it;
            if (!NonNegativeInt(e, "id", it.id) || it.id <= 0) return false;
            if (!NonNegativeInt(e, "buy_price", it.buyPrice, true)) return false;
            if (!NonNegativeInt(e, "buy_quantity", it.buyQuantity, true)) return false;
            if (!NonNegativeInt(e, "sell_price", it.sellPrice, true)) return false;
            if (!NonNegativeInt(e, "sell_quantity", it.sellQuantity, true)) return false;
            if (auto n = e.find("name"); n != e.end() && n->is_string()) it.name = n->get<std::string>();
            if (auto u = e.find("lastUpdate"); u != e.end() && u->is_string()) it.lastUpdate = u->get<std::string>();
            if (!seen.insert(it.id).second) return false;
            if (it.buyPrice > 0 && it.sellPrice > 0 && it.buyQuantity > 0 && it.sellQuantity > 0)
                ++activeRows;
            out.push_back(std::move(it));
        }

        return (int)out.size() >= kMinSaneRows && activeRows >= kMinSaneActiveRows;
    }

    bool ParseCache(const nlohmann::json& j, std::vector<MarketData::Item>& out, long long& fetchedUtc)
    {
        if (!j.is_object() || Api::Json::Int(j, "schema", 0) != 1 || !j.contains("items")) return false;
        if (!ParseItemsJson(j["items"], out)) return false;
        fetchedUtc = Api::Json::Int64(j, "fetchedUtc", 0);
        return true;
    }

    bool LoadCache()
    {
        std::string bytes;
        if (g_cacheFile.empty() || !ReadFile(g_cacheFile, bytes)) return false;

        nlohmann::json j;
        try { j = nlohmann::json::parse(bytes); } catch (...) { return false; }

        std::vector<MarketData::Item> next;
        long long fetched = 0;
        if (!ParseCache(j, next, fetched)) return false;

        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_stop) return false;
        g_items = std::move(next);
        g_fetchedUtc = fetched;
        g_loaded = true;
        ++g_version;
        return true;
    }

    void SaveCache(const std::vector<MarketData::Item>& items, long long fetchedUtc)
    {
        if (g_cacheFile.empty() || (int)items.size() < kMinSaneRows) return;

        std::vector<MarketData::Item> sorted = items;
        std::sort(sorted.begin(), sorted.end(), [](const MarketData::Item& a, const MarketData::Item& b) {
            return a.id < b.id;
        });

        nlohmann::json arr = nlohmann::json::array();
        for (const MarketData::Item& it : sorted)
        {
            nlohmann::json e;
            e["id"] = it.id;
            e["buy_price"] = it.buyPrice;
            e["buy_quantity"] = it.buyQuantity;
            e["sell_price"] = it.sellPrice;
            e["sell_quantity"] = it.sellQuantity;
            e["name"] = it.name;
            e["lastUpdate"] = it.lastUpdate;
            arr.push_back(std::move(e));
        }

        nlohmann::json j;
        j["schema"] = 1;
        j["source"] = kEndpoint;
        j["fetchedUtc"] = fetchedUtc;
        j["count"] = (int)sorted.size();
        j["items"] = std::move(arr);

        const std::string tmp = g_cacheFile + ".tmp";
        try
        {
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) return;
                f << j.dump();
            }
            std::error_code ec;
            fs::copy_file(tmp, g_cacheFile, fs::copy_options::overwrite_existing, ec);
            fs::remove(tmp, ec);
        }
        catch (...) { /* best effort; stale cache remains valid */ }
    }

    void FinishRefresh(const std::string& error)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_refreshing = false;
        g_lastError = error;
        if (!g_loaded)
        {
            g_requested = false;
            g_nextRetryUtc = (long long)std::time(nullptr) + kRetrySeconds;
        }
    }

    void RefreshThread()
    {
        Api::Http::RawResponse resp = Api::Http::Get(kEndpoint, {
            { "Accept", "application/json" },
            { "User-Agent", "TyrianCodex" },
        }, Api::Http::Scope::Market);

        if (resp.status != 200 || resp.body.empty())
        {
            FinishRefresh(resp.status > 0 ? ("HTTP " + std::to_string(resp.status)) : "Network error");
            return;
        }

        nlohmann::json j;
        try { j = nlohmann::json::parse(resp.body); } catch (...) { FinishRefresh("JSON parse failed"); return; }

        std::vector<MarketData::Item> next;
        if (!ParseItemsJson(j, next))
        {
            FinishRefresh("Snapshot validation failed");
            return;
        }

        const long long fetched = (long long)std::time(nullptr);
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (g_stop) { g_refreshing = false; return; }
            g_items = next;
            g_fetchedUtc = fetched;
            g_loaded = true;
            g_refreshing = false;
            g_lastError.clear();
            g_nextRetryUtc = 0;
            ++g_version;
        }
        SaveCache(next, fetched);
    }
}

void MarketData::Init(const std::string& addonDir)
{
    Shutdown();

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_stop = false;
        g_requested = false;
        g_refreshing = false;
        g_loaded = false;
        g_fetchedUtc = 0;
        g_items.clear();
        g_lastError.clear();
        g_nextRetryUtc = 0;
        ++g_version;
        g_cacheFile = addonDir.empty() ? std::string() : (addonDir + "\\market_snapshot.json");
    }

    if (!addonDir.empty())
    {
        std::error_code ec;
        fs::create_directories(addonDir, ec);
    }
    LoadCache();
}

void MarketData::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_stop = true;
        g_refreshing = false;
    }
    Api::Http::Abort(Api::Http::Scope::Market);   // unblock OUR worker's in-flight Get -- only our scope
    if (g_worker.joinable()) g_worker.join();

    std::lock_guard<std::mutex> lk(g_mtx);
    g_cacheFile.clear();
    g_items.clear();
    g_fetchedUtc = 0;
    g_loaded = false;
    g_requested = false;
    g_refreshing = false;
    g_nextRetryUtc = 0;
    g_lastError.clear();
    ++g_version;
}

void MarketData::Warm(bool force)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_stop || g_refreshing || (!force && g_requested)) return;
        const long long now = (long long)std::time(nullptr);
        if (!force && !g_loaded && g_nextRetryUtc > now) return;
    }
    if (g_worker.joinable()) g_worker.join();

    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_stop || g_refreshing || (!force && g_requested)) return;
    const long long now = (long long)std::time(nullptr);
    if (!force && !g_loaded && g_nextRetryUtc > now) return;
    Api::Http::Resume(Api::Http::Scope::Market);
    g_requested = true;
    g_refreshing = true;
    g_worker = std::thread(RefreshThread);
}

bool MarketData::IsReady()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_loaded;
}

bool MarketData::IsRefreshing()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_refreshing;
}

std::string MarketData::LastError()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_lastError;
}

int MarketData::Count()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return (int)g_items.size();
}

long long MarketData::FetchedUtc()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_fetchedUtc;
}

uint64_t MarketData::Version()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_version;
}

void MarketData::CopyItems(std::vector<Item>& out)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    out = g_items;
}
