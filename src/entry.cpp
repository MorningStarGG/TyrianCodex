#include "Shared.h"
#include "Version.h"
#include "model/Dataset.h"
#include "util/Projection.h"
#include "util/Coords.h"
#include "util/Textures.h"
#include "util/ImageCache.h"
#include "util/OwnedTex.h"       // addon-owned D3D11 textures: per-frame LRU tick + release-all on unload
#include "app/DataBootstrap.h"   // self-download the bundled data/ folder (Nexus ships only the DLL)
#include "resources/resources.h" // DLL-embedded tray-icon resource ids (present before the data download)
#include "model/ObjectiveTypes.h"
#include "guide/TrailFollower.h"
#include "guide/Progress.h"
#include "guide/Travel.h"
#include "guide/Story.h"
#include "ui/Gw2Ui.h"
#include "ui/Effect.h"
#include "util/Dyes.h"
#include "api/Client.h"
#include "app/App.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "world/Markers.h"
#include "world/CategoryMarkers.h"
#include "world/FishMarkers.h"
#include "guide/FarmingGuidance.h"
#include "guide/FishingGuidance.h"
#include "guide/FishHoles.h"
#include "world/PersonalMarker.h"
#include "guide/RouteMode.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/InstanceGuidance.h"
#include "guide/StepStatus.h"
#include "guide/VistaWatch.h"
#include "ui/SettingsWindow.h"
#include "ui/welcome/WelcomeWindow.h"
#include "ui/GuideViewer.h"
#include "ui/TrayMenu.h"
#include "ui/QuickMenu.h"           // QuickMenu::ArrowDispatch + CurrentTargetOverride for the shared arrow menu
#include "ui/viewer/ViewerLayout.h" // ViewerStyle* (tray icon panel toggle)
#include "ui/UiCommon.h"
#include "ui/dashboard/Notify.h"
#include "ui/dashboard/Toast.h"
#include "ui/dashboard/Dashboard.h"
#include "app/AccountData.h"
#include "app/StaticData.h"
#include "app/SessionTracker.h" // per-frame Session Tracker lifecycle (wallet/currency earnings)
#include "app/AccountLevels.h"  // account-wide lifetime level total (seeded from the character roster)
#include "app/EventFavorites.h" // account-wide event favorites + reminder flags
#include "app/EventWatch.h"     // staged "starts in N" reminders for belled events
#include "app/CharRegistry.h"   // name->created-id registry (rename detector for per-character data)
#include "app/SectorData.h"
#include "app/ItemCatalog.h"
#include "app/SkinCatalog.h"
#include "app/CosmeticCatalog.h"
#include "app/CraftingEngine.h"
#include "app/CraftKnown.h"
#include "app/CraftCart.h"
#include "app/MarketData.h"
#include "app/PriceHistory.h"
#include "app/TpEligibility.h"
#include "app/TpPrices.h"
#include "app/AchievementPoints.h"
#include "ui/tabs/ItemsTab.h"       // ShutdownItemsTab (free the item parsed-meta cache on unload)
#include "ui/tabs/CollectionsTab.h" // CollectionsNeededDomains (Wardrobe/Homestead account-data interest)
#include "ui/tabs/TradingPostTab.h" // TradingPostNeededDomains (Crafting view owned-materials interest)
#include "ui/tabs/DungeonsTab.h"    // ContentNeededDomains (Content-tab account-data interest)
#include "ui/hud/Hud.h"
#include "ui/infopanel/InfoPanel.h"
#include "ui/profiles/ConfigProfiles.h"
#include "ui/profiles/Loadouts.h"
#include "ui/wiki/WikiQuoteFont.h"
#include "ui/wiki/WikiReader.h"

#include <imgui.h>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cfloat>
#include <climits>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

// -- Global addon state (declared extern in Shared.h) -------------------------
AddonAPI_t *APIDefs = nullptr;
HMODULE Self = nullptr;
Mumble::LinkedMem *MumbleLink = nullptr;
Mumble::Identity *MumbleIdent = nullptr;
NexusLinkData_t *NexusLink = nullptr;

// The single owned application object: ALL long-lived state (config, runtime state, datasets, controllers,
// fonts/dyes) lives on it. Heap-allocated in AddonLoad, deleted in AddonUnload (so member ctors/dtors tie to
// the Nexus Load/Unload lifecycle). The file-static helpers below read/write it through this pointer.
static App *g_app = nullptr; // private to this TU: only the Nexus C callbacks below reach it; new'd in AddonLoad, delete'd in AddonUnload

// Nexus FONTS_RECEIVECALLBACK: the atlas finished baking our TTF; store the face + (re)apply the choice.
static void OnFontLoaded(const char *id, void *font)
{
    if (!id || !g_app)
        return;
    // Zone Display banner faces (96px) are SEPARATE from the UI ladder (intentionally scaled big; own math).
    if (std::strcmp(id, "TC_ZD_BANNER") == 0)
    {
        g_app->zdFont = (ImFont *)font;
        return;
    }
    else if (std::strcmp(id, "TC_ZD_KRYTAN") == 0)
    {
        g_app->zdKrytanFont = (ImFont *)font;
        return;
    }
    // UI ladder rung "TC_MENO_<px>" / "TC_MENO_IT_<px>". font may be NULL: a Nexus atlas rebuild (any addon
    // adding/resizing a font, or a UI-Size change) clears the atlas and fires the callback with null pre-build
    // then the new pointer post-build. Store whatever we get (the picker tolerates null/partial rungs), then
    // rebuild the Gw2Ui ladder from whatever has loaded so far.
    for (auto &m : g_app->menoLadder)
    {
        char idR[24], idI[24];
        std::snprintf(idR, sizeof idR, "TC_MENO_%d", (int)m.px);
        std::snprintf(idI, sizeof idI, "TC_MENO_IT_%d", (int)m.px);
        if (std::strcmp(id, idR) == 0)
        {
            m.regular = (ImFont *)font;
            if ((int)(m.px + 0.5f) == 24)
                g_app->menoFont = (ImFont *)font;
            break;
        }
        else if (std::strcmp(id, idI) == 0)
        {
            m.italic = (ImFont *)font;
            if ((int)(m.px + 0.5f) == 24)
                g_app->menoItalic = (ImFont *)font;
            break;
        }
    }
    ApplyFontChoice(*g_app);
}
// GW2 API key (Account / API section). Plaintext in settings.json (Nexus has no subtoken vault); applied to
// g_app->api on commit. g_apiKeyApplied tracks the last value pushed so we re-resolve scopes only when it changes.
static std::string g_apiKeyApplied;
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason, LPVOID /*reserved*/)
{
    if (ul_reason == DLL_PROCESS_ATTACH)
    {
        Self = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
// (The arrow right-click menu is now the shared QuickMenu -- every arrow surface builds QuickMenu::ArrowNodes and
//  the picked id is run by QuickMenu::ArrowDispatch; see the arrowDispatch lambda in Render.)

// -- The guide viewer: a GW2-styled floating panel with
// the zone header, the current objective (icon + name + distance/ETA + detail), the remaining-objective
// checklist (tick to complete / click to target, rows on RowBackground), and -- off-route / off-coverage
// -- a "next up" recommendation bar. Honors viewerLocked / panelOpacity / viewerFontSize / boldText /
// showUpcoming. Completion is the persistent ProgressStore. --

static bool HandleLocalKeybind(UINT uMsg, WPARAM wParam, LPARAM lParam);

static void DoToggleSettings()
{
    if (!g_app)
        return;
    g_app->state.showSettings = !g_app->state.showSettings;
}

static void DoToggleGuide()
{
    if (!g_app)
        return;
    g_app->config.showGuide = !g_app->config.showGuide;
    g_app->settingsDirty = true;
}

static void DoTogglePanel()
{
    if (!g_app)
        return;
    Config &c = g_app->config;
    if (c.viewerStyle == ViewerStyleNone)
        c.viewerStyle = (c.viewerStylePrev == ViewerStyleNone) ? ViewerStyleCompact : c.viewerStylePrev;
    else
    {
        c.viewerStylePrev = c.viewerStyle;
        c.viewerStyle = ViewerStyleNone;
    }
    g_app->settingsDirty = true;
}

static void DoMarkDone()
{
    if (!g_app || !g_app->state.zone.Loaded || g_app->state.curStep < 0 || g_app->state.curStep >= (int)g_app->state.zone.Steps.size())
        return;
    CompleteStep(g_app->progress, g_app->state, g_app->state.zone.Steps[g_app->state.curStep].StepId);
    g_app->state.manualTarget.clear();
}

static void DoBack()
{
    if (!g_app || g_app->state.undoStack.empty())
        return;
    g_app->progress.SetComplete(g_app->state.currentChar, g_app->state.undoStack.back(), false);
    g_app->state.undoStack.pop_back();
}

static void DoCopyWaypoint()
{
    if (g_app)
        CopyCurrentWaypoint(*g_app);
}

static void DoLoadoutCycle()
{
    if (g_app)
        Loadouts::CycleNext(*g_app);
}

static void DoLoadout(int index)
{
    if (g_app)
        Loadouts::ApplyIndex(*g_app, index);
}

static void DoZoneReannounce()
{
    if (g_app)
        g_app->zoneDisplay.Reannounce(*g_app);
}

static void DoToggleMarkers()
{
    if (!g_app)
        return;
    g_app->config.showMarkers = !g_app->config.showMarkers;
    g_app->settingsDirty = true;
}

static void DoToggleTrail()
{
    if (!g_app)
        return;
    g_app->config.showTrail = !g_app->config.showTrail;
    g_app->settingsDirty = true;
}

// Nexus WndProc hook: while one of OUR Gw2Ui windows is focused, CONSUME Escape so closing it doesn't also
// reach the game (Escape menu) or Nexus (closing its own window). Return 0 to consume, else the message to
// pass it on. Acts on the initial press only (skips auto-repeat) but consumes repeats too so no menu sneaks in.
static UINT OnWndProc(HWND /*hWnd*/, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (HandleLocalKeybind(uMsg, wParam, lParam))
        return 0;
    if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) && wParam == VK_ESCAPE && Gw2Ui::WindowEatsEscape())
    {
        if (!(lParam & (1 << 30)))
            Gw2Ui::RequestEscapeClose(); // initial press, not auto-repeat
        return 0;                        // consume -> no game menu, no Nexus close
    }
    return uMsg;
}

