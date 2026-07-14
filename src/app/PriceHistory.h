#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Per-item Trading Post PRICE HISTORY from datawars2 (the official GW2 API has none -- datawars2 has logged the
// TP since 2019). Additive to /v2/commerce (which stays the source of truth for live per-item prices).
//   Want(id)  -> enqueue a background fetch of an item's daily buy/sell history (loads the per-item disk cache
//                first, then a delta top-up from datawars2; never blocks the render thread).
//   Get(id)   -> the cached/merged series if available (read it behind the series being non-empty, not every frame).
// Disk-cached per item under cache/pricehistory/<id>.json; A bundled data/price_history.json seed is
// the always-available floor. Only a successful, non-empty, validated response mutates a series;
// new points MERGE into the existing series; a failed/empty fetch serves the stale cache.
namespace PriceHistory
{
    struct Point
    {
        int day = 0;     // days since 1970-01-01 UTC (compact, sortable x-axis)
        int buyAvg = 0;  // average unit buy price, copper
        int sellAvg = 0; // average unit sell price, copper
        int buyQty = 0;  // average buy-side quantity (volume proxy)
        int sellQty = 0; // average sell-side quantity
    };
    using Series = std::vector<Point>; // ascending by day

    void Init(const std::string &addonDir); // set the cache dir + load the bundled seed (if present)
    void Shutdown();

    void Want(int itemId);                         // request -- enqueue a bg fetch if not already cached/pending
    void Prewarm(const std::vector<int> &itemIds); // login warm: LOW-priority bg fetch of many owned ids
    bool Get(int itemId, Series &out);             // the merged series (bundle U disk U delta), else false
    bool IsFetching(int itemId);                   // true while a fetch for this id is queued/in-flight (UI "Loading...")
    std::string LastError();

    int TodayDay();                // current UTC day-number (for the chart's x-range)
    std::string DayLabel(int day); // day-number -> "YYYY-MM-DD" (axis/tooltip labels)

    // The market-wide total-value index (datawars2 v2/history/total/json) -- the whole Trading Post's
    // gold in buy orders vs sell listings over time. Values are int64 copper (the market totals overflow int).
    struct MarketPoint
    {
        int day = 0;
        long long buyValue = 0;
        long long sellValue = 0;
    };
    using MarketSeries = std::vector<MarketPoint>;
    void WantMarketIndex();                 // request the index (idempotent; fetched once per session)
    bool GetMarketIndex(MarketSeries &out); // the merged market series, else false
    bool IsFetchingMarket();                // true until the index has loaded (UI "Loading...")
}
