#include "util/ImageCache.h"
#include "util/TexPack.h"     // bundled icon/tile packs (decode straight from mmap'd memory)
#include "util/OwnedTex.h"    // addon-owned D3D11 textures (decode + SRV + eviction) -- replaces Nexus's texture API
#include "Shared.h"           // APIDefs (AddonAPI_t*) + Texture_t
#include "Version.h"          // TC_VERSION_STRING -- the version-keyed tile re-seed marker
#include "api/core/Http.h"    // the shared WinHTTP transport (also used by the API client)
#include "util/Json.h"        // Json::WriteAtomic (shared atomic writer)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace ImageCache
{
    namespace
    {
        // Per-entry SOURCE lifecycle: Pending = the source bytes aren't on disk yet (downloading); Ready = the
        // source is available (a cache file on disk, or a bundled pack slice); Missing = download failed / 404.
        // `created` tracks the separate GPU step: once a Create job has decoded the source into an OwnedTex SRV.
        enum class St { Pending, Ready, Missing };
        // A worker job: Disk = get the source onto disk (download/bundle) -- no GPU; Create = decode the source
        // (pack span / cache file) into an addon-owned texture; Revalidate = re-check a stale tile + swap on change.
        enum class Kind { Disk, Create, Revalidate };

        struct Entry
        {
            St             state = St::Pending;
            Ns             ns = Ns::Assets;
            std::string    path;              // on-disk cache file ("" for pack-only art)
            std::string    url;               // source URL ("" for pack-only art)
            const TexPack* pack = nullptr;    // bundled pack this entry lives in, if any
            std::string    packKey;           // key into that pack
            bool           fromPack = false;  // the source is a pack slice (decode from mmap, no disk)
            bool           evictable = false; // an unbounded cache (tiles / downloaded icons) OwnedTex may LRU-evict
            bool           creating = false;  // a Create job is queued/running (cleared by the worker) -- don't double-queue
            bool           stale = false;     // a revalidation found new bytes: drop the old texture (render-thread) + re-submit
            bool           ttlChecked = false;   // tiles: stat-and-maybe-revalidate done once (not per frame)
            bool           revalidating = false;
            std::uint64_t  lastReq = 0;           // monotonic access stamp (s_reqSeq) -> LRU bound in PruneEntriesLocked
        };
        struct Job { Kind kind = Kind::Disk; std::string id, url, path, bundled, packKey; const TexPack* pack = nullptr;
                     bool fromPack = false; bool evictable = false; };

        std::string                          s_tilesDir, s_assetsDir, s_bundledTilesDir, s_bundledItemsDir;
        TexPack                              s_itemsPack, s_tilesPack, s_mapsPack, s_portraitsPack;   // bundled icon / tile / static-map / portrait packs (mmap'd, open-once)
        TexPack                              s_decorationsPack, s_catsPack, s_skinsPack, s_cosmeticRendersPack;   // homestead deco/cat screenshots + wardrobe skin renders + cosmetic appearance renders (mmap'd, open-once)
        TexPack                              s_cosmeticIconsPack;   // Collections GRID icons (FNV-of-URL keyed, like items.pack); wiki appearance renders are a separate cosmetic_renders.pack
        TexPack                              s_loadingscreensPack;  // viewer header art (keyed by loading_screens.json relPath basename)
        constexpr int                        kImgWorkers = 4;   // parallel workers -> a wiki page's images fetch + decode concurrently
        std::vector<std::thread>             s_workers;
        std::mutex                           s_mtx;          // guards everything below
        std::condition_variable              s_cv;
        std::queue<Job>                      s_jobs;
        std::unordered_map<std::string, Entry> s_entries;    // live state, keyed by texture id
        std::uint64_t                        s_reqSeq = 0;   // bumped on every Ensure() access -> per-entry LRU stamp
        bool                                 s_stop = false;
        int  s_packSeeded = 0, s_looseSeeded = 0, s_downloaded = 0;   // distinct textures by seed source (diagnostics)

        constexpr auto kTileMaxAge = std::chrono::hours(24 * 30);   // ~30 days, then tiles revalidate

        std::string ReadFile(const std::string& path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return {};
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        // Delegate to the shared util/Json atomic writer (single implementation -> no drift, .tmp-leak fix).
        void WriteAtomic(const std::string& path, const std::string& bytes) { Json::WriteAtomic(path, bytes); }

        bool FileOlderThan(const std::string& path, std::chrono::seconds maxAge)
        {
            std::error_code ec;
            auto t = fs::last_write_time(path, ec);
            if (ec) return false;
            return (fs::file_time_type::clock::now() - t) > maxAge;
        }

        void WorkerLoop()
        {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lk(s_mtx);
                    s_cv.wait(lk, [] { return s_stop || !s_jobs.empty(); });
                    if (s_stop) return;
                    job = std::move(s_jobs.front()); s_jobs.pop();
                }

                if (job.kind == Kind::Revalidate)
                {
                    // Off the render thread: re-download the tile; if the bytes changed, swap the cache file and drop
                    // the old texture so the next Get re-creates it from the new bytes.
                    std::error_code ec;
                    Api::Http::RawResponse resp = Api::Http::Get(job.url, {}, Api::Http::Scope::Images);
                    const bool ok = (resp.status == 200) && !resp.body.empty();
                    std::lock_guard<std::mutex> lk(s_mtx);
                    auto it = s_entries.find(job.id);
                    if (it != s_entries.end()) it->second.revalidating = false;
                    if (ok)
                    {
                        if (ReadFile(job.path) != resp.body)
                        {
                            // Bytes changed: write the new file + mark stale. The RENDER thread evicts the old
                            // texture + re-submits (device Release must not run on a worker -- single-threaded device).
                            WriteAtomic(job.path, resp.body);
                            if (it != s_entries.end()) it->second.stale = true;
                        }
                        else fs::last_write_time(job.path, fs::file_time_type::clock::now(), ec);
                    }
                    continue;
                }

                if (job.kind == Kind::Disk)
                {
                    // Ensure the source bytes are on disk (no GPU): try the bundled loose copy, else download + cache.
                    std::string body; bool ok = false, usedBundle = false; std::error_code ec;
                    if (!job.bundled.empty() && fs::exists(job.bundled, ec)) { body = ReadFile(job.bundled); ok = usedBundle = !body.empty(); }
                    if (!ok)
                    {
                        Api::Http::RawResponse resp = Api::Http::Get(job.url, {}, Api::Http::Scope::Images);
                        if ((resp.status == 200) && !resp.body.empty()) { body = std::move(resp.body); ok = true; }
                    }
                    if (ok && !job.path.empty()) WriteAtomic(job.path, body);
                    std::lock_guard<std::mutex> lk(s_mtx);
                    auto it = s_entries.find(job.id);
                    if (it != s_entries.end()) it->second.state = ok ? St::Ready : St::Missing;
                    if (ok && usedBundle) ++s_looseSeeded; else if (ok) ++s_downloaded;
                    continue;
                }

                // Kind::Create: decode the ready source (pack slice or cache file) on THIS worker thread and QUEUE
                // the pixels for render-thread GPU upload (OwnedTex). No D3D11 device call here -- GW2's device is
                // single-threaded, so all texture creation stays on the render thread (see OwnedTex::Tick).
                const void* data = nullptr; size_t size = 0; std::string body; TexPack::Span span{};
                if (job.fromPack && job.pack) { span = job.pack->Get(job.packKey); data = span.data; size = span.size; }   // mmap span, valid for process lifetime
                else if (!job.path.empty()) { body = ReadFile(job.path); data = body.data(); size = body.size(); }
                if (data && size) OwnedTex::SubmitEncoded(job.id, data, size, job.evictable);
                std::lock_guard<std::mutex> lk(s_mtx);
                auto it = s_entries.find(job.id);
                if (it != s_entries.end()) it->second.creating = false;   // done -> allow a re-request (e.g. after eviction)
                if (job.fromPack) ++s_packSeeded;
            }
        }

        // Caller holds s_mtx. Bound the metadata map so a long session (heavy wiki/icon browsing = many distinct
        // ids) can't grow s_entries without limit. When it exceeds kMaxEntries, prune down to a lower watermark
        // (kKeep, hysteresis) by dropping entries -- and their VRAM -- not touched within the last kKeep Ensure()
        // calls and NOT in-flight. Displayed images are re-fetched every frame, so their lastReq stays in the window
        // and they're never dropped (no flicker); an evicted id re-seeds cheaply from pack/disk on next request.
        // Pruning to a watermark below the cap means this O(n) sweep runs ~once per (kMaxEntries - kKeep) new ids,
        // not on every new id once over cap.
        void PruneEntriesLocked()
        {
            constexpr size_t        kMaxEntries = 2048;            // cap on distinct-id metadata entries (~0.5-1 MB)
            constexpr std::uint64_t kKeep       = kMaxEntries * 3 / 4;   // prune down to ~75% so it doesn't re-fire per id
            if (s_entries.size() <= kMaxEntries) return;
            const std::uint64_t keepFrom = (s_reqSeq > kKeep) ? (s_reqSeq - kKeep) : 0;
            for (auto it = s_entries.begin(); it != s_entries.end();)
            {
                if (it->second.lastReq < keepFrom && !it->second.creating && !it->second.revalidating)
                {
                    OwnedTex::Evict(it->first);   // render-thread device Release (Ensure is render-thread only)
                    it = s_entries.erase(it);
                }
                else
                    ++it;
            }
        }

        // Caller holds s_mtx. First sight of an id: resolve the SOURCE. A bundled pack hit or an existing cache file
        // is Ready immediately; otherwise queue a Disk job (loose-seed then download) and mark Pending. Does NOT
        // create the GPU texture (see RequestCreate) -- so a Prefetch warms only disk, never VRAM.
        Entry& Ensure(const std::string& id, std::string path, std::string url, Ns ns,
                      std::string bundled, const TexPack* pack, std::string packKey, bool evictable)
        {
            auto it = s_entries.find(id);
            if (it != s_entries.end()) { it->second.lastReq = ++s_reqSeq; return it->second; }
            PruneEntriesLocked();   // bound s_entries before adding a new id (drops long-idle metadata + its VRAM)
            Entry e; e.ns = ns; e.path = path; e.url = url; e.evictable = evictable; e.lastReq = ++s_reqSeq;
            std::error_code ec;
            if (pack && pack->Has(packKey)) { e.fromPack = true; e.pack = pack; e.packKey = std::move(packKey); e.state = St::Ready; }
            else if (!path.empty() && fs::exists(path, ec)) e.state = St::Ready;
            else { s_jobs.push({ Kind::Disk, id, std::move(url), std::move(path), std::move(bundled), {}, nullptr, false, evictable }); s_cv.notify_one(); e.state = St::Pending; }
            return s_entries.emplace(id, std::move(e)).first->second;
        }

        // Caller holds s_mtx. Queue the one-shot Create job (worker decode -> render-thread upload) for a
        // Ready-source entry, exactly once. OwnedTex owns "is it uploaded yet"; we just avoid re-queuing.
        void RequestCreate(const std::string& id, Entry& e)
        {
            if (e.state != St::Ready || e.creating) return;
            if (OwnedTex::Wanted(id)) return;   // already created, or decoding / pending upload -> nothing to do. This
                                                // is false again AFTER an eviction, so an evicted image re-decodes here.
            e.creating = true;                  // cleared by the worker once the decode is submitted (or has failed)
            s_jobs.push({ Kind::Create, id, {}, e.path, {}, e.packKey, e.pack, e.fromPack, e.evictable });
            s_cv.notify_one();
        }

        void TileIds(int c, int f, int z, int x, int y, std::string& id, std::string& path, std::string& url, std::string& bundled, std::string& packKey)
        {
            char b[160];
            std::snprintf(b, sizeof(b), "TC_TILE_%d_%d_%d_%d_%d", c, f, z, x, y);                          id   = b;
            std::snprintf(b, sizeof(b), "%s\\%d_%d_%d_%d_%d.jpg", s_tilesDir.c_str(), c, f, z, x, y);        path = b;
            std::snprintf(b, sizeof(b), "https://tiles.guildwars2.com/%d/%d/%d/%d/%d.jpg", c, f, z, x, y);  url  = b;
            // bundled copy shipped with the addon: data/textures/tiles/{c}/{f}/{z}/{x}/{y}.jpg
            char tail[64]; std::snprintf(tail, sizeof(tail), "\\%d\\%d\\%d\\%d\\%d.jpg", c, f, z, x, y);     bundled = s_bundledTilesDir + tail;
            // key into tiles.pack: the forward-slash coordinate path (matches build_texpacks.py)
            char pk[64]; std::snprintf(pk, sizeof(pk), "%d/%d/%d/%d/%d.jpg", c, f, z, x, y);                 packKey = pk;
        }

        // FNV-1a (64-bit) - an EXPLICIT, deterministic hash so the asset filename is identical across launches
        // (std::hash<string> is not guaranteed stable across runs), which is what makes the disk cache persist.
        std::uint64_t Fnv1a(const std::string& s)
        {
            std::uint64_t h = 1469598103934665603ull;
            for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
            return h;
        }

        // Bare disk filename for an arbitrary URL: a hash of the URL + its extension (content-keyed -> never
        // stale). Identical on disk across launches AND identical to what the builder names the bundled icon,
        // so a shipped icon seeds the runtime cache (mirroring the tile bundle).
        std::string AssetFileName(const std::string& url)
        {
            std::string ext = ".png";
            const size_t slash = url.find_last_of('/');
            const size_t dot   = url.find_last_of('.');
            if (dot != std::string::npos && (slash == std::string::npos || dot > slash) && url.size() - dot <= 5)
                ext = url.substr(dot);
            char b[32];
            std::snprintf(b, sizeof(b), "%016llx%s", (unsigned long long)Fnv1a(url), ext.c_str());
            return b;
        }
        std::string AssetPath(const std::string& url)        { return s_assetsDir + "\\" + AssetFileName(url); }
        // Bundled icon shipped with the addon (data/textures/items/<hash>.<ext>) -- the seed copied into
        // cache/assets on first use, exactly like the tile bundle. "" when no bundle dir is configured.
        std::string AssetBundledPath(const std::string& url) { return s_bundledItemsDir.empty() ? std::string() : (s_bundledItemsDir + "\\" + AssetFileName(url)); }

        // The shared render-thread tail for the BUNDLE-ONLY packs (maps/portraits/decorations/skins/cosmetics):
        // Ensure a pack-source entry, request its create, and hand back the addon-owned texture once decoded.
        // These are EVICTABLE: the big renders (512x512 screenshots browsed in bulk) would otherwise pile up
        // resident forever, and they re-decode from the mmap'd pack instantly (no download) when shown again.
        const Texture_t* GetPacked(const std::string& id, const TexPack* pack, const std::string& key)
        {
            if (!APIDefs || !pack || !pack->IsOpen() || !pack->Has(key)) return nullptr;
            {
                std::lock_guard<std::mutex> lk(s_mtx);
                Entry& e = Ensure(id, "", "", Ns::Assets, "", pack, key, /*evictable*/ true);
                RequestCreate(id, e);
            }
            return OwnedTex::Get(id);
        }
    }

    void Init()
    {
        if (!APIDefs) return;
        const char* d = AddonDir();
        if (!d) return;
        OwnedTex::Init();   // acquire the D3D11 device (best-effort; Create re-acquires lazily if not yet ready)
        std::error_code ec;
        s_tilesDir  = std::string(d) + "\\cache\\tiles";
        s_assetsDir = std::string(d) + "\\cache\\assets";
        s_bundledTilesDir = std::string(d) + "\\data\\textures\\tiles";   // read-only loose tiles (fallback if no pack)
        s_bundledItemsDir = std::string(d) + "\\data\\textures\\items";   // read-only loose item icons (fallback if no pack)

        // -- Data-bootstrap plumbing (Nexus ships only the DLL; DataBootstrap self-downloads the packs). Done BEFORE
        //    opening the packs, once per launch. --
        const std::string texDir    = std::string(d) + "\\data\\textures";
        const std::string stagedDir = std::string(d) + "\\cache\\staged";
        // (1) Apply packs staged by a prior session's background UPDATE download: an already-mmap'd pack file can't
        //     be overwritten live, so an updated one waits in cache/staged and is moved into place now.
        for (const auto& p : fs::directory_iterator(stagedDir, ec))
        {
            if (ec) break;
            if (p.path().extension() != ".pack") continue;
            const fs::path dst = fs::path(texDir) / p.path().filename();
            std::error_code e2; fs::rename(p.path(), dst, e2);
            if (e2) { fs::copy_file(p.path(), dst, fs::copy_options::overwrite_existing, e2); fs::remove(p.path(), e2); }
        }
        ec.clear();
        // (2) Purge CDN duplicates: while the packs were still downloading last session, GetTile/GetUrl fell back to
        //     the live CDN into cache/tiles + cache/assets. Now that items.pack/tiles.pack are present those copies
        //     are redundant -> wipe them once (DataBootstrap drops this marker when the assets download finished).
        const std::string purgeMarker = std::string(d) + "\\cache\\.purge-cdn-dupes";
        if (fs::exists(purgeMarker, ec))
        {
            std::error_code e2;
            fs::remove_all(s_tilesDir, e2);
            fs::remove_all(s_assetsDir, e2);
            fs::remove(purgeMarker, e2);
        }
        ec.clear();

        // Bundled packs (preferred seed: mmap'd, from-memory, no loose files). Open-once; absent -> loose fallback.
        s_itemsPack.Open(std::string(d) + "\\data\\textures\\items.pack");
        s_tilesPack.Open(std::string(d) + "\\data\\textures\\tiles.pack");
        s_mapsPack.Open(std::string(d) + "\\data\\textures\\maps.pack");   // static instance-map images (keyed by slug)
        s_portraitsPack.Open(std::string(d) + "\\data\\textures\\portraits.pack");   // Content-tab boss/strike/fractal portraits (keyed by entity id)
        s_decorationsPack.Open(std::string(d) + "\\data\\textures\\decorations.pack");   // homestead decoration screenshots (keyed by decoration id)
        s_catsPack.Open(std::string(d) + "\\data\\textures\\cats.pack");                 // home-instance cat location maps (keyed by "cat"+id)
        s_skinsPack.Open(std::string(d) + "\\data\\textures\\skins.pack");               // wardrobe skin appearance renders (keyed by skin id)
        s_cosmeticRendersPack.Open(std::string(d) + "\\data\\textures\\cosmetic_renders.pack");   // cosmetic appearance renders (keyed "<kind>_<id>")
        s_cosmeticIconsPack.Open(std::string(d) + "\\data\\textures\\cosmetic_icons.pack");   // Collections grid icons (keyed by FNV-of-URL, like items.pack)
        s_loadingscreensPack.Open(std::string(d) + "\\data\\textures\\loadingscreens.pack");   // viewer header art (keyed by loading-screen filename)
        fs::create_directories(s_tilesDir, ec);
        fs::create_directories(s_assetsDir, ec);
        if (ec) { s_tilesDir.clear(); s_assetsDir.clear(); return; }

        // Version-keyed tile re-seed. Bundled map tiles are copied into cache/tiles the first time each is
        // used, so a refreshed bundle (shipped with a new addon version) would otherwise stay shadowed by the
        // already-seeded older copies. When the compiled addon version differs from the marker we wrote last
        // run, wipe cache/tiles so every tile re-seeds from the (updated) bundle. Only tiles can hold stale
        // BUNDLED content; cache/assets is keyed by URL hash (immutable), so it is left untouched. Done here,
        // before the worker starts -- a fresh process with nothing loaded yet, so an on-disk wipe suffices.
        const std::string verMarker = std::string(d) + "\\cache\\tiles.version";
        if (ReadFile(verMarker) != TC_VERSION_STRING)
        {
            for (const auto& p : fs::directory_iterator(s_tilesDir, ec))
            {
                if (ec) break;
                std::error_code e2; fs::remove_all(p.path(), e2);
            }
            ec.clear();
            WriteAtomic(verMarker, TC_VERSION_STRING);
        }

        Api::Http::Resume(Api::Http::Scope::Images);   // clear our scope's abort from a previous disable so downloads work again
        { std::lock_guard<std::mutex> lk(s_mtx); s_stop = false; }
        for (int i = 0; i < kImgWorkers; ++i) s_workers.emplace_back(WorkerLoop);
    }

    void Shutdown()
    {
        { std::lock_guard<std::mutex> lk(s_mtx); s_stop = true; }
        s_cv.notify_all();
        Api::Http::Abort(Api::Http::Scope::Images);   // cancel our scope's in-flight downloads so the joins return at once
        for (std::thread& w : s_workers) if (w.joinable()) w.join();
        s_workers.clear();
        std::queue<Job>().swap(s_jobs);
        s_entries.clear();
        // The OwnedTex textures + device are released in AddonUnload via OwnedTex::ReleaseAll, AFTER this join
        // (so no worker is mid-Create when we free them).
    }

    const Texture_t* GetTile(int c, int f, int z, int x, int y)
    {
        if (!APIDefs || s_tilesDir.empty()) return nullptr;
        std::string id, path, url, bundled, packKey; TileIds(c, f, z, x, y, id, path, url, bundled, packKey);
        std::string evictId;
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            Entry& e = Ensure(id, path, url, Ns::Tiles, bundled, &s_tilesPack, packKey, /*evictable*/ true);
            // TTL: once per entry, if the cached tile is old, queue a background revalidation (still serve it now).
            // Pack-seeded tiles have no cache file (FileOlderThan -> false), so they refresh via the version wipe.
            if (e.state == St::Ready && !e.ttlChecked && !e.revalidating && !e.fromPack)
            {
                e.ttlChecked = true;
                if (FileOlderThan(path, kTileMaxAge)) { e.revalidating = true; s_jobs.push({ Kind::Revalidate, id, url, path, {}, {}, nullptr, false, true }); s_cv.notify_one(); }
            }
            if (e.stale) { e.stale = false; e.creating = false; evictId = id; }   // revalidation found new bytes -> refresh
            else RequestCreate(id, e);
        }
        if (!evictId.empty()) OwnedTex::Evict(evictId);   // render-thread device Release; re-submits next frame
        return OwnedTex::Get(id);
    }

    void PrefetchTile(int c, int f, int z, int x, int y)
    {
        if (!APIDefs || s_tilesDir.empty()) return;
        std::string id, path, url, bundled, packKey; TileIds(c, f, z, x, y, id, path, url, bundled, packKey);
        std::lock_guard<std::mutex> lk(s_mtx);
        Ensure(id, path, url, Ns::Tiles, bundled, &s_tilesPack, packKey, true);   // disk/pack seed only. No GPU texture.
    }

    const Texture_t* GetUrl(const char* identifier, const char* url)
    {
        if (!APIDefs || s_assetsDir.empty() || !identifier || !*identifier || !url || !*url) return nullptr;
        const std::string id = identifier, path = AssetPath(url), packKey = AssetFileName(url);
        // URL icons live in items.pack (item icons) OR cosmetic_icons.pack (Collections grid icons), both keyed by
        // the FNV-of-URL filename -- pick whichever has it (items first), else download.
        const TexPack* pack = s_itemsPack.Has(packKey) ? &s_itemsPack
                            : (s_cosmeticIconsPack.Has(packKey) ? &s_cosmeticIconsPack : &s_itemsPack);
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            Entry& e = Ensure(id, path, url, Ns::Assets, AssetBundledPath(url), pack, packKey, /*evictable*/ true);
            RequestCreate(id, e);
        }
        return OwnedTex::Get(id);
    }

    // A bundled STATIC map image (maps.pack), keyed by map slug. Bundle-only -- no cache/download.
    const Texture_t* GetMapImage(const char* slug)
    {
        if (!slug || !*slug) return nullptr;
        return GetPacked(std::string("TC_MAP_") + slug, &s_mapsPack, slug);
    }

    // A bundled boss/strike/fractal PORTRAIT (portraits.pack), keyed by entity id. Bundle-only.
    const Texture_t* GetPortrait(const char* entityId)
    {
        if (!entityId || !*entityId) return nullptr;
        return GetPacked(std::string("TC_PORTRAIT_") + entityId, &s_portraitsPack, entityId);
    }

    const Texture_t* GetDecorationImage(int id)
    {
        if (id <= 0) return nullptr;
        const std::string key = std::to_string(id);
        return GetPacked(std::string("TC_DECOIMG_") + key, &s_decorationsPack, key);
    }

    const Texture_t* GetCatImage(int id)
    {
        if (id <= 0) return nullptr;
        const std::string key = "cat" + std::to_string(id);
        return GetPacked(std::string("TC_CATIMG_") + key, &s_catsPack, key);
    }

    const Texture_t* GetSkinImage(int id)
    {
        if (id <= 0) return nullptr;
        const std::string key = std::to_string(id);
        return GetPacked(std::string("TC_SKINIMG_") + key, &s_skinsPack, key);
    }

    const Texture_t* GetCosmeticRender(int kind, int id)
    {
        if (kind < 0 || id <= 0) return nullptr;
        const std::string key = std::to_string(kind) + "_" + std::to_string(id);   // == build_cosmetic_renders "<ki>_<sid>"
        return GetPacked(std::string("TC_COSMR_") + key, &s_cosmeticRendersPack, key);
    }

    const Texture_t* GetCosmeticRenderFrame(int kind, int id, int frame)
    {
        if (frame <= 0) return GetCosmeticRender(kind, id);   // frame 0 is the base key (also the static fallback)
        if (kind < 0 || id <= 0) return nullptr;
        const std::string key = std::to_string(kind) + "_" + std::to_string(id) + "@" + std::to_string(frame);
        return GetPacked(std::string("TC_COSMR_") + key, &s_cosmeticRendersPack, key);
    }

    const Texture_t* GetLoadingScreen(const char* key)
    {
        if (!key || !*key) return nullptr;
        return GetPacked(std::string("TC_LOADSCR_") + key, &s_loadingscreensPack, key);
    }

    // -- data-bootstrap: map a Pack id to its TexPack static + release-asset/on-disk filename --
    namespace
    {
        TexPack* PackPtr(Pack p)
        {
            switch (p)
            {
                case Pack::Items:           return &s_itemsPack;
                case Pack::Tiles:           return &s_tilesPack;
                case Pack::Maps:            return &s_mapsPack;
                case Pack::Portraits:       return &s_portraitsPack;
                case Pack::Decorations:     return &s_decorationsPack;
                case Pack::Cats:            return &s_catsPack;
                case Pack::Skins:           return &s_skinsPack;
                case Pack::CosmeticRenders: return &s_cosmeticRendersPack;
                case Pack::CosmeticIcons:   return &s_cosmeticIconsPack;
                case Pack::LoadingScreens:  return &s_loadingscreensPack;
            }
            return nullptr;
        }
    }

    const char* PackFileName(Pack p)
    {
        switch (p)
        {
            case Pack::Items:           return "items.pack";
            case Pack::Tiles:           return "tiles.pack";
            case Pack::Maps:            return "maps.pack";
            case Pack::Portraits:       return "portraits.pack";
            case Pack::Decorations:     return "decorations.pack";
            case Pack::Cats:            return "cats.pack";
            case Pack::Skins:           return "skins.pack";
            case Pack::CosmeticRenders: return "cosmetic_renders.pack";
            case Pack::CosmeticIcons:   return "cosmetic_icons.pack";
            case Pack::LoadingScreens:  return "loadingscreens.pack";
        }
        return "";
    }

    bool IsPackOpen(Pack p) { TexPack* tp = PackPtr(p); return tp && tp->IsOpen(); }

    int PackEntryCount(Pack p) { TexPack* tp = PackPtr(p); return (tp && tp->IsOpen()) ? (int)tp->Count() : 0; }

    void OpenPack(Pack p)
    {
        TexPack* tp = PackPtr(p);
        const char* d = APIDefs ? AddonDir() : nullptr;
        if (tp && d) tp->Open(std::string(d) + "\\data\\textures\\" + PackFileName(p));   // idempotent + thread-safe (first Open)
    }

    void PrefetchUrl(const char* identifier, const char* url)
    {
        if (!APIDefs || s_assetsDir.empty() || !identifier || !*identifier || !url || !*url) return;
        const std::string packKey = AssetFileName(url);
        // mirror GetUrl's pack pick (items first, then cosmetic icons) -- else a cosmetic_icons.pack icon
        // prefetched first is DOWNLOADED instead of seeded from the bundled pack.
        const TexPack* pack = s_itemsPack.Has(packKey) ? &s_itemsPack
                            : (s_cosmeticIconsPack.Has(packKey) ? &s_cosmeticIconsPack : &s_itemsPack);
        std::lock_guard<std::mutex> lk(s_mtx);
        Ensure(identifier, AssetPath(url), url, Ns::Assets, AssetBundledPath(url), pack, packKey, true);   // disk/pack seed only
    }

    Stats GetStats(Ns ns)
    {
        Stats s;
        const std::string& dir = (ns == Ns::Tiles) ? s_tilesDir : s_assetsDir;
        if (dir.empty()) return s;
        std::error_code ec;
        for (const auto& p : fs::directory_iterator(dir, ec))
        {
            if (ec) break;
            if (!p.is_regular_file() || p.path().extension() == ".tmp") continue;
            std::error_code e2; const auto sz = fs::file_size(p.path(), e2);
            if (!e2) { ++s.files; s.bytes += (long long)sz; }
        }
        return s;
    }

    PackInfo GetPackInfo()
    {
        PackInfo p;
        p.itemsOpen = s_itemsPack.IsOpen(); p.itemsCount = (int)s_itemsPack.Count();   // packs are open-once + immutable after Init
        p.tilesOpen = s_tilesPack.IsOpen(); p.tilesCount = (int)s_tilesPack.Count();
        std::lock_guard<std::mutex> lk(s_mtx);
        p.packSeeded = s_packSeeded; p.looseSeeded = s_looseSeeded; p.downloaded = s_downloaded;
        return p;
    }

    // Free the addon-owned texture VRAM for a namespace but KEEP its on-disk cache. Resets each entry so the next
    // Get re-decodes from the still-present file (instant, no re-download). Render thread (Diagnostics button).
    void FreeVram(Ns ns)
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        for (auto& kv : s_entries)
            if (kv.second.ns == ns) { OwnedTex::Evict(kv.first); kv.second.creating = false; }
    }

    void Clear(Ns ns)
    {
        const std::string& dir = (ns == Ns::Tiles) ? s_tilesDir : s_assetsDir;
        if (dir.empty()) return;
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            // Release the addon-owned VRAM for this namespace and forget the live entries; the next Get re-Ensures
            // (disk gone -> re-downloads) and re-creates a FRESH texture. Real eviction now (not a leaked orphan).
            for (auto it = s_entries.begin(); it != s_entries.end(); )
            {
                if (it->second.ns == ns) { OwnedTex::Evict(it->first); it = s_entries.erase(it); }
                else ++it;
            }
        }
        std::error_code ec;
        for (const auto& p : fs::directory_iterator(dir, ec))
        {
            if (ec) break;
            std::error_code e2; fs::remove(p.path(), e2);
        }
    }
}
