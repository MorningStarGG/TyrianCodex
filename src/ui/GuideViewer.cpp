#include "ui/GuideViewer.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "app/Glue.h"
#include "Shared.h"
#include "util/Draw.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/ZoneRow.h"
#include "ui/Gw2Ui.h"
#include "render/glyphs/Glyphs.h" // Render::Glyph::ResizeGrip (visible corner grip)
#include "ui/dashboard/Notify.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerModel.h"
#include "ui/viewer/ViewerStepCompact.h"
#include "ui/viewer/ViewerStepRich.h"
#include "util/Textures.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
#include <imgui.h>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

// Shared viewer components live in src/ui/viewer/ViewerComponents.cpp.

// Shared objective list lives in src/ui/viewer/ViewerObjectiveList.cpp.

namespace
{
    constexpr double kWaypointTravelOfferCooldownSeconds = 300.0;
    constexpr double kWaypointTravelOfferStartupDelaySeconds = 0.5;
    constexpr double kTravelOfferInputGraceSeconds = 0.35;
    constexpr float kWaypointTravelOfferMinCurrentDistance = 500.f;
    constexpr float kWaypointTravelOfferMinSavedDistance = 200.f;
    constexpr float kWaypointTravelOfferMaxRemainingRatio = 0.70f;
}

// Travel / dungeon / summary mode bodies live in src/ui/viewer/ViewerModeBodies.cpp.

// Rich step secondary/detail panels live in src/ui/viewer/ViewerStepRich.cpp.

static void DrawRouteDebug(App &app, float fs)
{
    if (!app.config.debugRoute)
        return;
    static const char *kMode[] = {"Trail", "HybridTrail", "HybridNearest", "DirectTrail", "DirectNearest"};
    // Show the ACTIVE route kind (Activate falls back to primary when the requested kind is absent, so this
    // can differ from the PathType setting) + a tag when the route is partial.
    const char *akind = app.state.zone.ActiveKind.empty() ? (app.config.pathType == 1 ? "mount" : "foot") : app.state.zone.ActiveKind.c_str();
    char dbg[180];
    std::snprintf(dbg, sizeof(dbg), "[%s] path:%s%s  step %d  off-route:%s  trail %.0fm",
                  kMode[std::clamp(app.config.routeMode, 0, 4)], akind, app.state.zone.ActiveComplete ? "" : " (partial)",
                  app.state.curStep, app.state.offRoute ? "yes" : "no", app.trailRenderer.NearestDist());
    Gw2Ui::Label(dbg, IM_COL32(150, 200, 255, 255), false, nullptr, fs - 4.f);
}

static void DrawTravelOfferDebug(App &app)
{
    if (!app.config.travelOfferDebug || app.state.travelOffer.debugLine.empty())
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 206, 126, 255));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("travel offer: %s", app.state.travelOffer.debugLine.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void DrawGuideWindow(App &app)
{
    UpdateCurrentChar(app.state);
    // Viewer style "None" hides ONLY this panel window. Guidance, the GPS arrow, the trail, world markers, map
    // pins, and travel targets all run in the separate showGuide-gated block in entry.cpp -- untouched -- and
    // the dashboard widgets render their own compact copies. So "None" != "guide off".
    if (app.config.viewerStyle == ViewerStyleNone)
        return;
    const float fs = kFontSizePx[std::clamp(app.config.viewerFontSize, 0, kFontSizeCount - 1)]; // current + description
    const float vs = ViewerScale(app);                                                          // uniform viewer zoom: TEXT via PushTextScale below, layout px via *vs in the modules
    const ImU32 kSub = Gw2Ui::kTextSub;

    // "Hide panel when complete": once every objective is done (and nothing manually targeted / no active
    // travel), don't draw the panel (a manual target or a travel destination still brings it back).
    if (ShouldHideGuidePanel(app))
        return;

    ImGui::SetNextWindowBgAlpha(std::clamp((float)app.config.panelOpacity / 100.f, 0.05f, 1.f));
    // The STEP/TRAVEL view is USER-RESIZABLE (persisted app.config.viewerW/H; drag the corner). The short modes
    // (Complete / Recommend / Dungeon) AUTO-FIT to content with a locked width - Sizes those
    // to the items, no dead space. When re-entering Step after a short mode (which auto-shrank the window),
    // restore the saved size once; afterwards ImGui owns the size and we read the drag back out below.
    static ViewerMode s_prevMode = ViewerMode::Message;
    static int s_prevStyle = 0;
    static float s_prevScale = 1.f;
    static bool s_styleInitialized = false;
    // Every in-game mode now uses the resizable, persisted-size per-style window (Message is gated out before
    // DrawGuideWindow runs). "Entering" = the first frame the panel reappears -> restore the saved size once.
    const bool prevStepLike = (s_prevMode != ViewerMode::Message);
    const bool enteringStep = !prevStepLike;
    const int styleIndex = ViewerStyleIndex(app);
    const bool styleChanged = s_styleInitialized && (styleIndex != s_prevStyle);
    const float prevScale = s_prevScale;
    const bool scaleChanged = s_styleInitialized && std::fabs(vs - prevScale) > 0.001f; // scale slider moved
    s_prevMode = app.state.viewerMode;
    s_prevStyle = styleIndex;
    s_prevScale = vs;
    s_styleInitialized = true;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoFocusOnAppearing;
    const bool mapAssistInputPassthrough =
        IsMapOpen() && app.state.mapAssist.active &&
        (app.state.mapAssist.panRequested || app.state.mapAssist.waitingForReadback ||
         app.state.mapAssist.centerConfirmWaiting);
    if (mapAssistInputPassthrough)
        flags |= ImGuiWindowFlags_NoInputs;

    const ViewerWindowSizing stepSizing = StepWindowSizing(app);
    ViewerAspectConstraint aspectConstraint{};
    if (scaleChanged && !enteringStep && !styleChanged && prevScale > 0.f && app.config.viewerW > 1.f)
    {
        // Scale slider moved: proportionally zoom the current window so the user's shape is preserved.
        const float k = vs / prevScale;
        ImGui::SetNextWindowSize(ClampStepWindowSize(ImVec2(app.config.viewerW * k, app.config.viewerH * k), stepSizing), ImGuiCond_Always);
    }
    else if (enteringStep || styleChanged)
    {
        const ImVec2 saved(app.config.viewerW, app.config.viewerH);
        ImGui::SetNextWindowSize(styleChanged ? stepSizing.Preferred : InitialStepWindowSize(saved, stepSizing), ImGuiCond_Always);
    }
    if (stepSizing.Aspect > 0.f)
    {
        aspectConstraint = {stepSizing.Aspect, stepSizing.Min.x, stepSizing.Max.x};
        ImGui::SetNextWindowSizeConstraints(stepSizing.Min, stepSizing.Max, FixedAspectResizeCallback, &aspectConstraint);
    }
    else
        ImGui::SetNextWindowSizeConstraints(stepSizing.Min, stepSizing.Max);
    flags |= ImGuiWindowFlags_NoScrollWithMouse; // the list child scrolls, not the window
    if (app.config.viewerLocked)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

    const bool rich = ViewerRich(app);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 9, 8, 255)); // alpha from SetNextWindowBgAlpha
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(118, 94, 48, 198));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.f); // round the window bg + ImGui border to match PanelChrome's rounded frame (no sharp-corner poke)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ViewerWindowPadding(app));

    if (ImGui::Begin("##tcguide", nullptr, flags))
    {
        Gw2Ui::PushTextScale(vs); // uniform viewer zoom: scales every Gw2Ui label + measured height/pill width
        const float op = std::clamp((float)app.config.panelOpacity / 100.f, 0.05f, 1.f);
        const int bgA = (int)std::round(150.f + 90.f * op);
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        Gw2Ui::PanelChrome(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                           IM_COL32(12, 13, 12, bgA),
                           IM_COL32(2, 4, 4, std::min(255, bgA + 10)),
                           IM_COL32(142, 108, 54, 150), rich);

        // Every in-game mode renders through the selected per-style viewer; the header / list area / detail
        // card each adapt by mode (Message is gated out before Begin). One path for Step / Travel /
        // Complete / Recommend / Dungeon.
        const int total = (int)app.state.zone.Steps.size();
        const int done = CountCompletedObjectives(app);
        const int dispStep = DisplayStepIndex(app, total);
        ZoneHeaderOpts zh;
        zh.pills = app.config.zhViewerPills;
        zh.bar = app.config.zhViewerBar;
        zh.barText = app.config.zhViewerBarText;
        zh.isWidget = false;
        zh.width = 0.f; // viewer reads the content region itself
        DrawViewerHeader(app, done, total, fs, ViewerCompact(app), zh);
        DrawRouteDebug(app, fs);
        DrawTravelOfferDebug(app);
        if (ViewerCompact(app))
            DrawCompactStepContent(app, dispStep, fs, kSub);
        else
            DrawRichStepContent(app, dispStep, fs, kSub);
        // Persist the user's drag-resize of the panel (all modes use the resizable window now).
        if (!app.config.viewerLocked)
        {
            const ImVec2 sz = ImGui::GetWindowSize();
            if (std::fabs(sz.x - app.config.viewerW) > 0.5f || std::fabs(sz.y - app.config.viewerH) > 0.5f)
            {
                app.config.viewerW = sz.x;
                app.config.viewerH = sz.y;
                app.settingsDirty = true;
            }

            // Visible resize-grip affordance at both bottom corners. The native ImGui grips are occluded by the
            // detail-card child windows (children render after the parent), so draw our own on the foreground draw
            // list (above the children); the native corner/edge resize still does the actual work.
            ImDrawList *fdl = ImGui::GetForegroundDrawList();
            const float g = 14.f;
            auto grip = [&](float cx, bool mirror)
            {
                const ImVec2 c(cx, wp.y + ws.y - g * 0.5f - 3.f);
                const bool hov = ImGui::IsMouseHoveringRect(ImVec2(c.x - g * 0.5f, c.y - g * 0.5f),
                                                            ImVec2(c.x + g * 0.5f, c.y + g * 0.5f), false);
                Render::GlyphStyle gs;
                gs.shadow = false;
                gs.mirrorX = mirror;
                Render::DrawGlyph(fdl, c, g, Render::Glyph::ResizeGrip,
                                  hov ? IM_COL32(255, 220, 150, 255) : IM_COL32(150, 128, 86, 205), gs);
            };
            grip(wp.x + ws.x - g * 0.5f - 3.f, /*mirror*/ false); // bottom-right
            grip(wp.x + g * 0.5f + 3.f, /*mirror*/ true);         // bottom-left
        }
        Gw2Ui::PopTextScale();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

// Put ASCII text on the Windows clipboard (chat links are ASCII like "[&BO8AAAA=]").
void SetClipboard(const std::string &s)
{
    if (s.empty() || !OpenClipboard(nullptr))
        return;
    EmptyClipboard();
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1))
    {
        if (void *p = GlobalLock(h))
        {
            std::memcpy(p, s.c_str(), s.size() + 1);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_TEXT, h))
                GlobalFree(h); // clipboard takes ownership only on success -> else free it
        }
        else
            GlobalFree(h); // couldn't lock the handle -> we still own it
    }
    CloseClipboard();
}

