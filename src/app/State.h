#pragma once
#include "model/Dataset.h"   // Zone, Instance, InstanceMarker
#include "util/Projection.h" // Math::Mat4, Math::Vec3
#include <imgui.h>           // ImVec2
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// The viewer's display MODE, computed once per frame (ComputeViewerMode) and read by BOTH the in-world
// guidance dispatch AND the panel -- a single source of truth.
// Message = not in a playable map; Recommend = off-coverage zone ("next up"); Dungeon = instance
// spot-to-spot; Travel = an Alt+click / "Travel here" target; Complete = every objective done; Step = the
// normal current-objective view. (Farming is a viewer TAB, not a mode -- see ui/viewer/ViewerFarming.)
enum class ViewerMode
{
    Message,
    Recommend,
    Dungeon,
    Travel,
    Complete,
    Step
};

// The content type driving the active route guidance. OpenWorld = the leveling guide; Dungeon = an instance
// spot-to-spot route; Farming = a user-started gathering run. Generalizes the old binary inDungeon so future
// content types slot in. (inDungeon is kept in sync for the existing dungeon code paths.)
enum class ContentType
{
    OpenWorld,
    Dungeon,
    Farming,
    Fishing
};

enum class MapAssistMode
{
    ShowOnly,
    Travel
};
enum class MapAssistTransport
{
    None,
    WndProc,
    SendInput,
    Blocked
};
enum class TargetOverrideKind
{
    None,
    Manual,
    Personal,
    Travel
};
enum class TravelOfferTargetKind
{
    Step,
    Personal,
    ZoneTravel,
    RouteTransition
};

// Best-effort map assist target created by "Show on Map". This is intentionally separate from the
// objective/manual target: manualTarget keeps the guide row selected, while mapAssist can point at the
// nearest waypoint coordinate for map panning/highlighting experiments.
struct MapAssistTarget
{
    bool active = false;
    MapAssistMode mode = MapAssistMode::ShowOnly;
    bool waypointTarget = false;
    bool waypointKnown = false;
    bool panRequested = false;
    bool panDone = false;
    int panAttempts = 0;
    int fineAttempts = 0;
    double nextPanTime = 0.0;

    uint32_t mapId = 0;
    float cx = 0.f, cy = 0.f;
    float panGainX = 0.10f, panGainY = 0.10f; // conservative default; snap-calibrated from the first probe (see panCal*)
    bool panCalX = false, panCalY = false;    // per-axis: has the drag sensitivity been measured yet?
    float lastMapCenterX = 0.f, lastMapCenterY = 0.f;
    float lastRequestedX = 0.f, lastRequestedY = 0.f;
    float lastMovedX = 0.f, lastMovedY = 0.f;
    float lastErrorX = 0.f, lastErrorY = 0.f;
    float lastDragFromX = 0.f, lastDragFromY = 0.f;
    float lastDragToX = 0.f, lastDragToY = 0.f;
    MapAssistTransport lastTransport = MapAssistTransport::None;
    int wndProcDragAttempts = 0;
    int sendInputDragAttempts = 0;
    int zeroMoveReadbacks = 0;
    int consecutiveZeroMoveReadbacks = 0;
    // Runaway detection, measured against the TARGET: distance to it at the previous readback, and how many
    // readbacks in a row have failed to close that distance. Leaving the active zone is normal for a
    // cross-zone pan; genuinely losing the target is not.
    float lastErrorLen = -1.f;
    int divergeReadbacks = 0;
    intptr_t lastWndProcResult = 0;
    uintptr_t lastWndProcHwnd = 0;
    bool lastGameFocus = false;
    bool lastTextboxFocus = false;
    bool lastWantCaptureMouse = false;
    std::string lastWndProcClass;
    std::string lastWndProcTitle;
    bool waitingForReadback = false;
    bool centerConfirmWaiting = false;
    double centerConfirmTime = 0.0;
    bool stepConfirmedAtStart = false;
    bool focusProbeRequested = false;
    bool focusProbeWaiting = false;
    bool focusProbeDone = false;
    bool focusProbeInvoked = false;
    bool mapOpenedSeen = false;
    double focusProbeReadbackTime = 0.0;
    float focusProbeBeforeX = 0.f, focusProbeBeforeY = 0.f;
    float focusProbeAfterX = 0.f, focusProbeAfterY = 0.f;
    float focusProbePlayerX = 0.f, focusProbePlayerY = 0.f;
    std::string panStatus;
    bool waypointClickRequested = false;
    bool waypointClickDone = false;
    double waypointClickTime = 0.0;
    std::string waypointClickStatus;
    std::string stepId;
    std::string waypointLink;
    std::string label;
};