// Clicked chat-link travel (config.chatLinkTravel) + its R&D probe (config.mapCenterProbe). When the world map
// OPENS it always centres on the PLAYER, then auto-pans to a clicked waypoint chat link's location over ~1s.
// So: watch the centre after open; when it SETTLES far from where it opened it was an automatic pan. If WE
// didn't drive it (mapAssist) and the user wasn't dragging the map (left button not held), it's a deliberate
// chat-link click -> set a guide target at that spot + offer travel (never auto-confirm). The probe logs the
// open/settle (+ ours/drag flags) for tuning the thresholds.
static void UpdateChatLinkWatch()
{
    if (!g_app || !MumbleLink || !APIDefs)
        return;
    const bool probe = g_app->config.mapCenterProbe;
    const bool feat = g_app->config.chatLinkTravel;
    if (!probe && !feat)
        return;
    const auto &ctx = MumbleLink->Context;
    const bool open = IsMapOpen();

    static bool s_prevOpen = false, s_watching = false, s_ours = false, s_dragged = false;
    static int s_frames = 0, s_stable = 0, s_dragRun = 0;
    static float s_openCx = 0.f, s_openCy = 0.f, s_lastCx = 0.f, s_lastCy = 0.f;
    static double s_openT = 0.0;

    if (open && !s_prevOpen) // map-open edge
    {
        s_watching = true;
        s_frames = 0;
        s_stable = 0;
        s_dragRun = 0;
        s_dragged = false;
        s_ours = g_app->state.mapAssist.active; // OUR travel/map-assist drove this open
        s_openCx = s_lastCx = ctx.MapCenterX;
        s_openCy = s_lastCy = ctx.MapCenterY;
        s_openT = ImGui::GetTime();
        if (probe)
        {
            char b[200];
            std::snprintf(b, sizeof(b), "[mapprobe] OPEN    center=(%.0f,%.0f) player=(%.0f,%.0f) ours=%d",
                          ctx.MapCenterX, ctx.MapCenterY, ctx.PlayerX, ctx.PlayerY, (int)s_ours);
            APIDefs->Log(LOGL_INFO, "Tyrian Codex", b);
        }
    }
    else if (open && s_watching) // track the auto-pan, who drove it, and whether it settled
    {
        ++s_frames;
        if (g_app->state.mapAssist.active)
            s_ours = true; // our pan (may start after open)
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        {
            if (++s_dragRun >= 10)
                s_dragged = true;
        }
        else
            s_dragRun = 0; // sustained hold = manual drag (not a brief click)
        const bool moved = std::hypot(ctx.MapCenterX - s_lastCx, ctx.MapCenterY - s_lastCy) >= 1.f;
        s_stable = moved ? 0 : (s_stable + 1);
        s_lastCx = ctx.MapCenterX;
        s_lastCy = ctx.MapCenterY;
        if (s_stable >= 6 || s_frames > 180) // ~6 stable frames = settled (give up after ~3s)
        {
            s_watching = false;
            const float panned = std::hypot(ctx.MapCenterX - s_openCx, ctx.MapCenterY - s_openCy);
            if (probe)
            {
                char b[224];
                std::snprintf(b, sizeof(b), "[mapprobe] SETTLED center=(%.0f,%.0f) panned=%.0f ours=%d drag=%d +%.0fms (%d f)",
                              ctx.MapCenterX, ctx.MapCenterY, panned, (int)s_ours, (int)s_dragged,
                              (ImGui::GetTime() - s_openT) * 1000.0, s_frames);
                APIDefs->Log(LOGL_INFO, "Tyrian Codex", b);
            }
            if (feat && panned > 300.f && !s_ours && !s_dragged) // automatic pan we didn't initiate = a clicked chat link
                OpenChatWaypointTravel(*g_app, ctx.MapCenterX, ctx.MapCenterY);
        }
    }
    s_prevOpen = open;
}

// -- Guide action keybinds (registered with Nexus InputBinds in AddonLoad). Fire on release. --
// Toggle on PRESS, not release: the tray icon's left-click goes through Nexus InputBindApi::Invoke(id),
// which fires the handler once with aIsRelease=false (a press). Acting only on release made the tray
// click a no-op. A keyboard bind still sends press then release, so this toggles once (release ignored).
static void OnToggleSettings(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoToggleSettings();
}

static void OnToggleGuide(const char *, bool aRelease)
{
    if (g_app && !aRelease)
        DoToggleGuide();
}
// The tray icon's left-click: show/hide just the GUIDE PANEL (viewerStyle None <-> last style) -- non-destructive
// (the arrow / trail / markers / map pins keep running). Remembers the last non-None style across reloads.
static void OnTogglePanel(const char *, bool aRelease)
{
    if (g_app && !aRelease)
        DoTogglePanel();
}
static void OnMarkDone(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoMarkDone();
}
static void OnBack(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoBack();
}
static void OnCopyWaypoint(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoCopyWaypoint();
}
// Loadout switches are DEFERRED (queued for the next frame's Loadouts::ProcessPending) -- a SetActive must not
// mutate a family mid-render. Fire on release like the other guide binds.
static void OnLoadoutCycle(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoLoadoutCycle();
}
static void OnLoadout1(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoLoadout(0);
}
static void OnLoadout2(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoLoadout(1);
}
static void OnLoadout3(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoLoadout(2);
}
static void OnZoneReannounce(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoZoneReannounce();
}
static void OnToggleMarkers(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoToggleMarkers();
}
static void OnToggleTrail(const char *, bool aRelease)
{
    if (g_app && aRelease)
        DoToggleTrail();
}

static bool IsExplicitlyUnbound(const char *combo)
{
    return combo && std::strcmp(combo, "(null)") == 0;
}

static bool IsUntouchedBind(const char *combo)
{
    return !combo || !combo[0] || std::strcmp(combo, "Not set") == 0;
}

static bool IsLocalBindActive(const char *combo)
{
    return combo && combo[0] && !IsUntouchedBind(combo) && !IsExplicitlyUnbound(combo);
}

static bool EntryKeyName(int vk, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    {
        std::snprintf(out, outSize, "%c", (char)vk);
        return true;
    }
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        std::snprintf(out, outSize, "F%d", vk - VK_F1 + 1);
        return true;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        std::snprintf(out, outSize, "NUM %d", vk - VK_NUMPAD0);
        return true;
    }

    const char *name = nullptr;
    switch (vk)
    {
    case VK_SPACE: name = "SPACE"; break;
    case VK_RETURN: name = "ENTER"; break;
    case VK_TAB: name = "TAB"; break;
    case VK_BACK: name = "BACKSPACE"; break;
    case VK_INSERT: name = "INSERT"; break;
    case VK_DELETE: name = "DELETE"; break;
    case VK_HOME: name = "HOME"; break;
    case VK_END: name = "END"; break;
    case VK_PRIOR: name = "PAGEUP"; break;
    case VK_NEXT: name = "PAGEDOWN"; break;
    case VK_UP: name = "UP"; break;
    case VK_DOWN: name = "DOWN"; break;
    case VK_LEFT: name = "LEFT"; break;
    case VK_RIGHT: name = "RIGHT"; break;
    case VK_ADD: name = "NUM +"; break;
    case VK_SUBTRACT: name = "NUM -"; break;
    case VK_MULTIPLY: name = "NUM *"; break;
    case VK_DIVIDE: name = "NUM /"; break;
    case VK_DECIMAL: name = "NUM ."; break;
    case VK_OEM_1: name = ";"; break;
    case VK_OEM_PLUS: name = "="; break;
    case VK_OEM_COMMA: name = ","; break;
    case VK_OEM_MINUS: name = "-"; break;
    case VK_OEM_PERIOD: name = "."; break;
    case VK_OEM_2: name = "/"; break;
    case VK_OEM_3: name = "`"; break;
    case VK_OEM_4: name = "["; break;
    case VK_OEM_5: name = "\\"; break;
    case VK_OEM_6: name = "]"; break;
    case VK_OEM_7: name = "'"; break;
    default: break;
    }
    if (name)
    {
        std::snprintf(out, outSize, "%s", name);
        return true;
    }

    char fallback[32] = {};
    const UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if (sc == 0 || GetKeyNameTextA((LONG)(sc << 16), fallback, sizeof(fallback)) <= 0)
        return false;
    for (char &c : fallback)
        c = (char)std::toupper((unsigned char)c);
    std::snprintf(out, outSize, "%s", fallback);
    return out[0] != '\0';
}

static bool EntryIsModifierOrMouse(int vk)
{
    switch (vk)
    {
    case VK_CONTROL:
    case VK_SHIFT:
    case VK_MENU:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    default:
        return false;
    }
}

static bool EventComboString(WPARAM wParam, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';
    const int vk = (int)wParam;
    if (vk == VK_ESCAPE || EntryIsModifierOrMouse(vk))
        return false;

    char keyName[32] = {};
    if (!EntryKeyName(vk, keyName, sizeof(keyName)))
        return false;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    std::snprintf(out, outSize, "%s%s%s%s",
                  ctrl ? "CTRL+" : "", shift ? "SHIFT+" : "", alt ? "ALT+" : "", keyName);
    return out[0] != '\0';
}

static bool ComboEquals(const char *a, const char *b)
{
    return a && b && _stricmp(a, b) == 0;
}