void ViewerAlert(const char *message)
{
    if (message && *message)
        Notify::Push(Notify::Kind::Action, message); // self-rendered toast (was Nexus GUI_SendAlert)
}

TargetOverrideKind CurrentTargetOverride(const App &app)
{
    if (app.travel.Active())
        return app.travel.IsPersonal() ? TargetOverrideKind::Personal : TargetOverrideKind::Travel;
    if (!app.state.manualTarget.empty())
        return TargetOverrideKind::Manual;
    return TargetOverrideKind::None;
}

const char *TargetClearLabel(TargetOverrideKind kind)
{
    switch (kind)
    {
    case TargetOverrideKind::Manual:
        return "Clear selected objective";
    case TargetOverrideKind::Personal:
        return "Clear personal waypoint";
    case TargetOverrideKind::Travel:
        return "Clear travel target";
    default:
        return "";
    }
}

void ClearTargetOverride(App &app, TargetOverrideKind kind)
{
    if (kind == TargetOverrideKind::Personal || kind == TargetOverrideKind::Travel)
        app.travel.Clear();
    else if (kind == TargetOverrideKind::Manual)
        app.state.manualTarget.clear();
}

static std::string WaypointLinkForStep(const Step &step)
{
    if (!step.WaypointChatLink.empty())
        return step.WaypointChatLink;
    return step.Type == "waypoint" ? step.ChatLink : std::string();
}

static const Step *FindStepById(const Zone &zone, const std::string &stepId)
{
    if (stepId.empty())
        return nullptr;
    for (const Step &step : zone.Steps)
        if (step.StepId == stepId)
            return &step;
    return nullptr;
}

static bool TravelAnchorInActiveZone(App &app, float &cx, float &cy, uint32_t &mapId, std::string &label)
{
    if (!app.travel.Active() || !app.state.zone.Loaded || !app.state.zone.HasRects)
        return false;

    float acx = 0.f, acy = 0.f;
    uint32_t anchorMapId = 0;
    if (!app.travel.GetAnchor(acx, acy, anchorMapId))
        return false;

    mapId = app.state.zone.MapId;
    label = app.travel.IsPersonal() ? app.travel.PersonalTitle() : ("Traveling to " + app.travel.GoalZoneName());
    const bool sameZone = (anchorMapId == app.state.zone.MapId) ||
                          (Coords::RectDistance(acx, acy, app.state.zone.ContRect) <= 0.f);
    if (sameZone)
    {
        cx = acx;
        cy = acy;
        return true;
    }

    const Travel::Entry entry = app.travel.GetZoneEntry(anchorMapId);
    if (entry.valid)
    {
        if (app.state.zone.MapId == entry.fromMapId)
        {
            cx = entry.ex;
            cy = entry.ey;
        }
        else
            Coords::ClampToRect(entry.ex, entry.ey, app.state.zone.ContRect, cx, cy);
    }
    else
        Coords::ClampToRect(acx, acy, app.state.zone.ContRect, cx, cy);
    return true;
}

static bool StartMapAssistPoint(App &app, uint32_t mapId, float cx, float cy, const std::string &label,
                                MapAssistMode mode, bool waypointTarget, bool waypointKnown,
                                const std::string &stepId, const std::string &waypointLink,
                                bool stepConfirmedAtStart, bool clearTravel)
{
    if (mapId == 0 || !std::isfinite(cx) || !std::isfinite(cy))
        return false;
    if (clearTravel)
        app.travel.Clear();

    MapAssistTarget target{};
    target.active = true;
    target.mode = mode;
    target.panRequested = true;
    target.mapId = mapId;
    target.cx = cx;
    target.cy = cy;
    target.waypointTarget = waypointTarget;
    target.waypointKnown = waypointKnown;
    target.stepConfirmedAtStart = stepConfirmedAtStart;
    target.stepId = stepId;
    target.waypointLink = waypointLink;
    target.label = label;
    target.focusProbeRequested = app.config.mapAssistFocusProbe;
    target.waypointClickRequested = mode == MapAssistMode::Travel && app.config.mapAssistClickWaypoint &&
                                    target.waypointTarget && target.waypointKnown;
    target.waypointClickStatus = mode == MapAssistMode::ShowOnly ? "show only" : !target.waypointTarget      ? "not waypoint"
                                                                             : !target.waypointKnown         ? "locked"
                                                                             : target.waypointClickRequested ? "armed"
                                                                                                             : "disabled";
    target.nextPanTime = ImGui::GetTime() + (target.focusProbeRequested ? 0.06 : 0.04);
    target.panStatus = "queued";
    app.state.mapAssist = target;

    bool opened = IsMapOpen();
    if (!opened && APIDefs && APIDefs->GameBinds_InvokeAsync)
    {
        const bool mapBound = !APIDefs->GameBinds_IsBound || APIDefs->GameBinds_IsBound(GB_MapToggle);
        if (mapBound)
        {
            APIDefs->GameBinds_InvokeAsync(GB_MapToggle, 90);
            opened = true;
        }
    }

    if (!opened)
        ViewerAlert("Target set. Map toggle is not bound.");
    else if (target.waypointClickRequested)
        ViewerAlert("Map target set - panning to confirmed waypoint.");
    else if (target.waypointTarget && !target.waypointKnown)
        ViewerAlert("Map target set - waypoint is not confirmed for travel.");
    else if (!waypointLink.empty())
        ViewerAlert(app.config.mapAssistFocusProbe ? "Map target set - probing recenter, then panning." : "Map target set - panning to waypoint.");
    else
        ViewerAlert("Target set on map.");

    return opened;
}

static bool StartMapAssistStep(App &app, const Step &selectedStep, const Step &assistStep,
                               const std::string &waypointLink, bool selectObjective,
                               MapAssistMode mode, bool clearTravel)
{
    if (selectedStep.StepId.empty() || assistStep.StepId.empty())
        return false;
    if (selectObjective)
        app.state.manualTarget = selectedStep.StepId;

    const bool waypointTarget = assistStep.Type == "waypoint";
    const bool waypointKnown = waypointTarget && app.progress.IsConfirmed(app.state.currentChar, assistStep.StepId);
    const bool confirmedAtStart = app.progress.IsConfirmed(app.state.currentChar, selectedStep.StepId);
    return StartMapAssistPoint(app, app.state.zone.MapId, assistStep.CX, assistStep.CY,
                               assistStep.Name.empty() ? selectedStep.Name : assistStep.Name,
                               mode, waypointTarget, waypointKnown, selectedStep.StepId,
                               waypointLink, confirmedAtStart, clearTravel);
}

static bool StartManualTravelChoiceForStep(App &app, const Step &step);
static bool StartManualTravelChoiceForActiveTravel(App &app);
static void ShowDestinationPointOnMap(App &app, float cx, float cy, uint32_t destMapId, const std::string &label);

static void ShowStepTargetOnly(App &app, const Step &step)
{
    if (step.StepId.empty())
        return;
    app.state.manualTarget = step.StepId;
    StartMapAssistPoint(app, app.state.zone.MapId, step.CX, step.CY,
                        step.Name.empty() ? step.Type : step.Name,
                        MapAssistMode::ShowOnly, false, false, step.StepId,
                        WaypointLinkForStep(step),
                        app.progress.IsConfirmed(app.state.currentChar, step.StepId),
                        true);
}

