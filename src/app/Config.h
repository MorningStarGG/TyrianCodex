#pragma once
#include <map>
#include <string>
#include <vector>
#include "ui/LayoutTypes.h" // HudBtn / InfoSlot / DashSlot + UiLayout::Ordered<> (json-free -> cheap to include here)
// -----------------------------------------------------------------------------------------------------
// Config: every user-tunable setting in ONE long-lived object (App owns exactly one). The settings table
// (entry.cpp Settings()) points into it with stable addresses; Save/LoadSettings serialize it keyed by the
// stable persistence keys. Moved verbatim from entry.cpp's settings globals -- field names are the old g_
// names minus the prefix, defaults unchanged, so the on-disk settings.json format is byte-for-byte identical.
// -----------------------------------------------------------------------------------------------------
struct Config
{
    // General -- guide-wide behavior
    bool showGuide = true;
    bool autoAdvance = true;
    bool zoneToast = true;
    bool useGw2Font = true;
    int tooltipFontSize = 3;                                 // index into kFontSizePx (20 px) - hover tooltip text size (title/muted offset from it)
    float arrival = 15.f;                                    // arrival distance, metres
    bool showTpPrices = true;                                // live Trading Post buy/sell + flip margin on item tooltips/cells
    bool tpToasts = true;                                    // toast when the Trading Post delivery box gains coins/items
    bool craftUseOwnMaterials = true;                        // Crafting Cart: subtract materials you already own from the shopping list
    bool craftSubComponents = true;                          // Crafting Cart: recurse into craftable intermediates when cheaper
    bool craftShopByItem = false;                            // Crafting Cart shopping list: false = flat (aggregated), true = grouped by cart item
    bool craftStepsByItem = false;                           // Crafting Cart crafting steps: false = flat list, true = grouped per cart item
    int itemsView = 1;                                       // Items tab: 0 icon grid, 1 details list (internal UI state)
    std::map<std::string, bool> inventorySingleGridBySource; // Inventory tab: per-source combined-grid toggle
    std::map<std::string, float> uiPaneWidths;               // resizable rail/column widths by stable key (Gw2Ui::VSplitter)
    float uiScale = 1.0f;                                    // global screen-space UI scale (all characters; local size settings stack on top)

    // Persisted width for a draggable rail/column by stable key; seeds `def` on first use and returns a
    // reference so the resize helper edits it in place (the caller marks settingsDirty on change).
    float &PaneW(const std::string &key, float def)
    {
        auto it = uiPaneWidths.find(key);
        if (it == uiPaneWidths.end())
            it = uiPaneWidths.emplace(key, def).first;
        return it->second;
    }

    // GPS Arrow
    bool showArrow = true;
    int arrowSize = 80;            // arrow image size, px
    int arrowTextSize = 4;         // index into kFontSizeNames (16)
    bool arrowLocked = true;       // arrow pinned in place; uncheck to drag it
    bool arrowShowTypeIcon = true; // GPS arrow: show the objective TYPE icon (heart/POI/material) before the name
    float arrowPosX = -1.f;        // saved arrow screen position (-1 = use default centre-low)
    float arrowPosY = -1.f;
    bool showDirectIndicator = true;    // Hybrid only: a glowing arc around the arrow at the STRAIGHT-line bearing
                                        // to the objective (the arrow itself points to the trail entrance)
    bool directIndicatorPing = true;    // ping (flash periodically) instead of a constant arc
    int directIndicatorEverySecs = 5;   // ping cadence: the full on+off cycle length, seconds
    int directIndicatorShowSecs = 2;    // how long each ping stays visible within the cycle, seconds
    bool directIndicatorChevron = true; // a mini chevron riding the arc, pointing at the objective
    bool wRouteArrowDirect = false;     // dashboard Route Arrow widget: show the direct-objective glow (independent enable)
    bool wRouteArrowIcon = true;        // dashboard Route Arrow widget: show the objective TYPE icon before the title
    bool wRouteArrowChevron = true;     // its own glow style (independent of the in-world arrow's)
    bool wRouteArrowPing = false;
    int wRouteArrowEverySecs = 5;
    int wRouteArrowShowSecs = 2;
    // WvW Map Control widgets. Data fields (shared by both): owned counts / PPT / per-type breakdown.
    bool wvwShowCounts = true; // per-color owned counts (R:n B:n G:n)
    bool wvwShowPpt = true;    // PPT contributed per color (this map)
    bool wvwShowTypes = false; // per-type breakdown (Keep / Tower / Camp / Castle)
    int wvwCurMode = 0;        // WvW Current Map widget: 0 Map+data, 1 Map only, 2 Data only
    int wvwMapEb = 3;          // WvW Maps widget per-map mode: 0 Off, 1 Data, 2 Map, 3 Map+data
    int wvwMapRbl = 3;         //   Red Borderlands
    int wvwMapBbl = 3;         //   Blue Borderlands
    int wvwMapGbl = 3;         //   Green Borderlands
    int wvwMapEotm = 0;        //   Edge of the Mists (off by default)

