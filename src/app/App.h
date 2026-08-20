#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "model/Dataset.h"
#include "model/EventAreas.h"
#include "model/EventTimers.h"
#include "model/LoadingScreens.h"
#include "guide/Progress.h"
#include "app/SessionHistory.h"
#include "guide/Travel.h"
#include "guide/TrailFollower.h"
#include "guide/Story.h"
#include "api/Client.h"
#include "app/wiki/WikiService.h"
#include "app/FishData.h"
#include "app/ContentData.h"
#include "app/DecorationCatalog.h"
#include "ui/Gw2Ui.h"
#include "world/TrailRenderer.h"
#include "world/GpsArrow.h"
#include "util/DpiWatcher.h"
#include "world/MapRenderer.h"
#include "guide/CharacterLevel.h"
#include "guide/StoryCompletion.h"
#include "guide/Guidance.h"
#include "guide/ZoneManager.h"
#include "ui/zonedisplay/ZoneDisplay.h"
#include <imgui.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// App: the single owned application object. Heap-allocated in AddonLoad (`g_app = new App()`) and deleted
// in AddonUnload, so its construction/destruction ties to the Nexus Load/Unload lifecycle (it never races
// the DLL unwind / font release). Owns ALL long-lived state: user config, cross-cutting runtime state, the
// bundled datasets, the long-lived controllers, fonts + dyes, and the settings-persistence bookkeeping.
//
// Model 2 (subsystems + Context): the extracted subsystems live on App (below) and take the NARROW slice they
// need by reference (e.g. TrailRenderer::Draw(const FrameCtx&, const Config&, const GuideState&)) -- never the
// whole App and never a global. entry.cpp keeps only the Nexus C entry points + the per-frame orchestration.
// -----------------------------------------------------------------------------------------------------
class App
{
public:
    Config config;    // all user-tunable settings (the settings table points into this)
    GuideState state; // cross-cutting runtime state

    // Bundled datasets (loaded once in AddonLoad), keyed by mapId
    std::map<uint32_t, Zone> zones;         // every bundled open-world zone dataset
    std::map<uint32_t, Instance> instances; // every bundled dungeon dataset
    std::map<uint32_t, Instance> farming;   // every bundled farming map (routes + gatherable nodes)
    std::map<uint32_t, FishMap> fishHoles;  // every bundled fishing map (in-world hole POIs + rects)
    EventTimerData eventTimers;             // generated offline timed-event and event-chain data
    EventAreaData eventAreas;               // generated offline static dynamic-event areas
    LoadingScreenData loadingScreens;       // generated offline wiki loading-screen header art
    std::vector<MapInfo> mapIndex;          // every real place (kind/region) for the Atlas all-maps browse + search
    FishData fish;                          // fishing-collection catalog + live caught status (Fishing tab)
    ContentData content;                    // Content tab: raids/dungeons structure + deterministic fractal/strike rotations
    DecorationCatalog decorations;          // Homestead collection catalogs (decorations/glyphs/cats/nodes; Items Homestead scope)

    // Long-lived controllers
    ProgressStore progress;             // per-character completed stepIds (progress.json)
    SessionHistoryStore sessionHistory; // per-character past-session earnings log (session-history.json)
    Travel::Controller travel;          // smart-travel state machine (Alt+click / "Travel here")
    Follow::TrailFollower follower;     // pure-pursuit follower over the (x,z) trail polyline
    Api::Client api;                    // GW2 Web API client (own worker thread; additive + non-blocking)
    Wiki::Service wiki;                 // Native GW2 Wiki client service/cache (worker-threaded, no render-thread HTTP)
    StoryData stories;                  // story spine (releases + completion achievement ids)
    StoryProgressStore storyStore;      // manual story-completion ticks

    // Fonts (Menomonia, loaded async in AddonLoad) + GW2 dye palette (/v2/colors) for ColorBox.
    // LADDER: the same TTF baked at several native px so the text core draws each run from the nearest size at
    // scale ~1.0 (crisp -- ImGui v1.80 has no dynamic rasterizer; one bake stretched = the "wavy" aliasing).
    // Faces filled async by OnFontLoaded (px preset here); the 24px rung MUST stay present (the default-size +
    // Zone-Display fallback anchor). menoFont/menoItalic alias the 24px rung for back-compat.
    struct MenoBake
    {
        float px = 0.f;
        ImFont *regular = nullptr;
        ImFont *italic = nullptr;
    };
    // 12..32 by 2. Every size Gw2Ui can ask for SNAPS to one of these (see Gw2UiInternal::SnapPx), so the
    // ladder must span the whole range the UI uses: 12 is the floor for shrunk-to-fit text, and 32 is the
    // largest entry in the kFontSizePx dropdown (without it, the biggest text setting magnified the 30 bake).
    static constexpr int kMenoLadderN = 11;
    MenoBake menoLadder[kMenoLadderN] = {{12.f}, {14.f}, {16.f}, {18.f}, {20.f}, {22.f},
                                         {24.f}, {26.f}, {28.f}, {30.f}, {32.f}};
    ImFont *menoFont = nullptr;        // == the 24px rung regular (back-compat + Zone-Display fallback denominator)
    ImFont *menoItalic = nullptr;      // == the 24px rung italic (Story-tab sub-headers)
    ImFont *zdFont = nullptr;          // Menomonia baked large (96px) for the Zone Display banner (crisp when scaled big)
    ImFont *zdKrytanFont = nullptr;    // New Krytan baked large (96px) for the Zone Display "Decode" entrance (Krytan->Latin reveal)
    std::vector<std::string> dyeNames; // backs the Dye.name pointers; keep alive alongside dyes
    std::vector<Gw2Ui::Dye> dyes;

    // In-world renderers (each owns its internal / per-frame diagnostic state)
    TrailRenderer trailRenderer;
    GpsArrow gpsArrow;
    DpiWatcher dpiWatcher;   // event-driven GW2/Nexus DPI flags; the map reads State() to correct its scale
    MapRenderer mapRenderer; // map/minimap trail + pins; owns the published MapProj for click placement

    // Guide subsystems (own their API-driven caches)
    CharacterLevel characterLevel;
    StoryCompletion storyCompletion; // Init'd in AddonLoad with refs to stories/storyStore/api/state
    Guidance guidance;               // open-world routing brain + arrow driver (owns the ETA speed smoothing)
    ZoneManager zoneManager;         // active-route lifecycle (zone/dungeon switch, path swap, recommend); Init'd in AddonLoad
    ZoneDisplay zoneDisplay;         // the animated Region/Zone/Sector entry banner (offline names via SectorData)

    // Settings persistence (auto-saved on each committed change + on unload)
    std::string settingsPath;
    bool settingsDirty = false;
};

// The single owned App instance lives as a file-static in entry.cpp (new'd in AddonLoad, delete'd in
// AddonUnload). It is NOT exposed here: every subsystem - logic AND UI - receives the state it needs as
// explicit refs (Model 2 / App& injection). Only entry.cpp's Nexus C callbacks (which can't take a
// parameter) reach the instance, through that private static.
