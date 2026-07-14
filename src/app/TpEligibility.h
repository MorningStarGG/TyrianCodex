#pragma once
#include <cstdint>
#include <string>
namespace Api { class Client; }

// Disk-backed stale-while-revalidate cache for the official Trading Post item universe.
// Loads tp_ids.json from the addon root so UI can answer "is this item TP-valid?" synchronously, then Warm()
// refreshes the small anonymous /v2/commerce/prices id list once per session and saves it back to disk.
namespace TpEligibility
{
    void Init(Api::Client* api, const std::string& addonDir);
    void Shutdown();
    void Warm();

    bool IsReady();
    bool IsRefreshing();
    bool IsTradeable(int itemId);
    int  Count();
    long long FetchedUtc();
    uint64_t Version();
}