    // Guide Panel
    bool viewerLocked = false;
    int viewerStyle = 0;              // ViewerStyle (0 Normal / 1 Expanded / 2 None)
    int viewerStylePrev = 0;          // last non-None style, for the tray panel show/hide toggle restore
    int richSecondary = 0;            // RichSecondary (Event Timers)
    int taskChipStyle = 1;            // TaskChipStyle (Icon + Text)
    int taskIconSize = 0;             // TaskIconSize (Auto; saved 2 is Medium)
    int taskIconArt = 1;              // TaskIconArt (Expanded)
    int viewerFontSize = 5;           // index into kFontSizePx (24 px) - current objective + description
    int listFontSize = 5;             // index into kFontSizePx (24 px) - the objectives step list
    bool viewerShowCompleted = false; // viewer/widget objective lists: reveal + uncheck completed objectives
    int stepHoverStyle = 1;           // VIEWER step-hover tooltip: 0 Details, 1 Details & Map (gates the viewer ONLY)
    bool boldText = true;
    int panelOpacity = 40; // %
    int showUpcoming = 0;  // UpcomingList
    bool hideWhenComplete = false;
    bool offerWaypointTravel = true; // prompt to travel when a confirmed waypoint is meaningfully closer to the next step
    float atlasTextScale = 1.0f;     // Atlas tab text/row scale (0.8..1.6); rows + fonts scale together
    float viewerW = 380.f;           // saved guide-panel size (the Step view is user-resizable)
    float viewerH = 480.f;
    float viewerScale = 1.0f;      // uniform guide-viewer zoom: text + icons + padding + window (0.70-1.30, 1 = today)
    float settingsWindowW = 902.f; // saved main Tyrian Codex shell size (resizable: default..200%, aspect-locked)
    float settingsWindowH = 705.f;

    // Wiki reader
    bool wikiConfirmExternal = true; // confirm before leaving to an external link in the browser
    float wikiWindowW = 1280.f;      // saved wiki reader window size (free aspect)
    float wikiWindowH = 760.f;
    float wikiRailW = 300.f; // saved wiki rail (infobox + Contents) width

    // Zone Header banner -- configured INDEPENDENTLY for the guide viewer and the dashboard "Zone Header"
    // widget. pills: 0 Side-by-side / 1 Stacked. bar: 0 Off / 1 Integrated / 2 Below. barText (Below only):
    // 0 On bar / 1 Above / 2 Below / 3 None. Viewer defaults Below+Above (matches the old dungeon bar); widget
    // defaults Integrated. See ui/viewer/ViewerHeader.h ZoneHeaderOpts.
    int zhViewerPills = 1;
    int zhViewerBar = 1;
    int zhViewerBarText = 0;
    int zhWidgetPills = 0;
    int zhWidgetBar = 1;
    int zhWidgetBarText = 1;