static bool HandleLocalKeybind(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (!g_app || (uMsg != WM_KEYDOWN && uMsg != WM_SYSKEYDOWN) || (lParam & (1 << 30)) || Gw2Ui::KeybindCaptureActive())
        return false;

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (!ctrl && !alt && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput)
        return false;

    char combo[64] = {};
    if (!EventComboString(wParam, combo, sizeof(combo)))
        return false;

    Config &c = g_app->config;
    struct LocalBind
    {
        const char *combo;
        void (*action)();
    };
    const LocalBind binds[] = {
        {c.keyToggleSettings, DoToggleSettings},
        {c.keyTogglePanel, DoTogglePanel},
        {c.keyToggleGuide, DoToggleGuide},
        {c.keyMarkDone, DoMarkDone},
        {c.keyBack, DoBack},
        {c.keyCopyWp, DoCopyWaypoint},
        {c.keyLoadoutCycle, DoLoadoutCycle},
        {c.keyLoadout1, []() { DoLoadout(0); }},
        {c.keyLoadout2, []() { DoLoadout(1); }},
        {c.keyLoadout3, []() { DoLoadout(2); }},
        {c.keyZoneReannounce, DoZoneReannounce},
        {c.keyToggleMarkers, DoToggleMarkers},
        {c.keyToggleTrail, DoToggleTrail},
    };

    for (const LocalBind &b : binds)
    {
        if (IsLocalBindActive(b.combo) && ComboEquals(b.combo, combo))
        {
            b.action();
            return true;
        }
    }
    return false;
}

// Nexus does not expose a read/write API for its saved user keybinds, only registration/default binding.
// User-configurable Tyrian Codex hotkeys are handled internally by OnWndProc. Do not register them with
// Nexus: registered InputBinds show up in Nexus settings and create a second, conflicting source of truth.
void ApplyKeybinds(App &app) // declared in app/Glue.h; retained as the lifecycle hook for the bridge/cleanup.
{
    (void)app;
    if (!APIDefs)
        return;
    const char *legacyIds[] = {
        "TC_TOGGLE_SETTINGS",
        "TC_TOGGLE_PANEL",
        "TC_TOGGLE_GUIDE",
        "TC_MARK_DONE",
        "TC_BACK",
        "TC_COPY_WP",
        "TC_LOADOUT_CYCLE",
        "TC_LOADOUT_1",
        "TC_LOADOUT_2",
        "TC_LOADOUT_3",
        "TC_ZONE_REANNOUNCE",
        "TC_TOGGLE_MARKERS",
        "TC_TOGGLE_TRAIL",
        "TC_TOGGLE_PANEL_QA",
    };
    if (APIDefs->InputBinds_Deregister)
        for (const char *id : legacyIds)
            APIDefs->InputBinds_Deregister(id);
    if (APIDefs->InputBinds_RegisterWithString)
        APIDefs->InputBinds_RegisterWithString("TC_TOGGLE_PANEL_QA", OnTogglePanel, "(null)");
}

// The active trail in CONTINENT coords, for the in-game map / minimap overlay (which work in continent
// coords, what MumbleLink's MapCenter uses). Rebuilt alongside the follower whenever the active route
// changes. Empty when the zone has no rects (then the map trail just doesn't draw).

// Per-frame dungeon guidance: auto-advance the instruction by proximity, aim the arrow along the trail to
// the current instruction spot (or the route end), and draw the arrow. Mirrors UpdateInstanceGuidance.

// (UIState flags IsMapOpen / IsCompassTopRight / IsCompassRotationEnabled and the UISize -> scale ladder
//  now live in the MumbleLink framework -- Shared.h live helpers + api/mumblelink/Mumble.h vocabulary.)

// Decide this frame's viewer mode -- the single source of truth read by the in-world guidance dispatch AND
// the panel. Order = priority: not-in-map / dungeon / travel / off-coverage / complete / step.
static ViewerMode ComputeViewerMode()
{
    if (!MumbleLink || !NexusLink || !NexusLink->IsGameplay || CurrentMapId() == 0)
        return ViewerMode::Message;
    UpdateCurrentChar(g_app->state);
    if (g_app->state.inDungeon && g_app->state.inst)
        return ViewerMode::Dungeon;
    if (g_app->travel.Active())
        return ViewerMode::Travel;
    if (!g_app->state.zone.Loaded)
        return ViewerMode::Recommend;
    if (!g_app->state.zone.Steps.empty())
    {
        // Memoize the O(N) all-done scan: it only changes when progress mutates, the character switches,
        // or the zone changes (map id is the zone token -- Steps are fixed per map). Recompute on the
        // change token, return the cached verdict every frame. (Performance: no per-frame step scan.)
        static bool s_allDone = false;
        static uint64_t s_progVer = (uint64_t)-1;
        static std::string s_char;
        static uint32_t s_map = 0;
        const uint64_t pv = g_app->progress.Version();
        const uint32_t mid = CurrentMapId();
        if (pv != s_progVer || g_app->state.currentChar != s_char || mid != s_map)
        {
            s_progVer = pv;
            s_char = g_app->state.currentChar;
            s_map = mid;
            s_allDone = true;
            for (const Step &s : g_app->state.zone.Steps)
                if (!StepIsDone(g_app->progress, g_app->state.currentChar, s))
                {
                    s_allDone = false;
                    break;
                }
        }
        if (s_allDone)
            return ViewerMode::Complete;
    }
    return ViewerMode::Step;
}

// Toast when the Trading Post delivery box GAINS coins/items (a sale completed or a buy order filled). Diffs the
// AccountData TP snapshot across refreshes; baselines silently on the first sample so it fires only on NEW arrivals.
static void UpdateTpDeliveryToasts()
{
    if (!g_app || !g_app->config.tpToasts)
        return;
    const AccountData::Model &m = AccountData::Get();
    if (!m.haveTp)
        return;
    static double s_lastAt = -1.0;
    static long long s_prevCoins = -1;
    static int s_prevItems = -1;
    if (m.tpAt == s_lastAt)
        return; // no fresh TP refresh since last check
    s_lastAt = m.tpAt;
    if (s_prevCoins < 0)
    {
        s_prevCoins = m.tpCoins;
        s_prevItems = m.tpItems;
        return;
    } // first sample = baseline
    const bool gained = m.tpCoins > s_prevCoins || m.tpItems > s_prevItems;
    s_prevCoins = m.tpCoins;
    s_prevItems = m.tpItems;
    if (!gained)
        return;

    std::string body;
    if (m.tpCoins > 0)
    {
        const long long c = m.tpCoins, g = c / 10000, s = (c / 100) % 100, cp = c % 100;
        char b[48];
        if (g > 0)
            std::snprintf(b, sizeof(b), "%lldg %llds", g, s);
        else if (s > 0)
            std::snprintf(b, sizeof(b), "%llds %lldc", s, cp);
        else
            std::snprintf(b, sizeof(b), "%lldc", cp);
        body = b;
    }
    if (m.tpItems > 0)
    {
        if (!body.empty())
            body += " + ";
        body += std::to_string(m.tpItems) + (m.tpItems == 1 ? " item" : " items");
    }
    if (body.empty())
        return;
    body += " ready to collect";
    Notify::Push(Notify::Kind::Event, "Trading Post", body);
}

// Fresh-install / update progress panel: shown (ImGui default font -- Menomonia isn't loaded until core arrives)
// while DataBootstrap fetches the core data, before any data-dependent render runs.
static void DrawBootstrapPanel()
{
    const DataBootstrap::Phase ph = DataBootstrap::CurrentPhase();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (ImGui::Begin("##tc-bootstrap", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav))
    {
        ImGui::TextUnformatted("Tyrian Codex");
        ImGui::Separator();
        if (ph == DataBootstrap::Phase::CoreFailed)
        {
            const std::string err = DataBootstrap::Error();
            ImGui::TextUnformatted(("Data download failed: " + (err.empty() ? std::string("network error") : err)).c_str());
            ImGui::TextUnformatted("Retrying automatically. You can keep playing; the guide loads once it succeeds.");
            if (ImGui::Button("Retry now"))
                DataBootstrap::RetryCore();
        }
        else if (ph == DataBootstrap::Phase::DownloadingCore)
        {
            const int pct = DataBootstrap::CorePct();
            const float frac = pct > 0 ? (float)pct / 100.f : std::fmod((float)ImGui::GetTime() * 0.6f, 1.0f); // extract% else animated
            char ov[32];
            if (pct > 0)
                std::snprintf(ov, sizeof ov, "installing %d%%", pct);
            else
                std::snprintf(ov, sizeof ov, "downloading");
            ImGui::TextUnformatted("Downloading Tyrian Codex data (first-time setup)...");
            ImGui::ProgressBar(frac, ImVec2(380.f, 0.f), ov);
        }
        else
        {
            ImGui::TextUnformatted("Preparing...");
        }
    }
    ImGui::End();
}

