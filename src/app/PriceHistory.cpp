#include "app/PriceHistory.h"
#include "api/core/Http.h"

#include <algorithm>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace
{
    namespace fs = std::filesystem;

    std::mutex g_mtx;
    std::condition_variable g_cv;
    std::string g_cacheDir;                                 // <addon>\cache\pricehistory
    std::string g_seedFile;                                 // <addon>\data\price_history.json
    std::unordered_map<int, PriceHistory::Series> g_series; // id -> merged series (bundle U disk U delta)
    std::unordered_set<int> g_done;                         // ids processed this session (don't re-queue)
    std::unordered_set<int> g_fetching;                     // ids queued or in-flight
    std::deque<int> g_queue;
    std::deque<int> g_prewarm;            // LOW-priority login-prewarm queue (drained when g_queue is idle)
    std::unordered_set<int> g_prewarmSet; // membership for g_prewarm (O(1) dedup / promote)
    bool g_stop = false;
    std::string g_lastError;
    std::thread g_worker;

    // Market-wide index (datawars2 .../history/total/json). One series, fetched once per session.
    PriceHistory::MarketSeries g_marketSeries;
    bool g_wantMarket = false; // user has requested the index
    bool g_marketDone = false; // processed this session
    bool g_marketFetching = false;

    constexpr const char *kHistEndpoint = "https://api.datawars2.ie/gw2/v2/history/json";
    constexpr int kDefaultDays = 365; // first-fetch lookback (covers the Charts view's longest range)

    // --- civil <-> day-number (Howard Hinnant's algorithms; UTC) -------------------------------------------------
    int DaysFromCivil(int y, unsigned m, unsigned d)
    {
        y -= m <= 2;
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int)doe - 719468;
    }
    void CivilFromDays(int z, int &y, unsigned &m, unsigned &d)
    {
        z += 719468;
        const int era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int yy = (int)yoe + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1;
        m = mp + (mp < 10 ? 3 : -9);
        y = yy + (m <= 2);
    }
    int TodayDayImpl()
    {
        const std::time_t t = std::time(nullptr);
        std::tm gm{};
        gmtime_s(&gm, &t);
        return DaysFromCivil(gm.tm_year + 1900, (unsigned)(gm.tm_mon + 1), (unsigned)gm.tm_mday);
    }
    // "2025-06-02T00:00:01.000Z" -> day-number; -1 on a malformed date.
    int DateStrToDay(const std::string &s)
    {
        if (s.size() < 10 || s[4] != '-' || s[7] != '-')
            return -1;
        const int y = std::atoi(s.substr(0, 4).c_str());
        const int mo = std::atoi(s.substr(5, 2).c_str());
        const int da = std::atoi(s.substr(8, 2).c_str());
        if (y < 2012 || mo < 1 || mo > 12 || da < 1 || da > 31)
            return -1;
        return DaysFromCivil(y, (unsigned)mo, (unsigned)da);
    }

    // datawars2 avg fields are floats (e.g. 24.7) -> round to copper. Missing/null = 0.
    int ReadNum(const nlohmann::json &j, const char *key)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null())
            return 0;
        if (it->is_number_integer())
        {
            long long v = it->get<long long>();
            return v < 0 ? 0 : (int)v;
        }
        if (it->is_number_float())
        {
            double v = it->get<double>();
            return v < 0 ? 0 : (int)(v + 0.5);
        }
        return 0;
    }

    // Whole-market totals (buy_value/sell_value) are huge -> int64.
    long long ReadI64(const nlohmann::json &j, const char *key)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null())
            return 0;
        if (it->is_number_integer())
        {
            long long v = it->get<long long>();
            return v < 0 ? 0 : v;
        }
        if (it->is_number_unsigned())
        {
            return (long long)it->get<unsigned long long>();
        }
        if (it->is_number_float())
        {
            double v = it->get<double>();
            return v < 0 ? 0 : (long long)(v + 0.5);
        }
        return 0;
    }

    bool ReadFileBytes(const std::string &path, std::string &out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !out.empty();
    }

    // Series is stored as a compact array-of-arrays [[day, buyAvg, sellAvg, buyQty, sellQty], ...].
    nlohmann::json SeriesToJson(const PriceHistory::Series &s)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &p : s)
            arr.push_back({p.day, p.buyAvg, p.sellAvg, p.buyQty, p.sellQty});
        return arr;
    }
    void SeriesFromJson(const nlohmann::json &arr, PriceHistory::Series &out)
    {
        if (!arr.is_array())
            return;
        out.reserve(arr.size());
        for (const auto &e : arr)
        {
            if (!e.is_array() || e.size() < 5)
                continue;
            PriceHistory::Point p;
            p.day = e[0].is_number() ? e[0].get<int>() : 0;
            p.buyAvg = e[1].is_number() ? e[1].get<int>() : 0;
            p.sellAvg = e[2].is_number() ? e[2].get<int>() : 0;
            p.buyQty = e[3].is_number() ? e[3].get<int>() : 0;
            p.sellQty = e[4].is_number() ? e[4].get<int>() : 0;
            if (p.day > 0)
                out.push_back(p);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b)
                  { return a.day < b.day; });
    }

    std::string DiskPath(int id) { return g_cacheDir + "\\" + std::to_string(id) + ".json"; }

    void LoadDisk(int id, PriceHistory::Series &out)
    {
        std::string bytes;
        if (g_cacheDir.empty() || !ReadFileBytes(DiskPath(id), bytes))
            return;
        try
        {
            nlohmann::json j = nlohmann::json::parse(bytes);
            if (j.is_object() && j.value("schema", 0) == 1 && j.contains("points"))
                SeriesFromJson(j["points"], out);
        }
        catch (...)
        {
            out.clear();
        }
    }

    void SaveDisk(int id, const PriceHistory::Series &s)
    {
        if (g_cacheDir.empty() || s.empty())
            return;
        nlohmann::json j;
        j["schema"] = 1;
        j["id"] = id;
        j["fetchedUtc"] = (long long)std::time(nullptr);
        j["points"] = SeriesToJson(s);
        const std::string path = DiskPath(id), tmp = path + ".tmp";
        try
        {
            const std::string text = j.dump();
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f)
                    return;
                f.write(text.data(), (std::streamsize)text.size());
            }
            std::error_code ec;
            fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
            fs::remove(tmp, ec);
        }
        catch (...)
        { /* best effort; stale cache remains valid */
        }
    }

    // Merge `add` into `base` (dedup by day; the newer fetch wins for an overlapping day). Result stays sorted.
    void MergeSeries(PriceHistory::Series &base, const PriceHistory::Series &add)
    {
        if (add.empty())
            return;
        std::unordered_map<int, PriceHistory::Point> byDay;
        byDay.reserve(base.size() + add.size());
        for (const auto &p : base)
            byDay[p.day] = p;
        for (const auto &p : add)
            byDay[p.day] = p; // add wins
        base.clear();
        base.reserve(byDay.size());
        for (auto &kv : byDay)
            base.push_back(kv.second);
        std::sort(base.begin(), base.end(), [](const auto &a, const auto &b)
                  { return a.day < b.day; });
    }

    // Fetch an item's history from datawars2 since `startDay`. Returns false (out empty) on any failure (never-clobber).
    bool FetchHistory(int id, int startDay, PriceHistory::Series &out, std::string &err)
    {
        int y;
        unsigned m, d;
        CivilFromDays(startDay, y, m, d);
        char start[16];
        std::snprintf(start, sizeof(start), "%04d-%02u-%02u", y, m, d);
        char url[256];
        std::snprintf(url, sizeof(url),
                      "%s?itemID=%d&start=%s&fields=date,buy_price_avg,sell_price_avg,buy_quantity_avg,sell_quantity_avg",
                      kHistEndpoint, id, start);

        Api::Http::RawResponse resp = Api::Http::Get(url, {
                                                              {"Accept", "application/json"},
                                                              {"User-Agent", "TyrianCodex"},
                                                          },
                                                     Api::Http::Scope::PriceHistory);
        if (resp.status != 200 || resp.body.empty())
        {
            err = resp.status > 0 ? ("HTTP " + std::to_string(resp.status)) : "Network error";
            return false;
        }
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(resp.body);
        }
        catch (...)
        {
            err = "parse failed";
            return false;
        }
        if (!j.is_array())
        {
            err = "bad shape";
            return false;
        }

        out.clear();
        out.reserve(j.size());
        for (const auto &e : j)
        {
            if (!e.is_object())
                continue;
            auto dt = e.find("date");
            if (dt == e.end() || !dt->is_string())
                continue;
            const int day = DateStrToDay(dt->get<std::string>());
            if (day <= 0)
                continue;
            PriceHistory::Point p;
            p.day = day;
            p.buyAvg = ReadNum(e, "buy_price_avg");
            p.sellAvg = ReadNum(e, "sell_price_avg");
            p.buyQty = ReadNum(e, "buy_quantity_avg");
            p.sellQty = ReadNum(e, "sell_quantity_avg");
            // drop a blank day that carries no usable price (would just be a gap)
            if (p.buyAvg > 0 || p.sellAvg > 0)
                out.push_back(p);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b)
                  { return a.day < b.day; });
        if (out.empty())
        {
            err = "empty";
            return false;
        }
        return true;
    }

    // Worker: disk cache -> (stale?) delta fetch -> merge. Holds NO lock during IO/network.
    void ProcessItem(int id, PriceHistory::Series &merged, std::string &err)
    {
        LoadDisk(id, merged);
        if (merged.empty())
        {
            // No disk cache yet -> seed from the in-memory bundle (the loaded seed) so we delta-top-up rather than
            // re-fetch the whole history for a bundled item.
            std::lock_guard<std::mutex> lk(g_mtx);
            auto it = g_series.find(id);
            if (it != g_series.end())
                merged = it->second;
        }
        const int today = TodayDayImpl();
        const bool stale = merged.empty() || merged.back().day < today - 1;
        if (!stale)
            return; // disk cache is current enough

        const int startDay = merged.empty() ? (today - kDefaultDays) : merged.back().day;
        PriceHistory::Series fetched;
        std::string ferr;
        if (FetchHistory(id, startDay, fetched, ferr))
        {
            MergeSeries(merged, fetched);
            SaveDisk(id, merged);
        }
        else
        {
            err = ferr; // keep `merged` = the stale disk cache (serve stale); never replacec with empty
        }
    }

    // --- The market-wide index (whole-TP buy_value / sell_value over time) ---------------------------
    std::string MarketDiskPath() { return g_cacheDir + "\\_market.json"; }

    nlohmann::json MarketToJson(const PriceHistory::MarketSeries &s)
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &p : s)
            arr.push_back({p.day, p.buyValue, p.sellValue});
        return arr;
    }
    void MarketFromJson(const nlohmann::json &arr, PriceHistory::MarketSeries &out)
    {
        if (!arr.is_array())
            return;
        out.reserve(arr.size());
        for (const auto &e : arr)
        {
            if (!e.is_array() || e.size() < 3)
                continue;
            PriceHistory::MarketPoint p;
            p.day = e[0].is_number() ? e[0].get<int>() : 0;
            p.buyValue = e[1].is_number() ? e[1].get<long long>() : 0;
            p.sellValue = e[2].is_number() ? e[2].get<long long>() : 0;
            if (p.day > 0)
                out.push_back(p);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b)
                  { return a.day < b.day; });
    }
    void LoadMarketDisk(PriceHistory::MarketSeries &out)
    {
        std::string bytes;
        if (g_cacheDir.empty() || !ReadFileBytes(MarketDiskPath(), bytes))
            return;
        try
        {
            nlohmann::json j = nlohmann::json::parse(bytes);
            if (j.is_object() && j.value("schema", 0) == 1 && j.contains("points"))
                MarketFromJson(j["points"], out);
        }
        catch (...)
        {
            out.clear();
        }
    }
    void SaveMarketDisk(const PriceHistory::MarketSeries &s)
    {
        if (g_cacheDir.empty() || s.empty())
            return;
        nlohmann::json j;
        j["schema"] = 1;
        j["fetchedUtc"] = (long long)std::time(nullptr);
        j["points"] = MarketToJson(s);
        const std::string path = MarketDiskPath(), tmp = path + ".tmp";
        try
        {
            const std::string text = j.dump();
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f)
                    return;
                f.write(text.data(), (std::streamsize)text.size());
            }
            std::error_code ec;
            fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
            fs::remove(tmp, ec);
        }
        catch (...)
        { /* best effort */
        }
    }
    void MergeMarket(PriceHistory::MarketSeries &base, const PriceHistory::MarketSeries &add)
    {
        if (add.empty())
            return;
        std::unordered_map<int, PriceHistory::MarketPoint> byDay;
        byDay.reserve(base.size() + add.size());
        for (const auto &p : base)
            byDay[p.day] = p;
        for (const auto &p : add)
            byDay[p.day] = p;
        base.clear();
        base.reserve(byDay.size());
        for (auto &kv : byDay)
            base.push_back(kv.second);
        std::sort(base.begin(), base.end(), [](const auto &a, const auto &b)
                  { return a.day < b.day; });
    }
    bool FetchMarket(int startDay, PriceHistory::MarketSeries &out, std::string &err)
    {
        int y;
        unsigned m, d;
        CivilFromDays(startDay, y, m, d);
        char start[16];
        std::snprintf(start, sizeof(start), "%04d-%02u-%02u", y, m, d);
        char url[256];
        std::snprintf(url, sizeof(url),
                      "https://api.datawars2.ie/gw2/v2/history/total/json?start=%s&fields=date,buy_value,sell_value", start);

        Api::Http::RawResponse resp = Api::Http::Get(url, {
                                                              {"Accept", "application/json"},
                                                              {"User-Agent", "TyrianCodex"},
                                                          },
                                                     Api::Http::Scope::PriceHistory);
        if (resp.status != 200 || resp.body.empty())
        {
            err = resp.status > 0 ? ("HTTP " + std::to_string(resp.status)) : "Network error";
            return false;
        }
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(resp.body);
        }
        catch (...)
        {
            err = "parse failed";
            return false;
        }
        if (!j.is_array())
        {
            err = "bad shape";
            return false;
        }

        out.clear();
        out.reserve(j.size());
        for (const auto &e : j)
        {
            if (!e.is_object())
                continue;
            auto dt = e.find("date");
            if (dt == e.end() || !dt->is_string())
                continue;
            const int day = DateStrToDay(dt->get<std::string>());
            if (day <= 0)
                continue;
            PriceHistory::MarketPoint p;
            p.day = day;
            p.buyValue = ReadI64(e, "buy_value");
            p.sellValue = ReadI64(e, "sell_value");
            if (p.buyValue > 0 || p.sellValue > 0)
                out.push_back(p);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b)
                  { return a.day < b.day; });
        if (out.empty())
        {
            err = "empty";
            return false;
        }
        return true;
    }
    void ProcessMarket(PriceHistory::MarketSeries &merged, std::string &err)
    {
        LoadMarketDisk(merged);
        const int today = TodayDayImpl();
        const bool stale = merged.empty() || merged.back().day < today - 1;
        if (!stale)
            return;
        const int startDay = merged.empty() ? (today - kDefaultDays) : merged.back().day;
        PriceHistory::MarketSeries fetched;
        std::string ferr;
        if (FetchMarket(startDay, fetched, ferr))
        {
            MergeMarket(merged, fetched);
            SaveMarketDisk(merged);
        }
        else
            err = ferr; // serve the stale disk cache; never clobber
    }

    void LoadSeed(const std::string &seedFile); // defined below; the worker loads the seed off-thread at startup

    void WorkerLoop()
    {
        // Load the bundled seed FIRST, off the main thread, so the ~15 MB JSON parse never hitches AddonLoad.
        {
            std::string seedFile;
            {
                std::lock_guard<std::mutex> lk(g_mtx);
                seedFile = g_seedFile;
            }
            LoadSeed(seedFile);
        }
        for (;;)
        {
            int id = 0;
            bool market = false;
            {
                std::unique_lock<std::mutex> lk(g_mtx);
                g_cv.wait(lk, []
                          { return g_stop || !g_queue.empty() || (g_wantMarket && !g_marketDone && !g_marketFetching) || !g_prewarm.empty(); });
                if (g_stop)
                    return;
                if (!g_queue.empty())
                {
                    id = g_queue.front();
                    g_queue.pop_front();
                } // on-view (high priority)
                else if (g_wantMarket && !g_marketDone && !g_marketFetching)
                {
                    market = true;
                    g_marketFetching = true;
                }
                else if (!g_prewarm.empty())
                {
                    id = g_prewarm.front();
                    g_prewarm.pop_front();
                    g_prewarmSet.erase(id);
                    g_fetching.insert(id);
                } // login prewarm (low)
                else
                    continue;
            }
            if (market)
            {
                PriceHistory::MarketSeries ms;
                std::string err;
                ProcessMarket(ms, err);
                std::lock_guard<std::mutex> lk(g_mtx);
                if (g_stop)
                    return;
                if (!ms.empty())
                    g_marketSeries = std::move(ms);
                g_marketFetching = false;
                g_marketDone = true;
                if (!err.empty())
                    g_lastError = err;
            }
            else
            {
                PriceHistory::Series merged;
                std::string err;
                ProcessItem(id, merged, err);
                std::lock_guard<std::mutex> lk(g_mtx);
                if (g_stop)
                    return;
                if (!merged.empty())
                    g_series[id] = std::move(merged);
                g_fetching.erase(id);
                g_done.insert(id);
                if (!err.empty())
                    g_lastError = err;
            }
        }
    }

    // Bundled seed: { schema, series:{ "<id>":[[day,buyAvg,sellAvg,buyQty,sellQty],...] } }. Best-effort.
    // Runs on the WORKER thread:  only the final merge into g_series takes it (briefly),
    // so a concurrent Get()/Want() never blocks on the parse. Existing
    // entries win (a runtime delta fetched before the seed loaded must not be overwritten by the stale bundle).
    void LoadSeed(const std::string &seedFile)
    {
        std::string bytes;
        if (seedFile.empty() || !ReadFileBytes(seedFile, bytes))
            return;
        std::unordered_map<int, PriceHistory::Series> seed;
        try
        {
            nlohmann::json j = nlohmann::json::parse(bytes); // JSON seed (CBOR dropped); the per-item runtime cache is JSON too
            if (!j.is_object() || !j.contains("series") || !j["series"].is_object())
                return;
            for (auto it = j["series"].begin(); it != j["series"].end(); ++it)
            {
                const int id = std::atoi(it.key().c_str());
                if (id <= 0)
                    continue;
                PriceHistory::Series s;
                SeriesFromJson(it.value(), s);
                if (!s.empty())
                    seed.emplace(id, std::move(s));
            }
        }
        catch (...)
        {
            return;
        } // no seed / bad seed -> runtime fetch covers it
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_stop)
            return;
        for (auto &kv : seed)
            if (!g_series.count(kv.first))
                g_series.emplace(kv.first, std::move(kv.second));
    }
}