    // Routing
    int routeMode = 2; // RouteMode (HybridNearest)
    int pathType = 0;  // PathType (OnFoot)
    int approachMeters = 260;
    bool debugRoute = false;
    bool debugApi = false;              // show the journal reward-fetch diagnostic (Diagnostics section toggle)
    bool debugVistaProbe = false;       // run + show the vista camera-vs-avatar probe (Diagnostics; off by default)
    bool travelOfferDebug = false;      // diagnostic: show why closer-waypoint travel was or was not offered
    bool mapAssistFocusProbe = false;   // diagnostic: invoke native map focus-player once before waypoint pan
    bool mapAssistDebug = true;         // show map-assist probe/readback overlay while the map is open
    bool mapCenterProbe = false;        // diagnostic: log map-open center-vs-player distance + settle timing (chat-link waypoint detection R&D)
    bool mapAssistClickWaypoint = true; // Routing: after the map pans to a confirmed waypoint, click it to open GW2's teleport confirmation (default on)
    // Auto travel: per-source, skip TC's travel-offer card and go straight to open-map + pan (+ click when
    // mapAssistClickWaypoint is on). The in-game teleport confirmation is UNAFFECTED -- the player still confirms.
    // All default OFF (opt-in; these auto-open the world map). The source is classified from the offer's
    // manualChoice + targetKind (see AutoTravelEnabledFor in GuideViewer.cpp).
    bool autoTravelCloserWaypoint = false; // the automatic "closer waypoint to your next objective" pop-up
    bool autoTravelPersonalTarget = false; // the automatic offer after you set a personal (Alt+click) target
    bool autoTravelZoneClick = false;      // the manual "Travel here" click on a zone / recommended row
    bool autoTravelViewerButton = false;   // the viewer's "Show on map" action button (step / travel target)
    // Clicking a waypoint chat link opens the map and auto-pans to the spot; detect that (map opens on the
    // player, then pans far with no drag + not our own pan) and set a guide target there + offer travel.
    bool chatLinkTravel = true;
    // Re-offer travel after a zone change: off (default) keeps a dismissed personal/travel target dismissed when
    // you walk into the next zone (you chose foot); on re-asks each zone you enter.
    bool reaskTravelOnZoneChange = false;

    // Route Trail (in-world)
    bool showTrail = true;
    bool showTrailWhenComplete = false; // keep the IN-WORLD trail on after every zone objective is done; off = hide it with the arrow
    int trailColor = 0;                 // TrailColorChoice (Gold)
    float trailWidth = 1.0f;            // ribbon width in metres   (RouteTrail.Width)
    float trailOpacity = 0.7f;          // master opacity 0..1       (RouteTrail.Opacity)
    float fadeFull = 10.f;              // full opacity within this many metres (RouteTrail.FadeNear)
    float fadeGone = 20.f;              // invisible past this many metres       (RouteTrail.FadeFar)
    bool trailBrighten = true;
    bool trailOffRoute = false;
    int trailStyle = 0;                 // TrailStyle texture: 0 Footprints, 1 Arrows, 2 Dashed, 3 Line, 4 Dots
    float trailAnimSpeed = 0.5f;        // texture flow along the trail (0 = static)
    bool trailFadeAroundChar = true;    // fade the ribbon where it overlaps your character on screen (FadeCenter)
    float trailFadeAroundRadius = 0.25f; // the on-screen fade radius around the character, as a fraction of screen height

    // Map Trails (in-game map / minimap)
    bool showMapTrail = true;
    bool showMapTrailWhenComplete = false; // keep the OPEN-map trail on after every zone objective is done
    bool showMiniMapTrail = true;
    bool showMiniMapTrailWhenComplete = false; // keep the MINIMAP/compass trail on after every zone objective is done
    bool showMapPins = true;                   // objective pins on the open-map + minimap (green, vs orange travel pins)
    // Map / minimap multi-level QoL
    int mapTrailWidth = 1;         // OPEN-map trail width: a px DROPDOWN index (kMapTrailWidthNames; px = index+1, default 2)
    int miniTrailWidth = 1;        // MINIMAP/compass trail width: a px DROPDOWN index (px = index+1, default 2)
    float mapTrailOpacity = 1.0f;  // multiplier on trailOpacity for the OPEN map (1 = same as in-world)
    float miniTrailOpacity = 1.0f; // multiplier on trailOpacity for the MINIMAP / compass
    bool mapAboveBelow = false;    // up/down chevron on a map objective pin that's on a different floor
    bool mapFadeHiLo = false;      // fade map/minimap trail segments far above/below your current height

    // Objective area circles (tinted ObjectiveCircle ring on the map/minimap; hearts sized to their real area)
    int objCircleSurface = 0;                               // ObjCircleSurface: 0 Both, 1 Map only, 2 Minimap only, 3 Off (master)
    bool objCircleKind[5] = {true, true, true, true, true}; // waypoint/poi/vista/hero/heart (parallel to kindEnabled)
    bool objCircleBg = false;                               // faint type-coloured fill inside each ring

