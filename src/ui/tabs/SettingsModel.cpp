#include "ui/tabs/SettingsModel.h"
#include "ui/SettingsWindow.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "Shared.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/Gw2Ui.h"
#include "ui/dashboard/Dashboard.h"
#include "ui/hud/Hud.h"
#include "ui/infopanel/InfoPanel.h"
#include "ui/profiles/Loadouts.h"
#include "ui/profiles/ConfigProfiles.h" // the one universal profile mechanism (config-slice snapshots)
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "util/Textures.h"
#include "util/ImageCache.h"
#include "util/Dyes.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

const char *kSections[SEC_COUNT] = {
    "General", "GPS Arrow", "Guide Panel", "Routing", "Route Trail", "Map Trails", "Markers", "Notifications", "Zone Display", "Widgets", "Dashboard", "HUD", "Info Panel", "Loadouts", "Wiki", "Account / API", "Keybinds", "Diagnostics"};

static Setting SBool(int sec, const char *k, const char *n, const char *s, bool *v,
                     const char *groupId = nullptr, const char *groupName = nullptr)
{
    Setting t{};
    t.section = sec;
    t.groupId = groupId;
    t.groupName = groupName;
    t.key = k;
    t.name = n;
    t.search = s;
    t.kind = SKind::Bool;
    t.vb = v;
    return t;
}

static Setting SFloat(int sec, const char *k, const char *n, const char *s, float *v, float mn, float mx, const char *fmt,
                      const char *groupId = nullptr, const char *groupName = nullptr)
{
    Setting t{};
    t.section = sec;
    t.groupId = groupId;
    t.groupName = groupName;
    t.key = k;
    t.name = n;
    t.search = s;
    t.kind = SKind::Float;
    t.vf = v;
    t.fmin = mn;
    t.fmax = mx;
    t.ffmt = fmt;
    return t;
}

static Setting SInt(int sec, const char *k, const char *n, const char *s, int *v, int mn, int mx,
                    const char *groupId = nullptr, const char *groupName = nullptr)
{
    Setting t{};
    t.section = sec;
    t.groupId = groupId;
    t.groupName = groupName;
    t.key = k;
    t.name = n;
    t.search = s;
    t.kind = SKind::Int;
    t.vi = v;
    t.imin = mn;
    t.imax = mx;
    return t;
}

static Setting SEnum(int sec, const char *k, const char *n, const char *s, int *v, const char *const *names, int count,
                     const char *groupId = nullptr, const char *groupName = nullptr)
{
    Setting t{};
    t.section = sec;
    t.groupId = groupId;
    t.groupName = groupName;
    t.key = k;
    t.name = n;
    t.search = s;
    t.kind = SKind::Enum;
    t.ve = v;
    t.enames = names;
    t.ecount = count;
    return t;
}

static Setting SKey(int sec, const char *k, const char *n, const char *s, char *buf, size_t sz,
                    const char *groupId = nullptr, const char *groupName = nullptr)
{
    Setting t{};
    t.section = sec;
    t.groupId = groupId;
    t.groupName = groupName;
    t.key = k;
    t.name = n;
    t.search = s;
    t.kind = SKind::Keybind;
    t.vk = buf;
    t.kbuf = sz;
    return t;
}

static Setting SStr(int sec, const char *k, const char *n, const char *s, char *buf, size_t sz,
                    const char *groupId = nullptr, const char *groupName = nullptr, bool secret = false)
{
    Setting t{};
    t.section = sec;
    t.groupId = groupId;
    t.groupName = groupName;
    t.key = k;
    t.name = n;
    t.search = s;
    t.kind = SKind::String;
    t.vs = buf;
    t.sbuf = sz;
    t.secret = secret;
    return t;
}

static const char *kWvwCurModeNames[] = {"Map + data", "Map only", "Data only"};
static const char *kWvwMapModeNames[] = {"Off", "Data only", "Map only", "Map + data"};
static const char *kTopBottomNames[] = {"Top", "Bottom"}; // HUD dock edge
static const char *kUiScaleNames[] = {
    "75%", "80%", "85%", "90%", "95%", "100%", "105%", "110%", "115%", "120%", "125%", "130%", "135%",
    "140%", "145%", "150%", "155%", "160%", "165%", "170%", "175%", "180%", "185%", "190%", "195%", "200%"};
static constexpr float kUiScaleMin = 0.75f;
static constexpr float kUiScaleMax = 2.0f;
static constexpr float kUiScaleStep = 0.05f;
static constexpr int kUiScaleCount = static_cast<int>(sizeof(kUiScaleNames) / sizeof(kUiScaleNames[0]));

static int UiScaleIndex(float value)
{
    const float clamped = std::clamp(value, kUiScaleMin, kUiScaleMax);
    const int index = static_cast<int>(std::lround((clamped - kUiScaleMin) / kUiScaleStep));
    return std::clamp(index, 0, kUiScaleCount - 1);
}

static float UiScaleValue(int index)
{
    index = std::clamp(index, 0, kUiScaleCount - 1);
    return std::round((kUiScaleMin + static_cast<float>(index) * kUiScaleStep) * 100.f) / 100.f;
}

static float SnapUiScale(float value)
{
    return UiScaleValue(UiScaleIndex(value));
}