static void Render()
{
    if (!g_app || !APIDefs)
        return;
    try
    {
        Gw2Ui::NewFrameEscReset();                                                                                // clear the per-frame "a Gw2Ui window is focused" flag; BeginWindow re-sets it
        Gw2Ui::SetGlobalScale(g_app->config.uiScale);                                                             // one addon-wide screen UI multiplier; local panel/text scales stack on top
        Gw2Ui::SetTooltipFontSize(kFontSizePx[std::clamp(g_app->config.tooltipFontSize, 0, kFontSizeCount - 1)]); // push the user's tooltip text size into the framework
        UpdateChatLinkWatch();                                                                                    // clicked-chat-link travel (default on) + the map-center probe (default off) -- inside the try so a chat/travel exception can't escape into GW2's render loop
        g_app->api.Pump();                      // deliver any completed API callbacks on this (render) thread before drawing
        g_app->wiki.Pump();                     // deliver wiki service completions on the render thread; network stays on its worker
        TpPrices::Pump();                       // batch-fetch any item prices the Items tab asked for last frame
        OwnedTex::Tick(ImGui::GetFrameCount()); // advance the addon-owned-texture frame + LRU-evict stale tile/icon VRAM

        // Data bootstrap gate: on a fresh install / update, EVERY data-dependent path below is held until the core
        // data download finishes. The pumps above keep the API/image/wiki workers alive during the download; here we
        // run LoadAllData ONCE on the render thread the frame core is Ready, then draw a progress panel until then.
        static bool s_dataLoaded = false;
        if (!s_dataLoaded)
        {
            if (DataBootstrap::CurrentPhase() == DataBootstrap::Phase::Ready)
            {
                LoadAllData(*g_app, DataBootstrap::Base());
                s_dataLoaded = true;
                DataBootstrap::StartAssets(); // now the addon is functional, background-download the image packs
            }
            else
            {
                // Auto-retry a failed core download (transient network) -- the panel promises it. RetryCore() no-ops
                // unless we're actually in CoreFailed, so this only fires while stuck.
                if (DataBootstrap::CurrentPhase() == DataBootstrap::Phase::CoreFailed)
                {
                    static double s_nextRetry = 0.0;
                    const double now = ImGui::GetTime();
                    if (now >= s_nextRetry)
                    {
                        DataBootstrap::RetryCore();
                        s_nextRetry = now + 30.0;
                    }
                }
                DrawBootstrapPanel();
                return;
            }
        }

        g_app->characterLevel.MaybeRefresh(g_app->api, g_app->state, CurrentCharName(), ImGui::GetIO().DeltaTime); // additive: keep state.charLevel current
        {                                                                                                          // The ONE session tracker (time + currency + levels + objectives), independent of dashboard visibility;
            // paused out of gameplay so loading screens don't fragment it. Backs the Sessions tab + Session Stats
            // widget + the session data texts -- a single baseline, no duplicate per-surface tracking.
            const bool seInGame = MumbleLink && NexusLink && NexusLink->IsGameplay && CurrentMapId() != 0;
            if (seInGame)
                UpdateCurrentChar(g_app->state); // keep state.currentChar fresh (formerly in UpdateSessionStats)
            SessionTracker::Update(*g_app, seInGame, seInGame ? CurrentCharName() : std::string());
        }
        EventWatch::Tick(*g_app); // event reminders; self-throttled, and NOT gated on the Timers tab being open
        // Apply queued loadouts/profile bindings before route selection and drawing. Route preferences live in the
        // General profile; doing this after SwitchZone can leave the active route on the previous profile.
        Loadouts::ProcessPending(*g_app);
        ConfigProfiles::TickAll(*g_app);

        // Warm every map/icon disk cache ONCE, the first frame we're actually in-game (MumbleLink + zones ready).
        if (!g_app->state.warmedCaches && MumbleLink && NexusLink && NexusLink->IsGameplay && !g_app->zones.empty())
        {
            WarmCaches(*g_app);
            AccountData::WarmAll();
            g_app->state.warmedCaches = true;
            // Once-per-session nudge: no key -> a clickable reminder (opens Options -> API; also clickable in the log).
            if (!g_app->api.HasKey())
                Notify::Push(Notify::Kind::Info, "No API key set",
                             "Add one to unlock level tips, your Story, Items & more.",
                             0, Notify::Action::OpenApiSettings);
        }

        // Pick the active dataset for the current map: a dungeon (spot-to-spot) if one is bundled, else the
        // open-world zone. SwitchInstance/SwitchZone each guard on g_app->state.activeMapId so this is cheap per frame.
        if (MumbleLink)
        {
            const uint32_t mid = CurrentMapId();
            if (g_app->instances.count(mid))
                g_app->zoneManager.SwitchInstance(mid);
            else
                g_app->zoneManager.SwitchZone(mid);

            // Re-poll the character level on every zone-in: the GW2 API only reflects a level change (level-up,
            // a tome / level-80 boost) AFTER the game persists the character server-side, which happens on a map
            // change -- so this is the moment it can actually have changed. RefreshNow self-guards (in-flight /
            // scope / empty name) and shares the level cache, so a real change lands within the (now-honored) 30s
            // TTL instead of waiting on the GW2 server's much longer Cache-Control window.
            static uint32_t s_levelPollMap = 0;
            if (mid != 0 && mid != s_levelPollMap)
            {
                s_levelPollMap = mid;
                g_app->characterLevel.RefreshNow(g_app->api, g_app->state, CurrentCharName());
            }

            // Resolve the travel hop + auto-copy the teleport EVERY frame while active (even with the map open,
            // so the cross-zone pins resolve there too -- the in-world guidance is paused while the map is open).
            if (g_app->travel.Active())
                g_app->travel.Recompute(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z);
        }

        // Vista auto-complete + (optional) camera probe. Gated on MumbleLink ONLY (NOT IsGameplay): the vista
        // cinematic flips IsGameplay false, so we must keep sampling through it to see the blip.
        if (MumbleLink)
        {
            const Math::Vec3 ava{MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Y, MumbleLink->AvatarPosition.Z};
            const bool gp = NexusLink && NexusLink->IsGameplay;

            // Vista auto-complete: watch the IsGameplay blip (the cinematic) against the nearest in-reach vista.
            VistaWatch::Update(*g_app, ava, CurrentMapId(), gp);

            // Read-only camera-vs-avatar probe -- only when the Diagnostics toggle is on (off by default).
            if (g_app->config.debugVistaProbe)
            {
                VistaProbe &vp = g_app->state.vistaProbe;
                const Math::Vec3 cam{MumbleLink->CameraPosition.X, MumbleLink->CameraPosition.Y, MumbleLink->CameraPosition.Z};
                const double now = ImGui::GetTime();
                const uint32_t tick = MumbleLink->UITick;
                auto dist = [](const Math::Vec3 &a, const Math::Vec3 &b)
                {
                    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
                    return std::sqrt(dx * dx + dy * dy + dz * dz);
                };
                vp.curDist = dist(cam, ava);
                vp.gameplayNow = gp;
                vp.tickNow = tick;
                if (vp.curDist > vp.peakDist)
                {
                    vp.peakDist = vp.curDist;
                    vp.peakAt = now;
                }
                if (!gp)
                {
                    vp.sawNotGameplay = true;
                    if (vp.curDist > vp.peakDistNoGp)
                        vp.peakDistNoGp = vp.curDist;
                    if (vp.have && tick != vp.lastTick)
                        vp.tickAdvNoGp = true; // link still writing during the cutscene
                }
                if (vp.have)
                {
                    const float camSeg = dist(cam, vp.lastCam), avaSeg = dist(ava, vp.lastAva);
                    if (camSeg < 60.f && avaSeg < 60.f)
                    {
                        vp.camAcc += camSeg;
                        vp.avaAcc += avaSeg;
                    } // skip teleport/load jumps
                    if (now - vp.winStart >= 2.0)
                    {
                        vp.camMoved2s = vp.camAcc;
                        vp.avaMoved2s = vp.avaAcc;
                        vp.camAcc = 0.f;
                        vp.avaAcc = 0.f;
                        vp.winStart = now;
                    }
                }
                else
                {
                    vp.winStart = now;
                    vp.have = true;
                }
                vp.lastCam = cam;
                vp.lastAva = ava;
                vp.lastTick = tick;
            }
        }

        g_app->state.viewerMode = ComputeViewerMode(); // one decision per frame, read by the guidance dispatch + the panel

        // Build the camera once, then draw the in-world overlays (trail + markers) + guidance/arrow.
        // Prepare the active map's farming nodes once per map change (overlay + combined run; cheap no-op otherwise).
        g_app->zoneManager.EnsureActiveGather(CurrentMapId());
        g_app->zoneManager.EnsureActiveFishHoles(CurrentMapId());

        // The shared farmRunActive / fishRunActive flags own the run: when on, switch the route guidance to Farming /
        // Fishing (the in-world arrow reads activeContent; the leveling arrow is suppressed). They are mutually
        // exclusive -- a fishing run stands down for a gathering run. Overlays are independent toggles. Fresh cross-off
        // each (re)entry.
        {
            Instance *fm = nullptr;
            const FishMap *fh = nullptr;
            if (FarmingRunActive(*g_app))
            {
                auto it = g_app->farming.find(CurrentMapId());
                if (it != g_app->farming.end())
                    fm = &it->second;
            }
            if (!fm && FishingRunActive(*g_app))
            {
                auto it = g_app->fishHoles.find(CurrentMapId());
                if (it != g_app->fishHoles.end())
                    fh = &it->second;
            }
            if (fm && g_app->state.activeContent != ContentType::Farming)
                g_app->state.farmGathered.clear();
            if (fh && g_app->state.activeContent != ContentType::Fishing)
                g_app->state.fishVisited.clear();
            g_app->state.activeContent = fm ? ContentType::Farming : (fh ? ContentType::Fishing : ContentType::OpenWorld);
            g_app->state.farm = fm;
            g_app->state.fishMap = fh;
            g_app->state.fishWatchTypes = FishHoles::WatchedTypes(*g_app); // once/frame -> the hole overlay + nav read it
        }

        // g_app->config.showGuide is the MASTER toggle (the tray left-click / "Show guide"): off hides EVERYTHING on
        // screen -- panel, arrow, trail AND markers -- not just the panel + arrow.
        // Also hidden while the fullscreen map is OPEN (IsGameplay stays true then -- the map is an overlay --
        // so these world-projected overlays would otherwise draw over the map; the flat MapRenderer::Draw shows there).
        if (g_app->config.showGuide && !IsMapOpen() && g_app->state.zone.Loaded && MumbleLink && MumbleIdent && NexusLink && NexusLink->IsGameplay && CurrentMapId() == g_app->state.zone.MapId && g_app->state.zone.Trail.size() >= 2)
        {
            const float W = ImGui::GetIO().DisplaySize.x, H = ImGui::GetIO().DisplaySize.y;
            if (W >= 1.f && H >= 1.f)
            {
                const Math::Vec3 camPos{MumbleLink->CameraPosition.X, MumbleLink->CameraPosition.Y, MumbleLink->CameraPosition.Z};
                const Math::Vec3 camFwd{MumbleLink->CameraFront.X, MumbleLink->CameraFront.Y, MumbleLink->CameraFront.Z};
                const Math::Vec3 camTop{MumbleLink->CameraTop.X, MumbleLink->CameraTop.Y, MumbleLink->CameraTop.Z};
                const Math::Vec3 avatar{MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Y, MumbleLink->AvatarPosition.Z};
                const float fov = (MumbleIdent->FOV > 0.01f) ? MumbleIdent->FOV : 1.222f;
                const Math::Mat4 vp = Math::BuildViewProj(camPos, camFwd, camTop, fov, W, H);

                // Pixels per (world metre at 1 m) for the on-screen size clamp: (1/tan(fovY/2)) * H/2.
                const float pxScale = (H * 0.5f) / std::tan(fov * 0.5f);

                // Bundle the frame camera once for the in-world renderers. (Markers + arrow still take loose
                // params for now; they fold into fc in later steps.)
                FrameCtx fc;
                fc.W = W;
                fc.H = H;
                fc.dt = ImGui::GetIO().DeltaTime;
                fc.camPos = camPos;
                fc.camFwd = camFwd;
                fc.camTop = camTop;
                fc.avatar = avatar;
                fc.fov = fov;
                fc.vp = vp;
                fc.pxScale = pxScale;
                fc.haveCamera = true;
                fc.inCombat = IsInCombat();

                // Hybrid/Direct render only the entrance->objective corridor
                // Trail mode + dungeons show the whole ribbon (the distance fade still limits it).
                // Uses last frame's current objective (the guidance below sets it).
                // With NO active route -- the zone is complete / nothing to guide to, so Guidance cleared the
                // snapshot and the arrow is hidden -- hide the trail too instead of falling through to the whole
                // ribbon below. A dungeon route or an active objective/travel arrow keeps it on, as does the
                // "Show trail when zone complete" option.
                if (g_app->state.inDungeon || g_app->state.guidanceSnapshot.active || g_app->config.showTrailWhenComplete)
                {
                    float trVisLo = 0.f;
                    int trVisHi = INT_MAX;
                    if (!ModeFollowsRibbon(g_app->config.routeMode) && !g_app->state.inDungeon &&
                        g_app->state.curStep >= 0 && g_app->state.curStep < (int)g_app->state.zone.Steps.size())
                    {
                        const int ti = g_app->state.zone.Steps[g_app->state.curStep].TrailIndex;
                        if (ti >= 0)
                        {
                            int seg;
                            float t;
                            g_app->follower.PointBackAlongTrail(ti, (float)g_app->config.approachMeters, seg, t);
                            trVisLo = (float)seg + t; // fractional start so the corridor grows smoothly (no vertex snap)
                            trVisHi = ti;
                        }
                    }
                    else if (g_app->state.inDungeon)
                    {
                        // Instance trails: explicit .trl sections are breaks;
                        // long valid segments inside a section are not. Clip to a moving corridor around the
                        // shared per-frame anchor without inventing distance-based break rules.
                        const std::vector<Math::Vec3> &T = g_app->state.zone.Trail;
                        const int nTrail = (int)T.size();
                        g_app->state.instTrailAnchor = InstanceGuidance::Localize(g_app->state, avatar);
                        const int anchor = std::clamp(g_app->state.instTrailAnchor, 0, nTrail - 1);
                        int secLo, secHi;
                        InstanceGuidance::TrailSectionBounds(g_app->state, anchor, secLo, secHi);
                        auto segLen = [&](int i)
                        {
                            const float dx = T[i + 1].x - T[i].x, dz = T[i + 1].z - T[i].z;
                            return std::sqrt(dx * dx + dz * dz);
                        };
                        constexpr float kBack = 8.f;   // corridor tail behind the player
                        constexpr float kAhead = 40.f; // corridor reach ahead (also hides far switchbacks)

                        // Forward edge: walk ahead within this explicit .trl section until kAhead of arc.
                        int hi = anchor;
                        float acc = 0.f;
                        while (hi < secHi)
                        {
                            const float d = segLen(hi);
                            if (acc + d > kAhead)
                                break;
                            acc += d;
                            ++hi;
                        }
                        trVisHi = hi;

                        // Back edge: a short lead-in behind, clamped to the same explicit .trl section.
                        trVisLo = (float)anchor;
                        float back = 0.f;
                        for (int i = anchor; i > secLo; --i)
                        {
                            const float d = segLen(i - 1);
                            if (back + d >= kBack)
                            {
                                const float f = d > 1e-6f ? (kBack - back) / d : 0.f; // 0 at i, 1 at i-1
                                trVisLo = (float)(i - 1) + (1.f - f);
                                break;
                            }
                            back += d;
                            trVisLo = (float)(i - 1);
                        }
                        if (trVisLo > (float)trVisHi)
                            trVisLo = (float)trVisHi;
                    }
                    g_app->trailRenderer.Draw(fc, g_app->config, g_app->state, trVisLo, trVisHi); // route ribbon (zone or dungeon trail)
                }
                const TargetOverrideKind arrowTargetKind = CurrentTargetOverride(*g_app); // for the arrow's "Clear target" item
                const auto arrowDispatch = [](int id)
                { if (g_app && id >= 0) QuickMenu::ArrowDispatch(*g_app, id); };
                if (g_app->state.viewerMode == ViewerMode::Dungeon)
                {
                    if (g_app->config.markerDungeon)
                        Markers::DrawInstance(fc, g_app->config, g_app->state);
                    InstanceGuidance::Update(W, H, avatar, camFwd, g_app->config, g_app->state, g_app->settingsDirty,
                                             g_app->follower, g_app->gpsArrow, g_app->guidance.SpeedEma(), arrowTargetKind, arrowDispatch);
                }
                else // Travel / Step / Complete: the open-world markers + arrow (UpdateGuidanceAndArrow handles
                {    // the current step, the travel override, confirm-on-reach, and the no-step/complete case)
                    if (g_app->config.showMarkers)
                        Markers::DrawWorld(fc, g_app->config, g_app->state, g_app->progress);
                    if (g_app->config.showPersonalMarker)
                        PersonalMarker::Draw(fc, g_app->config, g_app->state, g_app->travel, g_app->follower);
                    if (g_app->state.activeContent != ContentType::Farming) // an active farming run owns the arrow instead
                        g_app->guidance.Update(W, H, avatar, camFwd, ImGui::GetIO().DeltaTime,
                                               g_app->config, g_app->state, g_app->settingsDirty, g_app->progress,
                                               g_app->travel, g_app->follower, g_app->gpsArrow, arrowTargetKind, arrowDispatch);
                }
            }
        }

        // Farming node overlay + active RUN (in-world): INDEPENDENT of the guide route -- runs on ANY map with
        // gather data whenever the overlay master is on OR a farming run is active (no zone.Loaded / showGuide
        // requirement). Builds its own frame camera. Hidden while the fullscreen map is open.
        if ((g_app->config.gatherShow || FarmingGuidance::Active(*g_app) ||
             g_app->config.fishShow || FishingGuidance::Active(*g_app)) &&
            !IsMapOpen() &&
            MumbleLink && MumbleIdent && NexusLink && NexusLink->IsGameplay)
        {
            const float W = ImGui::GetIO().DisplaySize.x, H = ImGui::GetIO().DisplaySize.y;
            if (W >= 1.f && H >= 1.f)
            {
                const Math::Vec3 camPos{MumbleLink->CameraPosition.X, MumbleLink->CameraPosition.Y, MumbleLink->CameraPosition.Z};
                const Math::Vec3 camFwd{MumbleLink->CameraFront.X, MumbleLink->CameraFront.Y, MumbleLink->CameraFront.Z};
                const Math::Vec3 camTop{MumbleLink->CameraTop.X, MumbleLink->CameraTop.Y, MumbleLink->CameraTop.Z};
                const Math::Vec3 avatar{MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Y, MumbleLink->AvatarPosition.Z};
                const float fov = (MumbleIdent->FOV > 0.01f) ? MumbleIdent->FOV : 1.222f;
                FrameCtx fc;
                fc.W = W;
                fc.H = H;
                fc.camPos = camPos;
                fc.avatar = avatar;
                fc.inCombat = IsInCombat();
                fc.vp = Math::BuildViewProj(camPos, camFwd, camTop, fov, W, H);
                fc.pxScale = (H * 0.5f) / std::tan(fov * 0.5f);
                if (g_app->config.gatherShow && !g_app->state.activeGather.empty())
                    CategoryMarkers::DrawWorld(fc, g_app->config, g_app->state); // independent toggle (a run drives only the arrow)
                if (FarmingGuidance::Active(*g_app))
                    FarmingGuidance::Update(*g_app, W, H, avatar, camFwd);
                if (g_app->config.fishShow && !g_app->state.activeFish.empty())
                    FishMarkers::DrawWorld(fc, g_app->config, g_app->state);
                if (FishingGuidance::Active(*g_app))
                    FishingGuidance::Update(*g_app, W, H, avatar, camFwd);
            }
        }

        // While the fullscreen MAP is OPEN the in-world block above is skipped (arrow + trail pause - the map is an
        // overlay). But the guide PANEL + step checklist keep rendering, so the current objective must still
        // advance + re-order as you tick things off. Recompute JUST the current step here (camera-free; uses the
        // avatar position) - else curStep freezes and the list reverts to authored order until the map closes (e.g.
        // Hybrid Nearest visibly snapping to trail order while the map is up, then re-sorting on close).
        if (g_app->config.showGuide && IsMapOpen() && g_app->state.zone.Loaded && MumbleLink && NexusLink && NexusLink->IsGameplay && CurrentMapId() == g_app->state.zone.MapId && g_app->state.viewerMode != ViewerMode::Travel && !g_app->state.inDungeon)
        {
            const Follow::Vec2 playerXz{MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z};
            g_app->guidance.PickCurrentStep(playerXz, g_app->config, g_app->state, g_app->progress);
        }

        g_app->dpiWatcher.Refresh(g_app->settingsPath); // re-read the GW2 + Nexus DPI flags EVERY frame (throttled) - must be OUTSIDE MapRenderer::Draw's
                                                        // gameplay gate so it stays live while the settings window is focused or the map is open.

        // Route on the in-game map / minimap. Separate from the in-world block: the FULLSCREEN map is open
        // while IsGameplay is FALSE, so this must run outside that gate (it self-gates per surface). MUST run
        // BEFORE MapRenderer::HandleClick so the click handler inverts the minimap click against THIS frame's projection
        // (g_mapProj) -- otherwise a target placed while moving/turning lands a frame stale, a few px off from
        // GW2's own marker, and then one click can clear only one of the two.
        g_app->mapRenderer.Draw(g_app->config, g_app->state, g_app->travel, g_app->follower, g_app->progress, g_app->dpiWatcher.State());
        g_app->mapRenderer.UpdateAssist(g_app->config, g_app->state, g_app->progress);

        g_app->mapRenderer.HandleClick(g_app->config, g_app->state, g_app->travel); // Alt+click -> place/clear a personal target

        // The steps panel only makes sense during active gameplay on a real map -- NOT on the character-select
        // / loading / cutscene screens (no character, no map). Gate it like the arrow (IsGameplay). The
        // settings window stays available everywhere (it's account-wide, configurable off a character).
        const bool inGame = MumbleLink && NexusLink && NexusLink->IsGameplay && CurrentMapId() != 0;
        if (g_app->config.showGuide && inGame)
        {
            DrawGuideWindow(*g_app); // master toggle + in-game gate the steps panel
            DrawWaypointTravelOffer(*g_app);
        }
        DrawSettingsWindow(*g_app);
        Wiki::RenderReader(*g_app);
        Welcome::Draw(*g_app); // first-run API-key prompt (self-gates on onboardedV1 + no-key; account-wide)
        DrawTrayMenu(*g_app);  // our GW2-skinned tray right-click menu (opened on request from the Nexus callback)

        // API data hub: char-switch detection always; gentle TTL refresh for the domains a visible consumer shows.
        // The Dashboard wants everything while open; the Info Panel ORs in just the domains it currently draws; the
        // TP domain also polls (gentle TTL) whenever delivery toasts are on, so pickups are detected with the panel closed.
        AccountData::Tick((Dashboard::IsOpen() ? AccountData::DomAll : 0u) | InfoPanel::NeededDomains(*g_app) | (g_app->config.tpToasts ? AccountData::DomTradingPost : 0u) | AccountData::DomWallet // Session Tracker always baselines/accrues the wallet (gentle 300s TTL)
                          | AccountData::DomCharacters                                                                                                                                               // roster + per-char levels for the account-wide lifetime level total
                          | TradingPostNeededDomains() | ItemsNeededDomains(*g_app) | CollectionsNeededDomains(*g_app) | ContentNeededDomains(*g_app));

        // WvW match poll (anonymous; conditional + tiered). Separate from Tick because it needs no API key and
        // gates on MumbleLink (in WvW = fast) rather than the interest mask.
        AccountData::TickWvw(InfoPanel::HasWvwText(*g_app) || Dashboard::HasVisibleWvwWidget(*g_app), IsInWvW());

        // Once the account inventory index + the TP id set are loaded, prewarm price-history for the
        // player's owned TRADEABLE items not already in the bundle (low-priority background fetch). Once per session.
        if (!g_app->state.phPrewarmed)
        {
            const AccountData::Model &am = AccountData::Get();
            if (am.inventoryStatus.state == AccountData::InventoryState::Complete && TpEligibility::IsReady())
            {
                std::vector<int> owned;
                auto add = [&](int id)
                { if (id > 0 && TpEligibility::IsTradeable(id)) owned.push_back(id); };
                for (const auto &s : am.bankSlots)
                    add(s.id);
                for (const auto &s : am.sharedSlots)
                    add(s.id);
                for (const auto &mt : am.materials)
                    add(mt.id);
                for (const auto &kv : am.inv)
                    for (const auto &bag : kv.second.bags)
                        for (const auto &slot : bag.slots)
                            add(slot.id);
                std::sort(owned.begin(), owned.end());
                owned.erase(std::unique(owned.begin(), owned.end()), owned.end());
                PriceHistory::Prewarm(owned); // Prewarm skips bundled/done/in-flight ids -> only the long tail queues
                g_app->state.phPrewarmed = true;
            }
        }

        { // Keep the account-wide lifetime level total current (only when a NEW character fetch lands; persists on change).
            static double s_lastCharsAt = -1.0;
            const AccountData::Model &acc = AccountData::Get();
            if (acc.haveChars && acc.charsAt != s_lastCharsAt)
            {
                s_lastCharsAt = acc.charsAt;
                std::vector<std::pair<std::string, int>> roster;
                roster.reserve(acc.characters.size());
                for (const AccountData::AcctChar &c : acc.characters)
                    roster.push_back({c.name, c.level});
                AccountLevels::Reconcile(roster);
            }
        }
        UpdateTpDeliveryToasts();  // fire a toast if the TP delivery box just gained coins/items
        Dashboard::Render(*g_app); // notification center: edge handle + slide-out widget panel
        if (NexusLink && NexusLink->IsGameplay)
        {
            HUD::Render(*g_app);
            InfoPanel::Render(*g_app);
        } // launcher + data bar -- in-world only
        Toast::Render(*g_app);             // self-rendered notification pop-ups, on top of everything (foreground draw list)
        g_app->zoneDisplay.Update(*g_app); // detect map/sector change (internally gated); cheap every frame
        g_app->zoneDisplay.Draw(*g_app);   // the animated Region/Zone/Sector banner (foreground draw list, own surface)

        // Auto-persist: write settings.json once a change is COMMITTED (nothing being dragged), not every
        // frame of a slider drag. Mirrors ProgressStore's save-on-mutation.
        if (g_app->settingsDirty && !ImGui::IsAnyItemActive())
        {
            // Apply a changed API key on COMMIT (field deselected), not per keystroke -> one tokeninfo fetch.
            if (g_apiKeyApplied != g_app->config.apiKey)
            {
                g_apiKeyApplied = g_app->config.apiKey;
                g_app->api.SetApiKey(g_app->config.apiKey);
            }
            SaveSettings(*g_app);
            g_app->settingsDirty = false;
        }
    }
    catch (...)
    {
        // Never let an exception escape into the game's render loop (that crashes GW2). Log once.
        static bool logged = false;
        if (!logged && APIDefs)
        {
            APIDefs->Log(LOGL_CRITICAL, "Tyrian Codex", "Exception in Render suppressed.");
            logged = true;
        }
    }
}

