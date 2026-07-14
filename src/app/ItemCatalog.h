#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// ItemCatalog: the whole-game item database (the "Atlas, but for items" data layer). Holds every /v2/items
// entry -- base display/search fields + the type-specific stat block (kept as a compact raw-JSON string,
// parsed on display) + the resolved stat-set name -- so the All-Items browser searches + shows item info
// fully offline. Shipped as a builder-generated bundled seed (data/items.json) and seeded into a writable
// runtime cache (cache/items.json); a /v2/build check tops it up with new items after a game patch. Shares
// the one Api::Client + main-thread Pump (all callbacks land on the main thread), so no locking.
// -----------------------------------------------------------------------------------------------------
namespace ItemCatalog
{
    struct Item
    {
        int id = 0, level = 0, vendorValue = 0;
        std::string name, icon, rarity, type, subtype, chatLink, description, statSet;
        std::vector<std::string> flags, restrictions;
        std::string detailsRaw;   // the type-specific `details` object as compact JSON (parsed on display)
        std::string keyLower;     // precomputed lowercased haystack (name+type+subtype+rarity+statSet+desc+flags)
    };

    struct ProgressInfo { bool active = false; int fetched = 0; int total = 0; };
    struct TypeCount { std::string name; int count = 0; };

    enum class Sort { Relevance, Name, Level, Rarity, Type, Value };

    // dataDir = <addon>\data (bundled seed), cacheDir = <addon>\cache (writable). Loads the cache if present,
    // else the bundled seed, stale at startup.
    void Init(Api::Client* api, const std::string& dataDir, const std::string& cacheDir);
    void Shutdown();  // free ALL catalog memory on addon disable/unload (call from AddonUnload, after api.Shutdown)
    void Warm();      // login (background): /v2/build freshness check + delta, or a full fetch if empty
    void Rebuild();   // manual: force a full re-fetch of the whole catalog

    int          Count();
    uint64_t     Version();        // bumps whenever the catalog set changes (query-cache token)
    ProgressInfo Progress();       // {active,fetched,total} for the loading ring

    // Filter + sort -> indices into the catalog, cached behind a change token (never re-filtered per frame).
    // type "" / "all" = every type; subtype "" = every subtype. rarityMask: bit (1<<rank) per allowed rarity
    // (0 = all). lvMin/lvMax 0..80. textLower = the raw query (lowercased internally via QueryTerms).
    const std::vector<int>& Query(const char* text, const std::string& type, const std::string& subtype,
                                  unsigned rarityMask, int lvMin, int lvMax, Sort sort, bool asc);

    const std::vector<TypeCount>& Types();                       // distinct types present (+counts), display order
    const std::vector<TypeCount>& Subtypes(const std::string& type);  // distinct subtypes of a type (+counts)

    const Item& At(int index);     // index from Query() (or a raw catalog index); empty Item if out of range
    const Item& ById(int id);      // direct lookup by GW2 item id; empty Item if unknown
    int         RarityRank(const std::string& rarity);   // Junk(0)..Legendary(7); -1 unknown
}