void CopyWaypointForStep(App &app, const Step &step)
{
    (void)app;
    const std::string link = WaypointLinkForStep(step);
    if (link.empty())
    {
        ViewerAlert("No waypoint link for this objective.");
        return;
    }
    SetClipboard(link);
    ViewerAlert(step.Type == "waypoint" ? "Copied waypoint link - paste/click to teleport."
                                        : "Copied nearest waypoint link - paste/click to teleport.");
}

// Copy the current objective's waypoint chat link (its own if it's a waypoint, else the nearest waypoint
// to it) to the clipboard - paste in chat to travel. The travel system's copy-nearest-waypoint action.
void CopyCurrentWaypoint(App &app)
{
    if (!app.state.zone.Loaded || app.state.curStep < 0 || app.state.curStep >= (int)app.state.zone.Steps.size())
        return;
    CopyWaypointForStep(app, app.state.zone.Steps[app.state.curStep]);
}

void ShowStepOnMap(App &app, const Step &step)
{
    if (step.StepId.empty())
        return;

    if (StartManualTravelChoiceForStep(app, step))
        return;
    ShowStepTargetOnly(app, step);
}

void OnObjectiveTargetClicked(App &app, const Step &step)
{
    if (step.StepId.empty())
        return;
    if (app.travel.Active())
    {
        app.state.targetSwitch.active = true;
        app.state.targetSwitch.stepId = step.StepId;
        app.state.targetSwitch.stepName = step.Name.empty() ? step.Type : step.Name;
        return;
    }
    app.state.manualTarget = step.StepId;
}

static Follow::Vec2 StepWorldXz(const Zone &zone, const Step &step)
{
    float x = 0.f, z = 0.f;
    Coords::ContinentToWorldXZ(step.CX, step.CY, zone.ContRect, zone.MapRect, x, z);
    return {x, z};
}

static int TargetTrailIndex(App &app, Follow::Vec2 targetXz, int preferredIndex)
{
    if (preferredIndex >= 0)
        return preferredIndex;
    if (!app.follower.HasTrail())
        return -1;
    return app.follower.NearestTrailIndex(targetXz);
}

static void SetTravelOfferDebug(WaypointTravelOffer &offer, const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    offer.debugLine = buf;
}

static float RouteModeDistanceToTarget(App &app, Follow::Vec2 startXz, Follow::Vec2 targetXz, int targetTrailIndex)
{
    const float straight = Follow::Dist(startXz, targetXz);
    const bool haveTrail = app.follower.HasTrail() &&
                           targetTrailIndex >= 0 &&
                           targetTrailIndex < app.follower.Size();

    if (ModeIsDirect(app.config.routeMode) || !haveTrail)
        return straight;

    if (ModeFollowsRibbon(app.config.routeMode))
        return app.follower.AlongTrailDistance(startXz, targetXz, targetTrailIndex);

    if (app.follower.OnApproach(startXz, targetTrailIndex, app.config.approachMeters))
        return app.follower.AlongTrailDistance(startXz, targetXz, targetTrailIndex);

    const int entrance = app.follower.IndexBackAlongTrail(targetTrailIndex, (float)app.config.approachMeters);
    if (entrance < 0 || entrance >= (int)app.state.zone.Trail.size())
        return straight;

    const Follow::Vec2 entranceXz{app.state.zone.Trail[entrance].x, app.state.zone.Trail[entrance].z};
    const float toEntrance = Follow::Dist(startXz, entranceXz);
    if (straight <= toEntrance)
        return straight;

    const Follow::Vec2 targetTrailXz{app.state.zone.Trail[targetTrailIndex].x,
                                     app.state.zone.Trail[targetTrailIndex].z};
    return toEntrance +
           app.follower.TrailArc(entrance, targetTrailIndex) +
           Follow::Dist(targetTrailXz, targetXz);
}

static float RouteModeDistance(App &app, Follow::Vec2 startXz, const Step &targetStep)
{
    const Follow::Vec2 targetXz = StepWorldXz(app.state.zone, targetStep);
    return RouteModeDistanceToTarget(app, startXz, targetXz, targetStep.TrailIndex);
}

static bool TravelOfferRouteInputsReady(App &app, const Step &targetStep)
{
    if (!MumbleLink || !app.state.zone.Loaded || !app.state.zone.HasRects)
        return false;
    if (ModeIsDirect(app.config.routeMode) || targetStep.TrailIndex < 0)
        return true;
    return app.follower.HasTrail() &&
           targetStep.TrailIndex < app.follower.Size() &&
           targetStep.TrailIndex < (int)app.state.zone.Trail.size();
}

static const Step *FindNearestWaypointForPoint(App &app, float cx, float cy, bool confirmedOnly)
{
    if (!app.state.zone.Loaded)
        return nullptr;
    const Step *best = nullptr;
    float bestD = std::numeric_limits<float>::max();
    for (const Step &wp : app.state.zone.Steps)
    {
        if (wp.Type != "waypoint" || WaypointLinkForStep(wp).empty())
            continue;
        if (confirmedOnly && !app.progress.IsConfirmed(app.state.currentChar, wp.StepId))
            continue;
        const float dx = wp.CX - cx;
        const float dy = wp.CY - cy;
        const float d = dx * dx + dy * dy;
        if (d < bestD)
        {
            bestD = d;
            best = &wp;
        }
    }
    return best;
}

// Central gate: is a teleport hop worth offering? Far enough from the goal, saves enough, and the after-travel
// walk isn't most of the remaining trip. The SINGLE source of these thresholds -- BOTH offer builders call it.
static bool TravelOfferWorthIt(float playerDistance, float waypointDistance)
{
    const float saved = playerDistance - waypointDistance;
    return playerDistance >= kWaypointTravelOfferMinCurrentDistance && saved >= kWaypointTravelOfferMinSavedDistance && waypointDistance <= playerDistance * kWaypointTravelOfferMaxRemainingRatio;
}

static bool BuildPointWaypointTravelOffer(App &app, TravelOfferTargetKind kind,
                                          const std::string &targetId, const std::string &targetName,
                                          uint32_t targetMapId, float targetCx, float targetCy,
                                          int preferredTrailIndex, WaypointTravelOffer &out)
{
    if (!MumbleLink)
    {
        SetTravelOfferDebug(out, "waiting: MumbleLink unavailable");
        return false;
    }
    if (!app.state.zone.Loaded || !app.state.zone.HasRects)
    {
        SetTravelOfferDebug(out, "waiting: no active zone rects");
        return false;
    }
    if (targetId.empty())
    {
        SetTravelOfferDebug(out, "rejected: current target has no id");
        return false;
    }
    if (targetMapId != app.state.zone.MapId)
    {
        SetTravelOfferDebug(out, "rejected: %s is not on the active map", targetName.c_str());
        return false;
    }

    const Follow::Vec2 playerXz{MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z};
    float targetX = 0.f, targetZ = 0.f;
    Coords::ContinentToWorldXZ(targetCx, targetCy, app.state.zone.ContRect, app.state.zone.MapRect, targetX, targetZ);
    const Follow::Vec2 targetXz{targetX, targetZ};
    const int targetTrailIndex = TargetTrailIndex(app, targetXz, preferredTrailIndex);

    const float playerDistance = RouteModeDistanceToTarget(app, playerXz, targetXz, targetTrailIndex);

    const Step *best = nullptr;
    float bestWaypointDistance = std::numeric_limits<float>::max();
    int confirmedWaypoints = 0;
    int usableWaypoints = 0;
    int skippedNearPlayer = 0;
    for (const Step &wp : app.state.zone.Steps)
    {
        if (wp.Type != "waypoint" || WaypointLinkForStep(wp).empty())
            continue;
        if (!app.progress.IsConfirmed(app.state.currentChar, wp.StepId))
            continue;
        ++confirmedWaypoints;

        const Follow::Vec2 wpXz = StepWorldXz(app.state.zone, wp);
        const float waypointDistance = RouteModeDistanceToTarget(app, wpXz, targetXz, targetTrailIndex);
        const float playerToWaypoint = Follow::Dist(playerXz, wpXz);
        if (playerToWaypoint < 80.f)
        {
            ++skippedNearPlayer;
            continue;
        }
        ++usableWaypoints;
        if (waypointDistance < bestWaypointDistance)
        {
            best = &wp;
            bestWaypointDistance = waypointDistance;
        }
    }

    if (!best)
    {
        if (confirmedWaypoints == 0)
            SetTravelOfferDebug(out, "rejected: no confirmed waypoint candidates for %s", targetName.c_str());
        else
            SetTravelOfferDebug(out, "rejected: no usable waypoint for %s (%d confirmed, %d skipped because player is within 80m)",
                                targetName.c_str(), confirmedWaypoints, skippedNearPlayer);
        return false;
    }
    const float saved = playerDistance - bestWaypointDistance;
    if (!TravelOfferWorthIt(playerDistance, bestWaypointDistance)) // central gate (shared with BuildKnownTravelOffer)
    {
        SetTravelOfferDebug(out, "rejected: %s for %s current %.0fm, after %.0fm, saved %.0fm below thresholds",
                            best->Name.c_str(), targetName.c_str(), playerDistance, bestWaypointDistance, saved);
        return false;
    }

    out.active = true;
    out.openRequested = true;
    out.openedAt = ImGui::GetTime();
    out.targetKind = kind;
    out.targetStepId = kind == TravelOfferTargetKind::Step ? targetId : std::string();
    out.targetName = targetName;
    out.targetMapId = targetMapId;
    out.targetCx = targetCx;
    out.targetCy = targetCy;
    out.waypointStepId = best->StepId;
    out.waypointName = best->Name;
    out.waypointLink = WaypointLinkForStep(*best);
    out.waypointMapId = app.state.zone.MapId;
    out.waypointCx = best->CX;
    out.waypointCy = best->CY;
    out.playerDistance = playerDistance;
    out.waypointDistance = bestWaypointDistance;
    out.savedDistance = saved;
    SetTravelOfferDebug(out, "offered: %s for %s current %.0fm, after %.0fm, saved %.0fm (%d usable waypoints)",
                        best->Name.c_str(), targetName.c_str(), playerDistance,
                        bestWaypointDistance, saved, usableWaypoints);
    return true;
}

