#include "app/TpPrices.h"
#include "api/Client.h"
#include "api/v2/Commerce.h"
#include <imgui.h>            // ImGui::GetTime (the TTL clock; callbacks run during a frame)
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    Api::Client* g_api = nullptr;

    struct Entry { TpPrices::Price p; double at = -1e9; };
    std::unordered_map<int, Entry>  g_cache;     // id -> last known price (+ fetch time)
    std::unordered_set<int>         g_wanted;    // requested this frame, not yet fresh / in flight
    std::unordered_set<int>         g_inflight;  // ids in a pending request

    constexpr double kTtl   = 60.0;   // matches PricesEndpoint's 60s server cache
    constexpr size_t kCap   = 6000;   // bounded session cache (drop-all + refetch when exceeded)
    constexpr int    kBatch = 200;    // ids per /v2/commerce/prices request

    bool Fresh(const Entry& e, double now) { return e.p.known && (now - e.at) < kTtl; }
}

void TpPrices::Init(Api::Client* api) { g_api = api; }
void TpPrices::Shutdown() { g_api = nullptr; g_cache.clear(); g_wanted.clear(); g_inflight.clear(); }

void TpPrices::Want(int id)
{
    if (id <= 0 || g_inflight.count(id)) return;
    auto it = g_cache.find(id);
    if (it != g_cache.end() && Fresh(it->second, ImGui::GetTime())) return;
    g_wanted.insert(id);
}

bool TpPrices::Get(int id, Price& out)
{
    auto it = g_cache.find(id);
    if (it == g_cache.end()) { out = Price{}; return false; }
    out = it->second.p;
    return out.known;
}

void TpPrices::Pump()
{
    if (!g_api || g_wanted.empty()) return;
    if (g_cache.size() > kCap) g_cache.clear();   // coarse bound (like ItemsTab MetaFor): cheap to re-fetch visible

    std::vector<int> batch;
    batch.reserve(kBatch);
    for (auto it = g_wanted.begin(); it != g_wanted.end() && (int)batch.size() < kBatch; )
    {
        batch.push_back(*it);
        g_inflight.insert(*it);
        it = g_wanted.erase(it);
    }
    if (batch.empty()) return;

    g_api->V2().Commerce().Prices().Get(batch, [batch](Api::Result<std::vector<Api::V2::Price>> r) {
        for (int id : batch) g_inflight.erase(id);
        if (!r.ok) return;   // transient failure -> leave un-cached so a later Want re-requests
        const double now = ImGui::GetTime();
        std::unordered_set<int> got;
        for (const Api::V2::Price& pr : r.value)
        {
            Entry& e = g_cache[pr.id];
            e.p.buyUnit = pr.buyUnit; e.p.sellUnit = pr.sellUnit;
            e.p.buyQty  = pr.buyQty;  e.p.sellQty  = pr.sellQty;
            e.p.known = true; e.p.tradeable = true;
            e.at = now;
            got.insert(pr.id);
        }
        for (int id : batch)            // requested but no listing -> remember as non-tradeable (stop asking)
            if (!got.count(id))
            {
                Entry& e = g_cache[id];
                e.p = TpPrices::Price{};
                e.p.known = true; e.p.tradeable = false;
                e.at = now;
            }
    });
}