struct WaypointTravelOffer
{
    bool active = false;
    bool openRequested = false;
    bool manualChoice = false;
    double openedAt = 0.0;
    TravelOfferTargetKind targetKind = TravelOfferTargetKind::Step;
    double eligibleSince = 0.0;
    std::string evaluatedStepId;
    uint64_t evaluatedProgressVersion = 0;
    std::string targetStepId;
    std::string targetName;
    uint32_t targetMapId = 0;
    float targetCx = 0.f;
    float targetCy = 0.f;
    std::string waypointStepId;
    std::string waypointName;
    std::string waypointLink;
    std::vector<std::string> waypointChoiceNames;
    std::vector<std::string> waypointChoiceLinks;
    int waypointChoice = 0;
    bool routeChoiceRequired = false;
    std::string routeTransitionId;
    std::string routeTransitionMessage;
    uint32_t waypointMapId = 0;
    float waypointCx = 0.f;
    float waypointCy = 0.f;
    float playerDistance = 0.f;
    float waypointDistance = 0.f;
    float savedDistance = 0.f;
    std::string debugLine;
};

struct TargetSwitchPrompt
{
    bool active = false;
    std::string stepId;
    std::string stepName;
};

// Read-only instrumentation: distance between the reported camera and the avatar, plus how far each moved over
// the last ~2 s. A vista cinematic SHOULD send the camera far from a planted avatar; this probe confirms (in the
// Diagnostics tab) whether MumbleLink actually reports that camera pan before we build the auto-complete on it.
struct VistaProbe
{
    float curDist = 0.f;    // |camPos - avatarPos| this frame
    float peakDist = 0.f;   // max since reset
    double peakAt = 0.0;    // time of the peak (for "x s ago")
    float camMoved2s = 0.f; // camera path length over the last completed ~2 s window
    float avaMoved2s = 0.f; // avatar path length over the last completed ~2 s window
    // Cutscene disambiguation: a vista pan may flip IsGameplay false and/or freeze the link. Record what
    // happened WHILE not-gameplay so a post-hoc read tells viable (camera flew) from dead (link froze / pinned).
    bool gameplayNow = false;    // live: NexusLink->IsGameplay this frame
    uint32_t tickNow = 0;        // live: MumbleLink->UITick (advancing = link alive)
    bool sawNotGameplay = false; // did IsGameplay go false at any point since reset?
    float peakDistNoGp = 0.f;    // max cam-avatar dist seen while NOT gameplay (the cinematic, if it flips)
    bool tickAdvNoGp = false;    // did UITick change while NOT gameplay? (link live during the cutscene)
    uint32_t lastTick = 0;
    // window bookkeeping
    float camAcc = 0.f, avaAcc = 0.f;
    double winStart = 0.0;
    Math::Vec3 lastCam{}, lastAva{};
    bool have = false;
};

// Vista completion detector state. The vista cinematic flips IsGameplay false (and freezes the link), so we
// detect the true->false->true BLIP while standing at an incomplete vista and auto-complete it on return.
struct VistaWatchState
{
    bool prev = false;                // last frame's IsGameplay
    bool have = false;                // prev is valid
    std::string armedStepId;          // the incomplete vista we were within reach of when gameplay last dropped
    uint32_t armedMapId = 0;          // its map (a cross-map load -> different map on return -> no completion)
    float armedX = 0.f, armedZ = 0.f; // its world XZ (must still be near it on return -> not a teleport)

