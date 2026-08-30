#include "app/DataBootstrap.h"
#include "Version.h"          // TC_DATA_VERSION
#include "Shared.h"           // APIDefs->Log (diagnostic for a source override)
#include "api/core/Http.h"    // Api::Http::Get/Abort/Resume (worker-thread transport)
#include "util/TexPack.h"     // TexPack::ForEach (parse the core tcpk archive -- ONE parser, no drift)
#include "util/Json.h"        // Json::WriteAtomic (the ONE cache/settings writer)
#include "util/ImageCache.h"  // ImageCache::Pack / PackFileName / IsPackOpen / OpenPack / kPackCount

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // Sleep

namespace fs = std::filesystem;

namespace DataBootstrap
{
    namespace
    {
        // The GitHub Release the DLL pulls its data from -- always whichever release GitHub currently marks
        // "latest" (CI packages full data into every v* tag push, so latest always has it; see build.yml). This
        // is deliberately NOT a URL built from TC_DATA_VERSION: that scheme required every release to be tagged
        // v<TC_DATA_VERSION> exactly, a manual step with no build/release-time check, which silently 404s the
        // instant someone tags a release by the addon version (TC_VER_*) instead. TC_DATA_VERSION still gates
        // whether a download happens at all (see Init/StartAssets below, compared against the on-disk marker) --
        // only the SOURCE url no longer depends on it, so a DLL-only patch still doesn't force a redownload for
        // users already current.
        const std::string kGithubBaseUrl =
            "https://github.com/MorningStarGG/TyrianCodex/releases/latest/download/";
        const std::vector<std::pair<std::string, std::string>> kHdrs = { { "User-Agent", "TyrianCodex" } };

        // Resolved ONCE in Init: normally kGithubBaseUrl, but a `<addon>\bootstrap_url.txt` file OVERRIDES it so the
        // entire download path can be exercised with NO GitHub repo. Put in that file EITHER an http(s) prefix
        // (e.g. a local `python -m http.server` -> "http://localhost:8000/") OR a local directory holding the
        // release assets (read straight off disk, no server at all -> "D:\...\TyrianCodex\release").
        std::string s_baseUrl;
        bool IsHttp(const std::string& s) { return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0; }

        std::string          s_base;
        std::thread          s_coreWorker, s_assetsWorker;
        std::atomic<int>     s_phase{ (int)Phase::CheckCore };
        std::atomic<int>     s_pct{ 0 };
        std::atomic<bool>    s_stop{ false };
        std::atomic<bool>    s_assetsStarted{ false };
        std::mutex           s_errMtx;
        std::string          s_error;

        // Diagnostics counters (Diagnostics tab).
        std::atomic<bool>    s_sourceOverridden{ false };
        std::atomic<bool>    s_coreAdopted{ false };
        std::atomic<bool>    s_assetsActive{ false };
        std::atomic<bool>    s_assetsSettled{ false };
        std::atomic<int>     s_assetsFetched{ 0 };
        std::atomic<int>     s_assetsNeeded{ 0 };
        std::atomic<int>     s_assetsFailed{ 0 };
        std::atomic<int>     s_assetsCurIdx{ -1 };

        void SetError(const std::string& m) { std::lock_guard<std::mutex> lk(s_errMtx); s_error = m; }
        void SetPhase(Phase p) { s_phase.store((int)p, std::memory_order_release); }

        std::string ReadFile(const std::string& path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return {};
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }
        // Delegate to the shared util/Json atomic writer (single implementation -> no drift).
        void WriteAtomic(const std::string& path, const std::string& bytes) { Json::WriteAtomic(path, bytes); }

        bool AbortableSleep(int ms)   // returns false if we were asked to stop
        {
            for (int i = 0; i < ms && !s_stop.load(std::memory_order_acquire); i += 50) Sleep(50);
            return !s_stop.load(std::memory_order_acquire);
        }

        // Buffered download with 3x backoff (2/4/8 s). Returns true + fills `out` on HTTP 200. (A streaming
        // download-to-file variant would avoid buffering ~171 MB in RAM + give byte-level progress -- future work.)
        bool Download(const std::string& url, std::string& out)
        {
            for (int attempt = 0; attempt < 3 && !s_stop.load(std::memory_order_acquire); ++attempt)
            {
                Api::Http::RawResponse r = Api::Http::Get(url, kHdrs, Api::Http::Scope::Bootstrap);
                if (r.status == 200 && !r.body.empty()) { out = std::move(r.body); return true; }
                if (attempt < 2 && !AbortableSleep((2 << attempt) * 1000)) return false;
            }
            return false;
        }

        // Fetch one release asset by NAME (e.g. "tc-core.tcpk", "items.pack"), honoring s_baseUrl: an http(s) base
        // downloads; a local-directory base reads the file straight off disk (sideload -- test without a release).
        bool Fetch(const std::string& name, std::string& out)
        {
            if (IsHttp(s_baseUrl)) return Download(s_baseUrl + name, out);   // s_baseUrl already has a trailing '/'
            std::string dir = s_baseUrl;
            if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') dir += '\\';
            out = ReadFile(dir + name);
            return !out.empty();
        }