static bool BuildWaypointTravelOffer(App &app, const Step &targetStep, WaypointTravelOffer &out)
{
    if (targetStep.StepId.empty())
    {
        SetTravelOfferDebug(out, "rejected: current step has no step id");
        return false;
    }
    if (!TravelOfferRouteInputsReady(app, targetStep))
    {
        SetTravelOfferDebug(out, "waiting: route inputs are not ready for %s", targetStep.Name.c_str());
        return false;
    }
    return BuildPointWaypointTravelOffer(app, TravelOfferTargetKind::Step, targetStep.StepId,
                                         targetStep.Name.empty() ? targetStep.Type : targetStep.Name,
                                         app.state.zone.MapId, targetStep.CX, targetStep.CY,
                                         targetStep.TrailIndex, out);
}

static std::string TravelTargetEvalId(TravelOfferTargetKind kind, uint32_t mapId, float cx, float cy)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d:%u:%.1f:%.1f", (int)kind, mapId, cx, cy);
    return buf;
}

// The cooldown/eval key for the ACTIVE travel target, shared by the auto-offer and BuildKnownTravelOffer so a
// dismiss/accept cools down the SAME key the auto-offer checks. Default: key by the GOAL (GetAnchor), so a
// dismissed personal target STAYS dismissed across zone changes (you chose to walk in). With
// reaskTravelOnZoneChange on, key by the in-zone anchor instead, so each zone re-offers.
static std::string ActiveTravelEvalKey(App &app)
{
    const TravelOfferTargetKind kind = app.travel.IsPersonal() ? TravelOfferTargetKind::Personal
                                                               : TravelOfferTargetKind::ZoneTravel;
    float cx = 0.f, cy = 0.f;
    uint32_t mapId = 0;
    if (app.config.reaskTravelOnZoneChange)
    {
        std::string label;
        if (TravelAnchorInActiveZone(app, cx, cy, mapId, label))
            return TravelTargetEvalId(kind, mapId, cx, cy);
    }
    if (app.travel.GetAnchor(cx, cy, mapId))
        return TravelTargetEvalId(kind, mapId, cx, cy);
    return std::string();
}

// Build a TRAVEL offer (personal spot OR zone destination) from the controller's resolved hop. travel.Known()
// is the SINGLE cross-zone, destination-zone-preferred resolver, so the recommended card, the viewer "Show on
// map" button, AND the map pins all pick the SAME waypoint (no more current-zone-vs-cross-zone mismatch). Fills
// the goal + waypoint + approximate Current/After/Saved metrics. Returns false if no confirmed hop resolves
// (the caller then shows the thin "routes you there" card / the destination only). Sets only the target +
// waypoint fields; the caller owns active/manualChoice/openRequested.
static bool BuildKnownTravelOffer(App &app, WaypointTravelOffer &out)
{
    if (!app.travel.Active() || !MumbleLink || !app.state.zone.HasRects)
        return false;
    float gcx = 0.f, gcy = 0.f;
    uint32_t gmid = 0;
    if (!app.travel.GetAnchor(gcx, gcy, gmid))
        return false;
    const Travel::WpRef wp = app.travel.Known();
    if (!wp.valid)
        return false;

    out.targetKind = app.travel.IsPersonal() ? TravelOfferTargetKind::Personal : TravelOfferTargetKind::ZoneTravel;
    out.targetMapId = app.travel.IsPersonal() ? 0u : app.travel.DestinationMapId();
    out.targetName = app.travel.GoalZoneName();
    out.targetCx = gcx;
    out.targetCy = gcy;
    out.waypointStepId = wp.stepId;
    out.waypointName = wp.name;
    out.waypointLink = wp.chatLink;
    out.waypointMapId = wp.mapId;
    out.waypointCx = wp.cx;
    out.waypointCy = wp.cy;
    // Approx straight-line meters: map goal + waypoint through the active zone's continent->world transform
    // (distances scale uniformly, so a cross-zone comparison is valid) against the live player world pos.
    float gX, gZ, wX, wZ;
    Coords::ContinentToWorldXZ(gcx, gcy, app.state.zone.ContRect, app.state.zone.MapRect, gX, gZ);
    Coords::ContinentToWorldXZ(wp.cx, wp.cy, app.state.zone.ContRect, app.state.zone.MapRect, wX, wZ);
    const float pX = MumbleLink->AvatarPosition.X, pZ = MumbleLink->AvatarPosition.Z;
    auto dist = [](float ax, float az, float bx, float bz)
    { const float dx = ax - bx, dz = az - bz; return std::sqrt(dx * dx + dz * dz); };
    out.playerDistance = dist(pX, pZ, gX, gZ);
    out.waypointDistance = dist(wX, wZ, gX, gZ);
    out.savedDistance = out.playerDistance - out.waypointDistance;
    if (!TravelOfferWorthIt(out.playerDistance, out.waypointDistance)) // central gate (shared with steps)
    {
        SetTravelOfferDebug(out, "rejected: current %.0fm, after %.0fm, saved %.0fm below thresholds",
                            out.playerDistance, out.waypointDistance, out.savedDistance);
        return false;
    }
    // Tag this offer with the SAME eval key the per-frame auto-offer uses, so dismissing/accepting it cools down
    // that key -- otherwise the auto-offer re-pops the card (right after "Not now", or on a zone change).
    out.evaluatedStepId = ActiveTravelEvalKey(app);
    return true;
}

static bool StartManualTravelChoiceForStep(App &app, const Step &step)
{
    WaypointTravelOffer offer{};
    if (!BuildWaypointTravelOffer(app, step, offer))
        return false;
    offer.manualChoice = true;
    offer.openRequested = true;
    app.state.travelOffer = offer;
    return true;
}

static bool StartManualTravelChoiceForActiveTravel(App &app)
{
    WaypointTravelOffer offer{};
    if (!BuildKnownTravelOffer(app, offer)) // SAME resolver the recommended card + the map pins use
        return false;
    offer.active = true;
    offer.manualChoice = true;
    offer.openRequested = true;
    offer.openedAt = ImGui::GetTime();
    app.state.travelOffer = offer;
    return true;
}

static void ClearAutomaticWaypointTravelOffer(WaypointTravelOffer &offer)
{
    if (offer.manualChoice)
        return;
    offer.active = false;
    offer.openRequested = false;
}

static void ShowOfferTargetOnly(App &app, const WaypointTravelOffer &offer)
{
    if (offer.targetKind == TravelOfferTargetKind::Step)
    {
        if (const Step *target = FindStepById(app.state.zone, offer.targetStepId))
            ShowStepTargetOnly(app, *target);
        return;
    }

    ShowDestinationPointOnMap(app, offer.targetCx, offer.targetCy, offer.targetMapId,
                              offer.targetName.empty() ? "Travel target" : offer.targetName);
}

// The TRUE travel destination (the personal spot, or the destination zone's entry), regardless of zone --
// for "Show target", which must reveal where you're GOING, not the in-zone connector the arrow routes you
// through. (TravelAnchorInActiveZone stays for the in-zone pin/panel; this is purely for showing the goal.)
static bool TravelDestinationPoint(App &app, float &cx, float &cy, uint32_t &mapId, std::string &label)
{
    if (!app.travel.Active())
        return false;
    float acx = 0.f, acy = 0.f;
    uint32_t anchorMapId = 0;
    if (!app.travel.GetAnchor(acx, acy, anchorMapId))
        return false;
    cx = acx;
    cy = acy;
    mapId = anchorMapId;
    label = app.travel.IsPersonal() ? app.travel.PersonalTitle() : ("Traveling to " + app.travel.GoalZoneName());
    return true;
}

// Show a destination continent point on the open world map. The fullscreen map is ONE continent, so a
// cross-zone destination is still on it: keep the CURRENT map id for the assist (so the same-map guard
// survives while we view it) but relax the active-zone bound so the pan may cross into the goal's area.
static void ShowDestinationPointOnMap(App &app, float cx, float cy, uint32_t destMapId, const std::string &label)
{
    const uint32_t curMap = app.state.zone.MapId;
    const bool crossZone = destMapId != 0 && curMap != 0 && destMapId != curMap;
    StartMapAssistPoint(app, curMap != 0 ? curMap : destMapId, cx, cy, label, MapAssistMode::ShowOnly,
                        false, false, std::string(), std::string(), false, false);
    if (crossZone)
        app.state.mapAssist.relaxZoneBounds = true;
}

static void ShowActiveTravelTargetOnly(App &app)
{
    float cx = 0.f, cy = 0.f;
    uint32_t mapId = 0;
    std::string label;
    if (!TravelDestinationPoint(app, cx, cy, mapId, label))
    {
        ViewerAlert("No active travel target to show.");
        return;
    }
    ShowDestinationPointOnMap(app, cx, cy, mapId, label);
}