// Rebuilt fresh each call (by value) rather than cached in a function-static: the addresses now point into
// app.config, which is delete'd + new'd on a hot-reload, so a cached table would dangle. Rebuilding the
// (few-hundred-entry) vector per call is negligible (save/load + once per settings-window frame).
std::vector<Setting> Settings(App &app)
{
    using namespace SettingGroups;
    return {
        // General -- guide-wide behavior, then text/fonts.
        SBool(SEC_GENERAL, "showGuide", "Show guide (panel + arrow)", "guide master show toggle panel arrow on off", &app.config.showGuide, GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "autoAdvance", "Auto-complete on arrival", "auto advance complete arrival objective tick", &app.config.autoAdvance, GeneralBehaviorId, GeneralBehaviorName),
        SFloat(SEC_GENERAL, "arrivalDistance", "Arrival distance (m)", "arrival proximity reach complete distance auto", &app.config.arrival, 5.f, 50.f, "%.0f", GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "showTpPrices", "Show Trading Post prices on items", "trading post tp price buy sell flip margin item tooltip commerce", &app.config.showTpPrices, GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "tpToasts", "Trading Post delivery notifications", "trading post tp delivery pickup toast notify items ready collect", &app.config.tpToasts, GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "craftUseOwnMaterials", "Crafting: use materials I own", "crafting cart calculator use own materials storage subtract shopping list", &app.config.craftUseOwnMaterials, GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "craftSubComponents", "Crafting: craft sub-components", "crafting cart calculator craft sub components recurse intermediates buy", &app.config.craftSubComponents, GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "craftShopByItem", "Crafting: shopping list grouped by item", "crafting cart shopping list flat grouped by item per output materials", &app.config.craftShopByItem, GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "craftStepsByItem", "Crafting: steps grouped by item", "crafting cart steps build order flat grouped by item per output", &app.config.craftStepsByItem, GeneralBehaviorId, GeneralBehaviorName),
        SBool(SEC_GENERAL, "useGw2Font", "Use GW2 font (Menomonia)", "font gw2 menomonia typeface text readable game", &app.config.useGw2Font, GeneralTextId, GeneralTextName),
        SFloat(SEC_GENERAL, "uiScale", "Global UI scale", "global ui scale interface size bigger smaller readable all windows hud dashboard", &app.config.uiScale, 0.75f, 2.0f, "%.2f", GeneralTextId, GeneralTextName),
        SEnum(SEC_GENERAL, "tooltipFontSize", "Tooltip text size", "tooltip hover text size font bigger larger readable", &app.config.tooltipFontSize, kFontSizeNames, kFontSizeCount, GeneralTextId, GeneralTextName),
        SFloat(SEC_GENERAL, "atlasTextScale", "Atlas text scale", "atlas search location database text size scale font bigger larger readable", &app.config.atlasTextScale, 0.8f, 1.6f, "%.2f", GeneralTextId, GeneralTextName),

        // GPS Arrow -- the arrow itself, then the direct-objective glow.
        SBool(SEC_ARROW, "showArrow", "Show GPS arrow", "arrow gps compass hud direction pointer show navigation nav", &app.config.showArrow, ArrowMainId, ArrowMainName),
        SInt(SEC_ARROW, "arrowScale", "Arrow size (px)", "arrow size scale bigger smaller", &app.config.arrowSize, 40, 160, ArrowMainId, ArrowMainName),
        SEnum(SEC_ARROW, "arrowTextSize", "Arrow text size", "arrow text font size distance eta", &app.config.arrowTextSize, kFontSizeNames, kFontSizeCount, ArrowMainId, ArrowMainName),
        SBool(SEC_ARROW, "arrowLocked", "Lock GPS arrow", "arrow lock move drag reposition position pin", &app.config.arrowLocked, ArrowMainId, ArrowMainName),
        SBool(SEC_ARROW, "arrowShowTypeIcon", "Show objective type icon", "arrow type icon heart poi vista hero waypoint material farming show name", &app.config.arrowShowTypeIcon, ArrowMainId, ArrowMainName),
        SBool(SEC_ARROW, "showDirectIndicator", "Hybrid: direct-objective glow", "arrow hybrid direct objective indicator glow arc bearing straight line independent entrance", &app.config.showDirectIndicator, ArrowGlowId, ArrowGlowName),
        SBool(SEC_ARROW, "directIndicatorChevron", "Hybrid glow: chevron tip", "arrow hybrid direct glow chevron tip marker caret pointer arc", &app.config.directIndicatorChevron, ArrowGlowId, ArrowGlowName),
        SBool(SEC_ARROW, "directIndicatorPing", "Hybrid glow: ping instead of constant", "arrow hybrid direct glow ping flash periodic blink intermittent constant", &app.config.directIndicatorPing, ArrowGlowId, ArrowGlowName),
        SInt(SEC_ARROW, "directIndicatorEverySecs", "Hybrid glow: ping every (s)", "arrow hybrid direct glow ping interval every seconds cadence cycle", &app.config.directIndicatorEverySecs, 2, 10, ArrowGlowId, ArrowGlowName),
        SInt(SEC_ARROW, "directIndicatorShowSecs", "Hybrid glow: show for (s)", "arrow hybrid direct glow ping show duration seconds how long visible", &app.config.directIndicatorShowSecs, 1, 5, ArrowGlowId, ArrowGlowName),

        // Guide Panel.
        SBool(SEC_PANEL, "viewerLocked", "Lock guide panel", "panel lock move drag resize viewer", &app.config.viewerLocked, PanelLayoutId, PanelLayoutName),
        SEnum(SEC_PANEL, "viewerStyle", "Viewer style", "panel viewer style compact rich tracker hud normal expanded none hidden hide layout", &app.config.viewerStyle, kViewerStyleNames, kViewerStyleCount, PanelLayoutId, PanelLayoutName),
        SFloat(SEC_PANEL, "viewerScale", "Guide viewer scale", "panel viewer scale zoom size smaller bigger uniform shrink enlarge proportional text icons", &app.config.viewerScale, 0.70f, 1.30f, "%.2f", PanelLayoutId, PanelLayoutName),
        SEnum(SEC_PANEL, "richSecondary", "Expanded viewer secondary content", "expanded rich panel secondary content event timers areas dynamic events nearby objectives trail order", &app.config.richSecondary, kRichSecondaryNames, kRichSecondaryCount, PanelLayoutId, PanelLayoutName),
        SEnum(SEC_PANEL, "zhViewerPills", "Zone header: pill layout", "zone header banner level progress pill side by side stacked layout", &app.config.zhViewerPills, kZhPillsNames, kZhPillsCount, PanelLayoutId, PanelLayoutName),
        SEnum(SEC_PANEL, "zhViewerBar", "Zone header: bar", "zone header banner completion progress bar off integrated below", &app.config.zhViewerBar, kZhBarNames, kZhBarCount, PanelLayoutId, PanelLayoutName),
        SEnum(SEC_PANEL, "zhViewerBarText", "Zone header: bar text", "zone header banner below bar text percent on above below none", &app.config.zhViewerBarText, kZhBarTextNames, kZhBarTextCount, PanelLayoutId, PanelLayoutName),
        SEnum(SEC_PANEL, "taskChipStyle", "Task chip style", "heart task chips icon text labels tasks mode", &app.config.taskChipStyle, kTaskChipStyleNames, kTaskChipStyleCount, PanelObjectiveId, PanelObjectiveName),
        SEnum(SEC_PANEL, "taskIconSize", "Task icon size", "heart task chips icon size small medium large auto", &app.config.taskIconSize, kTaskIconSizeNames, kTaskIconSizeCount, PanelObjectiveId, PanelObjectiveName),
        SEnum(SEC_PANEL, "taskIconArt", "Task icon artwork", "heart task chips icon art simple expanded keyword atlas varied", &app.config.taskIconArt, kTaskIconArtNames, kTaskIconArtCount, PanelObjectiveId, PanelObjectiveName),
        SEnum(SEC_PANEL, "viewerFontSize", "Panel text size", "panel text font size viewer current objective description", &app.config.viewerFontSize, kFontSizeNames, kFontSizeCount, PanelTextId, PanelTextName),
        SEnum(SEC_PANEL, "listFontSize", "Step list text size", "step list checklist objectives text font size", &app.config.listFontSize, kFontSizeNames, kFontSizeCount, PanelTextId, PanelTextName),
        SBool(SEC_PANEL, "boldText", "Bold guide text", "bold panel text weight", &app.config.boldText, PanelTextId, PanelTextName),
        SInt(SEC_PANEL, "panelOpacity", "Background opacity (%)", "panel opacity background darkness alpha viewer transparency transparent see through", &app.config.panelOpacity, 0, 90, PanelTextId, PanelTextName),
        SEnum(SEC_PANEL, "stepHoverStyle", "Step hover style", "viewer step objective hover tooltip details map thumbnail show", &app.config.stepHoverStyle, kStepHoverStyleNames, kStepHoverStyleCount, PanelHoverTravelId, PanelHoverTravelName),
        SEnum(SEC_PANEL, "showUpcoming", "Show upcoming-steps list", "upcoming steps list open world dungeon", &app.config.showUpcoming, kUpcomingNames, 4, PanelHoverTravelId, PanelHoverTravelName),
        SBool(SEC_PANEL, "hideWhenComplete", "Hide panel when complete", "hide panel complete done finished zone", &app.config.hideWhenComplete, PanelHoverTravelId, PanelHoverTravelName),
        SBool(SEC_PANEL, "offerWaypointTravel", "Offer closer waypoint travel", "waypoint travel prompt closer known confirmed step change", &app.config.offerWaypointTravel, PanelHoverTravelId, PanelHoverTravelName),

        // Routing.
        SEnum(SEC_ROUTING, "routeMode", "Routing mode", "route mode trail hybrid direct nearest authored", &app.config.routeMode, kRouteModeNames, 5, RoutingBehaviorId, RoutingBehaviorName),
        SEnum(SEC_ROUTING, "pathType", "Route variant", "path foot mount route variant barefoot mounted tekkit lady elyssa source", &app.config.pathType, kPathTypeNames, 4, RoutingBehaviorId, RoutingBehaviorName),
        SInt(SEC_ROUTING, "approachMeters", "Approach distance (m)", "approach entrance distance hybrid handoff footprints", &app.config.approachMeters, 40, 400, RoutingBehaviorId, RoutingBehaviorName),
        SBool(SEC_ROUTING, "mapAssistClickWaypoint", "Click waypoint after pan", "routing travel map assist click waypoint after pan opens teleport confirmation", &app.config.mapAssistClickWaypoint, RoutingMapAssistId, RoutingMapAssistName),
        SBool(SEC_ROUTING, "autoTravelCloserWaypoint", "Auto-travel: suggested waypoint", "auto travel automatic closer waypoint offer prompt card skip pan", &app.config.autoTravelCloserWaypoint, RoutingAutoTravelId, RoutingAutoTravelName),
        SBool(SEC_ROUTING, "autoTravelPersonalTarget", "Auto-travel: personal target", "auto travel personal target alt click offer prompt card skip pan", &app.config.autoTravelPersonalTarget, RoutingAutoTravelId, RoutingAutoTravelName),
        SBool(SEC_ROUTING, "autoTravelZoneClick", "Auto-travel: Travel-here click", "auto travel zone travel here recommended click prompt card skip waypoint teleport", &app.config.autoTravelZoneClick, RoutingAutoTravelId, RoutingAutoTravelName),
        SBool(SEC_ROUTING, "autoTravelViewerButton", "Auto-travel: viewer Map button", "auto travel viewer show on map button prompt card skip pan click", &app.config.autoTravelViewerButton, RoutingAutoTravelId, RoutingAutoTravelName),
        SBool(SEC_ROUTING, "chatLinkTravel", "Travel to clicked chat waypoints", "chat link waypoint click map pan travel target deliberate", &app.config.chatLinkTravel, RoutingChatZoneId, RoutingChatZoneName),
        SBool(SEC_ROUTING, "reaskTravelOnZoneChange", "Re-ask travel after zone change", "travel offer re-ask re-offer zone change cross border personal target dismissed", &app.config.reaskTravelOnZoneChange, RoutingChatZoneId, RoutingChatZoneName),
        SBool(SEC_DIAG, "debugRoute", "Routing debug overlay", "debug route diagnostic readout cursor aim", &app.config.debugRoute, DiagTogglesId, DiagTogglesName),
        SBool(SEC_DIAG, "debugApi", "Reward-fetch diagnostic", "debug api reward fetch achievement loading diagnostic journal", &app.config.debugApi, DiagTogglesId, DiagTogglesName),
        SBool(SEC_DIAG, "debugVistaProbe", "Vista camera probe", "debug vista camera avatar distance probe cinematic mumble link diagnostic", &app.config.debugVistaProbe, DiagTogglesId, DiagTogglesName),
        SBool(SEC_DIAG, "travelOfferDebug", "Travel offer debug", "debug travel waypoint offer prompt rejected saved current remaining reason", &app.config.travelOfferDebug, DiagTogglesId, DiagTogglesName),
        SBool(SEC_DIAG, "mapAssistFocusProbe", "Map recenter probe", "debug map assist focus player recenter native waypoint pan", &app.config.mapAssistFocusProbe, DiagTogglesId, DiagTogglesName),
        SBool(SEC_DIAG, "mapAssistDebug", "Map assist debug overlay", "debug map assist overlay focus player recenter waypoint pan readback", &app.config.mapAssistDebug, DiagTogglesId, DiagTogglesName),
        SBool(SEC_DIAG, "mapCenterProbe", "Map-center probe (chat-link)", "debug map center player distance chat link waypoint settle timing", &app.config.mapCenterProbe, DiagTogglesId, DiagTogglesName),

        // Route Trail (in-world).
        SBool(SEC_TRAIL, "showTrail", "Show route trail", "trail show route in-world ground ribbon", &app.config.showTrail, TrailAppearanceId, TrailAppearanceName),
        SBool(SEC_TRAIL, "showTrailWhenComplete", "Show trail when complete", "trail show keep visible in-world after complete done finished all objectives nothing to guide", &app.config.showTrailWhenComplete, TrailAppearanceId, TrailAppearanceName),
        SEnum(SEC_TRAIL, "trailStyle", "Trail style", "trail style footprints arrows chevron dashed line dots texture look", &app.config.trailStyle, kTrailStyleNames, 5, TrailAppearanceId, TrailAppearanceName),
        SEnum(SEC_TRAIL, "trailColor", "Trail color", "trail color gold amber cyan green magenta white", &app.config.trailColor, kTrailColorNames, 6, TrailAppearanceId, TrailAppearanceName),
        SFloat(SEC_TRAIL, "trailWidth", "Trail size", "trail route width size scale thickness ribbon", &app.config.trailWidth, 0.3f, 3.0f, "%.2f", TrailAppearanceId, TrailAppearanceName),
        SFloat(SEC_TRAIL, "trailOpacity", "Trail opacity", "trail route opacity transparency alpha solid", &app.config.trailOpacity, 0.f, 1.0f, "%.2f", TrailAppearanceId, TrailAppearanceName),
        SFloat(SEC_TRAIL, "trailAnimSpeed", "Trail flow speed", "trail animate animation flow scroll speed move chevron arrows direction", &app.config.trailAnimSpeed, 0.f, 3.0f, "%.2f", TrailAppearanceId, TrailAppearanceName),
        SFloat(SEC_TRAIL, "fadeStart", "Fade start (m)", "trail fade near start distance close", &app.config.fadeFull, 5.f, 150.f, "%.0f", TrailFadeBehaviorId, TrailFadeBehaviorName),
        SFloat(SEC_TRAIL, "fadeEnd", "Fade end (m)", "trail fade far end gone distance vanish", &app.config.fadeGone, 10.f, 250.f, "%.0f", TrailFadeBehaviorId, TrailFadeBehaviorName),
        SBool(SEC_TRAIL, "trailBrighten", "Brighten trail ahead", "trail brighten ahead dim passed", &app.config.trailBrighten, TrailFadeBehaviorId, TrailFadeBehaviorName),
        SBool(SEC_TRAIL, "trailOffRoute", "Recolor when off-route", "trail off route recolor warning stray", &app.config.trailOffRoute, TrailFadeBehaviorId, TrailFadeBehaviorName),
        SBool(SEC_TRAIL, "trailFadeAroundChar", "Fade trails around character", "trail fade around character player on screen overlap declutter center", &app.config.trailFadeAroundChar, TrailFadeBehaviorId, TrailFadeBehaviorName),
        SFloat(SEC_TRAIL, "trailFadeAroundRadius", "Character fade size", "trail fade around character player size radius screen on-screen", &app.config.trailFadeAroundRadius, 0.05f, 0.40f, "%.2f", TrailFadeBehaviorId, TrailFadeBehaviorName),
        SBool(SEC_TRAIL, "trailFadeInCombat", "Fade trail in combat", "trail fade combat hide while fighting declutter", &app.config.trailFadeInCombat, TrailFadeBehaviorId, TrailFadeBehaviorName),

        // Map Trails (in-game map / minimap).
        SBool(SEC_MAPTRAIL, "showMapTrail", "Show route on map", "map trail fullscreen route", &app.config.showMapTrail, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "showMapTrailWhenComplete", "Show map trail when complete", "map fullscreen trail show keep visible after complete done finished all objectives nothing to guide", &app.config.showMapTrailWhenComplete, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "showMiniMapTrail", "Show route on minimap", "minimap compass trail route", &app.config.showMiniMapTrail, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "showMiniMapTrailWhenComplete", "Show minimap trail when complete", "minimap compass trail show keep visible after complete done finished all objectives nothing to guide", &app.config.showMiniMapTrailWhenComplete, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "showMapPins", "Show objective pins on map", "map minimap pins markers objectives guide steps current", &app.config.showMapPins, MapTrailLinesId, MapTrailLinesName),
        SEnum(SEC_MAPTRAIL, "mapTrailWidth", "Map trail width", "map fullscreen trail line width thickness px", &app.config.mapTrailWidth, kMapTrailWidthNames, 6, MapTrailLinesId, MapTrailLinesName),
        SEnum(SEC_MAPTRAIL, "miniTrailWidth", "Minimap trail width", "minimap compass trail line width thickness px", &app.config.miniTrailWidth, kMapTrailWidthNames, 6, MapTrailLinesId, MapTrailLinesName),
        SFloat(SEC_MAPTRAIL, "mapTrailOpacity", "Map trail opacity", "map fullscreen trail opacity transparency multiplier", &app.config.mapTrailOpacity, 0.1f, 1.0f, "%.2f", MapTrailLinesId, MapTrailLinesName),
        SFloat(SEC_MAPTRAIL, "miniTrailOpacity", "Minimap trail opacity", "minimap compass trail opacity transparency multiplier", &app.config.miniTrailOpacity, 0.1f, 1.0f, "%.2f", MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "mapAboveBelow", "Above/below indicators", "map minimap objective pin above below floor height up down arrow chevron different", &app.config.mapAboveBelow, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "mapFadeHiLo", "Fade high/low segments", "map minimap trail fade high low above below floor height segments declutter vertical multilevel", &app.config.mapFadeHiLo, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "gatherOnMap", "Gathering nodes on map", "gathering node map fullscreen world pins flat surface farming", &app.config.gatherOnMap, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "gatherOnMinimap", "Gathering nodes on minimap", "gathering node minimap compass pins flat surface farming", &app.config.gatherOnMinimap, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "fishOnMap", "Fishing holes on map", "fishing hole map fullscreen world pins flat surface", &app.config.fishOnMap, MapTrailLinesId, MapTrailLinesName),
        SBool(SEC_MAPTRAIL, "fishOnMinimap", "Fishing holes on minimap", "fishing hole minimap compass pins flat surface", &app.config.fishOnMinimap, MapTrailLinesId, MapTrailLinesName),
        SEnum(SEC_MAPTRAIL, "objCircleSurface", "Objective circles", "objective circle area ring map minimap heart surface where show hide off", &app.config.objCircleSurface, kObjCircleSurfaceNames, 4, MapTrailCirclesId, MapTrailCirclesName),
        SBool(SEC_MAPTRAIL, "objCircleWaypoint", "Circles: Waypoints", "objective circle ring waypoint area map type", &app.config.objCircleKind[0], MapTrailCirclesId, MapTrailCirclesName),
        SBool(SEC_MAPTRAIL, "objCirclePoi", "Circles: Points of Interest", "objective circle ring poi point of interest area map type", &app.config.objCircleKind[1], MapTrailCirclesId, MapTrailCirclesName),
        SBool(SEC_MAPTRAIL, "objCircleVista", "Circles: Vistas", "objective circle ring vista area map type", &app.config.objCircleKind[2], MapTrailCirclesId, MapTrailCirclesName),
        SBool(SEC_MAPTRAIL, "objCircleHero", "Circles: Hero Points", "objective circle ring hero point area map type skill challenge", &app.config.objCircleKind[3], MapTrailCirclesId, MapTrailCirclesName),
        SBool(SEC_MAPTRAIL, "objCircleHeart", "Circles: Hearts", "objective circle ring heart renown area map type", &app.config.objCircleKind[4], MapTrailCirclesId, MapTrailCirclesName),
        SBool(SEC_MAPTRAIL, "objCircleBg", "Circle area shading", "objective circle fill shade background faint area inside tint", &app.config.objCircleBg, MapTrailCirclesId, MapTrailCirclesName),

        // Markers (in-world objective icons).
        SBool(SEC_MARKERS, "showMarkers", "Show in-world markers", "markers show master world icons toggle enable hide off", &app.config.showMarkers, MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "markerWaypoint", "Waypoints", "waypoint marker travel teleport", &app.config.kindEnabled[0], MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "markerPoi", "Points of Interest", "poi point of interest landmark marker", &app.config.kindEnabled[1], MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "markerVista", "Vistas", "vista viewpoint marker", &app.config.kindEnabled[2], MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "markerHero", "Hero Points", "hero point skill challenge marker", &app.config.kindEnabled[3], MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "markerHeart", "Hearts", "heart renown task karma marker", &app.config.kindEnabled[4], MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "markerDungeon", "Dungeon markers", "dungeon marker instruction spot stack interact", &app.config.markerDungeon, MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "markerPersonal", "Personal target", "personal target marker waypoint alt click destination in-world", &app.config.showPersonalMarker, MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "gatherShow", "Gathering nodes", "gathering farming nodes overlay ore wood plant material in-world billboards", &app.config.gatherShow, MarkersVisibilityId, MarkersVisibilityName),
        SBool(SEC_MARKERS, "fishShow", "Fishing holes", "fishing hole overlay water lake river coastal in-world billboards", &app.config.fishShow, MarkersVisibilityId, MarkersVisibilityName),
        SFloat(SEC_MARKERS, "markerSize", "Marker size", "marker icon size scale big small", &app.config.markerSize, 0.5f, 4.0f, "%.2f", MarkersAppearanceId, MarkersAppearanceName),
        SFloat(SEC_MARKERS, "markerHeight", "Marker height (m)", "marker height lift above ground elevation float", &app.config.markerExtraH, 0.f, 8.0f, "%.1f", MarkersAppearanceId, MarkersAppearanceName),
        SFloat(SEC_MARKERS, "markerFadeNear", "Marker fade near (m)", "marker fade near start distance close appear", &app.config.markerFadeNear, 5.f, 200.f, "%.0f", MarkersAppearanceId, MarkersAppearanceName),
        SFloat(SEC_MARKERS, "markerFadeFar", "Marker fade far (m)", "marker fade far end distance disappear hide", &app.config.markerFadeFar, 10.f, 400.f, "%.0f", MarkersAppearanceId, MarkersAppearanceName),
        SBool(SEC_MARKERS, "markerFadeInCombat", "Fade markers in combat", "marker fade combat hide while fighting declutter", &app.config.markerFadeInCombat, MarkersAppearanceId, MarkersAppearanceName),
        SBool(SEC_MARKERS, "fadeBetweenCamChar", "Fade between camera & character", "marker fade between camera character declutter occlude", &app.config.fadeBetweenCamChar, MarkersAppearanceId, MarkersAppearanceName),
        SFloat(SEC_MARKERS, "maxMarkerOpacity", "Max marker opacity", "marker max opacity ceiling cap global transparency override", &app.config.maxMarkerOpacity, 0.1f, 1.0f, "%.2f", MarkersAppearanceId, MarkersAppearanceName),
        SBool(SEC_MARKERS, "worldAboveBelow", "Above/below floor arrows", "marker above below floor height level up down arrow chevron different multi", &app.config.worldAboveBelow, MarkersAppearanceId, MarkersAppearanceName),

        // Notifications (toasts) -- the self-rendered pop-ups that replace Nexus's GUI_SendAlert queue.
        SBool(SEC_NOTIFY, "notifyEnabled", "Show notifications", "notify notification toast alert popup show master enable on off", &app.config.notifyEnabled, NotifyDisplayId, NotifyDisplayName),
        SInt(SEC_NOTIFY, "notifyTtl", "Duration (s)", "notify toast duration time seconds dwell how long stay", &app.config.notifyTtl, 1, 15, NotifyDisplayId, NotifyDisplayName),
        SInt(SEC_NOTIFY, "notifyMax", "Max on screen", "notify toast max stack count visible cap", &app.config.notifyMax, 1, 8, NotifyDisplayId, NotifyDisplayName),
        SBool(SEC_NOTIFY, "notifyBackground", "Card background", "notify toast background card panel fill hud minimal", &app.config.notifyBackground, NotifyDisplayId, NotifyDisplayName),
        SBool(SEC_NOTIFY, "notifyIcons", "Show icon", "notify toast icon marker dot type", &app.config.notifyIcons, NotifyDisplayId, NotifyDisplayName),
        SEnum(SEC_NOTIFY, "notifyFont", "Font", "notify toast font menomonia gw2 stock typeface nexus", &app.config.notifyFont, kNotifyFontNames, 2, NotifyDisplayId, NotifyDisplayName),
        SEnum(SEC_NOTIFY, "notifyCorner", "Position", "notify toast position corner top bottom left right center placement", &app.config.notifyCorner, kNotifyCornerNames, kNotifyCornerCount, NotifyPlacementId, NotifyPlacementName),
        SFloat(SEC_NOTIFY, "notifyOffsetX", "Offset X (px)", "notify toast offset x horizontal nudge position", &app.config.notifyOffsetX, -400.f, 400.f, "%.0f", NotifyPlacementId, NotifyPlacementName),
        SFloat(SEC_NOTIFY, "notifyOffsetY", "Offset Y (px)", "notify toast offset y vertical nudge position", &app.config.notifyOffsetY, -400.f, 400.f, "%.0f", NotifyPlacementId, NotifyPlacementName),
        SBool(SEC_NOTIFY, "notifyKindZone", "Type: Zone", "notify toast type zone now guiding announce enter map", &app.config.notifyKind[0], NotifyTypesId, NotifyTypesName),
        SBool(SEC_NOTIFY, "notifyKindTravel", "Type: Travel", "notify toast type travel waypoint teleport copy", &app.config.notifyKind[1], NotifyTypesId, NotifyTypesName),
        SBool(SEC_NOTIFY, "notifyKindAction", "Type: Action", "notify toast type action confirmation copied marked", &app.config.notifyKind[2], NotifyTypesId, NotifyTypesName),
        SBool(SEC_NOTIFY, "notifyKindEvent", "Type: Event", "notify toast type event meta boss timer", &app.config.notifyKind[3], NotifyTypesId, NotifyTypesName),
        SBool(SEC_NOTIFY, "notifyKindInfo", "Type: Info", "notify toast type info system general", &app.config.notifyKind[4], NotifyTypesId, NotifyTypesName),

        SBool(SEC_NOTIFY, "eventRemind10m", "Remind 10 minutes before", "event reminder remind 10 minutes before boss meta timer bell lead warning", &app.config.eventRemind10m, NotifyEventRemindId, NotifyEventRemindName),
        SBool(SEC_NOTIFY, "eventRemind5m", "Remind 5 minutes before", "event reminder remind 5 minutes before boss meta timer bell lead warning", &app.config.eventRemind5m, NotifyEventRemindId, NotifyEventRemindName),
        SBool(SEC_NOTIFY, "eventRemind1m", "Remind 1 minute before", "event reminder remind 1 minute before boss meta timer bell lead warning", &app.config.eventRemind1m, NotifyEventRemindId, NotifyEventRemindName),
        SBool(SEC_NOTIFY, "eventRemind30s", "Remind 30 seconds before", "event reminder remind 30 seconds before boss meta timer bell lead warning", &app.config.eventRemind30s, NotifyEventRemindId, NotifyEventRemindName),
        SBool(SEC_NOTIFY, "eventRemind10s", "Remind 10 seconds before", "event reminder remind 10 seconds before boss meta timer bell lead warning", &app.config.eventRemind10s, NotifyEventRemindId, NotifyEventRemindName),
        SBool(SEC_NOTIFY, "eventRemindCombine", "Combine events starting together", "event reminder combine merge group same time one toast fewer spam", &app.config.eventRemindCombine, NotifyEventRemindId, NotifyEventRemindName),

        // Zone Display -- the animated Region/Zone/Area banner. Four groups: Behaviour (when it shows) /
        // Appearance (how it looks) / Animation (the map-entry banner's motion) / Area (the crossing). Text
        // COLOR (swatch + hex picker, under Appearance) + the live Preview buttons (under Behaviour) are drawn by
        // the custom UI in OptionsTab. Enter/Hold/Exit timing is fixed (locked at the tuned defaults, no row).
        // -- Behaviour: when the banner appears --
        SBool(SEC_ZONEDISPLAY, "zdEnabled", "Show zone display", "zone display banner region sector area name popup show master enable on off", &app.config.zdEnabled, ZdBehaviorId, ZdBehaviorName),
        SBool(SEC_ZONEDISPLAY, "zdOnMapChange", "Announce on map change", "zone display banner announce enter new map zone region show", &app.config.zdOnMapChange, ZdBehaviorId, ZdBehaviorName),
        SBool(SEC_ZONEDISPLAY, "zdOnAreaChange", "Announce on area change", "zone display banner announce cross area sector subzone show", &app.config.zdOnAreaChange, ZdBehaviorId, ZdBehaviorName),
        SBool(SEC_ZONEDISPLAY, "zdHideCombat", "Hide in combat", "zone display banner hide suppress combat fighting", &app.config.zdHideCombat, ZdBehaviorId, ZdBehaviorName),
        SBool(SEC_ZONEDISPLAY, "zdHideCompetitive", "Hide in WvW / PvP", "zone display banner hide suppress competitive wvw pvp", &app.config.zdHideCompetitive, ZdBehaviorId, ZdBehaviorName),
        SBool(SEC_ZONEDISPLAY, "zdHideMapOpen", "Hide while map open", "zone display banner hide suppress full map open world", &app.config.zdHideMapOpen, ZdBehaviorId, ZdBehaviorName),
        SBool(SEC_ZONEDISPLAY, "zdRequireFocus", "Only when focused", "zone display banner only game window focus foreground", &app.config.zdRequireFocus, ZdBehaviorId, ZdBehaviorName),
        SBool(SEC_ZONEDISPLAY, "zdMirrorToast", "Also notify (toast)", "zone display banner mirror notification toast zone lane", &app.config.zdMirrorToast, ZdBehaviorId, ZdBehaviorName),
        // -- Appearance: how the banner looks --
        SBool(SEC_ZONEDISPLAY, "zdShowRegion", "Show region line", "zone display banner region eyebrow top line show", &app.config.zdShowRegion, ZdAppearanceId, ZdAppearanceName),
        SBool(SEC_ZONEDISPLAY, "zdShowArea", "Show area line", "zone display banner area sector subtitle bottom line show", &app.config.zdShowArea, ZdAppearanceId, ZdAppearanceName),
        SInt(SEC_ZONEDISPLAY, "zdScale", "Size (%)", "zone display banner size scale percent bigger smaller", &app.config.zdScale, 50, 200, ZdAppearanceId, ZdAppearanceName),
        SEnum(SEC_ZONEDISPLAY, "zdWidth", "Width", "zone display banner width normal wide letter spacing tracking spread broad", &app.config.zdWidth, kZdWidthNames, kZdWidthCount, ZdAppearanceId, ZdAppearanceName),
        SInt(SEC_ZONEDISPLAY, "zdVerticalPct", "Vertical position (%)", "zone display banner vertical position screen height percent top", &app.config.zdVerticalPct, 5, 80, ZdAppearanceId, ZdAppearanceName),
        SEnum(SEC_ZONEDISPLAY, "zdBackdropStyle", "Backdrop", "zone display banner backdrop band vignette per line glow none dark readability behind background shade box", &app.config.zdBackdropStyle, kZdBackdropNames, kZdBackdropCount, ZdAppearanceId, ZdAppearanceName),
        SInt(SEC_ZONEDISPLAY, "zdBackdropOpacity", "Backdrop opacity (%)", "zone display banner backdrop opacity percent darkness transparency alpha", &app.config.zdBackdropOpacity, 0, 100, ZdAppearanceId, ZdAppearanceName),
        // -- Animation: the map-entry / big-banner motion --
        SEnum(SEC_ZONEDISPLAY, "zdAnimStyle", "Entrance", "zone display banner entrance animation style heralded cascade clean fade decode krytan reveal slide sweep", &app.config.zdAnimStyle, kZdAnimStyleNames, kZdAnimStyleCount, ZdAnimationId, ZdAnimationName),
        SEnum(SEC_ZONEDISPLAY, "zdExitStyle", "Exit style", "zone display banner exit style fade lift drift dissolve crumble burn slide", &app.config.zdExitStyle, kZdExitStyleNames, kZdExitStyleCount, ZdAnimationId, ZdAnimationName),
        SEnum(SEC_ZONEDISPLAY, "zdShineStyle", "Shine", "zone display banner gold shine glint sweep letter thin thick gpu shader none", &app.config.zdShineStyle, kZdShineNames, kZdShineCount, ZdAnimationId, ZdAnimationName),
        // -- Area: the named-area crossing (GW2 API "sector"; one-line style has its own entrance/exit/shine) --
        SEnum(SEC_ZONEDISPLAY, "zdAreaStyle", "Area style", "zone display area sector crossing style one line big banner", &app.config.zdAreaStyle, kZdAreaStyleNames, kZdAreaStyleCount, ZdAreaId, ZdAreaName),
        SEnum(SEC_ZONEDISPLAY, "zdAreaEntrance", "Area entrance", "zone display one line area sector entrance animation heralded clean decode slide twist separate", &app.config.zdAreaEntrance, kZdAnimStyleNames, kZdAnimStyleCount, ZdAreaId, ZdAreaName),
        SEnum(SEC_ZONEDISPLAY, "zdAreaExit", "Area exit", "zone display one line area sector exit animation fade letter dissolve crumble burn slide separate", &app.config.zdAreaExit, kZdExitStyleNames, kZdExitStyleCount, ZdAreaId, ZdAreaName),
        SEnum(SEC_ZONEDISPLAY, "zdAreaShine", "Area shine", "zone display one line area sector shine glint sweep letter thin thick gpu shader none separate", &app.config.zdAreaShine, kZdShineNames, kZdShineCount, ZdAreaId, ZdAreaName),

        // Dashboard (notification center) -- the edge handle + slide-out widget panel. Per-widget show/order/span
        // are drawn after these globals by DrawDashboardSection (registry-driven), not in this scalar table.
        // Widgets section: per-widget CONTENT/behavior (the dashboard's layout/order/dock stays in Dashboard).
        SEnum(SEC_WIDGETS, "zhWidgetPills", "Zone Header: pill layout", "widget zone header banner level progress pill side by side stacked layout", &app.config.zhWidgetPills, kZhPillsNames, kZhPillsCount, WidgetZoneHeaderId, WidgetZoneHeaderName),
        SEnum(SEC_WIDGETS, "zhWidgetBar", "Zone Header: bar", "widget zone header banner completion progress bar off integrated below", &app.config.zhWidgetBar, kZhBarNames, kZhBarCount, WidgetZoneHeaderId, WidgetZoneHeaderName),
        SEnum(SEC_WIDGETS, "zhWidgetBarText", "Zone Header: bar text", "widget zone header banner below bar text percent on above below none", &app.config.zhWidgetBarText, kZhBarTextNames, kZhBarTextCount, WidgetZoneHeaderId, WidgetZoneHeaderName),
        SBool(SEC_WIDGETS, "wRouteArrowDirect", "Direct-objective glow", "widget route arrow hybrid direct objective glow indicator independent", &app.config.wRouteArrowDirect, WidgetRouteArrowId, WidgetRouteArrowName),
        SBool(SEC_WIDGETS, "wRouteArrowIcon", "Show objective type icon", "widget route arrow type icon heart poi vista hero material show before title", &app.config.wRouteArrowIcon, WidgetRouteArrowId, WidgetRouteArrowName),
        SBool(SEC_WIDGETS, "wRouteArrowChevron", "Glow: chevron tip", "widget route arrow hybrid direct glow chevron tip marker", &app.config.wRouteArrowChevron, WidgetRouteArrowId, WidgetRouteArrowName),
        SBool(SEC_WIDGETS, "wRouteArrowPing", "Glow: ping instead of constant", "widget route arrow hybrid direct glow ping flash periodic constant", &app.config.wRouteArrowPing, WidgetRouteArrowId, WidgetRouteArrowName),
        SInt(SEC_WIDGETS, "wRouteArrowEverySecs", "Glow: ping every (s)", "widget route arrow hybrid direct glow ping every interval seconds cadence", &app.config.wRouteArrowEverySecs, 2, 10, WidgetRouteArrowId, WidgetRouteArrowName),
        SInt(SEC_WIDGETS, "wRouteArrowShowSecs", "Glow: show for (s)", "widget route arrow hybrid direct glow ping show duration seconds", &app.config.wRouteArrowShowSecs, 1, 5, WidgetRouteArrowId, WidgetRouteArrowName),
        SBool(SEC_WIDGETS, "wvwShowCounts", "WvW: show owned counts", "wvw widget map control owned objective counts per color red blue green data field", &app.config.wvwShowCounts, WidgetWvwId, WidgetWvwName),
        SBool(SEC_WIDGETS, "wvwShowPpt", "WvW: show PPT", "wvw widget map control points per tick ppt per color data field", &app.config.wvwShowPpt, WidgetWvwId, WidgetWvwName),
        SBool(SEC_WIDGETS, "wvwShowTypes", "WvW: show per-type breakdown", "wvw widget map control keeps towers camps castle per type breakdown data field", &app.config.wvwShowTypes, WidgetWvwId, WidgetWvwName),
        SEnum(SEC_WIDGETS, "wvwCurMode", "WvW Current Map: display", "wvw current map widget display mode map data both only", &app.config.wvwCurMode, kWvwCurModeNames, 3, WidgetWvwId, WidgetWvwName),
        SEnum(SEC_WIDGETS, "wvwMapEb", "WvW Maps: Eternal Battlegrounds", "wvw maps widget eternal battlegrounds eb per map mode off data map both", &app.config.wvwMapEb, kWvwMapModeNames, 4, WidgetWvwId, WidgetWvwName),
        SEnum(SEC_WIDGETS, "wvwMapRbl", "WvW Maps: Red Borderlands", "wvw maps widget red borderlands rbl per map mode off data map both", &app.config.wvwMapRbl, kWvwMapModeNames, 4, WidgetWvwId, WidgetWvwName),
        SEnum(SEC_WIDGETS, "wvwMapBbl", "WvW Maps: Blue Borderlands", "wvw maps widget blue borderlands bbl per map mode off data map both", &app.config.wvwMapBbl, kWvwMapModeNames, 4, WidgetWvwId, WidgetWvwName),
        SEnum(SEC_WIDGETS, "wvwMapGbl", "WvW Maps: Green Borderlands", "wvw maps widget green borderlands gbl per map mode off data map both", &app.config.wvwMapGbl, kWvwMapModeNames, 4, WidgetWvwId, WidgetWvwName),
        SEnum(SEC_WIDGETS, "wvwMapEotm", "WvW Maps: Edge of the Mists", "wvw maps widget edge of the mists eotm per map mode off data map both", &app.config.wvwMapEotm, kWvwMapModeNames, 4, WidgetWvwId, WidgetWvwName),
        SBool(SEC_WIDGETS, "wMapTipCurrent", "Current Objective: map in tooltip", "widget current objective hover tooltip map thumbnail show", &app.config.wMapTipCurrent, WidgetCurrentId, WidgetCurrentName),
        SBool(SEC_WIDGETS, "wMapTipZoneWp", "Zone Waypoints: map in tooltip", "widget zone waypoints hover tooltip map thumbnail show", &app.config.wMapTipZoneWp, WidgetZoneWpId, WidgetZoneWpName),
        SBool(SEC_WIDGETS, "wMapTipSearch", "Search: map in tooltip", "widget search hover tooltip map thumbnail show", &app.config.wMapTipSearch, WidgetSearchId, WidgetSearchName),
        SBool(SEC_WIDGETS, "wMapTipRecommended", "Recommended Zones: map in tooltip", "widget recommended zones hover tooltip map thumbnail show", &app.config.wMapTipRecommended, WidgetRecommendedId, WidgetRecommendedName),
        SEnum(SEC_WIDGETS, "quickNotesEditorMode", "Quick Notes: editor mode", "widget quick notes markdown editor auto popup mode half width", &app.config.quickNotesEditorMode, kQuickNotesEditorModeNames, kQuickNotesEditorModeCount, WidgetQuickNotesId, WidgetQuickNotesName),
        SEnum(SEC_WIDGETS, "quickNotesTextSize", "Quick Notes: text size", "widget quick notes markdown text size font readable", &app.config.quickNotesTextSize, kFontSizeNames, kFontSizeCount, WidgetQuickNotesId, WidgetQuickNotesName),
        SBool(SEC_WIDGETS, "quickNotesRememberEditorSize", "Quick Notes: remember editor size", "widget quick notes markdown popup editor remember size resize dimensions", &app.config.quickNotesRememberEditorSize, WidgetQuickNotesId, WidgetQuickNotesName),
        SBool(SEC_WIDGETS, "quickControlsTileGuide", "Quick Controls: Guide tile", "widget quick controls tile guide visibility show hide control center", &app.config.quickControlsTileGuide, WidgetQuickControlsId, WidgetQuickControlsName),
        SBool(SEC_WIDGETS, "quickControlsTileArrow", "Quick Controls: Arrow tile", "widget quick controls tile arrow gps visibility show hide control center", &app.config.quickControlsTileArrow, WidgetQuickControlsId, WidgetQuickControlsName),
        SBool(SEC_WIDGETS, "quickControlsTileTrail", "Quick Controls: Trail tile", "widget quick controls tile trail route visibility show hide control center", &app.config.quickControlsTileTrail, WidgetQuickControlsId, WidgetQuickControlsName),
        SBool(SEC_WIDGETS, "quickControlsTileMarkers", "Quick Controls: Markers tile", "widget quick controls tile markers visibility show hide control center", &app.config.quickControlsTileMarkers, WidgetQuickControlsId, WidgetQuickControlsName),
        SBool(SEC_WIDGETS, "quickControlsTileMapPins", "Quick Controls: Map Pins tile", "widget quick controls tile map pins visibility show hide control center", &app.config.quickControlsTileMapPins, WidgetQuickControlsId, WidgetQuickControlsName),
        SBool(SEC_WIDGETS, "quickControlsTileMapTrail", "Quick Controls: Map Trail tile", "widget quick controls tile map trail visibility show hide control center", &app.config.quickControlsTileMapTrail, WidgetQuickControlsId, WidgetQuickControlsName),
        SBool(SEC_WIDGETS, "quickControlsTileMinimap", "Quick Controls: Minimap tile", "widget quick controls tile minimap route visibility show hide control center", &app.config.quickControlsTileMinimap, WidgetQuickControlsId, WidgetQuickControlsName),
        SBool(SEC_WIDGETS, "quickControlsTilePersonal", "Quick Controls: Personal tile", "widget quick controls tile personal marker visibility show hide control center", &app.config.quickControlsTilePersonal, WidgetQuickControlsId, WidgetQuickControlsName),

        SBool(SEC_DASHBOARD, "dashEnabled", "Enable dashboard", "dashboard panel notification center handle edge tab enable show", &app.config.dashEnabled, DashboardPanelId, DashboardPanelName),
        SEnum(SEC_DASHBOARD, "dashEdge", "Dock edge", "dashboard edge dock top bottom left right side", &app.config.dashEdge, kDashEdgeNames, 4, DashboardPanelId, DashboardPanelName),
        SFloat(SEC_DASHBOARD, "dashPos", "Position along edge", "dashboard position along edge slide handle place", &app.config.dashPos, 0.f, 1.f, "%.2f", DashboardPanelId, DashboardPanelName),
        SFloat(SEC_DASHBOARD, "dashWidth", "Panel width (px)", "dashboard panel width size wide narrow resize", &app.config.dashWidth, 300.f, 600.f, "%.0f", DashboardPanelId, DashboardPanelName),
        SFloat(SEC_DASHBOARD, "dashTextScale", "Text size", "dashboard text size scale font bigger larger readable", &app.config.dashTextScale, 1.0f, 1.6f, "%.2f", DashboardPanelId, DashboardPanelName),
        SBool(SEC_DASHBOARD, "dashLocked", "Lock position & size", "dashboard lock handle position move drag resize size locked fixed", &app.config.dashLocked, DashboardPanelId, DashboardPanelName),
        SBool(SEC_DASHBOARD, "dashAutoHideHoverOff", "Auto-hide when cursor leaves", "dashboard auto hide collapse close cursor mouse leave hover off", &app.config.dashAutoHideHoverOff, DashboardPanelId, DashboardPanelName),
        SBool(SEC_DASHBOARD, "dashAutoHideClickOff", "Auto-hide on click outside", "dashboard auto hide collapse close click outside off elsewhere", &app.config.dashAutoHideClickOff, DashboardPanelId, DashboardPanelName),
        SBool(SEC_DASHBOARD, "dashAutoOpenHover", "Auto-open on hover", "dashboard auto open hover handle show cursor mouse over", &app.config.dashAutoOpenHover, DashboardPanelId, DashboardPanelName),

        // HUD launcher bar -- config-backed scalars (owned per-character by the HUD ConfigProfileFamily). The
        // button LAYOUT (which/order/side) is structured and edited in HUD::DrawSettings, below these rows.
        SBool(SEC_HUD, "hudEnabled", "Enable HUD bar", "hud launcher bar buttons clock enable show on off toolbar", &app.config.hudEnabled),
        SEnum(SEC_HUD, "hudEdge", "Dock edge", "hud placement edge top bottom position dock screen", &app.config.hudEdge, kTopBottomNames, 2),
        SBool(SEC_HUD, "hudLocked", "Lock position", "hud lock position move drag pin fixed", &app.config.hudLocked),
        SBool(SEC_HUD, "hudHideInCombat", "Hide in combat", "hud hide combat declutter fight", &app.config.hudHideInCombat),
        SBool(SEC_HUD, "hudHideOnMap", "Hide on full map", "hud hide map open fullscreen world", &app.config.hudHideOnMap),
        SBool(SEC_HUD, "hudHideInWvW", "Hide in WvW", "hud hide wvw world versus mists borderlands eternal battlegrounds declutter", &app.config.hudHideInWvW),
        SBool(SEC_HUD, "hudLabels", "Show button labels", "hud button label text under icon name caption", &app.config.hudLabels),
        SBool(SEC_HUD, "hudClockShow", "Show clock", "hud clock time centre center show display", &app.config.hudClockShow),
        SEnum(SEC_HUD, "hudClockPrimary", "Main time", "hud clock main time local server tyrian zone", &app.config.hudClockPrimary, kHudClockPrimaryNames, 3),
        SEnum(SEC_HUD, "hudClockSecondary", "Second line", "hud clock second line time local server tyrian none", &app.config.hudClockSecondary, kHudClockSecondaryNames, 4),
        SBool(SEC_HUD, "hudClock24h", "24-hour time", "hud clock 24 hour time format military", &app.config.hudClock24h),
        SBool(SEC_HUD, "hudClockAmpm", "Show AM/PM", "hud clock am pm meridiem 12 hour", &app.config.hudClockAmpm),
        SBool(SEC_HUD, "hudClockTooltip", "All times on hover", "hud clock hover tooltip all times local server tyrian", &app.config.hudClockTooltip),

        // Info Panel master toggle. Per-bar placement, text layout, and per-text options are structured and edited
        // in InfoPanel::DrawSettings because there are up to five independently configured bars.
        SBool(SEC_INFO, "infoEnabled", "Enable Info Panel", "info panel data bar text hud enable show on off", &app.config.infoEnabled),

        // Wiki -- the in-overlay GW2 Wiki reader window.
        SBool(SEC_WIKI, "wikiConfirmExternal", "Confirm before opening external links", "wiki external link confirm browser leave open safety prompt", &app.config.wikiConfirmExternal),

        // Account / API -- the GW2 API key (additive: enables character level, story auto-check, map names).
        SStr(SEC_API, "apiKey", "GW2 API key", "api key token account authentication scope tokeninfo level story permission permissions arenanet", app.config.apiKey, sizeof(app.config.apiKey), nullptr, nullptr, /*secret*/ true),

        // Keybinds -- window/panel controls, guide actions, loadout switching, then display.
        SKey(SEC_KEYS, "keyToggleSettings", "Toggle Tyrian Codex window", "keybind toggle options settings main window show hide", app.config.keyToggleSettings, sizeof(app.config.keyToggleSettings), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyTogglePanel", "Toggle guide panel only", "keybind toggle guide panel viewer only show hide tray", app.config.keyTogglePanel, sizeof(app.config.keyTogglePanel), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyToggleGuide", "Toggle guide", "keybind toggle guide hotkey show hide", app.config.keyToggleGuide, sizeof(app.config.keyToggleGuide), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyMarkDone", "Skip / mark step done", "keybind mark done skip step complete advance objective", app.config.keyMarkDone, sizeof(app.config.keyMarkDone), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyBack", "Back (undo last)", "keybind back undo previous step", app.config.keyBack, sizeof(app.config.keyBack), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyCopyWaypoint", "Copy nearest waypoint", "keybind copy waypoint chat link travel", app.config.keyCopyWp, sizeof(app.config.keyCopyWp), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyToggleMarkers", "Toggle in-world markers", "keybind toggle markers in-world icons show hide", app.config.keyToggleMarkers, sizeof(app.config.keyToggleMarkers), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyToggleTrail", "Toggle route trail", "keybind toggle trail route ribbon show hide", app.config.keyToggleTrail, sizeof(app.config.keyToggleTrail), KeysGuideId, KeysGuideName),
        SKey(SEC_KEYS, "keyLoadoutCycle", "Cycle loadout", "keybind loadout cycle next switch profile bundle setup", app.config.keyLoadoutCycle, sizeof(app.config.keyLoadoutCycle), KeysLoadoutId, KeysLoadoutName),
        SKey(SEC_KEYS, "keyLoadout1", "Apply loadout 1", "keybind loadout 1 slot apply switch profile bundle", app.config.keyLoadout1, sizeof(app.config.keyLoadout1), KeysLoadoutId, KeysLoadoutName),
        SKey(SEC_KEYS, "keyLoadout2", "Apply loadout 2", "keybind loadout 2 slot apply switch profile bundle", app.config.keyLoadout2, sizeof(app.config.keyLoadout2), KeysLoadoutId, KeysLoadoutName),
        SKey(SEC_KEYS, "keyLoadout3", "Apply loadout 3", "keybind loadout 3 slot apply switch profile bundle", app.config.keyLoadout3, sizeof(app.config.keyLoadout3), KeysLoadoutId, KeysLoadoutName),
        SKey(SEC_KEYS, "keyZoneReannounce", "Re-show zone display", "keybind zone display re-announce region area sector banner show again", app.config.keyZoneReannounce, sizeof(app.config.keyZoneReannounce), KeysDisplayId, KeysDisplayName),
    };
}

// One-line hover description per setting, keyed by the STABLE persistence key (so renaming a label never
// breaks it). Kept separate from the search keywords. Empty -> no tooltip.
static const char *SettingDesc(const char *key)
{
    struct D
    {
        const char *k;
        const char *d;
    };
    static const D kDescs[] = {
        {"showGuide", "Master switch for the entire guide -- the panel, GPS arrow, trail, and in-world markers."},
        {"trailStyle", "The look of the route trail: footprints, flowing arrows, a dashed line, a solid line, or dots."},
        {"trailAnimSpeed", "Scroll the trail texture along its length so it flows toward the objective (0 = static). Most visible on the Arrows / Dashed styles."},
        {"trailFadeAroundChar", "Fade the trail into a soft hole where it overlaps your character on screen (front and back) so it never paints over you. Off in first person."},
        {"trailFadeAroundRadius", "Size of the fade-around-character hole, as a fraction of screen height -- larger clears a bigger area around you. Only when 'Fade trails around character' is on."},
        {"trailFadeInCombat", "Fade the route trail to a faint wash while you're in combat, to declutter the screen during fights."},
        {"markerFadeInCombat", "Fade the in-world markers to a faint wash while you're in combat, to declutter the screen during fights."},
        {"fadeBetweenCamChar", "Dim a marker that sits between the camera and your character (drawing over you), to reduce clutter on a close camera."},
        {"maxMarkerOpacity", "Global ceiling on marker opacity -- lower it to make every in-world marker more transparent at once (1.00 = no cap)."},
        {"worldAboveBelow", "Show an up / down arrow on an objective marker when that objective is on a different floor than you (its height differs by more than a few metres)."},
        {"keyToggleMarkers", "Keybind to show / hide all in-world objective markers at once."},
        {"keyToggleTrail", "Keybind to show / hide the in-world route trail at once."},
        {"showTpPrices", "Show Trading Post buy / sell prices (and the flip margin) on items in the Items / Inventory browsers. Public prices need no key; your own listings need an API key."},
        {"tpToasts", "Pop a toast when Trading Post sales or bought items are ready to collect, so you know to swing by a Trading Post. Needs an API key."},
        {"craftUseOwnMaterials", "In the crafting cart, subtract materials you already own so the shopping list shows only what you still need to buy. Needs an API key for your material storage."},
        {"craftSubComponents", "In the crafting cart, expand intermediate components into their own recipes (craft them) instead of buying the intermediates outright."},
        {"craftShopByItem", "Crafting-cart shopping list layout: group the materials under each output item instead of one flat combined list."},
        {"craftStepsByItem", "Crafting-cart build-order layout: group the craft steps under each output item instead of one flat combined order."},
        {"uiScale", "Global size multiplier for Tyrian Codex screen UI, chosen in 5% steps. Existing panel, dashboard, arrow, tooltip, and banner size settings still fine-tune on top of this."},
        {"atlasTextScale", "Text + row size for the Atlas search / location browser only. Raise it for a more readable list."},
        {"wikiConfirmExternal", "Ask for confirmation before the in-overlay Wiki opens a link that leaves the wiki (an external website in your browser)."},
        {"keyLoadoutCycle", "Keybind to switch to the next loadout (a whole-bundle swap of your saved setups)."},
        {"keyLoadout1", "Keybind to apply loadout slot 1 directly."},
        {"keyLoadout2", "Keybind to apply loadout slot 2 directly."},
        {"keyLoadout3", "Keybind to apply loadout slot 3 directly."},
        {"autoAdvance", "Tick an objective automatically when you reach it (hearts and hero points still need a manual tick)."},
        {"arrivalDistance", "How close, in metres, counts as 'arrived' for auto-complete and the arrow's arrival cue."},
        {"useGw2Font", "Render the UI in the real GW2 Menomonia font instead of the generic Nexus font."},
        {"tooltipFontSize", "Text size for hover tooltips. Titles render a little larger and muted/category lines a little smaller, relative to this."},
        {"showArrow", "Show the floating 3D navigation arrow that points along the route."},
        {"arrowScale", "On-screen size of the GPS arrow, in pixels."},
        {"arrowTextSize", "Font size of the distance / ETA text under the arrow."},
        {"arrowLocked", "Pin the arrow in place; uncheck to drag it somewhere else."},
        {"arrowShowTypeIcon", "Show the objective's type icon (heart / POI / vista / hero / waypoint, or the material icon during a farming run) next to the name on the GPS arrow."},
        {"showDirectIndicator", "In Hybrid routing, draw a glowing arc around the arrow pointing at the straight-line direction to the objective -- independent of the arrow, which heads to the trail entrance. No effect in Direct or Trail modes."},
        {"directIndicatorChevron", "Add a small chevron on the direct-objective glow, its tip pointing at the objective."},
        {"directIndicatorPing", "Flash the direct-objective glow periodically instead of showing it constantly."},
        {"directIndicatorEverySecs", "Ping cadence: the full on+off cycle length, in seconds. Only when 'ping instead of constant' is on."},
        {"directIndicatorShowSecs", "How long each ping stays visible within the cycle, in seconds. Only when 'ping instead of constant' is on."},
        {"wRouteArrowDirect", "On the dashboard Route Arrow widget, show the Hybrid direct-objective glow (independent of the in-world arrow). It has its own chevron / ping / timing below."},
        {"wRouteArrowIcon", "On the dashboard Route Arrow widget, show the objective's type icon (heart / POI / material) before the title."},
        {"wRouteArrowChevron", "Add a chevron on the Route Arrow widget's direct-objective glow, pointing at the objective."},
        {"wRouteArrowPing", "Flash the Route Arrow widget's glow periodically instead of constantly."},
        {"wRouteArrowEverySecs", "Route Arrow glow ping cadence (full on+off cycle), seconds. Only when its ping is on."},
        {"wRouteArrowShowSecs", "How long each Route Arrow glow ping stays visible, seconds. Only when its ping is on."},
        {"viewerLocked", "Pin the guide panel in place; uncheck to move and resize it."},
        {"viewerStyle", "Panel layout: Normal Viewer (slim), Expanded Viewer (larger, scenic header), or None to hide the panel. None hides ONLY the panel -- the GPS arrow, trail, world markers, map pins, travel targets, and the dashboard widgets all keep working."},
        {"viewerScale", "Uniformly zoom the whole guide panel -- text, icons, padding, and the window all shrink/grow together (1.00 = default). Unlike dragging the corner (which only reshapes it), this scales the contents proportionally."},
        {"richSecondary", "What the Expanded viewer's secondary area shows: timed / area events, or nearby objectives."},
        {"zhViewerPills", "Zone header banner: lay the level + progress pills Side-by-side (level left, progress right) or Stacked (level on top). Collapses to Stacked automatically when the panel is narrow."},
        {"zhViewerBar", "Zone header completion bar: Off, Integrated (a slim fill on the banner's bottom edge), or Below (a full bar under the banner)."},
        {"zhViewerBarText", "Where the below-bar shows its count + percent: On bar, Above, Below, or None. Only applies when the completion bar is set to Below."},
        {"zhWidgetPills", "Dashboard Zone Header widget: lay the level + progress pills Side-by-side or Stacked. At half width it reflows (zone name, then region + level pill) and hides the progress pill."},
        {"zhWidgetBar", "Dashboard Zone Header widget completion bar: Off, Integrated (slim fill on the banner edge), or Below (a full bar under the banner)."},
        {"zhWidgetBarText", "Where the widget's below-bar shows its count + percent: On bar, Above, Below, or None. Only applies when the completion bar is set to Below."},
        {"taskChipStyle", "How heart task hints render: text, icon + text, or icons only."},
        {"taskIconSize", "Size of the heart task icons (Auto picks a size per panel style)."},
        {"taskIconArt", "Heart task icon artwork set: simple or expanded."},
        {"viewerFontSize", "Font size of the current-objective title and its description."},
        {"listFontSize", "Font size of the objective checklist rows."},
        {"boldText", "Use a heavier (faux-bold) weight for the panel's headings and objective names."},
        {"panelOpacity", "Darkness of the panel background (0% = nearly transparent)."},
        {"showUpcoming", "Which upcoming-step lists to show: open-world, dungeon, both, or none."},
        {"hideWhenComplete", "Hide the panel once every objective in the zone is done (unless traveling or targeting)."},
        {"offerWaypointTravel", "On a step change, offer to travel to a closer unlocked waypoint when it meaningfully shortens the route."},
        {"routeMode", "How the arrow routes you: follow the authored trail, a hybrid, or head direct / nearest."},
        {"pathType", "Preferred route source and movement type. If it is unavailable on the current map, Tyrian Codex temporarily falls back to another available route without changing this setting."},
        {"approachMeters", "In hybrid modes, how close to a zone entrance before the route hands off to the trail."},
        {"mapAssistClickWaypoint", "After 'Show on map' pans to a confirmed waypoint, click it to open GW2's teleport confirmation (you still confirm the teleport)."},
        {"autoTravelCloserWaypoint", "When the addon suggests a closer unlocked waypoint for your next objective, skip the prompt card and go straight to open-map + pan (and click it if 'Click waypoint after pan' is on). You still confirm the teleport in-game. Needs 'Offer waypoint travel' on."},
        {"autoTravelPersonalTarget", "When you set a personal (Alt+click) travel target, skip the prompt card and go straight to open-map + pan to the nearest unlocked waypoint toward it. You still confirm the teleport. Needs 'Offer waypoint travel' on."},
        {"autoTravelZoneClick", "When you click 'Travel here' on a zone or recommended row, skip the confirm card and start travel immediately (pans + clicks if the destination has a waypoint). You still confirm the teleport."},
        {"autoTravelViewerButton", "When you press the viewer's 'Show on map' action button, skip the choice card and go straight to open-map + pan (and click it if 'Click waypoint after pan' is on). You still confirm the teleport."},
        {"chatLinkTravel", "Clicking a waypoint link in chat opens the map and pans to it; detect that and set a guide target there (you still choose to travel). Only fires for a deliberate click - not our own pans or manual map dragging."},
        {"reaskTravelOnZoneChange", "Re-offer the travel card when you cross into a new zone toward a personal/chat target. Off (default): a target you dismissed stays dismissed when you walk in on foot. On: each zone you enter re-asks."},
        {"debugRoute", "Overlay live routing state (mode, current step, off-route, nearest-trail distance)."},
        {"debugApi", "Show the journal reward-fetch diagnostic line."},
        {"debugVistaProbe", "Run and show the vista camera-vs-avatar probe (used to validate vista cinematic detection)."},
        {"travelOfferDebug", "Show why the closer-waypoint travel offer was accepted, rejected, or is waiting."},
        {"mapAssistFocusProbe", "Diagnostic: invoke GW2's native focus-player once before a waypoint pan and record the result."},
        {"mapAssistDebug", "Show the map-assist pan / readback / focus overlay while the map is open."},
        {"mapCenterProbe", "Log map-open center-vs-player distance + settle timing (R&D for detecting a clicked chat-link waypoint)."},
        {"showTrail", "Draw the route as a ribbon on the ground in the world."},
        {"trailColor", "Colour of the in-world route trail."},
        {"trailWidth", "Width of the trail ribbon, in metres."},
        {"trailOpacity", "Overall opacity of the trail."},
        {"fadeStart", "Distance within which the trail is fully opaque."},
        {"fadeEnd", "Distance past which the trail fades out to invisible."},
        {"trailBrighten", "Brighten the trail ahead of you and dim the part you've already passed."},
        {"trailOffRoute", "Recolour the trail as a warning when you stray off the route."},
        {"showMapTrail", "Draw the route on the fullscreen world map."},
        {"showMiniMapTrail", "Draw the route on the minimap / compass."},
        {"showMapPins", "Show objective pins on the world map and minimap."},
        {"mapTrailWidth", "Line width (px) of the route drawn on the fullscreen world map."},
        {"miniTrailWidth", "Line width (px) of the route drawn on the minimap / compass."},
        {"mapTrailOpacity", "Opacity multiplier for the route on the fullscreen world map (1.00 = same as the in-world trail opacity)."},
        {"miniTrailOpacity", "Opacity multiplier for the route on the minimap / compass (1.00 = same as the in-world trail opacity)."},
        {"mapAboveBelow", "Show an up / down arrow next to a map objective pin when that objective is on a different floor than you."},
        {"mapFadeHiLo", "Fade map / minimap route segments that are far above or below your current height, so a flat map of a vertical zone (e.g. HoT) reads cleaner."},
        {"objCircleSurface", "Tinted area rings around objectives on the map (hearts sized to their real area; other types a default radius). Choose map, minimap, both, or Off."},
        {"objCircleWaypoint", "Show area rings for waypoints."},
        {"objCirclePoi", "Show area rings for points of interest."},
        {"objCircleVista", "Show area rings for vistas."},
        {"objCircleHero", "Show area rings for hero points."},
        {"objCircleHeart", "Show area rings for renown hearts, sized to the heart's actual area."},
        {"objCircleBg", "Fill each ring with a faint, type-coloured shade."},
        {"notifyEnabled", "Master switch for the self-rendered on-screen notifications (replaces the old Nexus alert pop-ups)."},
        {"notifyCorner", "Where the notification stack appears: a screen corner, the top/bottom centre, or the middle."},
        {"notifyOffsetX", "Nudge the whole notification stack horizontally from its anchor (positive = right)."},
        {"notifyOffsetY", "Nudge the whole notification stack vertically from its anchor (positive = down)."},
        {"notifyTtl", "How long, in seconds, each notification stays on screen (hovering it pauses the timer)."},
        {"notifyMax", "Maximum notifications shown stacked at once; older ones drop off as new arrive."},
        {"notifyBackground", "Draw a card background behind each notification; off shows just outlined text + an accent bar (HUD style)."},
        {"notifyIcons", "Show a small type marker/icon on each notification."},
        {"notifyFont", "Font for notifications: the GW2 Menomonia face or the Nexus stock font."},
        {"notifyKindZone", "Show 'Now guiding' notifications when you enter a guided zone."},
        {"notifyKindTravel", "Show travel notifications (waypoint copied, travelling to a destination)."},
        {"notifyKindAction", "Show action confirmations (e.g. 'Copied waypoint link', 'Target set')."},
        {"notifyKindEvent", "Show event notifications (meta / boss timers)."},
        {"notifyKindInfo", "Show general info / system notifications."},
        {"eventRemind10m", "Remind you 10 minutes before an event you have belled starts. Bell an event with the "
                           "bell icon on its row in the Timers tab."},
        {"eventRemind5m", "Remind you 5 minutes before a belled event starts."},
        {"eventRemind1m", "Remind you 1 minute before a belled event starts."},
        {"eventRemind30s", "Remind you 30 seconds before a belled event starts."},
        {"eventRemind10s", "Remind you 10 seconds before a belled event starts - the last call to be standing there."},
        {"eventRemindCombine", "When several belled events start at the same time, show ONE notification listing "
                               "them instead of one per event. Turn off to get a separate toast for each."},
        {"zdEnabled", "Master switch for the animated Region / Zone / Area banner shown when you enter a map or cross into a new area."},
        {"zdOnMapChange", "Announce the big three-tier banner (region, zone name, area) when you enter a new map."},
        {"zdOnAreaChange", "Announce when you cross into a new named area within a map."},
        {"zdHideCombat", "Suppress the banner while you're in combat."},
        {"zdHideCompetitive", "Suppress the banner in WvW and PvP."},
        {"zdHideMapOpen", "Suppress the banner while the full world map is open."},
        {"zdRequireFocus", "Only show the banner when the game window has focus."},
        {"zdMirrorToast", "Also raise a gold 'Zone' notification toast alongside the banner."},
        {"zdShowRegion", "Show the small region 'eyebrow' line above the zone name (e.g. KRYTA)."},
        {"zdShowArea", "Show the area subtitle line below the zone name."},
        {"zdScale", "Overall size of the banner, as a percentage (100% = default)."},
        {"zdWidth", "Banner width: Wide spreads the letters farther apart for a broader, expanded look; Normal is the default spacing."},
        {"zdBackdropStyle", "The darkening behind the text for readability: a soft band, an all-sides vignette, a per-line glow, or none."},
        {"zdBackdropOpacity", "How dark the backdrop is, as a percentage."},
        {"zdVerticalPct", "Vertical position of the banner, as a percentage of screen height from the top."},
        {"zdAnimStyle", "Heralded = letters cascade in with a shine; Clean = the whole name fades + drifts in; Decode = the GW2 'New Krytan translates to readable' reveal, each glyph morphing in place; Slide = a diagonal sweep-in; Twist = the Krytan cross-dissolves on its own spacing for a swiped look. Decode/Slide/Twist pair best with the Burn or Slide exit."},
        {"zdShineStyle", "The shine across the zone name: a crisp per-letter glint, a thin or thick GPU light-sweep, or none."},
        {"zdExitStyle", "How the banner leaves: Fade + lift, a per-letter Fade, Dissolve (each letter erodes by noise), Crumble (letters break into ember cells), Burn (the whole banner burns away in place over ~1.6s with an orange ember front), or Slide ( diagonal sweep, in place)."},
        {"zdAreaStyle", "How an area crossing is shown: a single One-line label, or the full big banner. (Map entry is always the big banner.)"},
        {"zdAreaEntrance", "Entrance animation for the One-line area crossing -- set independently of the main banner's Entrance."},
        {"zdAreaExit", "Exit animation for the One-line area crossing -- set independently of the main banner's Exit."},
        {"zdAreaShine", "The shine across the One-line area label -- set independently of the main banner's Shine."},
        {"keyZoneReannounce", "Keybind to re-show the zone display banner for your current map / area."},
        {"dashEnabled", "Show the dashboard handle on a screen edge; click it to open the notification center / widget panel."},
        {"dashEdge", "Which screen edge the dashboard handle docks to."},
        {"dashPos", "Where along the docked edge the handle sits (drag the handle in-game to move it too)."},
        {"dashWidth", "Width of the slide-out dashboard panel, in pixels."},
        {"dashTextScale", "Text size for everything inside the dashboard panel (scales the widgets' text and row heights together)."},
        {"dashLocked", "Lock the dashboard handle's position and the panel size -- the handle still opens/closes it, but you can't drag it to move or resize the panel by its corner. Unlock to reposition or resize."},
        {"dashAutoHideHoverOff", "Collapse the open dashboard a moment after the cursor moves off it (it reopens when you click the handle)."},
        {"dashAutoHideClickOff", "Collapse the open dashboard when you click anywhere outside it."},
        {"dashAutoOpenHover", "Open the dashboard when the cursor hovers its edge handle -- the inverse of 'Auto-hide when cursor leaves'. Pair the two for a peek-on-hover panel that opens as you mouse over the handle and closes once you mouse away."},
        {"stepHoverStyle", "What the VIEWER shows when you hover an objective: Details (wiki summary + tasks) only, or Details & Map (also a small zone map with the objective circled). Affects the guide viewer only -- Atlas/Checklist always show the map, and each dashboard widget has its own toggle in the Widgets section."},
        {"wMapTipCurrent", "Add the zone map (objective circled) to the Current Objective widget's hover tooltip."},
        {"wMapTipZoneWp", "Add the zone map (waypoint circled) to the Zone Waypoints widget's hover tooltips."},
        {"wMapTipSearch", "Add the zone map (result circled) to the Search widget's hover tooltips."},
        {"wMapTipRecommended", "Add the zone map (all objectives pinned) to the Recommended Zones widget's hover tooltips."},
        {"quickNotesEditorMode", "How the Quick Notes widget edits Markdown: Auto edits inline when wide and in a popup when narrow; Popup only always uses the larger popup editor."},
        {"quickNotesTextSize", "Text size for the Quick Notes Markdown preview and editor."},
        {"quickNotesRememberEditorSize", "Remember the Quick Notes popup editor's resized window size. Off: it opens at the default size each time."},
        {"quickControlsTileGuide", "Show the Guide tile in Quick Controls. This only controls tile visibility, not whether the guide itself is on."},
        {"quickControlsTileArrow", "Show the Arrow tile in Quick Controls. This only controls tile visibility, not whether the GPS arrow itself is on."},
        {"quickControlsTileTrail", "Show the Trail tile in Quick Controls. This only controls tile visibility, not whether the route trail itself is on."},
        {"quickControlsTileMarkers", "Show the Markers tile in Quick Controls. This only controls tile visibility, not whether in-world markers are on."},
        {"quickControlsTileMapPins", "Show the Map Pins tile in Quick Controls. This only controls tile visibility, not whether map pins are on."},
        {"quickControlsTileMapTrail", "Show the Map Trail tile in Quick Controls. This only controls tile visibility, not whether the map trail is on."},
        {"quickControlsTileMinimap", "Show the Minimap tile in Quick Controls. This only controls tile visibility, not whether the minimap trail is on."},
        {"quickControlsTilePersonal", "Show the Personal tile in Quick Controls. This only controls tile visibility, not whether the personal target marker is on."},
        {"showMarkers", "Master toggle for in-world objective marker icons."},
        {"markerWaypoint", "Show in-world markers for waypoints."},
        {"markerPoi", "Show in-world markers for points of interest."},
        {"markerVista", "Show in-world markers for vistas."},
        {"markerHero", "Show in-world markers for hero points."},
        {"markerHeart", "Show in-world markers for renown hearts."},
        {"markerDungeon", "Show in-world dungeon instruction markers (spots, stacks, interacts)."},
        {"markerPersonal", "Show an in-world billboard for your personal / Alt+click travel target."},
        {"gatherShow", "Show gathering nodes as in-world billboards (independent of a farming run -- pick which materials in the Farming tab)."},
        {"gatherOnMap", "Show gathering nodes as pins on the fullscreen world map (independent of a farming run)."},
        {"gatherOnMinimap", "Show gathering nodes as pins on the minimap / compass (independent of a farming run)."},
        {"fishShow", "Show fishing holes as in-world billboards, tinted by hole kind (Track a fish in the Fishing tab to highlight + navigate to its holes)."},
        {"fishOnMap", "Show fishing holes as pins on the fullscreen world map."},
        {"fishOnMinimap", "Show fishing holes as pins on the minimap / compass."},
        {"markerSize", "Base size of in-world marker icons."},
        {"markerHeight", "How high above the ground markers float, in metres."},
        {"markerFadeNear", "Distance within which markers are fully visible."},
        {"markerFadeFar", "Distance past which markers fade out."},
        {"apiKey", "Your GW2 API key. Unlocks account-gated features -- character level, story auto-check, wallet/session earnings, Trading Post, etc."},
        {"keyToggleSettings", "Hotkey to show / hide the Tyrian Codex window."},
        {"keyTogglePanel", "Hotkey to hide or restore only the guide panel while leaving arrow, trail, and markers alone."},
        {"keyToggleGuide", "Hotkey to show / hide the guide."},
        {"keyMarkDone", "Hotkey to tick the current step done / skip it."},
        {"keyBack", "Hotkey to undo the last marked step."},
        {"keyCopyWaypoint", "Hotkey to copy the nearest waypoint's chat link."},
        {"showTrailWhenComplete", "Keep the in-world trail visible after every objective in the zone is done; off hides it along with the arrow."},
        {"showMapTrailWhenComplete", "Keep the route on the full world map visible even after every zone objective is done."},
        {"showMiniMapTrailWhenComplete", "Keep the route on the minimap / compass visible even after every zone objective is done."},
        {"wvwShowCounts", "In the WvW widgets' data table, show the Owned row -- how many objectives each team (red/blue/green) holds."},
        {"wvwShowPpt", "In the WvW widgets' data table, show the PPT row -- points-per-tick each team earns from what it holds."},
        {"wvwShowTypes", "In the WvW widgets' data table, add a per-type breakdown of Keeps / Towers / Camps / Castle held by each team."},
        {"wvwCurMode", "What the WvW Current Map widget shows for the map you are in: Map + data, Map only, or Data only."},
        {"wvwMapEb", "How the WvW Maps widget lists Eternal Battlegrounds: Off, Data, Map, or Map + data."},
        {"wvwMapRbl", "How the WvW Maps widget lists the Red Borderlands: Off, Data, Map, or Map + data."},
        {"wvwMapBbl", "How the WvW Maps widget lists the Blue Borderlands: Off, Data, Map, or Map + data."},
        {"wvwMapGbl", "How the WvW Maps widget lists the Green Borderlands: Off, Data, Map, or Map + data."},
        {"wvwMapEotm", "How the WvW Maps widget lists Edge of the Mists: Off, Data, Map, or Map + data (off by default)."},
        // HUD launcher bar (per-character profile)
        {"hudEnabled", "Show the HUD launcher bar (the icon bar with a centre clock) for this profile."},
        {"hudEdge", "Dock the HUD bar to the top or bottom edge of the screen."},
        {"hudLocked", "Lock the HUD bar in place; uncheck to drag it anywhere on screen by grabbing any part of "
                      "it. Use Reset position to put it back at its docked edge."},
        {"hudHideInCombat", "Hide the HUD bar while you are in combat."},
        {"hudHideOnMap", "Hide the HUD bar while the full map is open."},
        {"hudHideInWvW", "Hide the HUD bar on WvW match maps (Eternal Battlegrounds, the borderlands, Obsidian "
                         "Sanctum, Edge of the Mists). The WvW Lounge is not affected."},
        {"hudLabels", "Show a text label under each HUD button icon."},
        {"hudClockShow", "Show the clock in the centre of the HUD bar."},
        {"hudClockPrimary", "Which time the HUD clock's main line shows: Local, Server, or Tyrian."},
        {"hudClockSecondary", "An optional second time line under the main HUD clock (or None)."},
        {"hudClock24h", "Show the HUD clock in 24-hour time instead of 12-hour."},
        {"hudClockAmpm", "Show the AM/PM suffix on the 12-hour HUD clock."},
        {"hudClockTooltip", "Hovering the HUD clock shows all three time zones (local, server, Tyrian) at once."},
        // Info Panel data bar (per-character profile)
        {"infoEnabled", "Master toggle for all Info Panel bars in this profile. Individual bars can also be enabled or disabled on the Info Panel layout page."},
    };
    if (!key)
        return "";
    for (const D &e : kDescs)
        if (std::strcmp(e.k, key) == 0)
            return e.d;
    return "";
}

static bool IsAssignedKeybind(const char *bind)
{
    return bind && bind[0] && std::strcmp(bind, "Not set") != 0 && std::strcmp(bind, "(null)") != 0;
}

static bool SameKeybindCombo(const char *a, const char *b)
{
    return IsAssignedKeybind(a) && IsAssignedKeybind(b) && _stricmp(a, b) == 0;
}

bool NormalizeKeybinds(App &app, const char *preferredKey)
{
    std::vector<Setting> rows = Settings(app);
    bool changed = false;

    const Setting *preferred = nullptr;
    if (preferredKey && *preferredKey)
        for (const Setting &s : rows)
            if (s.kind == SKind::Keybind && s.key && std::strcmp(s.key, preferredKey) == 0)
            {
                preferred = &s;
                break;
            }

    if (preferred && IsAssignedKeybind(preferred->vk))
    {
        for (const Setting &s : rows)
        {
            if (s.kind != SKind::Keybind || !s.vk || !s.key || std::strcmp(s.key, preferred->key) == 0)
                continue;
            if (SameKeybindCombo(s.vk, preferred->vk))
            {
                std::snprintf(s.vk, s.kbuf, "(null)");
                changed = true;
            }
        }
    }

    // Clean up old persisted duplicates too. First surviving row wins unless `preferredKey` already claimed it.
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const Setting &keep = rows[i];
        if (keep.kind != SKind::Keybind || !IsAssignedKeybind(keep.vk))
            continue;
        for (size_t j = i + 1; j < rows.size(); ++j)
        {
            const Setting &dupe = rows[j];
            if (dupe.kind != SKind::Keybind || !dupe.vk || !SameKeybindCombo(keep.vk, dupe.vk))
                continue;
            std::snprintf(dupe.vk, dupe.kbuf, "(null)");
            changed = true;
        }
    }

    return changed;
}