// Write a DLL-embedded RCDATA resource to `path` if the file isn't already present. Used for the Nexus tray
// icons, which must exist before the data/ self-download completes on a fresh install (Nexus ships only the DLL).
static void ExtractResourceToFile(int resId, const std::string &path)
{
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
        return;
    HRSRC hr = FindResourceA(Self, MAKEINTRESOURCEA(resId), (LPCSTR)RT_RCDATA);
    if (!hr)
        return;
    HGLOBAL hg = LoadResource(Self, hr);
    if (!hg)
        return;
    const void *p = LockResource(hg);
    const DWORD n = SizeofResource(Self, hr);
    if (!p || !n)
        return;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f)
        f.write(static_cast<const char *>(p), static_cast<std::streamsize>(n));
}

// Every load that reads the bundled data/ folder (or a data/-seeded catalog). Runs ONCE, from Render on the
// render thread, the frame after DataBootstrap reports core is present (fonts + several Inits require the
// render/main thread). Split out of AddonLoad so the addon can DEFER all of this behind the core download on a
// fresh install (Nexus ships only the DLL). `app` is g_app; referenced as g_app below.
void LoadAllData(App &app, const std::string &base)
{
    (void)app;
    try
    {
        SectorData::Init(&g_app->api, base + "\\data", base + "\\cache");     // offline Zone Display names (data\sectors.json)
        ItemCatalog::Init(&g_app->api, base + "\\data", base + "\\cache");    // whole-game item DB (data\items.json seed)
        SkinCatalog::Init(&g_app->api, base + "\\data", base + "\\cache");    // wardrobe skin DB (data\skins.json seed)
        CosmeticCatalog::Init(base + "\\data");                               // visual-cosmetic catalogs (bundled-only seeds)
        CraftingEngine::Init(&g_app->api, base + "\\data", base + "\\cache"); // recipe catalog (data\recipes.json seed)
        PriceHistory::Init(base);                                             // datawars2 per-item price history (per-item disk cache + data\price_history.json seed)

        // Real GW2 font (Menomonia, bundled). Async: the callback stores each face + reapplies; until then Gw2Ui
        // uses Nexus's font. Bake a native-size ladder (regular + italic) so runs draw at scale ~1.0.
        if (APIDefs->Fonts_AddFromFile)
        {
            const std::string reg = base + "\\data\\fonts\\menomonia.ttf";
            const std::string ita = base + "\\data\\fonts\\menomonia-italic.ttf";
            for (const App::MenoBake &m : g_app->menoLadder)
            {
                char idR[24], idI[24];
                std::snprintf(idR, sizeof idR, "TC_MENO_%d", (int)m.px);
                std::snprintf(idI, sizeof idI, "TC_MENO_IT_%d", (int)m.px);
                APIDefs->Fonts_AddFromFile(idR, m.px, reg.c_str(), OnFontLoaded, nullptr);
                APIDefs->Fonts_AddFromFile(idI, m.px, ita.c_str(), OnFontLoaded, nullptr);
            }
            APIDefs->Fonts_AddFromFile("TC_ZD_BANNER", 96.f, reg.c_str(), OnFontLoaded, nullptr);
            APIDefs->Fonts_AddFromFile("TC_ZD_KRYTAN", 96.f, (base + "\\data\\fonts\\NewKrytan.ttf").c_str(), OnFontLoaded, nullptr);
        }

        if (Dyes::Load(base, g_app->dyes, g_app->dyeNames))
            APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("Loaded " + std::to_string(g_app->dyes.size()) + " GW2 dyes.").c_str());

        g_app->zoneManager.SetDataDirs(base + "\\data\\zones", base + "\\data\\instances", base + "\\data\\fishing");

        const int nz = LoadAllZones(base + "\\data\\zones", g_app->zones);
        if (nz > 0)
            APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("Loaded " + std::to_string(nz) + " zones.").c_str());
        else
            APIDefs->Log(LOGL_WARNING, "Tyrian Codex", ("No zones loaded from " + base + "\\data\\zones").c_str());
        const int ni = LoadAllInstances(base + "\\data\\instances", g_app->instances);
        APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("Loaded " + std::to_string(ni) + " dungeons.").c_str());
        const int nf = LoadAllFarming(base + "\\data\\farming", g_app->farming);
        APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("Loaded " + std::to_string(nf) + " farming maps.").c_str());
        const int nfh = LoadAllFishing(base + "\\data\\fishing", g_app->fishHoles);
        APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("Loaded " + std::to_string(nfh) + " fishing maps.").c_str());

        const int nm = LoadMapIndex(base + "\\data\\maps\\index.json", g_app->mapIndex);
        for (MapInfo &mi : g_app->mapIndex)
            mi.HasGuide = g_app->zones.count(mi.MapId) || g_app->instances.count(mi.MapId);
        APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("Loaded " + std::to_string(nm) + " indexed maps.").c_str());

        g_app->fish.Load(base + "\\data\\fish.json");
        g_app->fish.Init(&g_app->api);
        g_app->content.Load(base + "\\data\\content.json", base + "\\data\\rotations.json");
        g_app->decorations.Load(base + "\\data\\decorations.json");
        g_app->eventTimers.Load(base + "\\data\\event_timers.json");
        g_app->eventAreas.Load(base + "\\data\\event_areas.json");
        g_app->loadingScreens.Load(base + "\\data\\loading_screens.json");

        // Bridge pre-confirmation progress: any already-completed waypoint becomes a confirmed (unlocked) one.
        {
            std::vector<std::string> wpIds;
            for (const auto &kv : g_app->zones)
                for (const Step &s : kv.second.Steps)
                    if (s.Type == "waypoint")
                        wpIds.push_back(s.StepId);
            g_app->progress.SeedConfirmedFromCompleted(wpIds);
        }

        if (g_app->stories.Load(base + "\\data\\stories.json"))
            APIDefs->Log(LOGL_INFO, "Tyrian Codex", ("Loaded story spine: " + std::to_string(g_app->stories.ByRelease().size()) + " releases.").c_str());
        g_app->storyStore.Load(base + "\\story-account.json", base + "\\story-characters.json");
        EventFavorites::Load();   // account-wide event favorites + reminder flags (event-favorites.json)
        g_app->storyCompletion.RecomputeCompletedReleases();

        LoadJournalCache(base + "\\journal-cache.json");
        LoadJournalOverrides(base + "\\data\\journal_overrides.json");
        LoadJournalLocations(base + "\\data\\journal_locations.json");
        LoadStoryTimes(base + "\\data\\story_times.json");
        LoadPersonalStory(base + "\\data\\personal_story.json");
        AchPoints::Load(base + "\\data\\achievement_points.json");
        LoadPersonalStoryCache(base + "\\personal-story-cache.json");
    }
    catch (...)
    {
        if (APIDefs)
            APIDefs->Log(LOGL_CRITICAL, "Tyrian Codex", "LoadAllData: exception while loading bundled data (suppressed).");
    }
}