void ShowTravelTargetOnMap(App &app)
{
    if (!app.travel.Active())
        return;
    if (StartManualTravelChoiceForActiveTravel(app))
        return;
    ShowActiveTravelTargetOnly(app);
}

// Recommended-zone selection -> the Travel / Show target / Not now choice card. Commits the on-foot route, then
// resolves the SAME cross-zone hop the viewer "Show on map" button + the map pins use (travel.Known(), via
// BuildKnownTravelOffer), so all three agree. Falls back to a thin "routes you there" card when nothing confirmed.
void OpenZoneTravelChoice(App &app, uint32_t mapId)
{
    if (mapId == 0)
        return;
    app.travel.StartTravel(mapId); // commit on-foot routing now, so declining the teleport still walks you there
    if (MumbleLink)
        app.travel.Recompute(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z); // resolve the hop NOW

    WaypointTravelOffer offer{};
    offer.active = true;
    offer.manualChoice = true;
    offer.openRequested = true;
    offer.openedAt = ImGui::GetTime();
    if (!BuildKnownTravelOffer(app, offer)) // rich card (the resolved cross-zone waypoint + metrics)
    {
        // No confirmed hop resolved -> the thin "routes you there" card (the arrow still guides you in on foot).
        offer.targetKind = TravelOfferTargetKind::ZoneTravel;
        offer.targetMapId = mapId;
        auto it = app.zones.find(mapId);
        offer.targetName = (it != app.zones.end()) ? it->second.Name : std::string("this zone");
        float gcx = 0.f, gcy = 0.f;
        uint32_t gmid = 0;
        if (app.travel.GetAnchor(gcx, gcy, gmid))
        {
            offer.targetCx = gcx;
            offer.targetCy = gcy;
        }
    }
    app.state.travelOffer = offer;
}

// Nearest waypoint (any loaded zone) to a continent point, within tolerance. A clicked chat-link settles the
// map ~on the waypoint, so this matches it back to the real waypoint (coords + chat code) we already ship.
static bool FindNearestZoneWaypoint(App &app, float cx, float cy, float tolContinent, Travel::WpRef &out)
{
    const Step *best = nullptr;
    uint32_t bestMap = 0;
    float bestD2 = tolContinent * tolContinent;
    for (const auto &kv : app.zones)
        for (const Step &s : kv.second.Steps)
        {
            if (s.Type != "waypoint")
                continue;
            const float dx = s.CX - cx, dy = s.CY - cy;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2)
            {
                bestD2 = d2;
                best = &s;
                bestMap = kv.second.MapId;
            }
        }
    if (!best)
        return false;
    out = Travel::WpRef{};
    out.valid = true;
    out.name = best->Name;
    out.chatLink = WaypointLinkForStep(*best);
    out.stepId = best->StepId;
    out.cx = best->CX;
    out.cy = best->CY;
    out.mapId = bestMap;
    return true;
}

// A clicked chat WAYPOINT link (detected from the map auto-pan): resolve it to the real waypoint and store it as
// the personal hop, so the SAME card + pan + click path used everywhere else handles it (no bare-point pan).
void OpenChatWaypointTravel(App &app, float cx, float cy)
{
    Travel::WpRef wp;
    if (!FindNearestZoneWaypoint(app, cx, cy, 150.f, wp) || wp.chatLink.empty())
    {
        // Not a waypoint we know (e.g. a POI link) -> just drop the pin and guide on foot.
        app.travel.SetPersonalTarget(cx, cy, "Chat waypoint", "Linked in chat - follow the arrow or use Travel.");
        return;
    }
    app.travel.SetPersonalWaypoint(wp.cx, wp.cy, wp, wp.name, "Linked in chat - travel or follow the arrow.");
    if (MumbleLink)
        app.travel.Recompute(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z); // Known() = the linked wp

    WaypointTravelOffer offer{};
    offer.active = true;
    offer.manualChoice = true;
    offer.openRequested = true;
    offer.openedAt = ImGui::GetTime();
    if (BuildKnownTravelOffer(app, offer)) // shared builder, now resolving the linked waypoint
        app.state.travelOffer = offer;
}

// A clicked search/atlas/waypoint-list result -> the SAME personal-target + distance-gated card path the chat
// waypoint uses, but driven by the result's own data (so a waypoint travels via its exact chat code). Zones are
// handled by OpenZoneTravelChoice; this covers the 5 objective kinds.
void TravelToLocation(App &app, const std::string &type, uint32_t mapId, float cx, float cy,
                      const std::string &name, const std::string &chatLink)
{
    const std::string label = name.empty() ? std::string("Location") : name;
    if (type == "waypoint" && !chatLink.empty())
    {
        Travel::WpRef wp;
        wp.valid = true;
        wp.name = label;
        wp.chatLink = chatLink;
        wp.cx = cx;
        wp.cy = cy;
        wp.mapId = mapId;
        app.travel.SetPersonalWaypoint(cx, cy, wp, label, "Travel here or follow the arrow.");
    }
    else
        app.travel.SetPersonalTarget(cx, cy, label, "Heading to this location - travel or follow the arrow.");

    if (MumbleLink)
        app.travel.Recompute(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z);

    WaypointTravelOffer offer{};
    offer.active = true;
    offer.manualChoice = true;
    offer.openRequested = true;
    offer.openedAt = ImGui::GetTime();
    if (BuildKnownTravelOffer(app, offer)) // pops the Travel/Show/Not-now card only when distance warrants it
        app.state.travelOffer = offer;
}

void CopyTravelTargetWaypoint(App &app)
{
    if (!app.travel.Active())
    {
        ViewerAlert("No travel target is active.");
        return;
    }

    const Travel::WpRef &known = app.travel.Known();
    if (known.valid && !known.chatLink.empty())
    {
        SetClipboard(known.chatLink);
        ViewerAlert("Copied travel waypoint link - paste/click to teleport.");
        return;
    }

    const Travel::WpRef &finalWp = app.travel.Final();
    if (finalWp.valid && !finalWp.chatLink.empty())
    {
        SetClipboard(finalWp.chatLink);
        ViewerAlert("Copied nearest waypoint link - paste/click to teleport.");
        return;
    }

    float cx = 0.f, cy = 0.f;
    uint32_t mapId = 0;
    std::string label;
    if (TravelAnchorInActiveZone(app, cx, cy, mapId, label))
    {
        if (const Step *wp = FindNearestWaypointForPoint(app, cx, cy, false))
        {
            const std::string link = WaypointLinkForStep(*wp);
            if (!link.empty())
            {
                SetClipboard(link);
                ViewerAlert("Copied nearest waypoint link - paste/click to teleport.");
                return;
            }
        }
    }

    ViewerAlert("No waypoint link for this travel target.");
}