        std::string Marker(const char* name) { return s_base + "\\data\\" + name; }
        // A few core files whose presence means "the data folder is already installed" -- used to ADOPT an
        // existing install (data present, but no version marker yet, e.g. the first launch of this DLL update)
        // instead of re-downloading ~600 MB.
        bool SentinelPresent()
        {
            return fs::exists(s_base + "\\data\\loading_screens.json")
                && fs::exists(s_base + "\\data\\zones\\index.json")
                && fs::exists(s_base + "\\data\\fonts\\menomonia.ttf");
        }
        bool AllPacksPresent()
        {
            for (int i = 0; i < ImageCache::kPackCount; ++i)
                if (!fs::exists(s_base + "\\data\\textures\\" + ImageCache::PackFileName((ImageCache::Pack)i)))
                    return false;
            return true;
        }

        // --- CORE: download tc-core.tcpk, extract every entry (relpath -> file) under data/. ---
        void CoreWorkerFn()
        {
            s_coreAdopted.store(false);   // we're actually downloading, not adopting existing data
            SetPhase(Phase::DownloadingCore); s_pct.store(0);
            std::string body;
            if (!Fetch("tc-core.tcpk", body) || s_stop.load())
            { if (!s_stop.load()) { SetError("Core data download failed."); SetPhase(Phase::CoreFailed); } return; }

            int total = 0;
            TexPack::ForEach(body.data(), body.size(), [&](const std::string&, const void*, size_t) { ++total; });
            if (total <= 0) { SetError("Core archive is empty or corrupt."); SetPhase(Phase::CoreFailed); return; }

            int written = 0;
            const bool ok = TexPack::ForEach(body.data(), body.size(),
                [&](const std::string& key, const void* data, size_t len)
                {
                    if (s_stop.load()) return;
                    // Path-safety: the archive is ours over HTTPS, but never let a key escape data/ -- reject any
                    // "..", absolute, or drive-qualified key so a tampered archive can't write outside the tree.
                    if (key.empty() || key.front() == '/' || key.front() == '\\' ||
                        key.find("..") != std::string::npos || key.find(':') != std::string::npos)
                    { ++written; return; }
                    std::string rel = key; for (char& c : rel) if (c == '/') c = '\\';   // relpath -> Windows path
                    WriteAtomic(s_base + "\\data\\" + rel, std::string((const char*)data, len));
                    s_pct.store(100 * (++written) / total);
                });
            if (!ok || s_stop.load())
            { if (!s_stop.load()) { SetError("Core archive is corrupt."); SetPhase(Phase::CoreFailed); } return; }

            WriteAtomic(Marker(".coreversion"), TC_DATA_VERSION);
            SetPhase(Phase::Ready);
        }

        // --- ASSETS (background): per pack, download-live (absent) or stage (stale-but-open). ---
        void AssetsWorkerFn()
        {
            // A present pack is re-fetched ONLY on a genuine version bump (marker present AND different). An ABSENT
            // marker (fresh/adopted install) keeps the present packs as-is and just fills in any missing one.
            const std::string mk = ReadFile(Marker(".assetsversion"));
            const bool assetsStale = !mk.empty() && mk != TC_DATA_VERSION;

            int needed = 0;   // packs this run will actually fetch (diagnostics)
            for (int i = 0; i < ImageCache::kPackCount; ++i)
                if (!ImageCache::IsPackOpen((ImageCache::Pack)i) || assetsStale) ++needed;
            s_assetsNeeded.store(needed);
            s_assetsActive.store(true);

            bool allOk = true;
            for (int i = 0; i < ImageCache::kPackCount && !s_stop.load(); ++i)
            {
                const ImageCache::Pack p = (ImageCache::Pack)i;
                const std::string file = ImageCache::PackFileName(p);
                if (!ImageCache::IsPackOpen(p))          // absent this session -> download + open LIVE
                {
                    s_assetsCurIdx.store(i);
                    std::string body;
                    if (!Fetch(file, body)) { allOk = false; s_assetsFailed.fetch_add(1); continue; }
                    WriteAtomic(s_base + "\\data\\textures\\" + file, body);
                    ImageCache::OpenPack(p);             // TexPack is thread-safe for a first Open -> live switch-in
                    s_assetsFetched.fetch_add(1);
                }
                else if (assetsStale)                    // present + mmap'd but stale -> stage, apply next launch
                {
                    s_assetsCurIdx.store(i);
                    std::string body;
                    if (!Fetch(file, body)) { allOk = false; s_assetsFailed.fetch_add(1); continue; }
                    WriteAtomic(s_base + "\\cache\\staged\\" + file, body);
                    s_assetsFetched.fetch_add(1);
                }
                // else present + current -> skip
            }
            s_assetsCurIdx.store(-1);
            s_assetsActive.store(false);
            s_assetsSettled.store(true);
            if (allOk && !s_stop.load())
            {
                WriteAtomic(Marker(".assetsversion"), TC_DATA_VERSION);
                // Purge the CDN icons/tiles that were downloaded while the packs were still absent -- next launch.
                WriteAtomic(s_base + "\\cache\\.purge-cdn-dupes", "1");
            }
        }