void PriceHistory::Init(const std::string &addonDir)
{
    Shutdown();
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_stop = false;
        g_series.clear();
        g_done.clear();
        g_fetching.clear();
        g_queue.clear();
        g_prewarm.clear();
        g_prewarmSet.clear();
        g_marketSeries.clear();
        g_wantMarket = false;
        g_marketDone = false;
        g_marketFetching = false;
        g_lastError.clear();
        g_cacheDir = addonDir.empty() ? std::string() : (addonDir + "\\cache\\pricehistory");
        g_seedFile = addonDir.empty() ? std::string() : (addonDir + "\\data\\price_history.json");
    }
    if (!g_cacheDir.empty())
    {
        std::error_code ec;
        fs::create_directories(g_cacheDir, ec);
        // one-time: drop the legacy pre-JSON-migration .cbor price caches (per-item + market caches are .json now)
        try
        {
            std::vector<fs::path> stale;
            for (const auto &de : fs::directory_iterator(g_cacheDir))
                if (de.is_regular_file() && de.path().extension() == ".cbor")
                    stale.push_back(de.path());
            for (const fs::path &p : stale)
            {
                std::error_code e;
                fs::remove(p, e);
            }
        }
        catch (...)
        { /* best effort */
        }
    }
    g_worker = std::thread(WorkerLoop); // the worker loads the bundled seed off-thread, then serves requests
}

