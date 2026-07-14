#pragma once

struct Texture_t;   // from Nexus.h (Width / Height / Resource)

// Disk-backed cache for EVERY networked image the addon shows, so things load instantly from disk instead of
// re-downloading each session. Nexus's URL texture loader only caches in MEMORY for the session, so without
// this every game restart re-fetches every map tile + icon. A blob is downloaded ONCE on a background worker
// (off the render thread, via the shared Api::Http transport), written under <addon>/cache/<ns>/, and
// thereafter loaded straight from that file.
//
// Two namespaces:
//   * Tiles  (cache/tiles/)  - GW2 map tiles (tiles.guildwars2.com/{c}/{f}/{z}/{x}/{y}.jpg), keyed by
//     coordinate. The coordinate is NOT content-bound (ArenaNet can redraw a zone's art), so tiles get a
//     ~30-day TTL with stale-while-revalidate: the disk tile is served instantly while a background
//     re-download checks for a change and hot-swaps it if the bytes differ.
//   * Assets (cache/assets/) - arbitrary image URLs (gw2dat icons, render.guildwars2.com reward icons,
//     item icons), keyed on disk by a HASH OF THE URL. The URL is content-bound (asset id / content
//     signature), so an asset is immutable-by-key and never goes stale -> no TTL. Like tiles, assets seed
//     from a read-only bundle shipped with the addon (data/textures/items/<hash>.<ext>, named to match the
//     cache key) so a shipped icon loads instantly + offline; a NEW item's icon is a new URL -> new key, so
//     it just seeds from the (updated) bundle or downloads -- no cache-clear needed (immutable by key).
//
// Live refresh note: textures are addon-OWNED (util/OwnedTex) with real eviction, so a changed file CAN be
// reloaded under its id. A revalidation swap (tile TTL) or a manual Clear evicts the OwnedTex entry and
// re-submits the fresh bytes -- workers only download/decode; the render thread does the GPU upload. There is
// no Nexus-session texture id left to orphan.
namespace ImageCache
{
    enum class Ns { Tiles, Assets };
    struct Stats { int files = 0; long long bytes = 0; };
    // Whether the bundled packs opened + how many textures this session were seeded from each source. Lets the
    // Diagnostics tab prove the pack (from-memory) path is actually in use vs the loose files / network.
    struct PackInfo
    {
        bool itemsOpen = false, tilesOpen = false;
        int  itemsCount = 0, tilesCount = 0;                     // entries in each bundled pack
        int  packSeeded = 0, looseSeeded = 0, downloaded = 0;    // distinct textures by seed source this session
    };

    void Init();       // resolve + create the cache dirs and start the fetch worker (call in AddonLoad)
    void Shutdown();   // stop + join the worker (call in AddonUnload BEFORE the API/WinHTTP shutdown)

    // -- map tiles (was MapTiles::Get/Prefetch) --
    // Get: disk-cached tile texture; first sight kicks a background download; null until on disk AND GPU-loaded
    // (call every frame). Also queues a TTL revalidation if the cached file is >~30 days old. Render thread only.
    const Texture_t* GetTile(int continentId, int floor, int zoom, int x, int y);
    void PrefetchTile(int continentId, int floor, int zoom, int x, int y);   // ensure on disk, no GPU texture

    // -- arbitrary image URL (gw2dat / render icons) --
    // `identifier` is the Nexus session texture id (stable, e.g. "TC_DAT_123"); the disk file is keyed by a
    // hash of `url` so a reworked icon (new URL/signature) becomes a new file automatically.
    const Texture_t* GetUrl(const char* identifier, const char* url);
    void PrefetchUrl(const char* identifier, const char* url);               // ensure on disk, no GPU texture

    // -- bundled static map image (maps.pack), keyed by map slug -- for instanced content with no live tiles.
    // Bundle-only (no download); null if the slug isn't packed. Render thread only.
    const Texture_t* GetMapImage(const char* slug);

    // -- bundled boss/strike/fractal portrait (portraits.pack), keyed by entity id (raid boss id, else slug(name)).
    // Bundle-only (no download); null if not packed (caller falls back to a glyph). Render thread only.
    const Texture_t* GetPortrait(const char* entityId);

    // -- bundled homestead decoration SCREENSHOT (decorations.pack, keyed by decoration id) + home-instance cat
    // location map (cats.pack, keyed by "cat"+id) -- the Homestead-tab tooltips. Bundle-only; null if not packed.
    const Texture_t* GetDecorationImage(int id);
    const Texture_t* GetCatImage(int id);

    // -- bundled wardrobe skin APPEARANCE render (skins.pack, keyed by skin id) -- the Wardrobe skin tooltip hero
    // image. Bundle-only; null if not packed (caller falls back to the skin's inventory icon). Render thread only.
    const Texture_t* GetSkinImage(int id);
    const Texture_t* GetCosmeticRender(int kind, int id);   // cosmetic appearance render (cosmetic_renders.pack, keyed "<kind>_<id>"); null -> caller falls back to the API icon
    const Texture_t* GetCosmeticRenderFrame(int kind, int id, int frame);   // one outcome frame of a multi-outcome cosmetic ("<kind>_<id>@N"; frame 0 == GetCosmeticRender); null -> fall back to frame 0

    // -- bundled loading-screen header art (loadingscreens.pack), keyed by the loading_screens.json relPath
    // basename (e.g. "18-divinity-s-reach.jpg") -- the viewer header banner per map. Bundle-only; null if not
    // packed yet (caller falls back to the default banner). Render thread only.
    const Texture_t* GetLoadingScreen(const char* key);

    // -- data-bootstrap support (Nexus ships only the DLL; the addon self-downloads its texture packs). `Pack`
    // identifies each shipped .pack; DataBootstrap downloads a missing one to data/textures/ then OpenPack()s it
    // LIVE (TexPack is thread-safe for a first Open). PackFileName is the release-asset + on-disk filename. --
    enum class Pack { Items, Tiles, Maps, Portraits, Decorations, Cats, Skins, CosmeticRenders, CosmeticIcons, LoadingScreens };
    inline constexpr int kPackCount = 10;
    const char* PackFileName(Pack p);   // "items.pack" ... "loadingscreens.pack"
    bool        IsPackOpen(Pack p);     // already mmap'd this session?
    int         PackEntryCount(Pack p); // # of images in the pack (0 if not open) -- diagnostics
    void        OpenPack(Pack p);       // (re)Open from data/textures/<file>; idempotent + thread-safe (first Open)

    // -- diagnostics / maintenance --
    Stats GetStats(Ns ns);   // on-disk file count + total bytes for a namespace (cheap fs scan; not per-frame)
    PackInfo GetPackInfo();   // bundled-pack open status + per-source seed counts (diagnostics)
    void  Clear(Ns ns);      // delete that namespace's on-disk STORAGE (+ free its VRAM); re-downloads on next view
    void  FreeVram(Ns ns);   // free that namespace's addon-owned texture VRAM but KEEP the disk cache -> the next
                             // view re-decodes from the still-present file (instant, no re-download)
}