    // Notifications (toasts) -- self-rendered pop-ups replacing Nexus GUI_SendAlert (see ui/dashboard/Notify).
    bool notifyEnabled = true;                              // master toggle for on-screen toasts
    int notifyCorner = 1;                                   // 0 TL,1 TC,2 TR,3 Center,4 BL,5 BC,6 BR
    float notifyOffsetX = 0.f;                              // nudge from the anchor, px (+ right)
    float notifyOffsetY = 0.f;                              // nudge from the anchor, px (+ down)
    int notifyTtl = 4;                                      // seconds a toast stays on screen
    int notifyMax = 3;                                      // max stacked toasts
    bool notifyBackground = true;                           // card background (off = HUD text + accent only)
    bool notifyIcons = true;                                // show the type marker/icon
    int notifyFont = 0;                                     // 0 GW2 (Menomonia), 1 stock
    bool notifyKind[5] = {false, false, true, true, false}; // Zone/Travel/Action/Event/Info (parallel to Notify::Kind)

    // Zone Display -- the bold animated Region/Zone/Sector banner shown on entering a map / crossing a sector
    // (see ui/zonedisplay/ZoneDisplay). Names come from the bundled data/sectors.json (offline; SectorData).
    bool zdEnabled = true;         // master toggle
    bool zdOnMapChange = true;     // announce when you enter a new map (big three-tier banner)
    bool zdOnAreaChange = true;    // announce when you cross into a new named area (GW2 API "sector")
    int zdAreaStyle = 0;           // area-crossing layout: 0 One line / 1 Big banner
    int zdAreaEntrance = 0;        // One-line area ENTRANCE (same set as zdAnimStyle): Heralded/Clean/Decode/Slide/Twist
    int zdAreaExit = 3;            // One-line area EXIT (same set as zdExitStyle): Fade/Letter/Dissolve/Crumble/Burn/Slide
    int zdAreaShine = 0;           // One-line area SHINE (same set as zdShineStyle): Letter glint / Thin / Thick / None
    bool zdHideCombat = false;     // suppress while in combat
    bool zdHideCompetitive = true; // suppress in WvW / PvP
    bool zdHideMapOpen = true;     // suppress while the full map is open
    bool zdRequireFocus = true;    // only when the game window has focus
    bool zdMirrorToast = false;    // also raise the gold Zone notification toast
    // Appearance
    bool zdShowRegion = true;                              // the region eyebrow line (top)
    bool zdShowArea = true;                                // the area subtitle line (bottom)
    int zdScale = 150;                                     // overall size scale, percent (50..200; 100 = the clone size)
    int zdWidth = 0;                                       // banner width: 0 Normal / 1 Wide (extra per-letter spacing)
    int zdBackdropStyle = 2;                               // backdrop behind the text: 0 Band / 1 Soft vignette / 2 Per-line glow / 3 None
    int zdBackdropOpacity = 45;                            // backdrop opacity, percent
    int zdVerticalPct = 25;                                // banner anchor: percent of screen height from the top
    unsigned int zdColR = 223, zdColG = 194, zdColB = 149; // hero text color (brightGold); persisted by key
    // Animation
    int zdAnimStyle = 0; // ENTRANCE: 0 Heralded (cascade+glint) / 1 Clean (fade+drift) / 2 Decode (Krytan->Latin in-place reveal) / 3 Slide (sweep in) / 4 Twist (Krytan swipe)
    // Timing is fixed (no panel control) -- these are the tuned durations; Heralded always cascades its hero letters.
    int zdEnterMs = 550;  // enter (cascade/reveal) duration, ms
    int zdHoldMs = 2600;  // full-opacity hold, ms
    int zdExitMs = 700;   // exit (fade/dissolve) duration, ms
    int zdShineStyle = 0; // shine across the hero line: 0 Letter glint / 1 Thin sweep / 2 Thick sweep / 3 None.
                          // Thin/Thick are the GPU sheen shader (auto-fall back to the glint if it can't compile).
    int zdExitStyle = 3;  // exit: 0 Fade+lift / 1 Letter fade / 2 Dissolve (per-letter) / 3 Crumble /
                          // 4 Burn (whole-banner dissolve) / 5 Slide (diagonal sweep)

