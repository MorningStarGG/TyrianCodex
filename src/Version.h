#pragma once
// Single source of the addon version. Bump one of the four numbers below to cut a new release; that one
// edit drives three things, with no other file to touch:
//   1. The version Nexus shows  -- GetAddonDef() (entry.cpp) builds AddonVersion_t from these macros.
//   2. A one-time cache re-seed  -- on the user's next launch the runtime (ImageCache::Init) compares
//      TC_VERSION_STRING to a marker it stored last run; if they differ it wipes cache/tiles so every
//      map tile re-seeds from the (possibly refreshed) bundled data/textures/tiles.
//   3. A build-time bundle check -- because src/Version.h changed, cmake re-runs cmake/refresh_bundle.cmake,
//      which (only on a machine that has the private builder/) re-pulls any changed tiles into the bundle.
//      A public checkout / CI has no builder/, so that step is a clean no-op and the committed data/ is used.
//
// Keep the format MAJOR.MINOR.BUILD.REVISION (four ints) -- cmake parses these lines with a regex.
#define TC_VER_MAJOR    1
#define TC_VER_MINOR    1
#define TC_VER_BUILD    0
#define TC_VER_REVISION 5

// "0.1.0.0" -- used as the cache-version marker (ImageCache).
#define TC_STRINGIZE2(x) #x
#define TC_STRINGIZE(x)  TC_STRINGIZE2(x)
#define TC_VERSION_STRING                                              \
    TC_STRINGIZE(TC_VER_MAJOR) "." TC_STRINGIZE(TC_VER_MINOR) "."   \
    TC_STRINGIZE(TC_VER_BUILD) "." TC_STRINGIZE(TC_VER_REVISION)

// The shipped DATA-package version -- drives the self-download bootstrap (DataBootstrap). Nexus's updater ships
// only the DLL; the ~600MB data/ folder is fetched from GitHub's "latest" release (DataBootstrap.cpp always
// downloads from releases/latest/download/, not a tag built from this string -- CI packages full data into every
// v* release, so latest always has it). Bump THIS value ONLY when the shipped data payload (tc-core.tcpk or any
// *.pack) changes -- it is INDEPENDENT of TC_VER_* so a DLL-only patch never forces a re-download: it is written
// to data/.coreversion + data/.assetsversion, and a mismatch against that on-disk marker is what triggers the
// redownload, not the release tag name.
#define TC_DATA_VERSION "1.0.3"