    // Per-map vista cache (rebuilt only when the map changes) so the per-frame path is a tiny loop over the
    // handful of vistas -- no full step scan / coordinate transforms every frame.
    struct CachedVista
    {
        std::string stepId;
        float wx = 0.f, wz = 0.f;
    };
    std::vector<CachedVista> vistas;
    uint32_t cachedMapId = 0;
    bool cacheBuilt = false;
};

// The ONE session baseline (currency + progress), driven by app/SessionTracker and read by the Sessions tab, the
// Session Stats + Session Earnings widgets, and the session data texts -- no per-surface duplicate tracking.
// Updated from the main render path (not a widget draw), so it tracks even with every surface hidden.
// Two independent baselines:
//   * currency (account-wide): `base`/`haveBase`/`startedAt`/`startedEpoch` -- re-baselined by Reset/Save.
//   * progress (per-character): `baseLevel`/`baseObjectives` etc. -- baselined on character entry ONLY (never by
//     Reset/Save). Because levels/objectives are per-character, the old session's final value is gone the instant
//     you switch, so we also track `lastLevel`/`lastObjectives` every active frame for the finalize snapshot.
// `sessionStartedAt` is the OVERALL session timer (entry), distinct from `startedAt` (the resettable currency run).
struct SessionEarnings
{
    bool active = false;
    std::string character;
    // -- currency run (resettable) --
    double startedAt = 0.0;        // ImGui::GetTime() at currency (re)baseline -- the Currencies-scope timer
    long long startedEpoch = 0;    // wall-clock seconds at the currency (re)baseline -- for currency-run dates
    std::map<int, long long> base; // currency id -> wallet value at baseline (captured once the wallet loads)
    bool haveBase = false;         // currency baseline captured yet (waits for AccountData::haveWallet)
    // -- overall session (entry) --
    double sessionStartedAt = 0.0;     // ImGui::GetTime() at character entry -- the overview/progress timer
    long long sessionStartedEpoch = 0; // wall-clock seconds at entry -- for the full-session history date
    // -- progress (per-character; baselined on entry only) --
    int baseLevel = 0;
    bool baseLevelKnown = false; // level may be 0/unknown at entry -> captured on the first known frame
    int baseObjectives = 0;
    int lastLevel = 0; // last in-gameplay value (for the finalize snapshot after a char switch)
    int lastObjectives = 0;
};

// Last computed route direction. Written by the guidance systems alongside the floating GPS arrow and read by
// dashboard widgets that need route status without duplicating objective/travel/dungeon routing logic.
struct GuidanceSnapshot
{
    bool active = false;
    bool arrived = false;
    bool offRoute = false;
    bool dungeon = false;
    bool travel = false;
    float distance = 0.f;
    float speed = 0.f;
    float targetRot = 0.f;  // radians, same screen-space rotation as the floating GPS arrow
    float align = 1.f;      // 1 = ahead, -1 = behind, used for status/tint
    float directRot = 0.f;  // Hybrid: screen rotation to the DIRECT objective (the arrow points at the entrance)
    bool hasDirect = false; // a direct-objective bearing is available this frame (Hybrid, en route)
    std::string caption;
    void *iconTex = nullptr; // the objective TYPE icon (heart/POI/...) or the farming/fishing material icon,
                             // resolved by the guidance update; surfaces draw it before the caption. Null = none.
    double updatedAt = 0.0;
};

