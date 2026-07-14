#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// StaticData: the shared, key-agnostic REFERENCE-data cache (game content that never changes per account) --
// the "by concern" sibling of AccountData. Lazily fetched via the same Api::Client (callbacks land on the main
// thread through the existing Pump). Its real job is a SYNCHRONOUS, no-expiry per-frame lookup: the render path
// can't block, so SpecName(id) returns the cached name instantly (or "" while a single in-flight fetch fills
// it). That's why it can't lean on the API client's own Cache middleware -- that cache is memory-only AND
// expires (default 600s), and a lookup through it is still an async round-trip, not an instant string.
//
// Disk-backed stale-while-revalidate: this is all STATIC game content (spec names, map names, material
// categories) -- ids that never change per account -- so the last-known id->name maps are persisted to
// <cache>/staticdata.json and loaded STALE at Init. Names are therefore available at frame 0 from the previous
// session; the in-memory revalidate paths still run and re-save on any change, so only a cold first run pays the
// fetch. (Mirrors AccountData::LoadCache + CharacterLevel's charlevels.json.) Worst case after a game update: a
// brand-new id shows its fallback ("Category N" / "") for one session until its background fetch lands.
// -----------------------------------------------------------------------------------------------------
namespace StaticData
{
    // Store the client + cache dir, then load <cacheDir>/staticdata.json stale (call once at startup, next to
    // AccountData::Init).
    void Init(Api::Client* api, const std::string& cacheDir);
    void Shutdown();   // free the reference maps on addon disable/unload (call from AddonUnload, after api.Shutdown)

    // Elite-spec name for a /v2/specializations id; returns "" until the (one-time, anonymous) fetch lands,
    // kicking that fetch off the first time an unknown id is asked for.
    const char* SpecName(int id);

    // Map display name for a /v2/maps id; "" until the (one-time, anonymous) fetch lands, kicking that fetch off
    // lazily on first ask (30s backoff after a failed fetch, since this is polled per-frame from the off-coverage
    // header). A successful empty name is cached too (never refetched).
    const char* MapName(uint32_t id);
    // Convenience: MapName(id) but returns `fallback` instead of "" while the name is still unresolved.
    const char* MapNameOr(uint32_t id, const char* fallback);

    // Kick the one-time BULK /v2/materials fetch (the whole small, day-cached catalog) so category names are ready
    // before the Materials view renders. Idempotent. Warmed on login / Inventory-tab open alongside the inventory
    // storage (AccountData::WarmInventoryIndex), so MaterialCategoryName is normally a straight cache hit.
    void WarmMaterials();
    // Material-storage CATEGORY name for a /v2/materials category id. Returns a "Category {id}" fallback until the
    // catalog lands (kicking WarmMaterials as a safety net if it hasn't been warmed yet).
    std::string MaterialCategoryName(int categoryId);

    // Kick the one-time BULK /v2/itemstats fetch (the small attribute-combo catalog) so stat-set names are ready
    // before the item catalog resolves them. Idempotent. Warmed at the start of an ItemCatalog (re)build.
    void WarmItemStats();
    // Attribute-combination ("stat set") name for a /v2/itemstats id (e.g. 161 -> "Berserker's"). Returns "" until
    // the catalog lands (kicking WarmItemStats as a safety net). Used to bake the stat-set name into item entries.
    std::string ItemStatName(int id);

    // A wallet-currency catalog entry (resolved from the one-time BULK /v2/currencies fetch). `order` is GW2's
    // display order; `icon` is a render URL the UI loads via ImageCache (key "TC_CURRENCY_<id>").
    struct CurrencyRef { int id = 0; std::string name; std::string icon; int order = 0; };

    // Kick the one-time BULK /v2/currencies fetch (small, day-cached) so currency names/icons are ready before the
    // wallet/session surfaces render. Idempotent; warmed at login (WarmCaches). Also prefetches the icons.
    void WarmCurrencies();
    // Currency name for a /v2/currencies id (e.g. 2 -> "Karma"). "" until the catalog lands (kicks WarmCurrencies).
    const char* CurrencyName(int id);
    // Currency icon (render URL) for a /v2/currencies id. "" until the catalog lands.
    const char* CurrencyIcon(int id);
    // The whole currency catalog, sorted by GW2 display order (empty until the fetch lands). For pickers + the
    // wallet/session surfaces that enumerate every currency rather than a hardcoded handful.
    const std::vector<CurrencyRef>& Currencies();

    // --- WvW reference data (worlds + objectives + map rects) for the WvW Info Panel texts + widgets ---
    // Anonymous, day/24h-cached, persisted in staticdata.json; synchronous lookups (return "" / false until the
    // one-time bulk fetch lands, like the catalogs above). Warmed from AccountData::ResolveWvwTeam.
    void WarmWorlds();                                   // one-time BULK /v2/worlds (id -> name)
    const char* WorldName(int id);                      // world/team name; "" until landed (kicks WarmWorlds)

    void WarmWvwObjectives();                           // one-time BULK /v2/wvw/objectives (name + type + map + coord)
    const char* ObjectiveName(const std::string& id);   // objective display name; "" until landed (kicks the warm)
    // The objective's continent coord (x,y) on its map; false if unknown -- drives the rendered-map owner pins.
    bool ObjectiveCoord(const std::string& id, float& cx, float& cy);

    // A WvW map's continent rect [l,t,r,b] + continent id (the Mists = 2); false until the per-map /v2/maps fetch
    // lands (kicked lazily on a miss with a 30s backoff, like MapName). Used to composite the WvW mini-map tiles.
    bool WvwMapRect(int mapId, float rect[4], int& continentId);
}
