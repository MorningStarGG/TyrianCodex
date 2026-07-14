#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Runtime market snapshot from datawars2. This is additive to the official GW2 commerce API:
// official /v2/commerce stays the source of truth for per-item live prices/details, while this cache gives
// Flip Finder one current whole-market snapshot without paging thousands of official API requests.
// Stale-while-refresh: Init loads market_snapshot.json from the addon root, Warm refreshes in the background.
namespace MarketData
{
    struct Item
    {
        int id = 0;
        int buyPrice = 0;
        int buyQuantity = 0;
        int sellPrice = 0;
        int sellQuantity = 0;
        std::string name;
        std::string lastUpdate;
    };

    void Init(const std::string &addonDir);
    void Shutdown();
    void Warm(bool force = false);

    bool IsReady();
    bool IsRefreshing();
    std::string LastError();
    int Count();
    long long FetchedUtc();
    uint64_t Version();

    // Thread-safe copy. Use behind Version() in UI code so it is not copied every frame.
    void CopyItems(std::vector<Item> &out);
}