// -----------------------------------------------------------------------------------------------------
// GuideState: cross-cutting RUNTIME state -- written by one subsystem and read by others + the panel.
// (The long-lived controllers -- ProgressStore/Travel/Follower/Api/Story* -- are NOT copied in here; App
// owns them and hands each subsystem the narrow ref it needs.) Moved verbatim from entry.cpp's runtime
// globals; field names are the old g_ names minus the prefix (g_Zone -> zone to avoid shadowing the type).
// -----------------------------------------------------------------------------------------------------
struct GuideState
{
    // Active open-world zone
    Zone zone;                // currently ACTIVE zone (copied from App::zones on each map change)
    uint32_t activeMapId = 0; // map id `zone` currently reflects (0 = none/off-route)

    // Dungeon / instance runtime (spot-to-spot). When the current map is a dungeon we run instance guidance
    // instead of the open-world one; the active route's world-coord trail is loaded into `zone` (so the
    // trail + follower are reused) and inDungeon switches the guidance + panel + markers to the instance path.
    bool inDungeon = false;                        // current map is an instance
    Instance *inst = nullptr;                      // the active instance (null off a dungeon map)
    int instRouteIdx = 0;                          // active path index within inst->Routes
    std::vector<const InstanceMarker *> instSteps; // the active route's text instructions, in order
    int instStep = 0;                              // current instruction index (spot-to-spot)
    std::vector<TrailSectionRange> instTrailSections; // active route's explicit .trl sections; empty = legacy continuous
    int instTrailAnchor = -1;                      // player's trail vertex this frame (InstanceGuidance::Localize;
                                                   // -1 = not localized yet) -- shared by the ribbon corridor + arrow
    int instTrailReacquireFrames = 0;              // consecutive frames where local segment projection was genuinely far
    std::map<uint32_t, int> instRouteChoice;       // remembered path per dungeon map id

    // Farming runtime (a gathering run on an open-world map). `farmRunActive` is the ONE shared on/off flag for
    // the run -- the viewer's Farming chip, the dashboard Objectives/Farming toggle widget, and a tray/keybind all
    // read+write it, so every surface stays in sync and it works with the panel hidden. When set (on a farming map,
    // not travelling / in a dungeon) it switches activeContent to Farming: the arrow aims at the nearest enabled,
    // not-yet-gathered node in the selected category, and the dashboard objective widgets follow.
    bool farmRunActive = false;
    bool farmRunComplete = false;                       // every enabled-in-category node gathered this session -> announce-once latch
    ContentType activeContent = ContentType::OpenWorld; // OpenWorld / Dungeon / Farming (derived from farmRunActive)
    Instance *farm = nullptr;                           // the active farming map (App.farming[mapId]); null when not farming
    std::set<const FarmNode *> farmGathered;            // nodes crossed off THIS run (session-only; nodes respawn) -- cleared on (re)entry

    // The current map's farming nodes, prepared ONCE per map change (world + continent coords + FarmNode ptr).
    // Shared by the scattered overlay (in-world + map pins) AND the combined-nearest run. Built when the active
    // map has a bundled farming dataset; cleared otherwise.
    struct ActiveGatherNode
    {
        float X = 0, Y = 0, Z = 0, CX = 0, CY = 0;
        bool HasCont = false;
        const FarmNode *Node = nullptr;
    };
    std::vector<ActiveGatherNode> activeGather;
    uint32_t activeGatherMapId = 0; // map id activeGather was built for (rebuild guard)

    // Fishing runtime (a hole run on a fishing map; mirrors the farming run, and is mutually exclusive with it).
    // `fishRunActive` is the ONE shared on/off flag: when set (on a map with bundled holes, not travelling / in a
    // dungeon) it switches activeContent to Fishing and the arrow aims at the nearest unvisited hole whose kind
    // matches a watched fish. The hole overlay (fishShow) is an INDEPENDENT toggle (a run does not force it on).
    bool fishRunActive = false;
    bool fishRunComplete = false;           // every matching hole visited this session -> announce-once latch
    const FishMap *fishMap = nullptr;       // the active fishing map (App.fishHoles[mapId]); null when not fishing
    std::set<const FishHole *> fishVisited; // holes crossed off THIS run (session-only) -- cleared on (re)entry
    std::set<int> fishWatch;                // tracked fish item ids (FishingTab "Track") -> which hole kinds light up
    std::set<std::string> fishWatchTypes;   // the hole kinds those watched fish map to (computed once/frame; empty = any)