static void UpdateWaypointTravelOffer(App &app)
{
    WaypointTravelOffer &offer = app.state.travelOffer;
    if (offer.active && offer.manualChoice)
        return;

    if (!app.config.offerWaypointTravel)
    {
        offer.active = false;
        offer.eligibleSince = 0.0;
        SetTravelOfferDebug(offer, "off: Offer closer waypoint travel is disabled");
        return;
    }
    if (app.travel.Active())
    {
        if (!offer.manualChoice && offer.active &&
            (offer.targetKind == TravelOfferTargetKind::Step || (!app.travel.IsPersonal() && offer.targetKind == TravelOfferTargetKind::Personal)))
            ClearAutomaticWaypointTravelOffer(offer);
        if (!app.travel.IsPersonal())
        {
            ClearAutomaticWaypointTravelOffer(offer);
            offer.eligibleSince = 0.0;
            SetTravelOfferDebug(offer, "waiting: zone travel target is active");
            return;
        }
        if (app.state.inDungeon)
        {
            ClearAutomaticWaypointTravelOffer(offer);
            offer.eligibleSince = 0.0;
            SetTravelOfferDebug(offer, "off: current map is a dungeon/instance");
            return;
        }
        if (!app.state.zone.Loaded || !app.state.zone.HasRects)
        {
            ClearAutomaticWaypointTravelOffer(offer);
            offer.eligibleSince = 0.0;
            SetTravelOfferDebug(offer, "waiting: no active zone rects");
            return;
        }

        float cx = 0.f, cy = 0.f;
        uint32_t mapId = 0;
        std::string label;
        if (!TravelAnchorInActiveZone(app, cx, cy, mapId, label))
        {
            ClearAutomaticWaypointTravelOffer(offer);
            offer.eligibleSince = 0.0;
            SetTravelOfferDebug(offer, "waiting: no personal travel target");
            return;
        }

        const double now = ImGui::GetTime();
        if (offer.eligibleSince <= 0.0)
        {
            offer.eligibleSince = now;
            SetTravelOfferDebug(offer, "waiting: startup grace %.1fs", kWaypointTravelOfferStartupDelaySeconds);
            return;
        }
        if (now - offer.eligibleSince < kWaypointTravelOfferStartupDelaySeconds)
        {
            SetTravelOfferDebug(offer, "waiting: startup grace %.1fs", kWaypointTravelOfferStartupDelaySeconds - (now - offer.eligibleSince));
            return;
        }

        const std::string evaluated = ActiveTravelEvalKey(app); // goal-keyed (default): no re-ask on zone change
        const uint64_t progressVersion = app.progress.Version();
        if (evaluated == offer.evaluatedStepId && progressVersion == offer.evaluatedProgressVersion)
            return;

        offer = WaypointTravelOffer{};
        offer.eligibleSince = now - kWaypointTravelOfferStartupDelaySeconds;
        offer.evaluatedStepId = evaluated;
        offer.evaluatedProgressVersion = progressVersion;
        auto cooldown = app.state.travelOfferCooldownUntil.find(evaluated);
        if (cooldown != app.state.travelOfferCooldownUntil.end() && now < cooldown->second)
        {
            SetTravelOfferDebug(offer, "cooldown: personal target can be offered again in %.0fs", cooldown->second - now);
            return;
        }
        if (BuildKnownTravelOffer(app, offer)) // SAME resolver as the viewer button + recommended + pins (Known())
        {
            offer.active = true;
            offer.openRequested = true;
            offer.openedAt = ImGui::GetTime();
        }
        offer.evaluatedStepId = evaluated;
        offer.evaluatedProgressVersion = progressVersion;
        return;
    }
    if (!offer.manualChoice && offer.active && offer.targetKind != TravelOfferTargetKind::Step)
        ClearAutomaticWaypointTravelOffer(offer);
    if (app.state.inDungeon)
    {
        ClearAutomaticWaypointTravelOffer(offer);
        offer.eligibleSince = 0.0;
        SetTravelOfferDebug(offer, "off: current map is a dungeon/instance");
        return;
    }
    if (app.state.viewerMode != ViewerMode::Step)
    {
        ClearAutomaticWaypointTravelOffer(offer);
        offer.eligibleSince = 0.0;
        SetTravelOfferDebug(offer, "waiting: viewer mode is not Step");
        return;
    }
    if (!app.state.zone.Loaded || app.state.curStep < 0 || app.state.curStep >= (int)app.state.zone.Steps.size())
    {
        ClearAutomaticWaypointTravelOffer(offer);
        offer.eligibleSince = 0.0;
        SetTravelOfferDebug(offer, "waiting: no valid current zone step");
        return;
    }

    const double now = ImGui::GetTime();
    if (offer.eligibleSince <= 0.0)
    {
        offer.eligibleSince = now;
        SetTravelOfferDebug(offer, "waiting: startup grace %.1fs", kWaypointTravelOfferStartupDelaySeconds);
        return;
    }
    if (now - offer.eligibleSince < kWaypointTravelOfferStartupDelaySeconds)
    {
        SetTravelOfferDebug(offer, "waiting: startup grace %.1fs", kWaypointTravelOfferStartupDelaySeconds - (now - offer.eligibleSince));
        return;
    }

    const Step &step = app.state.zone.Steps[app.state.curStep];
    const uint64_t progressVersion = app.progress.Version();
    if (!TravelOfferRouteInputsReady(app, step))
    {
        ClearAutomaticWaypointTravelOffer(offer);
        SetTravelOfferDebug(offer, "waiting: route inputs are not ready for %s", step.Name.c_str());
        return;
    }

    if (step.StepId.empty())
    {
        ClearAutomaticWaypointTravelOffer(offer);
        SetTravelOfferDebug(offer, "rejected: current step has no step id");
        return;
    }
    if (step.StepId == offer.evaluatedStepId && progressVersion == offer.evaluatedProgressVersion)
        return;

    const std::string evaluated = step.StepId;
    offer = WaypointTravelOffer{};
    offer.eligibleSince = now - kWaypointTravelOfferStartupDelaySeconds;
    offer.evaluatedStepId = evaluated;
    offer.evaluatedProgressVersion = progressVersion;
    auto cooldown = app.state.travelOfferCooldownUntil.find(evaluated);
    if (cooldown != app.state.travelOfferCooldownUntil.end() && now < cooldown->second)
    {
        SetTravelOfferDebug(offer, "cooldown: %s can be offered again in %.0fs", step.Name.c_str(), cooldown->second - now);
        return;
    }
    BuildWaypointTravelOffer(app, step, offer);
    offer.evaluatedStepId = evaluated;
    offer.evaluatedProgressVersion = progressVersion;
}

static void CooldownWaypointTravelOffer(App &app, const std::string &stepId)
{
    if (stepId.empty())
        return;
    const double now = ImGui::GetTime();
    // Drop already-expired entries before inserting -> the map holds only currently-cooling-down steps
    // (bounded), instead of growing one entry per distinct step ever offered across the session.
    auto &cd = app.state.travelOfferCooldownUntil;
    for (auto it = cd.begin(); it != cd.end();)
    {
        if (now >= it->second)
            it = cd.erase(it);
        else
            ++it;
    }
    cd[stepId] = now + kWaypointTravelOfferCooldownSeconds;
}

static void AcceptWaypointTravelOffer(App &app)
{
    WaypointTravelOffer &offer = app.state.travelOffer;
    // Cooldown the step/target so the AUTOMATIC generator doesn't immediately re-pop the card for the waypoint
    // we just travelled to. This applies to manual accepts too: manual clicks bypass the cooldown CHECK, so it
    // only suppresses the auto re-offer, never a future manual click. (Skipping it for manualChoice was the
    // "card re-pops right after Travel" bug.)
    CooldownWaypointTravelOffer(app, !offer.evaluatedStepId.empty() ? offer.evaluatedStepId : offer.targetStepId);

    // Zone travel: commit/keep the on-foot route so declining the teleport still walks you there.
    if (offer.targetKind == TravelOfferTargetKind::ZoneTravel && offer.targetMapId != 0)
        app.travel.StartTravel(offer.targetMapId);

    if (!offer.waypointStepId.empty())
    {
        // Pan + click the resolved hop. A closer-OBJECTIVE (Step) offer keeps its waypoint AND target in the
        // active zone -> the step assist. Otherwise (TRAVEL: zone or personal) the hop may be in ANOTHER zone;
        // the open world map is one continent, so pan with the CURRENT map id (satisfies the map-assist same-map
        // guard) and relax the zone bound when the waypoint lives elsewhere. The player still confirms in-game.
        const Step *inActive = FindStepById(app.state.zone, offer.waypointStepId);
        const Step *tgtStep = offer.targetKind == TravelOfferTargetKind::Step ? FindStepById(app.state.zone, offer.targetStepId) : nullptr;
        if (inActive && tgtStep)
            StartMapAssistStep(app, *tgtStep, *inActive, WaypointLinkForStep(*inActive), false, MapAssistMode::Travel, false);
        else
        {
            const uint32_t guardMap = app.state.zone.MapId != 0 ? app.state.zone.MapId : offer.waypointMapId;
            const float cx = inActive ? inActive->CX : offer.waypointCx;
            const float cy = inActive ? inActive->CY : offer.waypointCy;
            StartMapAssistPoint(app, guardMap, cx, cy,
                                offer.waypointName.empty() ? offer.targetName : offer.waypointName,
                                MapAssistMode::Travel, true, true, offer.waypointStepId, offer.waypointLink, true, false);
            if (offer.waypointMapId != 0 && offer.waypointMapId != app.state.zone.MapId)
                app.state.mapAssist.relaxZoneBounds = true;
        }
    }
    offer.active = false;
    offer.openRequested = false;
}

enum class TravelOfferPromptResult
{
    None,
    Travel,
    ShowTarget,
    Dismiss,
};

enum class TravelMetricIcon
{
    Current,
    Waypoint,
    Saved,
};

static ImU32 TravelAlpha(ImU32 col, int alpha)
{
    return (col & 0x00FFFFFFu) | ((ImU32)std::clamp(alpha, 0, 255) << 24);
}

static void FormatTravelMeters(char *buf, size_t bufSize, float meters)
{
    FormatDistance(buf, bufSize, meters); // one canonical "<n> m" formatter (util/Draw)
}