void PriceHistory::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_stop = true;
    }
    g_cv.notify_all();
    Api::Http::Abort(Api::Http::Scope::PriceHistory); // unblock OUR worker's in-flight Get -- only our scope
    if (g_worker.joinable())
        g_worker.join();

    std::lock_guard<std::mutex> lk(g_mtx);
    g_series.clear();
    g_done.clear();
    g_fetching.clear();
    g_queue.clear();
    g_prewarm.clear();
    g_prewarmSet.clear();
    g_marketSeries.clear();
    g_wantMarket = false;
    g_marketDone = false;
    g_marketFetching = false;
    g_cacheDir.clear();
    g_seedFile.clear();
    g_lastError.clear();
}

void PriceHistory::Want(int itemId)
{
    if (itemId <= 0)
        return;
    bool notify = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_stop)
            return;
        // Do NOT short-circuit on g_series: a bundled/loaded item still needs a one-time delta top-up this session
        // (the bundle ages). g_done/g_fetching gate the single fetch.
        if (g_done.count(itemId) || g_fetching.count(itemId))
            return;
        if (g_prewarmSet.erase(itemId)) // queued for low-priority prewarm -> promote to the live (high-pri) queue
            for (auto it = g_prewarm.begin(); it != g_prewarm.end(); ++it)
                if (*it == itemId)
                {
                    g_prewarm.erase(it);
                    break;
                }
        g_fetching.insert(itemId);
        g_queue.push_back(itemId);
        notify = true;
    }
    if (notify)
    {
        Api::Http::Resume(Api::Http::Scope::PriceHistory); // re-enable OUR scope if a prior shutdown aborted it
        g_cv.notify_one();
    }
}

