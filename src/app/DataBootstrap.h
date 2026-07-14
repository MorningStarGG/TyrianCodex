#pragma once
#include <string>

class App;

// -----------------------------------------------------------------------------------------------------
// DataBootstrap: the addon fetches its own ~600 MB data/ folder at runtime, because Nexus's updater ships
// ONLY the DLL. Two tiers, keyed by src/Version.h TC_DATA_VERSION (a GitHub Release tagged v<TC_DATA_VERSION>):
//   * CORE (blocking) -- one tc-core.tcpk archive of all non-pack data (datasets + fonts + loose UI/marker art).
//     The addon shows a "Downloading data..." panel and defers ALL its data loads until core is present.
//   * ASSETS (background) -- the image .pack files; downloaded after startup. A freshly-downloaded (absent) pack
//     is opened LIVE this session; a stale-but-already-mmap'd pack is staged + applied at the next AddonLoad.
// See the plan + [[owned-textures]] (packs) + ImageCache::OpenPack / TexPack (thread-safe live Open).
// -----------------------------------------------------------------------------------------------------

// Run every data-dependent load (datasets, fonts, catalogs, ...) once core is present. Defined in entry.cpp
// (needs the file-local Load*/catalog symbols); called from Render on the render thread at the core-ready
// transition (fonts + several Inits require the render/main thread).
void LoadAllData(App& app, const std::string& base);

namespace DataBootstrap
{
    enum class Phase { CheckCore, DownloadingCore, Ready, CoreFailed };

    void Init(const std::string& base);   // AddonLoad: Ready immediately if core is current, else start the core worker
    void StartAssets();                   // kick the background assets download (call from Render AFTER LoadAllData ran)
    void RetryCore();                     // user retry from the panel after CoreFailed
    void Shutdown();                      // AddonUnload (BEFORE api/Http shutdown): stop + Abort + join both workers

    Phase       CurrentPhase();           // atomic snapshot for the render thread
    int         CorePct();                // core extract progress 0..100 (download stage is indeterminate)
    std::string Error();                  // last error text (for the panel)
    const std::string& Base();            // the resolved addon directory

    // Diagnostics (Diagnostics tab). SourceUrl is the resolved base (GitHub release, or a bootstrap_url.txt
    // override); the Assets* accessors report the BACKGROUND pack download so progress is visible.
    std::string SourceUrl();
    bool        SourceOverridden();       // bootstrap_url.txt overrode the default GitHub source
    bool        CoreAdopted();            // core became Ready by adopting existing data (no download)
    bool        AssetsActive();           // a background pack download is currently running
    bool        AssetsSettled();          // the assets tier is done for this run (adopted, or all packs handled)
    int         AssetsFetched();          // packs downloaded/staged so far this run
    int         AssetsNeeded();           // packs that needed downloading this run (0 = all present/adopted)
    int         AssetsFailed();           // packs that failed this run
    std::string AssetsCurrent();          // filename of the pack currently downloading ("" if none)
}