// Render one setting with the right GW2 control for its type. Non-bool/keybind rows put the name on the
// LEFT and align the control at a column, a "##" id suppresses the control's
// own label so the name we drew is the only one. The whole row is grouped so a hover shows its description.
bool DrawSetting(const Setting &s, float labelCol) // returns true the frame the value changes (drives the auto-save)
{
    char id[96];
    std::snprintf(id, sizeof(id), "##%s", s.name);
    bool changed = false;
    const float ui = Gw2Ui::GlobalScale();
    const float rowW = std::max(1.f, Gw2Ui::CardInnerWidth());
    const float minControlW = 170.f * ui;
    auto placeControl = [&]()
    {
        if (rowW - labelCol >= minControlW)
            ImGui::SameLine(labelCol);
        else
            ImGui::Dummy(ImVec2(0.f, 2.f * ui));
    };
    auto fitControlW = [&](float logicalW)
    {
        return std::max(1.f, std::min(logicalW * ui, ImGui::GetContentRegionAvail().x));
    };
    ImGui::BeginGroup();
    switch (s.kind)
    {
    case SKind::Bool:
        changed = Gw2Ui::Checkbox(s.name, s.vb);
        break;
    case SKind::Float:
        Gw2Ui::Label(s.name);
        placeControl();
        if (s.vf && s.key && std::strcmp(s.key, "uiScale") == 0)
        {
            int selected = UiScaleIndex(*s.vf);
            const float snapped = UiScaleValue(selected);
            if (std::fabs(*s.vf - snapped) > 0.001f)
            {
                *s.vf = snapped;
                changed = true;
            }
            if (Gw2Ui::DropdownPx(id, kUiScaleNames, kUiScaleCount, &selected, fitControlW(150.f)))
            {
                *s.vf = UiScaleValue(selected);
                changed = true;
            }
        }
        else
            changed = Gw2Ui::Slider(id, s.vf, s.fmin, s.fmax, s.ffmt);
        break;
    case SKind::Int:
        Gw2Ui::Label(s.name);
        placeControl();
        changed = Gw2Ui::SliderInt(id, s.vi, s.imin, s.imax);
        break;
    case SKind::Enum:
        Gw2Ui::Label(s.name);
        placeControl();
        changed = Gw2Ui::DropdownPx(id, s.enames, s.ecount, s.ve, fitControlW(240.f));
        break;
    case SKind::Keybind:
    {
        const float widthPx = std::min(540.f * ui, rowW);
        const float namePx = std::min(230.f * ui, widthPx * 0.43f);
        changed = Gw2Ui::KeybindAssigner(s.name, s.vk, s.kbuf, Gw2Ui::Unscaled(widthPx), Gw2Ui::Unscaled(namePx));
        break;
    }
    case SKind::String:
        Gw2Ui::Label(s.name);
        placeControl();
        if (s.secret)
        {
            // Masked by default (hidden) so a key never shows on stream; an eye toggle reveals it to verify.
            // Reveal state is per-key UI state (resets to hidden each session).
            static std::map<std::string, bool> s_reveal;
            changed = Gw2Ui::TextBoxSecret(id, s.vs, s.sbuf, fitControlW(240.f), &s_reveal[s.key]);
        }
        else
            changed = Gw2Ui::TextBox(id, s.vs, s.sbuf, fitControlW(240.f));
        break;
    }
    ImGui::EndGroup();
    if (const char *d = SettingDesc(s.key); d && *d && ImGui::IsItemHovered())
    {
        Gw2Ui::TooltipBegin();
        Gw2Ui::TooltipTitle(s.name);
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipText(d);
        Gw2Ui::TooltipEnd();
    }
    return changed;
}