    // Dashboard (notification center) -- edge-docked handle + slide-out widget panel (see ui/dashboard/Dashboard).
    bool dashEnabled = true;           // show the edge handle
    int dashEdge = 2;                  // 0 Top,1 Bottom,2 Left,3 Right
    float dashPos = 0.5f;              // 0..1 position along the edge
    float dashWidth = 600.f;           // slide-out panel width, px (clamp 300..600; resizable via the corner grip)
    float dashTextScale = 1.50f;       // panel text size multiplier (1.0..1.6); scales all widget text + rows
    bool dashLocked = false;           // lock the handle position + panel resize (handle still toggles open/close)
    bool dashAutoHideHoverOff = false; // collapse the open panel a moment after the cursor leaves it
    bool dashAutoHideClickOff = true;  // collapse the open panel when you click outside it (default on)
    // Per-widget "show map in tooltip" toggles (the Widgets settings section). Default ON (the zone thumbnail is
    // the most useful part of the hover); each objective/waypoint widget reads its own to gate the thumbnail.
    bool wMapTipCurrent = true;
    bool wMapTipZoneWp = true;
    bool wMapTipSearch = true;
    bool wMapTipRecommended = true;
    int quickNotesEditorMode = 0;              // Quick Notes widget: 0 auto (wide inline, narrow popup), 1 popup only
    int quickNotesTextSize = 2;                // index into kFontSizePx (18 px) - Quick Notes preview/editor text
    bool quickNotesRememberEditorSize = false; // remember resized Quick Notes popup editor dimensions
    float quickNotesEditorW = 720.f;           // internal: remembered Quick Notes popup editor width
    float quickNotesEditorH = 520.f;           // internal: remembered Quick Notes popup editor height
    bool quickControlsTileGuide = true;        // Quick Controls widget: show this tile in the control-center grid
    bool quickControlsTileArrow = true;
    bool quickControlsTileTrail = true;
    bool quickControlsTileMarkers = true;
    bool quickControlsTileMapPins = true;
    bool quickControlsTileMapTrail = true;
    bool quickControlsTileMinimap = true;
    bool quickControlsTilePersonal = true;
    UiLayout::Ordered<DashSlot> dashLayout; // per-widget order/show + span + hover-chrome (per-character; owned by the Dashboard family)

    // HUD launcher bar -- every setting lives in Config (searchable/tooltipped + read from ONE place). Per
    // character via the HUD ConfigProfileFamily (owner Hud), which snapshots this hud* slice + hudButtons.
    bool hudEnabled = true; // show the HUD bar for this profile
    int hudEdge = 0;        // 0 Top, 1 Bottom
    float hudOffset = 0.f;  // horizontal nudge from centre (drag position; internal, not a Setting row)
    bool hudLocked = true;  // lock the bar in place (uncheck to drag)
    bool hudHideInCombat = false;
    bool hudHideOnMap = false;
    bool hudLabels = false;               // text label under each icon
    int hudClockPrimary = 0;              // main clock time source (0 Local / 1 Server / 2 Tyrian)
    int hudClockSecondary = 0;            // optional second line (0 None / 1 Local / 2 Server / 3 Tyrian)
    bool hudClock24h = false;             // 24-hour time
    bool hudClockAmpm = true;             // show AM/PM on the 12-hour clock
    bool hudClockShow = true;             // show the centre clock
    bool hudClockTooltip = true;          // show all times on hover
    UiLayout::Ordered<HudBtn> hudButtons; // shown buttons (ordered) + per-button side + removed set

    // Info Panel data bar -- same model. Per character via the Info ConfigProfileFamily (owner Info), which
    // snapshots this info* slice + infoTexts (placement) + infoTextOpts (per-text options).
    bool infoEnabled = true; // show the data bar for this profile
    int infoEdge = 1;        // 0 Top, 1 Bottom
    int infoWidthPct = 100;   // width as a percentage of the screen (100 = full width)
    float infoOffsetX = 0.f;  // horizontal nudge from centered position, px
    float infoOffsetY = 0.f;  // inward nudge from the dock edge, px
    int infoOpacity = 60;    // background %
    float infoTextSize = 24.f;
    int infoBarHeight = 0;   // 0 Auto, else index into the Info Panel bar-height preset list
    bool infoHideInCombat = true;
    bool infoHideOnMap = false;
    UiLayout::Ordered<InfoSlot> infoTexts;                          // shown texts (ordered) + per-text zone + removed set
    std::map<std::string, std::map<std::string, int>> infoTextOpts; // per-text options (textKey -> optKey -> value)