    // The current map's fishing holes, prepared ONCE per map change (world + continent coords + FishHole ptr).
    struct ActiveFishHole
    {
        float X = 0, Y = 0, Z = 0, CX = 0, CY = 0;
        bool HasCont = false;
        const FishHole *Hole = nullptr;
    };
    std::vector<ActiveFishHole> activeFish;
    uint32_t activeFishMapId = 0; // map id activeFish was built for (rebuild guard)

    // Guidance result (current objective), set each frame by guidance
    int curStep = -1;
    float curTravel = 0.f;
    bool curArrived = false;
    bool offRoute = false;             // set by guidance; recolors the trail (warning) when trailOffRoute is on
    GuidanceSnapshot guidanceSnapshot; // same target/rotation/distance the floating GPS arrow uses

    // Targeting / progress context
    std::string currentChar = "default";                    // last-known character name (key for progress)
    SessionEarnings sessionEarnings;                        // the one session baseline (time + currency + levels + objectives)
    std::string manualTarget;                               // clicked checklist row -> overrides auto-advance until done
    MapAssistTarget mapAssist;                              // map-open/pan/highlight target for "Show on Map"
    WaypointTravelOffer travelOffer;                        // step-change prompt for closer confirmed waypoint travel
    TargetSwitchPrompt targetSwitch;                        // row-click confirmation while personal/zone travel is active
    std::map<std::string, double> travelOfferCooldownUntil; // session-only step prompt cooldown timestamps
    std::vector<std::string> undoStack;                     // mark-done undo stack ("Back")
    int charLevel = 0;                                      // character level for zone recommendations (0 = unknown -> all)
    bool warmedCaches = false;                              // first-in-game one-shot done (WarmCaches + WarmAll + no-key nudge); on State so a disable/re-enable re-runs it
    bool phPrewarmed = false;                               // price-history owned-item prewarm one-shot done (same re-enable reasoning)

    ViewerMode viewerMode = ViewerMode::Message;
    bool showSettings = false; // settings window open (set by tray/keybind/arrow-menu; read by the window)
    bool showWiki = false;     // wiki reader window open (set by tray/Hub/deep-links; read by WikiReader)
    int compactTab = 0;        // Compact viewer active tab: 0 Objectives/Recommended, 1 Events, 2 Farming (CompactTab* in ViewerLayout.h)

    std::vector<ImVec2> trailCont; // active trail in CONTINENT coords (MapRenderer::Draw reads this; 1:1 with zone.Trail)

    VistaProbe vistaProbe;      // diagnostic: camera-vs-avatar distance probe (vista cinematic detection)
    VistaWatchState vistaWatch; // vista auto-complete: IsGameplay-blip detector state
};

// -----------------------------------------------------------------------------------------------------
// FrameCtx: read-mostly per-frame camera + viewport, built ONCE in Render's in-world block and handed to
// the in-world renderers (TrailRenderer, Markers, GpsArrow, ...) so none of them recompute the camera.
// Rebuilt every frame; haveCamera is false on frames where MumbleLink/viewport are not yet valid.
// -----------------------------------------------------------------------------------------------------
struct FrameCtx
{
    float W = 0.f, H = 0.f; // viewport size, px
    float dt = 0.f;         // frame delta seconds
    float pxScale = 0.f;    // pixels per (world metre at 1 m) for on-screen size clamps
    float fov = 0.f;
    Math::Mat4 vp; // view-projection
    Math::Vec3 camPos, camFwd, camTop, avatar;
    bool haveCamera = false;
    bool inCombat = false; // MumbleLink combat flag
};