static bool EnsureQuickControlsTiles(App &app)
{
    if (app.config.quickControlsTileGuide || app.config.quickControlsTileArrow || app.config.quickControlsTileTrail || app.config.quickControlsTileMarkers || app.config.quickControlsTileMapPins || app.config.quickControlsTileMapTrail || app.config.quickControlsTileMinimap || app.config.quickControlsTilePersonal)
        return false;

    app.config.quickControlsTileGuide = true;
    app.config.quickControlsTileArrow = true;
    app.config.quickControlsTileTrail = true;
    app.config.quickControlsTileMarkers = true;
    return true;
}

void SaveSettings(App &app)
{
    if (app.settingsPath.empty())
        return;
    EnsureQuickControlsTiles(app);
    NormalizeKeybinds(app);
    nlohmann::json j = nlohmann::json::object();
    for (const Setting &s : Settings(app))
    {
        if (ConfigProfiles::OwnerOfSetting(s) != ConfigProfiles::Owner::Global)
            continue; // profiled rows live in their family store, not flat
        switch (s.kind)
        {
        case SKind::Bool:
            j[s.key] = *s.vb;
            break;
        case SKind::Float:
            j[s.key] = *s.vf;
            break;
        case SKind::Int:
            j[s.key] = *s.vi;
            break;
        case SKind::Enum:
            j[s.key] = *s.ve;
            break;
        case SKind::Keybind:
            j[s.key] = std::string(s.vk);
            break;
        case SKind::String:
            j[s.key] = std::string(s.vs);
            break;
        }
    }
    j["arrowPosX"] = app.config.arrowPosX; // internal (not a table setting): saved arrow drag position
    j["arrowPosY"] = app.config.arrowPosY;
    j["storyTrack"] = app.config.storyTrack; // internal: the release the story suggestion is pinned to ("" = auto)
    j["viewerW"] = app.config.viewerW; // internal: saved guide-panel size (the Step view is user-resizable)
    j["viewerH"] = app.config.viewerH;
    j["settingsWindowW"] = app.config.settingsWindowW; // internal: saved main shell size (resizable, aspect-locked)
    j["settingsWindowH"] = app.config.settingsWindowH;
    j["wikiWindowW"] = app.config.wikiWindowW; // internal: saved wiki reader window size (free aspect)
    j["wikiWindowH"] = app.config.wikiWindowH;
    j["wikiRailW"] = app.config.wikiRailW;             // internal: saved wiki rail (infobox + Contents) width
    j["viewerStylePrev"] = app.config.viewerStylePrev; // internal: last non-None panel style (tray icon / Viewer>None restore)
    j["itemsView"] = app.config.itemsView;             // internal: Items tab grid/list view
    {
        nlohmann::json invGrid = nlohmann::json::object();
        for (const auto &kv : app.config.inventorySingleGridBySource)
            invGrid[kv.first] = kv.second;
        j["inventorySingleGridBySource"] = std::move(invGrid); // internal: Inventory tab per-source layout toggle
    }
    {
        nlohmann::json pw = nlohmann::json::object();
        for (const auto &kv : app.config.uiPaneWidths)
            pw[kv.first] = kv.second;
        j["uiPaneWidths"] = std::move(pw); // internal: resizable rail/column widths (Gw2Ui::VSplitter)
    }
    {
        nlohmann::json gh = nlohmann::json::object(); // per-map HIDDEN gathering types (the shared overlay+run filter)
        for (const auto &kv : app.config.gatherHidden)
            gh[std::to_string(kv.first)] = kv.second;
        j["gatherHidden"] = std::move(gh);
    }
    j["gatherCategory"] = app.config.gatherCategory;       // Farming tab Row-1 chip (remembered across sessions)
    j["quickNotesEditorW"] = app.config.quickNotesEditorW; // internal: remembered Quick Notes popup editor size
    j["quickNotesEditorH"] = app.config.quickNotesEditorH;
    j["zdColR"] = app.config.zdColR; // internal (custom swatch/hex block, not a table row): Zone Display text color
    j["zdColG"] = app.config.zdColG;
    j["zdColB"] = app.config.zdColB;
    j["onboardedV1"] = app.config.onboardedV1;       // internal: first-run Welcome window shown once (no panel row)
    j["mapScaleCustom"] = app.config.mapScaleCustom; // internal: opt-in to the custom fine-tune multipliers (else pure auto)
    j["mapMultOpen"] = app.config.mapOpenScale;      // internal: open-map fine-tune multiplier (1.0 = auto). NEW key so the old
    j["mapMultMini"] = app.config.miniMapScale;      // internal: minimap fine-tune multiplier. "mapOpenScale"/"miniMapScale" (old
                                                     //           absolute factors) are deliberately not read into these multipliers.
    Loadouts::Serialize(app, j);          // may promote referenced family profiles for shared loadouts before profile stores write
    ConfigProfiles::SerializeAll(app, j); // the 4 families (General/HUD/Info/Dashboard) -- ONE universal mechanism, each a config slice
    Json::WriteAtomic(app.settingsPath, j.dump(2));
}