static void DrawTravelMetricIcon(ImDrawList *dl, ImVec2 c, TravelMetricIcon icon, ImU32 accent, float scale = 1.f)
{
    const ImU32 shade = IM_COL32(0, 0, 0, 150);
    const ImU32 col = TravelAlpha(accent, 245);
    auto pt = [&](float dx, float dy)
    {
        return ImVec2(c.x + dx * scale, c.y + dy * scale);
    };
    auto line = [&](ImVec2 a, ImVec2 b, float thick = 1.6f)
    {
        dl->AddLine(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x + 1.f, b.y + 1.f), shade, thick + 1.f);
        dl->AddLine(a, b, col, thick);
    };
    auto circle = [&](ImVec2 p, float r, float thick = 1.5f)
    {
        dl->AddCircle(ImVec2(p.x + 1.f, p.y + 1.f), r * scale, shade, 24, thick * scale + 0.65f);
        dl->AddCircle(p, r * scale, col, 24, thick * scale);
    };

    if (icon == TravelMetricIcon::Current)
    {
        circle(c, 9.f, 1.4f);
        line(pt(0.f, -13.f), pt(0.f, -5.f), 1.2f * scale);
        line(pt(0.f, 5.f), pt(0.f, 13.f), 1.2f * scale);
        line(pt(-13.f, 0.f), pt(-5.f, 0.f), 1.2f * scale);
        line(pt(5.f, 0.f), pt(13.f, 0.f), 1.2f * scale);
        dl->AddCircleFilled(ImVec2(c.x + 1.f, c.y + 1.f), 2.4f * scale, shade, 12);
        dl->AddCircleFilled(c, 2.3f * scale, col, 12);
    }
    else if (icon == TravelMetricIcon::Waypoint)
    {
        const ImVec2 pts[4] = {
            pt(0.f, -12.f),
            pt(12.f, 0.f),
            pt(0.f, 12.f),
            pt(-12.f, 0.f),
        };
        ImVec2 sh[4] = {
            ImVec2(pts[0].x + 1.f, pts[0].y + 1.f),
            ImVec2(pts[1].x + 1.f, pts[1].y + 1.f),
            ImVec2(pts[2].x + 1.f, pts[2].y + 1.f),
            ImVec2(pts[3].x + 1.f, pts[3].y + 1.f),
        };
        dl->AddPolyline(sh, 4, shade, true, 3.f * scale);
        dl->AddPolyline(pts, 4, col, true, 1.8f * scale);
        dl->AddCircleFilled(ImVec2(c.x + 1.f, c.y + 1.f), 3.4f * scale, shade, 14);
        dl->AddCircleFilled(c, 3.2f * scale, col, 14);
    }
    else
    {
        line(pt(-12.f, 8.f), pt(-4.f, 8.f), 1.5f * scale);
        line(pt(-4.f, 8.f), pt(4.f, -2.f), 1.5f * scale);
        line(pt(4.f, -2.f), pt(11.f, -2.f), 1.5f * scale);
        const ImVec2 tri[3] = {
            pt(12.f, -2.f),
            pt(5.f, -7.f),
            pt(5.f, 3.f),
        };
        ImVec2 sh[3] = {
            ImVec2(tri[0].x + 1.f, tri[0].y + 1.f),
            ImVec2(tri[1].x + 1.f, tri[1].y + 1.f),
            ImVec2(tri[2].x + 1.f, tri[2].y + 1.f),
        };
        dl->AddTriangleFilled(sh[0], sh[1], sh[2], shade);
        dl->AddTriangleFilled(tri[0], tri[1], tri[2], col);
        circle(pt(-12.f, 8.f), 3.f, 1.1f);
    }
}

static void DrawTravelMetricTile(const char *id, const char *label, const char *value,
                                 TravelMetricIcon icon, ImU32 accent, ImVec2 size)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, size);
    (void)pressed;
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 b(p.x + size.x, p.y + size.y);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const ImU32 bg = active    ? IM_COL32(48, 36, 20, 178)
                     : hovered ? IM_COL32(33, 28, 18, 172)
                               : IM_COL32(5, 7, 7, 112);
    const ImU32 br = (hovered || active) ? TravelAlpha(accent, active ? 245 : 220)
                                         : IM_COL32(165, 128, 66, 72);
    dl->AddRectFilled(p, b, bg, 3.f);
    dl->AddRect(p, b, br, 3.f, 0, hovered ? 1.25f : 1.f);

    const float iconSlot = 64.f;
    const ImVec2 iconC(p.x + 36.f, p.y + size.y * 0.5f);
    DrawTravelMetricIcon(dl, iconC, icon, accent, 1.22f);

    const float labelH = 18.f;
    const float valueH = 27.f;
    const float textGap = 2.f;
    const float textBlockH = labelH + textGap + valueH;
    const ImVec2 textA(p.x + iconSlot, p.y + (size.y - textBlockH) * 0.5f);
    const ImVec2 textB(b.x - 10.f, textA.y + textBlockH);
    Gw2Ui::LabelIn(textA, ImVec2(textB.x, textA.y + labelH), label,
                   Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                   IM_COL32(196, 184, 156, 250), false, nullptr, 14.f);
    Gw2Ui::LabelIn(ImVec2(textA.x, textA.y + labelH + textGap), textB, value,
                   Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                   icon == TravelMetricIcon::Saved ? IM_COL32(255, 219, 120, 255)
                                                   : IM_COL32(245, 235, 207, 255),
                   true, nullptr, 22.f);
}

static bool DrawTravelPromptButton(const char *id, const char *label, float width, float height, bool primary)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 b(p.x + width, p.y + height);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const ImU32 accent = primary ? IM_COL32(226, 166, 70, 245) : IM_COL32(174, 143, 84, 215);
    const ImU32 bg = primary
                         ? (hovered ? IM_COL32(84, 58, 22, 218) : IM_COL32(54, 39, 18, 196))
                         : (hovered ? IM_COL32(42, 35, 24, 210) : IM_COL32(12, 14, 13, 188));
    const ImU32 br = hovered ? TravelAlpha(accent, 245) : TravelAlpha(accent, primary ? 185 : 128);
    dl->AddRectFilled(p, b, bg, 2.f);
    dl->AddRect(p, b, br, 2.f, 0, hovered ? 1.35f : 1.f);
    dl->AddRect(ImVec2(p.x + 1.f, p.y + 1.f), ImVec2(b.x - 1.f, b.y - 1.f),
                IM_COL32(255, 226, 150, primary ? 22 : 13), 1.f, 0, 1.f);

    Gw2Ui::LabelIn(p, b, label, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                   hovered ? IM_COL32(255, 231, 178, 255) : IM_COL32(224, 208, 176, 250),
                   true, nullptr, 16.f);
    return clicked;
}

static TravelOfferPromptResult DrawTravelOfferPromptCard(const WaypointTravelOffer &offer, bool allowActions)
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(680.f, std::max(280.f, display.x - 32.f));
    const bool stackedMetrics = width < 600.f;
    const float padX = width < 420.f ? 18.f : 24.f;
    const float padY = 18.f;
    const float contentW = std::max(120.f, width - padX * 2.f);
    const float titleFs = 24.f;
    const float bodyFs = 18.f;
    const float metricGap = 10.f;
    const float metricH = stackedMetrics ? 68.f : 88.f;
    const float metricsH = stackedMetrics ? metricH * 3.f + metricGap * 2.f : metricH;
    const float buttonH = 30.f;
    const float buttonGap = 12.f;

    const std::string waypointName = offer.waypointName.empty() ? "the nearest waypoint" : offer.waypointName;
    const std::string targetName = offer.targetName.empty() ? "the next objective" : offer.targetName;
    const bool hasMetrics = !offer.waypointName.empty(); // a prospective zone target has no closer-waypoint metrics
    char title[256];
    std::snprintf(title, sizeof(title), "Travel to %s?", hasMetrics ? waypointName.c_str() : targetName.c_str());
    char body[384];
    if (!hasMetrics)
        std::snprintf(body, sizeof(body), "Start guided travel to %s. Travel routes you there; Show target reveals it on the map.",
                      targetName.c_str());
    else if (offer.manualChoice)
        std::snprintf(body, sizeof(body), "This is closer to %s. Travel opens the waypoint confirmation; Show target just pans to your destination.",
                      targetName.c_str());
    else
        std::snprintf(body, sizeof(body), "This is closer to %s. Guild Wars 2 will still ask you to confirm the teleport.",
                      targetName.c_str());

    const float titleH = std::max(28.f, Gw2Ui::MeasureWrappedHeight(title, titleFs, contentW));
    const float bodyH = std::max(22.f, Gw2Ui::MeasureWrappedHeight(body, bodyFs, contentW));
    const float metricsBlock = hasMetrics ? (16.f + metricsH) : 0.f;
    const float height = std::ceil(padY + titleH + 6.f + bodyH + metricsBlock + 17.f + buttonH + padY);
    const float minCenterY = height * 0.5f + 8.f;
    const float maxCenterY = std::max(minCenterY, display.y - height * 0.5f - 8.f);
    const float centerY = std::min(std::max(display.y * 0.43f, minCenterY), maxCenterY);

    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, centerY), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
        (allowActions ? 0 : ImGuiWindowFlags_NoInputs);

    TravelOfferPromptResult result = TravelOfferPromptResult::None;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    if (ImGui::Begin("Waypoint travel suggestion##tc", nullptr, flags))
    {
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 b(p.x + width, p.y + height);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        dl->AddRectFilledMultiColor(p, b,
                                    IM_COL32(9, 16, 15, 218), IM_COL32(9, 16, 15, 218),
                                    IM_COL32(2, 5, 5, 232), IM_COL32(2, 5, 5, 232));
        dl->AddRectFilledMultiColor(p, ImVec2(b.x, p.y + 54.f),
                                    IM_COL32(255, 220, 140, 14), IM_COL32(255, 220, 140, 4),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 28));
        dl->AddRect(p, b, IM_COL32(0, 0, 0, 238), 3.f, 0, 2.f);
        dl->AddRect(ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(b.x - 2.f, b.y - 2.f),
                    IM_COL32(154, 124, 66, 125), 2.f, 0, 1.f);
        dl->AddRectFilled(ImVec2(p.x + 7.f, p.y + 10.f), ImVec2(p.x + 10.f, b.y - 10.f),
                          IM_COL32(226, 166, 70, 180), 2.f);

        float y = p.y + padY;
        const float x = p.x + padX;
        Gw2Ui::LabelIn(ImVec2(x, y), ImVec2(x + contentW, y + titleH), title,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                       IM_COL32(255, 238, 202, 255), true, nullptr, titleFs, contentW, 1.15f);
        y += titleH + 6.f;
        Gw2Ui::LabelIn(ImVec2(x, y), ImVec2(x + contentW, y + bodyH), body,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       IM_COL32(210, 202, 182, 250), false, nullptr, bodyFs, contentW);
        y += bodyH;

        if (hasMetrics)
        {
            y += 16.f;
            char current[32], after[32], saved[32];
            FormatTravelMeters(current, sizeof(current), offer.playerDistance);
            FormatTravelMeters(after, sizeof(after), offer.waypointDistance);
            FormatTravelMeters(saved, sizeof(saved), offer.savedDistance);

            if (stackedMetrics)
            {
                ImGui::SetCursorScreenPos(ImVec2(x, y));
                DrawTravelMetricTile("##metric_current", "Current", current, TravelMetricIcon::Current,
                                     IM_COL32(142, 188, 224, 235), ImVec2(contentW, metricH));
                y += metricH + metricGap;
                ImGui::SetCursorScreenPos(ImVec2(x, y));
                DrawTravelMetricTile("##metric_after", "After travel", after, TravelMetricIcon::Waypoint,
                                     IM_COL32(105, 214, 166, 235), ImVec2(contentW, metricH));
                y += metricH + metricGap;
                ImGui::SetCursorScreenPos(ImVec2(x, y));
                DrawTravelMetricTile("##metric_saved", "Saved", saved, TravelMetricIcon::Saved,
                                     IM_COL32(246, 190, 76, 245), ImVec2(contentW, metricH));
                y += metricH;
            }
            else
            {
                const float tileW = (contentW - metricGap * 2.f) / 3.f;
                ImGui::SetCursorScreenPos(ImVec2(x, y));
                DrawTravelMetricTile("##metric_current", "Current", current, TravelMetricIcon::Current,
                                     IM_COL32(142, 188, 224, 235), ImVec2(tileW, metricH));
                ImGui::SameLine(0.f, metricGap);
                DrawTravelMetricTile("##metric_after", "After travel", after, TravelMetricIcon::Waypoint,
                                     IM_COL32(105, 214, 166, 235), ImVec2(tileW, metricH));
                ImGui::SameLine(0.f, metricGap);
                DrawTravelMetricTile("##metric_saved", "Saved", saved, TravelMetricIcon::Saved,
                                     IM_COL32(246, 190, 76, 245), ImVec2(tileW, metricH));
                y += metricH;
            }
        } // hasMetrics

        // Three actions: Travel (opens GW2's waypoint confirmation), Show target (just reveals the
        // destination on the map, no click), Not now (dismiss). Widths shrink to fit a narrow card.
        const float btnW = std::min(132.f, (contentW - buttonGap * 2.f) / 3.f);
        const float buttonsW = btnW * 3.f + buttonGap * 2.f;
        y += 17.f;
        ImGui::SetCursorScreenPos(ImVec2(p.x + (width - buttonsW) * 0.5f, y));
        if (DrawTravelPromptButton("##travel_accept", "Travel", btnW, buttonH, true) && allowActions)
            result = TravelOfferPromptResult::Travel;
        ImGui::SameLine(0.f, buttonGap);
        if (DrawTravelPromptButton("##travel_show", "Show target", btnW, buttonH, false) && allowActions)
            result = TravelOfferPromptResult::ShowTarget;
        ImGui::SameLine(0.f, buttonGap);
        if (DrawTravelPromptButton("##travel_dismiss", "Not now", btnW, buttonH, false) && allowActions)
            result = TravelOfferPromptResult::Dismiss;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return result;
}