static void AddonLoad(AddonAPI_t *aApi)
{
    APIDefs = aApi;
    g_app = new App(); // construct all long-lived state for this addon session (deleted in AddonUnload)
    g_app->storyCompletion.Init(&g_app->stories, &g_app->storyStore, &g_app->api, &g_app->state);
    g_app->zoneManager.Init(&g_app->state, &g_app->config, &g_app->zones, &g_app->instances, &g_app->follower, &g_app->gpsArrow, &g_app->farming, &g_app->fishHoles);

    // Share Nexus's single ImGui context + allocator (compiled against ImGui 1.80).
    ImGui::SetCurrentContext(static_cast<ImGuiContext *>(aApi->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void *(*)(size_t, void *)>(aApi->ImguiMalloc),
        reinterpret_cast<void (*)(void *, void *)>(aApi->ImguiFree));

    MumbleLink = static_cast<Mumble::LinkedMem *>(aApi->DataLink_Get(DL_MUMBLE_LINK));
    MumbleIdent = static_cast<Mumble::Identity *>(aApi->DataLink_Get(DL_MUMBLE_LINK_IDENTITY));
    NexusLink = static_cast<NexusLinkData_t *>(aApi->DataLink_Get(DL_NEXUS_LINK));

    // Load the bundled zone dataset from <GW2>/addons/TyrianCodex/data/zones/ + the progress store.
    const char *dir = aApi->Paths_GetAddonDirectory(kAddonDirName); // one source: Shared.h
    const std::string base = dir ? dir : "";
    // Guard every persisted-data loader: a corrupt account/cache/settings file must NOT throw into GW2 and
    // crash the game on enable (the Render loop has the same backstop at the bottom of Render()). The targeted
    // Api::Json null-safe readers below mean a bad field no longer ABORTS the sequence; this catch is the
    // last-resort net. The API worker + ImageCache init and Render/keybind registration below run regardless.
    try
    {
        g_app->progress.Load(base + "\\progress.json");
        g_app->sessionHistory.Load(base + "\\session-history.json"); // per-character past-session earnings log (survives cache clear)
        AccountLevels::Init(base + "\\account-levels.json");         // account-wide lifetime level total (per-character ledger)
        g_app->characterLevel.Load(base + "\\charlevels.json");      // last-known per-character levels -> instant stale recommendations
        CharRegistry::Init(base + "\\char-registry.json");           // name->created-id; detects a future rename to auto-merge per-char data

        // Register each surface's structured Config fields (button/text/widget layouts), then initialize the ONE
        // universal profile mechanism -- it captures the baked default slices while Config is still fresh -- BEFORE
        // loading saved settings. LoadSettings then restores the flat globals + each family's per-character profiles.
        HUD::RegisterConfig();
        InfoPanel::RegisterConfig();
        Dashboard::RegisterConfig();
        ConfigProfiles::Init(*g_app);

        // Load saved settings (global/account-wide); overrides the in-memory defaults.
        g_app->settingsPath = base + "\\settings.json";
        LoadSettings(*g_app);

        // Dashboard API data: cache-then-refresh (last-known from disk now, live refresh once in-game / on key).
        AccountData::Init(&g_app->api, base);
        AccountData::LoadCache();
        StaticData::Init(&g_app->api, base); // reference-data cache (spec/map/material names); loads staticdata.json stale
        MarketData::Init(base);              // datawars2 whole-market snapshot: stale root disk cache now, fresh snapshot in WarmCaches
        // (SectorData / ItemCatalog / SkinCatalog / CosmeticCatalog / CraftingEngine / PriceHistory read the
        //  bundled data/ -> moved to LoadAllData, which runs after the core data download on a fresh install.)
        TpEligibility::Init(&g_app->api, base);      // official TP id set: stale root disk cache now, one refresh in WarmCaches
        CraftKnown::Init(&g_app->api, base);         // learned-recipe sets (account + per character): stale root disk cache now, refresh in WarmCaches
        CraftCart::Init(base + "\\craft_cart.json"); // persistent account-wide crafting cart (named projects + materials check-off)
        TpPrices::Init(&g_app->api);                 // live Trading Post price cache for the Items-tab price pills + the TP tab
        g_app->wiki.Init(base);                      // native GW2 Wiki reader cache/service; all fetches are worker-threaded
    }
    catch (...)
    {
        aApi->Log(LOGL_CRITICAL, "Tyrian Codex", "AddonLoad: exception while loading saved data (suppressed); continuing.");
    }

    // Start the GW2 Web API client worker, then resolve the saved key's scopes (additive; a missing/invalid
    // key just disables the auth-gated features). g_apiKeyApplied tracks it so the commit-path won't re-fetch.
    g_app->api.Init();
    ImageCache::Init(); // disk-backed cache for ALL networked images (tiles + icons); worker stopped in AddonUnload
    // Self-download the bundled data/ folder (Nexus ships only the DLL). Ready immediately if core is already
    // present; else starts the core download -> Render shows a progress panel + defers LoadAllData until Ready.
    DataBootstrap::Init(base);
    // When the key's scopes (re)resolve, immediately re-attempt the level fetch (the SubtokenUpdated
    // equivalent) instead of waiting for the 3s timer -> recommendations go level-correct as soon as a
    // 'characters'-scoped key is pasted.
    g_app->api.OnScopesChanged([]()
                               {
                                   if (!g_app)
                                       return;
                                   g_app->characterLevel.ResetAttempt();
                                   g_app->characterLevel.RefreshNow(g_app->api, g_app->state, CurrentCharName());
                                   g_app->storyCompletion.RefreshAchievements(true);
                                   AccountData::WarmAll(); // a newly-resolved key (re)warms the dashboard's API data (incl. the inventory index)
                               });
    g_apiKeyApplied = g_app->config.apiKey;
    if (g_app->config.apiKey[0])
        g_app->api.SetApiKey(g_app->config.apiKey);

    // NOTE: all of the bundled-data loads (fonts, Dyes, LoadAllZones/Instances/Farming/Fishing, LoadMapIndex,
    // fish/content/decorations/events/loadingScreens, the waypoint bridge, stories + journal) now live in
    // LoadAllData(), which Render() runs on the render thread ONCE the core data download reports Ready. This
    // lets a fresh install (Nexus ships only the DLL) defer everything behind the DataBootstrap core download.

    Notify::Init(&g_app->config); // notification store reads master + per-kind enables from config

    // Smart-travel: give the controller the zone map + progress + small getters/sinks. Travel applies only
    // in open-world zones (never in a dungeon).
    g_app->travel.Init(&g_app->zones, &g_app->progress, []() -> std::string
                       { return g_app->state.currentChar; }, []() -> const Zone *
                       { return (g_app->state.zone.Loaded && !g_app->state.inDungeon) ? &g_app->state.zone : nullptr; }, [](const std::string &s)
                       { SetClipboard(s); }, [](const std::string &s)
                       { Notify::Push(Notify::Kind::Travel, s); });

    aApi->GUI_Register(RT_Render, Render);

    // Pre-load the highlight effect textures now; the settings-window chrome + tab icons warm in WarmCaches
    // (first in-game frame) where kSettingsTabs lives -- both ready before the window is first opened.
    Fx::Warm();

    // Tray icon: a white nav-arrow silhouette (normal ~0.6 alpha, hover full).
    // NOTE: use FRESH texture identifiers, never a reused one. Nexus caches textures by identifier for the
    // whole game session, so GetOrCreateFromFile on an id that already loaded (e.g. an earlier wrench under
    // "TC_QA_ICON") returns the STALE texture and ignores the new file -- bump the id when the art changes.
    // ...the id suffix (_V2 here) is the cache-buster: BUMP it whenever the PNG bytes change.
    // On a fresh install the data/ folder isn't downloaded yet, so extract the tray icons from the DLL (RCDATA)
    // to disk if missing -- then load them the known-working way. Guarantees the tray/QuickAccess icon appears
    // immediately, not blank until the core download + a reload.
    ExtractResourceToFile(RESID_TRAY_ICON, base + "\\data\\textures\\ui\\tc-icon.png");
    ExtractResourceToFile(RESID_TRAY_ICON_HOVER, base + "\\data\\textures\\ui\\tc-icon-hover.png");
    aApi->Textures_GetOrCreateFromFile("TC_TRAY_ARROW_V2", (base + "\\data\\textures\\ui\\tc-icon.png").c_str());
    aApi->Textures_GetOrCreateFromFile("TC_TRAY_ARROW_V2_HOVER", (base + "\\data\\textures\\ui\\tc-icon-hover.png").c_str());
    if (aApi->WndProc_Register)
        aApi->WndProc_Register(OnWndProc); // consume Escape while our window is focused

    // Guide-action keybinds: register them from the saved settings (also bindable in Nexus's keybind UI).
    ApplyKeybinds(*g_app);

    // Tray shortcut: QuickAccess left-clicks require a registered InputBind. This internal bridge is the only
    // Nexus bind we keep; user-editable hotkeys are handled by Tyrian Codex itself.
    aApi->QuickAccess_Add("TC_SHORTCUT", "TC_TRAY_ARROW_V2", "TC_TRAY_ARROW_V2_HOVER", "TC_TOGGLE_PANEL_QA",
                          "Tyrian Codex - left-click hides/shows the guide panel, right-click for the menu");
    aApi->QuickAccess_AddContextMenu("TC_CTX", "TC_SHORTCUT", RenderQuickAccessMenu);

    aApi->Log(LOGL_INFO, "Tyrian Codex", "Loaded.");
}

static void AddonUnload()
{
    if (!APIDefs)
        return;
    if (g_app)
        SessionTracker::Finalize(*g_app); // save the active earnings session before teardown (AccountData still alive)
    AccountLevels::Shutdown();            // clear the lifetime-level ledger (Init reloads it on re-enable)
    CharRegistry::Shutdown();             // clear the name->created-id registry (Init reloads it on re-enable)
    const auto unloadStart = std::chrono::steady_clock::now();
    auto logUnload = [&](const char *label)
    {
        if (!APIDefs || !APIDefs->Log)
            return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - unloadStart)
                                 .count();
        char msg[192]{};
        std::snprintf(msg, sizeof(msg), "Unload %s (%lld ms)", label, (long long)elapsed);
        APIDefs->Log(LOGL_INFO, "Tyrian Codex", msg);
    };

    logUnload("begin");
    APIDefs->GUI_Deregister(Render); // deregister render FIRST (hot-reload crash-safety)
    logUnload("after GUI_Deregister");
    if (!g_app)
    {
        logUnload("no app");
        APIDefs = nullptr;
        MumbleLink = nullptr;
        MumbleIdent = nullptr;
        NexusLink = nullptr;
        return;
    }

    if (g_app->state.mapAssist.active && APIDefs->Log)
    {
        const MapAssistTarget &ma = g_app->state.mapAssist;
        char msg[256]{};
        std::snprintf(msg, sizeof(msg),
                      "Unload map assist active: attempts=%d wnd=%d send=%d zero=%d/%d status=%s",
                      ma.panAttempts, ma.wndProcDragAttempts, ma.sendInputDragAttempts,
                      ma.consecutiveZeroMoveReadbacks, ma.zeroMoveReadbacks,
                      ma.panStatus.empty() ? "-" : ma.panStatus.c_str());
        APIDefs->Log(LOGL_INFO, "Tyrian Codex", msg);
    }

    APIDefs->QuickAccess_RemoveContextMenu("TC_CTX");
    APIDefs->QuickAccess_Remove("TC_SHORTCUT");
    if (APIDefs->WndProc_Deregister)
        APIDefs->WndProc_Deregister(OnWndProc);
    APIDefs->InputBinds_Deregister("TC_TOGGLE_SETTINGS");
    APIDefs->InputBinds_Deregister("TC_TOGGLE_PANEL_QA");
    APIDefs->InputBinds_Deregister("TC_TOGGLE_PANEL");
    APIDefs->InputBinds_Deregister("TC_TOGGLE_GUIDE");
    APIDefs->InputBinds_Deregister("TC_MARK_DONE");
    APIDefs->InputBinds_Deregister("TC_BACK");
    APIDefs->InputBinds_Deregister("TC_COPY_WP");
    APIDefs->InputBinds_Deregister("TC_LOADOUT_CYCLE"); // the 4 Loadout keybinds (added with the Loadouts feature)
    APIDefs->InputBinds_Deregister("TC_LOADOUT_1");
    APIDefs->InputBinds_Deregister("TC_LOADOUT_2");
    APIDefs->InputBinds_Deregister("TC_LOADOUT_3");
    APIDefs->InputBinds_Deregister("TC_ZONE_REANNOUNCE"); // Zone Display reannounce bind (was leaking on unload)
    APIDefs->InputBinds_Deregister("TC_TOGGLE_MARKERS");
    APIDefs->InputBinds_Deregister("TC_TOGGLE_TRAIL");
    logUnload("after Nexus removals");

    // Release our fonts BEFORE unload: Fonts_AddFromFile registered OnFontLoaded (a pointer INTO this DLL) and
    // a font reference in Nexus's shared atlas. Without releasing, Nexus rebuilds the atlas after we unload and
    // calls our now-freed callback / dereferences freed memory -> crash in d3d11 (FONT_DEF on the stack).
    Gw2Ui::SetGw2Font(nullptr, nullptr); // stop drawing with the font we're about to release
    Wiki::ShutdownQuoteFont();
    if (APIDefs->Fonts_Release)
    {
        for (const App::MenoBake &m : g_app->menoLadder) // release every ladder rung (regular + italic)
        {
            char idR[24], idI[24];
            std::snprintf(idR, sizeof idR, "TC_MENO_%d", (int)m.px);
            std::snprintf(idI, sizeof idI, "TC_MENO_IT_%d", (int)m.px);
            APIDefs->Fonts_Release(idR, OnFontLoaded);
            APIDefs->Fonts_Release(idI, OnFontLoaded);
        }
        APIDefs->Fonts_Release("TC_ZD_BANNER", OnFontLoaded); // Zone Display banner font (96px) -- MUST release or atlas-rebuild crashes
        APIDefs->Fonts_Release("TC_ZD_KRYTAN", OnFontLoaded); // Zone Display Decode-entrance Krytan font (96px) -- MUST release too
    }
    logUnload("after fonts");

    g_app->dpiWatcher.Close(); // close the DPI folder-change watches (RAII dtor on delete g_app is the backstop)
    logUnload("after dpi watcher");

    logUnload("before market data shutdown");
    MarketData::Shutdown();   // stop the datawars2 snapshot worker BEFORE WinHTTP closes
    PriceHistory::Shutdown(); // stop the price-history worker BEFORE WinHTTP closes
    logUnload("after market data shutdown");
    logUnload("before wiki shutdown");
    g_app->wiki.Shutdown(); // stop wiki page/search/CSS worker BEFORE shared HTTP/image/API teardown
    logUnload("after wiki shutdown");
    logUnload("before data bootstrap shutdown");
    DataBootstrap::Shutdown(); // stop + join the core/assets download workers (Abort unblocks in-flight GET) BEFORE WinHTTP closes
    logUnload("after data bootstrap shutdown");
    logUnload("before image cache shutdown");
    ImageCache::Shutdown(); // stop the cache worker BEFORE WinHTTP is closed below
    logUnload("after image cache shutdown");
    OwnedTex::ReleaseAll(); // free ALL addon-owned texture VRAM + the device ref (workers now joined)
    logUnload("after owned-texture release");
    logUnload("before api shutdown");
    g_app->api.Shutdown(); // stop the API worker thread + close WinHTTP
    logUnload("after api shutdown");
    // Free the big static data caches now (worker stopped above, so no callback can fire into freed state).
    // These live in file-statics, NOT on g_app, so `delete g_app` below would not reclaim them -- without this a
    // disable/re-enable cycle would retain (and compound) tens of MB of catalog + account data.
    ItemCatalog::Shutdown();
    SkinCatalog::Shutdown();
    CosmeticCatalog::Shutdown();
    CraftingEngine::Shutdown();
    SectorData::Shutdown();
    StaticData::Shutdown();
    AccountData::Shutdown();
    TpEligibility::Shutdown();
    CraftKnown::Shutdown();
    CraftCart::Shutdown();
    TpPrices::Shutdown();
    ShutdownItemsTab();
    logUnload("after data caches shutdown");
    Fx::Shutdown(); // release the ScrollingHighlight D3D objects (no COM leak on disable/re-enable)
    logUnload("after fx shutdown");
    SaveSettings(*g_app); // backstop flush (changes already auto-save on commit)
    logUnload("after settings save");
    logUnload("before app delete");
    delete g_app;
    g_app = nullptr; // member dtors run AFTER all Nexus teardown above (they only free C++ memory)
    logUnload("after app delete");

    APIDefs = nullptr;
    MumbleLink = nullptr;
    MumbleIdent = nullptr;
    NexusLink = nullptr;
}

static AddonDefinition_t s_AddonDef{};

extern "C" __declspec(dllexport) AddonDefinition_t *GetAddonDef()
{
    s_AddonDef.Signature = 0x54434458; // 'TCDX' (Tyrian Codex) - unique Nexus addon id.
    s_AddonDef.APIVersion = NEXUS_API_VERSION;
    s_AddonDef.Name = "Tyrian Codex";
    s_AddonDef.Version = {TC_VER_MAJOR, TC_VER_MINOR, TC_VER_BUILD, TC_VER_REVISION};
    s_AddonDef.Author = "MorningStarGG";
    s_AddonDef.Description = "Guild Wars 2 companion: leveling / map-completion guide, 3D nav, dashboard, HUD, info panel, items & journal.";
    s_AddonDef.Load = AddonLoad;
    s_AddonDef.Unload = AddonUnload;
    s_AddonDef.Flags = AF_None;
    s_AddonDef.Provider = UP_GitHub;
    s_AddonDef.UpdateLink = "https://github.com/MorningStarGG/TyrianCodex";
    return &s_AddonDef;
}