void LoadSettings(App &app)
{
    if (app.settingsPath.empty())
        return;
    std::ifstream f(app.settingsPath);
    if (!f)
        return;
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return;
    }
    if (!j.is_object())
        return;
    for (const Setting &s : Settings(app))
    {
        if (ConfigProfiles::OwnerOfSetting(s) != ConfigProfiles::Owner::Global)
            continue; // profiled rows are restored by their family store
        auto it = j.find(s.key);
        if (it == j.end())
            continue;
        try
        {
            switch (s.kind)
            {
            case SKind::Bool:
                if (it->is_boolean())
                    *s.vb = it->get<bool>();
                break;
            case SKind::Float:
                if (it->is_number())
                    *s.vf = it->get<float>();
                break;
            case SKind::Int:
                if (it->is_number_integer())
                    *s.vi = it->get<int>();
                break;
            case SKind::Enum:
                if (it->is_number_integer())
                    *s.ve = it->get<int>();
                break;
            case SKind::Keybind:
                if (it->is_string())
                    std::snprintf(s.vk, s.kbuf, "%s", it->get<std::string>().c_str());
                break;
            case SKind::String:
                if (it->is_string())
                    std::snprintf(s.vs, s.sbuf, "%s", it->get<std::string>().c_str());
                break;
            }
        }
        catch (...)
        { /* malformed value -> keep the default */
        }
    }
    app.config.uiScale = SnapUiScale(app.config.uiScale);
    if (auto it = j.find("arrowPosX"); it != j.end() && it->is_number())
        app.config.arrowPosX = it->get<float>();
    if (auto it = j.find("arrowPosY"); it != j.end() && it->is_number())
        app.config.arrowPosY = it->get<float>();
    if (auto it = j.find("storyTrack"); it != j.end() && it->is_string())
        app.config.storyTrack = it->get<std::string>();
    if (auto it = j.find("viewerW"); it != j.end() && it->is_number())
        app.config.viewerW = it->get<float>();
    if (auto it = j.find("viewerH"); it != j.end() && it->is_number())
        app.config.viewerH = it->get<float>();
    // Main shell size: clamp to [default .. 2x] and re-derive height from the locked aspect so a corrupt
    // value (or a future default change) can never wedge the window off-screen or off-aspect.
    if (auto it = j.find("settingsWindowW"); it != j.end() && it->is_number())
    {
        constexpr float kDefW = 902.f, kAspect = 902.f / 705.f;
        float w = std::clamp(it->get<float>(), kDefW, kDefW * 2.f);
        app.config.settingsWindowW = w;
        app.config.settingsWindowH = w / kAspect;
    }
    // Wiki reader geometry is free-aspect (min ~720x520); clamp defensively against a corrupt value.
    if (auto it = j.find("wikiWindowW"); it != j.end() && it->is_number())
        app.config.wikiWindowW = std::clamp(it->get<float>(), 720.f, 4000.f);
    if (auto it = j.find("wikiWindowH"); it != j.end() && it->is_number())
        app.config.wikiWindowH = std::clamp(it->get<float>(), 520.f, 4000.f);
    if (auto it = j.find("wikiRailW"); it != j.end() && it->is_number())
        app.config.wikiRailW = std::clamp(it->get<float>(), 250.f, 1200.f);
    if (auto it = j.find("viewerStylePrev"); it != j.end() && it->is_number())
        app.config.viewerStylePrev = it->get<int>();
    if (auto it = j.find("itemsView"); it != j.end() && it->is_number_integer())
        app.config.itemsView = it->get<int>();
    if (auto it = j.find("inventorySingleGridBySource"); it != j.end() && it->is_object())
    {
        app.config.inventorySingleGridBySource.clear();
        for (auto kv = it->begin(); kv != it->end(); ++kv)
            if (kv.value().is_boolean())
                app.config.inventorySingleGridBySource[kv.key()] = kv.value().get<bool>();
    }
    if (auto it = j.find("uiPaneWidths"); it != j.end() && it->is_object())
    {
        app.config.uiPaneWidths.clear();
        for (auto kv = it->begin(); kv != it->end(); ++kv)
            if (kv.value().is_number())
                app.config.uiPaneWidths[kv.key()] = kv.value().get<float>();
    }
    if (auto it = j.find("gatherHidden"); it != j.end() && it->is_object())
    {
        app.config.gatherHidden.clear();
        for (auto kv = it->begin(); kv != it->end(); ++kv)
            if (kv.value().is_array())
            {
                std::vector<std::string> v;
                for (const auto &e : kv.value())
                    if (e.is_string())
                        v.push_back(e.get<std::string>());
                if (!v.empty())
                    app.config.gatherHidden[(uint32_t)std::strtoul(kv.key().c_str(), nullptr, 10)] = std::move(v);
            }
    }
    if (auto it = j.find("gatherCategory"); it != j.end() && it->is_number_integer())
        app.config.gatherCategory = it->get<int>();
    if (auto it = j.find("quickNotesEditorW"); it != j.end() && it->is_number())
        app.config.quickNotesEditorW = it->get<float>();
    if (auto it = j.find("quickNotesEditorH"); it != j.end() && it->is_number())
        app.config.quickNotesEditorH = it->get<float>();
    if (auto it = j.find("zdColR"); it != j.end() && it->is_number_unsigned())
        app.config.zdColR = it->get<unsigned int>();
    if (auto it = j.find("zdColG"); it != j.end() && it->is_number_unsigned())
        app.config.zdColG = it->get<unsigned int>();
    if (auto it = j.find("zdColB"); it != j.end() && it->is_number_unsigned())
        app.config.zdColB = it->get<unsigned int>();
    if (auto it = j.find("onboardedV1"); it != j.end() && it->is_boolean())
        app.config.onboardedV1 = it->get<bool>();
    if (auto it = j.find("mapScaleCustom"); it != j.end() && it->is_boolean())
        app.config.mapScaleCustom = it->get<bool>();
    if (auto it = j.find("mapMultOpen"); it != j.end() && it->is_number())
        app.config.mapOpenScale = it->get<float>();
    if (auto it = j.find("mapMultMini"); it != j.end() && it->is_number())
        app.config.miniMapScale = it->get<float>();
    // Back-compat: the old General "Announce zone on entry" (zoneToast) is now the Zone notification kind.
    if (!j.contains("notifyKindZone"))
        if (auto it = j.find("zoneToast"); it != j.end() && it->is_boolean())
            app.config.notifyKind[0] = it->get<bool>();
    ConfigProfiles::DeserializeAll(app, j); // the 4 families -> each restores + applies the current char's active profile into Config
    Loadouts::Deserialize(app, j);          // per-character loadouts (no auto-apply; families restore their own active)
    if (app.config.viewerStyle < 0 || app.config.viewerStyle >= kViewerStyleCount)
    {
        app.config.viewerStyle = ViewerStyleCompact;
        app.settingsDirty = true;
    }
    if (app.config.itemsView != 0 && app.config.itemsView != 1)
    {
        app.config.itemsView = 1;
        app.settingsDirty = true;
    }
    if (app.config.quickNotesEditorMode < 0 || app.config.quickNotesEditorMode >= kQuickNotesEditorModeCount)
    {
        app.config.quickNotesEditorMode = 0;
        app.settingsDirty = true;
    }
    if (app.config.quickNotesTextSize < 0 || app.config.quickNotesTextSize >= kFontSizeCount)
    {
        app.config.quickNotesTextSize = 2;
        app.settingsDirty = true;
    }
    if (EnsureQuickControlsTiles(app))
        app.settingsDirty = true;
    const float qnW = std::clamp(app.config.quickNotesEditorW, 360.f, 2400.f);
    const float qnH = std::clamp(app.config.quickNotesEditorH, 260.f, 1600.f);
    if (qnW != app.config.quickNotesEditorW || qnH != app.config.quickNotesEditorH)
    {
        app.config.quickNotesEditorW = qnW;
        app.config.quickNotesEditorH = qnH;
        app.settingsDirty = true;
    }
}

