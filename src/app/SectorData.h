#pragma once
#include <cstdint>
#include <string>
namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// SectorData: the offline sector/zone-name source for the Zone Display popup (region / zone / sector name on
// entering a map and on crossing into a sector). The Zone Display fires on EVERY map -- cities, hubs,
// dungeons/fractals, story instances, festival maps -- so this is a STANDALONE all-maps dataset (data/
// sectors.json, built by builder/build_sectors.py), NOT the guide's per-leveling-zone datasets.
//
// At Init it loads the bundle into memory (mapId -> {name, regionName, continentId, rects, sector polygons in
// continent coords}). SectorAt() transforms the player's world position to continent coords (Coords::
// WorldXZToContinent) and point-in-polygons locally -- so the popup needs NO network at runtime. The ONLY
// fallback is a brand-new map absent from the bundle (a release before the next rebuild): one async /v2/maps
// (+ floor) fetch fills it via the shared api.Pump and persists to <cache>/sectors.json. Missing data degrades
// gracefully to region + zone with no sector. Single-threaded (render/compute path + Pump callbacks, no locks),
// mirroring StaticData.
// -----------------------------------------------------------------------------------------------------
namespace SectorData
{
    // Load bundled <dataDir>/sectors.json, then overlay any <cacheDir>/sectors.json (runtime-fetched new maps).
    // Call once at startup (next to StaticData::Init). cacheDir "" = no runtime persistence.
    void Init(Api::Client* api, const std::string& dataDir, const std::string& cacheDir);
    void Shutdown();   // free the maps on addon disable/unload (call from AddonUnload, after api.Shutdown)

    bool        HasMap(uint32_t mapId);                 // is this map present (bundle or runtime cache)?
    const char* MapName(uint32_t mapId);                // map display name ("" + kicks the fallback fetch on miss)
    const char* RegionName(uint32_t mapId);             // region/zone-group name ("" on miss; kicks the fallback)

    // Named sector containing the player's WORLD position (raw MumbleLink metres) on `mapId`. Returns "" when the
    // map isn't loaded, has no sectors, or the point is outside every sector polygon. Kicks the fallback on a miss.
    std::string SectorAt(uint32_t mapId, float worldX, float worldZ);

    // Ensure a map's data is present; on a bundle miss kicks the one-shot async API fallback (idempotent,
    // back-off after a failure). Safe to call per-frame.
    void EnsureMap(uint32_t mapId);
}