static void DrawTargetSwitchPrompt(App &app)
{
    TargetSwitchPrompt &prompt = app.state.targetSwitch;
    if (!prompt.active)
        return;
    const Step *target = FindStepById(app.state.zone, prompt.stepId);
    if (!target)
    {
        prompt = TargetSwitchPrompt{};
        return;
    }

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min(560.f, std::max(280.f, display.x - 32.f));
    const float padX = width < 420.f ? 18.f : 24.f;
    const float padY = 18.f;
    const float contentW = std::max(120.f, width - padX * 2.f);
    const float titleFs = 24.f;
    const float bodyFs = 18.f;
    const float buttonH = 30.f;
    const float buttonGap = 12.f;
    const std::string name = prompt.stepName.empty() ? (target->Name.empty() ? target->Type : target->Name) : prompt.stepName;
    const char *title = "Switch target?";
    char body[384];
    std::snprintf(body, sizeof(body), "This will clear your current travel target and select %s.", name.c_str());
    const float titleH = std::max(28.f, Gw2Ui::MeasureWrappedHeight(title, titleFs, contentW));
    const float bodyH = std::max(22.f, Gw2Ui::MeasureWrappedHeight(body, bodyFs, contentW));
    const float height = std::ceil(padY + titleH + 8.f + bodyH + 18.f + buttonH + padY);
    const float centerY = std::clamp(display.y * 0.43f, height * 0.5f + 8.f, std::max(height * 0.5f + 8.f, display.y - height * 0.5f - 8.f));

    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, centerY), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    if (ImGui::Begin("Switch travel target##tc", nullptr, flags))
    {
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 b(p.x + width, p.y + height);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(p, b, IM_COL32(9, 16, 15, 218), IM_COL32(9, 16, 15, 218),
                                    IM_COL32(2, 5, 5, 232), IM_COL32(2, 5, 5, 232));
        dl->AddRect(p, b, IM_COL32(0, 0, 0, 238), 3.f, 0, 2.f);
        dl->AddRect(ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(b.x - 2.f, b.y - 2.f),
                    IM_COL32(154, 124, 66, 125), 2.f, 0, 1.f);
        dl->AddRectFilled(ImVec2(p.x + 7.f, p.y + 10.f), ImVec2(p.x + 10.f, b.y - 10.f),
                          IM_COL32(226, 166, 70, 180), 2.f);
        float y = p.y + padY;
        const float x = p.x + padX;
        Gw2Ui::LabelIn(ImVec2(x, y), ImVec2(x + contentW, y + titleH), title,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                       IM_COL32(255, 238, 202, 255), true, nullptr, titleFs, contentW, 1.15f);
        y += titleH + 8.f;
        Gw2Ui::LabelIn(ImVec2(x, y), ImVec2(x + contentW, y + bodyH), body,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       IM_COL32(210, 202, 182, 250), false, nullptr, bodyFs, contentW);
        y += bodyH + 18.f;
        const float primaryW = 112.f;
        const float secondaryW = 112.f;
        const float buttonsW = primaryW + buttonGap + secondaryW;
        ImGui::SetCursorScreenPos(ImVec2(p.x + (width - buttonsW) * 0.5f, y));
        if (DrawTravelPromptButton("##switch_target", "Switch", primaryW, buttonH, true))
        {
            app.travel.Clear();
            app.state.manualTarget = target->StepId;
            prompt = TargetSwitchPrompt{};
        }
        ImGui::SameLine(0.f, buttonGap);
        if (DrawTravelPromptButton("##keep_target", "Keep target", secondaryW, buttonH, false))
            prompt = TargetSwitchPrompt{};
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// Auto-travel: should this offer skip TC's prompt card and go straight to the Travel action (open map + pan,
// and click-if-enabled)? Classified purely from the offer's existing fields -- no card is shown. The in-game
// teleport confirmation is UNAFFECTED: AcceptWaypointTravelOffer only pans + optionally clicks; the player
// still confirms the teleport. Source map: !manual+Step = the auto closer-waypoint pop-up; !manual+Personal =
// the auto personal (Alt+click) target; manual+ZoneTravel = a "Travel here" zone/recommend click; manual+other
// = the viewer's "Show on map" action button.
static bool AutoTravelEnabledFor(const App &app, const WaypointTravelOffer &offer)
{
    if (offer.manualChoice)
        return offer.targetKind == TravelOfferTargetKind::ZoneTravel ? app.config.autoTravelZoneClick
                                                                     : app.config.autoTravelViewerButton;
    return offer.targetKind == TravelOfferTargetKind::Personal ? app.config.autoTravelPersonalTarget
                                                               : app.config.autoTravelCloserWaypoint;
}

void DrawWaypointTravelOffer(App &app)
{
    UpdateWaypointTravelOffer(app);
    WaypointTravelOffer &offer = app.state.travelOffer;

    if (!offer.active)
    {
        DrawTargetSwitchPrompt(app);
        return;
    }

    if (AutoTravelEnabledFor(app, offer))
    {
        AcceptWaypointTravelOffer(app); // skip our card: open map + pan (+click if enabled). Player still confirms in-game.
        DrawTargetSwitchPrompt(app);
        return;
    }

    const double now = ImGui::GetTime();
    const bool suppressOpeningInput =
        offer.openRequested ||
        (offer.openedAt > 0.0 && now - offer.openedAt < kTravelOfferInputGraceSeconds);

    if (offer.openRequested)
        offer.openRequested = false;

    const TravelOfferPromptResult result = DrawTravelOfferPromptCard(offer, !suppressOpeningInput);
    if (result == TravelOfferPromptResult::Travel)
        AcceptWaypointTravelOffer(app);
    else if (result == TravelOfferPromptResult::ShowTarget)
    {
        ShowOfferTargetOnly(app, offer);
        offer.active = false;
        offer.openRequested = false;
    }
    else if (result == TravelOfferPromptResult::Dismiss)
    {
        CooldownWaypointTravelOffer(app, !offer.evaluatedStepId.empty() ? offer.evaluatedStepId : offer.targetStepId);
        offer.active = false;
    }
    DrawTargetSwitchPrompt(app);
}