// Case-insensitive: do ALL whitespace-separated query terms appear in the setting's name/keywords/group?
// Per-term (AND) rather than one contiguous substring, so "hide markers" matches when both words appear even
// when they are not adjacent (e.g. name "...markers" + keyword "hide"). `hayLower` must already be lowercase.
// Shared so the searchable ACTIONS (live Diagnostics, the HUD bar actions) match exactly like the model rows
// do -- a plain substring test there would drop "hud reset" while every row still matched it.
bool KeywordsMatch(const char *hayLower, const char *qLower)
{
    if (!qLower || !*qLower)
        return true;
    if (!hayLower)
        return false;
    const std::string hay(hayLower);
    for (const char *p = qLower; *p;)
    {
        while (*p == ' ')
            ++p;
        const char *start = p;
        while (*p && *p != ' ')
            ++p;
        if (p > start && hay.find(std::string(start, p)) == std::string::npos)
            return false; // some term is missing -> not a match
    }
    return true;
}

bool SettingMatches(const Setting &s, const char *qLower)
{
    if (!qLower || !*qLower)
        return true;
    std::string hay = std::string(s.name) + " " + (s.search ? s.search : "") + " " + (s.groupName ? s.groupName : "") + " " + ((s.section >= 0 && s.section < SEC_COUNT) ? kSections[s.section] : ""); // + section name ("hud"/"info panel"/...)
    for (char &c : hay)
        c = (char)std::tolower((unsigned char)c);
    return KeywordsMatch(hay.c_str(), qLower);
}
