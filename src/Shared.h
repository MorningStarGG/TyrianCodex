#pragma once
// Keep <windows.h> from defining the min/max macros (they break std::min/std::max/std::clamp). CMake
// also passes -DNOMINMAX, but defining it here before the first <windows.h> include keeps IntelliSense
// (which doesn't see the CMake flag) and the build in sync.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include "Nexus.h"
#include "api/mumblelink/Mumble.h"   // the MumbleLink framework: structs + typed UIState/MapType/Mount/scale vocabulary

// -- Global addon state -------------------------------------------------------
extern AddonAPI_t*        APIDefs;
extern HMODULE            Self;
extern Mumble::LinkedMem* MumbleLink;
extern Mumble::Identity*  MumbleIdent;
extern NexusLinkData_t*   NexusLink;    // screen size + IsGameplay (hide signal)

// -- Addon identity (ONE source) ----------------------------------------------
// The <GW2>/addons/<this> folder name. settings.json, cache/, and the bundled data/ all live under it, and
// MULTIPLE modules resolve it independently (Textures, ImageCache, favorites, notes, entry) -- so renaming the
// addon means editing ONLY this line. (The DLL filename is CMakeLists OUTPUT_NAME; the Nexus display name is
// GetAddonDef. Nexus CREATES this folder on first Paths_GetAddonDirectory call, so a stale name silently makes
// a second, empty folder -- which is why this must stay centralized.)
inline constexpr const char* kAddonDirName = "TyrianCodex";

// Resolved absolute <GW2>/addons/TyrianCodex directory (Nexus creates it), or nullptr if the API isn't up yet.
inline const char* AddonDir() { return APIDefs ? APIDefs->Paths_GetAddonDirectory(kAddonDirName) : nullptr; }

// -- Live-link convenience helpers --------------------------------------------
// These read the global pointers above; the typed vocabulary (flags / enums / scale) lives in
// api/mumblelink/Mumble.h. Together they are the addon's Gw2Mumble-style surface (replace raw `& 0xNN` / hand-rolled
// UISize ladders with these). All are null-safe (return a sensible default when the link isn't available).

// In a playable map?  (MapId == 0 on character select / loading.)
inline bool IsInGame()
{
    return MumbleLink && MumbleLink->Context.MapId != 0;
}

// True while the game world is actively TICKING -- i.e. NOT on a loading / character-select / cutscene screen.
// During those the game stops writing MumbleLink, so UITick FREEZES while MapId / UIState stay STALE: a teleport
// in particular leaves the destination map id and a possibly-open map flag set, which is why a map overlay can
// bleed through the loading screen. We detect the freeze via UITick freshness (a short grace window absorbs the
// odd frame hitch without flicker). Self-updating; call once per frame from the render path. Null/idle -> false.
inline bool GameIsLive()
{
    if (!MumbleLink) return false;
    static uint32_t s_lastTick   = 0;
    static double   s_lastChange = 0.0;
    const uint32_t  tick = MumbleLink->UITick;
    const double    now  = (double)GetTickCount64() / 1000.0;
    if (tick != s_lastTick) { s_lastTick = tick; s_lastChange = now; }
    return (now - s_lastChange) < 0.1;   // ticked within the last 0.1s -> live
}

// Current map id, or 0 if not in game.
inline uint32_t CurrentMapId()
{
    return MumbleLink ? MumbleLink->Context.MapId : 0u;
}

// Current map TYPE (Public / Instance / Pvp / ...), or Redirect(0) if unavailable.
inline uint32_t CurrentMapType()
{
    return MumbleLink ? MumbleLink->Context.MapType : 0u;
}

// True when in any WvW match map (Eternal Battlegrounds, the borderlands, Obsidian Sanctum, Edge of the Mists).
// Excludes the WvW Lounge (not a match map). Gates + speeds up the WvW match poll (AccountData::TickWvw).
inline bool IsInWvW()
{
    const uint32_t t = CurrentMapType();
    return t >= (uint32_t)Mumble::MapType::WvwEternalBattlegrounds && t <= (uint32_t)Mumble::MapType::WvwEdgeOfTheMists;
}

// --- UIState flags (full set; see Mumble::UiState) ---
inline bool UiFlag(uint32_t bit)            { return MumbleLink && Mumble::UiState::Has(MumbleLink->Context.UIState, bit); }
inline bool IsMapOpen()                     { return UiFlag(Mumble::UiState::MapOpen); }
inline bool IsCompassTopRight()             { return UiFlag(Mumble::UiState::CompassTopRight); }
inline bool IsCompassRotationEnabled()      { return UiFlag(Mumble::UiState::CompassRotation); }
inline bool GameHasFocus()                  { return UiFlag(Mumble::UiState::GameHasFocus); }
inline bool IsInCompetitiveMode()           { return UiFlag(Mumble::UiState::CompetitiveMode); }
inline bool TextboxHasFocus()               { return UiFlag(Mumble::UiState::TextboxFocus); }
inline bool IsInCombat()                    { return UiFlag(Mumble::UiState::InCombat); }

// Interface-size scale factor for the live link (Small .900 / Normal 1 / Large 1.111 / Larger 1.222).
// Centralizes the ladder that the map/minimap projection used to inline in two places.
inline float UiScaleNow()
{
    return MumbleIdent ? Mumble::UiScale(MumbleIdent->UISize) : 1.000f;
}

// Active mount (None when dismounted / unavailable).
inline Mumble::MountType CurrentMount()
{
    return MumbleLink ? Mumble::ToMount(MumbleLink->Context.MountIndex) : Mumble::MountType::None;
}