    // Markers (in-world objective icons)
    bool showMarkers = true;                               // master toggle
    bool kindEnabled[5] = {true, true, true, true, false}; // waypoint/poi/vista/hero/heart
    bool markerDungeon = true;
    bool showPersonalMarker = true; // in-world billboard for the personal travel target (orange waypoint)
    float markerSize = 1.8f;        // base world size, metres (WorldMarker.Size, "100%")
    float markerExtraH = 2.0f;      // lift above ground, metres (WorldMarkers.ExtraHeight)
    float markerFadeNear = 30.f;    // WorldMarkers.FadeNear
    float markerFadeFar = 55.f;     // WorldMarkers.FadeFar
    // Context fades + global caps. Defaults = off / preserve today's look. Combat fade is split into
    // independent trail + marker toggles; view distance is already per-type (fadeGone / markerFadeFar).
    bool trailFadeInCombat = true;   // fade the route trail to a low wash while in combat
    bool markerFadeInCombat = false; // fade in-world markers to a low wash while in combat
    bool fadeBetweenCamChar = false; // dim markers that sit between the camera and your character (declutter)
    float maxMarkerOpacity = 1.0f;   // global ceiling on marker opacity (1 = off)
    bool worldAboveBelow = true;     // up/down chevron on a marker whose objective is on a different floor

    // Farming / gathering node overlay (independent visibility toggles, like showMarkers / showMapPins -- a farming
    // RUN does NOT force them; they're left on/off by preference). Listed in Markers > Visibility (in-world) +
    // Map Trails (map pins). Both OFF by default (opt-in; gathering is a heavy overlay).
    bool gatherShow = true;       // show gathering nodes as in-world billboards
    bool gatherOnMap = false;     // show gathering nodes as pins on the fullscreen world map
    bool gatherOnMinimap = false; // show gathering nodes as pins on the minimap / compass
    int gatherCategory = 0;       // Farming-tab Row-1 chip: 0 All / 1 Ore / 2 Wood / 3 Plant / 4 Seafood / 5 Routes (Farm::kCategoryNames)
    // Per-map HIDDEN material types -- the per-material control, surfaced as the Farming tab's Row-2 chips and
    // shared by the overlay AND the active run (absent map or type => shown). Serialized manually (like
    // uiPaneWidths) as { "<mapId>": ["iron", ...] }.
    std::map<uint32_t, std::vector<std::string>> gatherHidden;

    // Is material `mat` shown on `mapId`? (default true; the user hides specific types per map.)
    bool GatherShown(uint32_t mapId, const std::string &mat) const
    {
        auto it = gatherHidden.find(mapId);
        if (it == gatherHidden.end())
            return true;
        for (const std::string &s : it->second)
            if (s == mat)
                return false;
        return true;
    }
    void SetGatherShown(uint32_t mapId, const std::string &mat, bool shown)
    {
        std::vector<std::string> &v = gatherHidden[mapId];
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == mat)
            {
                if (shown)
                {
                    v.erase(v.begin() + i);
                }
                return;
            } // already hidden / unhide
        if (!shown)
            v.push_back(mat); // hide it
        if (v.empty())
            gatherHidden.erase(mapId);
    }

    // Fishing-hole overlay (mirrors the gather overlay; reuses the marker* tunables above). Independent toggles --
    // a fishing run drives only the arrow, not these.
    bool fishShow = false;      // show fishing holes as in-world billboards
    bool fishOnMap = false;     // show fishing holes as pins on the fullscreen world map
    bool fishOnMinimap = false; // show fishing holes as pins on the minimap / compass

    // Map scale fine-tune (false = pure auto 1/Scaling; the multipliers below apply only when custom)
    bool mapScaleCustom = false;
    float mapOpenScale = 1.0f;
    float miniMapScale = 1.0f;

    // First-run onboarding: set true once the Welcome window is saved/skipped (internal key, not a panel row).
    bool onboardedV1 = false;

    // Account / API + keybind buffers (plaintext in settings.json; Nexus has no subtoken vault)
    char apiKey[80] = "";
    char keyToggleGuide[64] = "Not set";
    char keyMarkDone[64] = "Not set";
    char keyBack[64] = "Not set";
    char keyCopyWp[64] = "Not set";
    char keyLoadoutCycle[64] = "Not set"; // cycle to the next loadout (whole-bundle switch)
    char keyLoadout1[64] = "Not set";     // apply loadout slot 1/2/3 directly (by order)
    char keyLoadout2[64] = "Not set";
    char keyLoadout3[64] = "Not set";
    char keyZoneReannounce[64] = "Not set"; // re-show the Zone Display banner for the current map/sector
    char keyToggleMarkers[64] = "Not set";  // toggle in-world markers on/off
    char keyToggleTrail[64] = "Not set";    // toggle the route trail on/off
};