        void JoinCore()   { if (s_coreWorker.joinable())   s_coreWorker.join(); }
        void JoinAssets() { if (s_assetsWorker.joinable()) s_assetsWorker.join(); }
    }

    void Init(const std::string& base)
    {
        s_base = base;
        s_stop.store(false);
        Api::Http::Resume(Api::Http::Scope::Bootstrap);

        // Resolve the data source. Default = the GitHub release; overridden by `<addon>\bootstrap_url.txt` (an
        // http(s) prefix OR a local dir) so the download path can be tested with no repo. Be forgiving of a
        // hand-edited file: strip a UTF-8 BOM (Notepad's default save -- else it defeats the http:// detection) and
        // surrounding whitespace/newlines.
        std::string ov = ReadFile(base + "\\bootstrap_url.txt");
        if (ov.rfind("\xEF\xBB\xBF", 0) == 0) ov.erase(0, 3);
        { size_t a = ov.find_first_not_of(" \t\r\n"); size_t b = ov.find_last_not_of(" \t\r\n");
          ov = (a == std::string::npos) ? std::string() : ov.substr(a, b - a + 1); }
        s_baseUrl = ov.empty() ? kGithubBaseUrl : ov;
        if (IsHttp(s_baseUrl))   // an http(s) base needs exactly one trailing '/'; tolerate a pasted trailing '\'
        {
            if (s_baseUrl.back() == '\\') s_baseUrl.back() = '/';
            if (s_baseUrl.back() != '/')  s_baseUrl += '/';
        }
        s_sourceOverridden.store(!ov.empty());
        if (APIDefs && !ov.empty())
            APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("DataBootstrap: data source overridden -> " + s_baseUrl).c_str());

        const std::string mk = ReadFile(Marker(".coreversion"));
        if (mk == TC_DATA_VERSION) { s_coreAdopted.store(true); SetPhase(Phase::Ready); return; }   // returning user: core already current
        if (mk.empty() && SentinelPresent())                            // existing install: adopt data in place
        { WriteAtomic(Marker(".coreversion"), TC_DATA_VERSION); s_coreAdopted.store(true); SetPhase(Phase::Ready); return; }
        JoinCore();                                                     // fresh install OR version bump -> download
        s_coreWorker = std::thread(CoreWorkerFn);
    }

    void StartAssets()
    {
        if (s_assetsStarted.exchange(true)) return;   // once
        const std::string mk = ReadFile(Marker(".assetsversion"));
        if (mk == TC_DATA_VERSION && AllPacksPresent()) { s_assetsSettled.store(true); return; }   // all packs present + current
        if (mk.empty() && AllPacksPresent())                           // existing install: adopt packs in place
        { WriteAtomic(Marker(".assetsversion"), TC_DATA_VERSION); s_assetsSettled.store(true); return; }
        JoinAssets();                                                  // download missing/updated packs (worker adopts the rest)
        s_assetsWorker = std::thread(AssetsWorkerFn);
    }

    void RetryCore()
    {
        if (CurrentPhase() != Phase::CoreFailed) return;
        JoinCore();
        { std::lock_guard<std::mutex> lk(s_errMtx); s_error.clear(); }
        s_coreWorker = std::thread(CoreWorkerFn);
    }

    void Shutdown()
    {
        s_stop.store(true);
        Api::Http::Abort(Api::Http::Scope::Bootstrap);   // unblock our scope's in-flight GET at once (mirrors ImageCache::Shutdown)
        JoinCore();
        JoinAssets();
    }

    Phase       CurrentPhase() { return (Phase)s_phase.load(std::memory_order_acquire); }
    int         CorePct()      { return s_pct.load(std::memory_order_acquire); }
    std::string Error()        { std::lock_guard<std::mutex> lk(s_errMtx); return s_error; }
    const std::string& Base()  { return s_base; }

    std::string SourceUrl()        { return s_baseUrl; }
    bool        SourceOverridden() { return s_sourceOverridden.load(); }
    bool        CoreAdopted()      { return s_coreAdopted.load(); }
    bool        AssetsActive()     { return s_assetsActive.load(); }
    bool        AssetsSettled()    { return s_assetsSettled.load(); }
    int         AssetsFetched()    { return s_assetsFetched.load(); }
    int         AssetsNeeded()     { return s_assetsNeeded.load(); }
    int         AssetsFailed()     { return s_assetsFailed.load(); }
    std::string AssetsCurrent()
    {
        const int i = s_assetsCurIdx.load();
        return (i >= 0 && i < ImageCache::kPackCount) ? ImageCache::PackFileName((ImageCache::Pack)i) : std::string();
    }
}