void PriceHistory::Prewarm(const std::vector<int> &itemIds)
{
    bool any = false;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_stop)
            return;
        for (int id : itemIds)
        {
            // skip g_series too: a bundled item already has the floor (on-view delta handles it) -- prewarm is for
            // the LONG TAIL of owned items that aren't in the bundle.
            if (id <= 0 || g_series.count(id) || g_done.count(id) || g_fetching.count(id) || g_prewarmSet.count(id))
                continue;
            g_prewarm.push_back(id);
            g_prewarmSet.insert(id);
            any = true;
        }
    }
    if (any)
    {
        Api::Http::Resume(Api::Http::Scope::PriceHistory);
        g_cv.notify_one();
    }
}

bool PriceHistory::Get(int itemId, Series &out)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_series.find(itemId);
    if (it == g_series.end() || it->second.empty())
        return false;
    out = it->second;
    return true;
}

bool PriceHistory::IsFetching(int itemId)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_fetching.count(itemId) != 0;
}

void PriceHistory::WantMarketIndex()
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_stop || g_wantMarket)
            return;
        g_wantMarket = true;
    }
    Api::Http::Resume(Api::Http::Scope::PriceHistory);
    g_cv.notify_one();
}

bool PriceHistory::GetMarketIndex(MarketSeries &out)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_marketSeries.empty())
        return false;
    out = g_marketSeries;
    return true;
}

bool PriceHistory::IsFetchingMarket()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_wantMarket && !g_marketDone;
}

std::string PriceHistory::LastError()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_lastError;
}

int PriceHistory::TodayDay() { return TodayDayImpl(); }

std::string PriceHistory::DayLabel(int day)
{
    int y;
    unsigned m, d;
    CivilFromDays(day, y, m, d);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
    return buf;
}
