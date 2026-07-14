#include "ui/QuickMenu.h"
#include "app/App.h"            // App, Config, GuideState
#include "app/State.h"          // TargetOverrideKind
#include "ui/Gw2Ui.h"           // MenuNode
#include "ui/GuideViewer.h"     // CurrentTargetOverride / TargetClearLabel / ClearTargetOverride / CopyCurrentWaypoint
#include "guide/Completion.h"   // CompleteStep
#include "model/Dataset.h"      // Step (zone.Steps)
#include "ui/SettingsWindow.h"  // OpenSettingsTab (Open settings -> Options, Open Hub -> Hub)
#include "util/Draw.h"          // kRouteModeNames
#include <algorithm>

namespace
{
    constexpr int kRouteBase = 10000;   // routing leaf ids: kRouteBase + mode (0..4)
    constexpr int kToggleBase = 11000;  // toggle leaf ids:  kToggleBase + i  (0..7)
    // (Action leaf ids are the public QuickMenu::Id* values -- shared with the tray.)

    struct Tog { const char* label; bool Config::* field; };
    const Tog kToggles[] = {
        { "Guide",           &Config::showGuide },        { "Arrow",          &Config::showArrow },
        { "Trail",           &Config::showTrail },        { "Markers",        &Config::showMarkers },
        { "Map pins",        &Config::showMapPins },      { "Map trail",      &Config::showMapTrail },
        { "Minimap trail",   &Config::showMiniMapTrail }, { "Personal marker", &Config::showPersonalMarker },
    };
    constexpr int kToggleCount = (int)(sizeof(kToggles) / sizeof(kToggles[0]));
    constexpr int kRouteCount = 5;   // RouteMode count (kRouteModeNames)
}

void QuickMenu::RoutingNodes(const Config& cfg, std::vector<Gw2Ui::MenuNode>& out)
{
    Gw2Ui::MenuNode r; r.label = "Routing"; r.id = -1;
    for (int i = 0; i < kRouteCount; ++i)
    { Gw2Ui::MenuNode c; c.label = kRouteModeNames[i]; c.id = kRouteBase + i; c.selected = (cfg.routeMode == i); r.children.push_back(std::move(c)); }
    out.push_back(std::move(r));
}

void QuickMenu::TogglesNodes(const Config& cfg, std::vector<Gw2Ui::MenuNode>& out)
{
    Gw2Ui::MenuNode t; t.label = "In-world toggles"; t.id = -1;
    for (int i = 0; i < kToggleCount; ++i)
    { Gw2Ui::MenuNode c; c.label = kToggles[i].label; c.id = kToggleBase + i; c.selected = (cfg.*(kToggles[i].field)); t.children.push_back(std::move(c)); }
    out.push_back(std::move(t));
}

bool QuickMenu::ApplyConfigPick(Config& cfg, int id, bool& settingsDirty)
{
    if (id >= kRouteBase && id < kRouteBase + kRouteCount) { cfg.routeMode = id - kRouteBase; settingsDirty = true; return true; }
    if (id >= kToggleBase && id < kToggleBase + kToggleCount) { bool& b = cfg.*(kToggles[id - kToggleBase].field); b = !b; settingsDirty = true; return true; }
    return false;
}

void QuickMenu::CurrentStepNodes(std::vector<Gw2Ui::MenuNode>& out)
{
    Gw2Ui::MenuNode cs; cs.label = "Current step"; cs.id = -1;
    auto child = [&](const char* label, int id) { Gw2Ui::MenuNode c; c.label = label; c.id = id; cs.children.push_back(std::move(c)); };
    child("Mark current step done", IdMarkDone);
    child("Back (undo last step)",  IdUndo);
    child("Copy nearest waypoint",  IdCopy);
    out.push_back(std::move(cs));
}

void QuickMenu::ArrowNodes(const Config& cfg, TargetOverrideKind kind, bool dungeon, bool farming, bool fishing, bool inWorld, std::vector<Gw2Ui::MenuNode>& out)
{
    auto leaf = [&](const char* label, int id) { Gw2Ui::MenuNode n; n.label = label; n.id = id; out.push_back(std::move(n)); };
    auto sep  = [&]() { Gw2Ui::MenuNode s; s.separator = true; out.push_back(std::move(s)); };

    if (kind != TargetOverrideKind::None) leaf(TargetClearLabel(kind), IdClear);
    if (farming)                                          // a gathering run: its own actions, not the leveling checklist
    {
        leaf("Reset gathering run", IdFarmReset);         // re-show nodes you've already passed (session cross-off; nodes respawn)
        leaf("Stop gathering run",  IdFarmStop);
    }
    else if (fishing)                                     // a fishing run: nearest-hole actions
    {
        leaf("Reset fishing run", IdFishReset);           // re-show holes you've already reached this session
        leaf("Stop fishing run",  IdFishStop);
    }
    else if (dungeon) leaf("Skip this instruction", IdSkip);   // dungeons advance by instruction, not the open-world checklist
    else              CurrentStepNodes(out);                   // "Current step >": Mark done / Back / Copy -- same as the tray
    sep();
    if (!farming && !fishing) RoutingNodes(cfg, out);   // route mode doesn't apply to the nearest-target runs
    TogglesNodes(cfg, out);
    if (inWorld)
    {
        sep();
        leaf(cfg.arrowLocked ? "Unlock arrow" : "Lock arrow", IdLockArrow);
        leaf("Reset arrow position", IdResetArrow);
    }
    sep();
    leaf("Open Hub", IdHub);
    leaf("Open settings", IdSettings);
}

bool QuickMenu::ArrowDispatch(App& app, int id)
{
    if (id < 0) return false;
    if (ApplyConfigPick(app.config, id, app.settingsDirty)) return true;
    switch (id)
    {
        case IdClear:    ClearTargetOverride(app, CurrentTargetOverride(app)); return true;
        case IdMarkDone:
            if (app.state.zone.Loaded && app.state.curStep >= 0 && app.state.curStep < (int)app.state.zone.Steps.size())
            { CompleteStep(app.progress, app.state, app.state.zone.Steps[app.state.curStep].StepId); app.state.manualTarget.clear(); }
            return true;
        case IdUndo:     if (!app.state.undoStack.empty()) { app.progress.SetComplete(app.state.currentChar, app.state.undoStack.back(), false); app.state.undoStack.pop_back(); } return true;
        case IdCopy:     if (app.state.zone.Loaded && app.state.curStep >= 0) CopyCurrentWaypoint(app); return true;
        case IdLockArrow:  app.config.arrowLocked = !app.config.arrowLocked; app.settingsDirty = true; return true;
        case IdResetArrow: app.config.arrowPosX = -1.f; app.config.arrowPosY = -1.f; app.settingsDirty = true; return true;
        case IdSettings: OpenSettingsTab(app, SettingsTabOptions); return true;
        case IdHub:      OpenSettingsTab(app, SettingsTabHub);     return true;
        case IdSkip:     if (app.state.instStep < (int)app.state.instSteps.size() - 1) app.state.instStep++; return true;
        case IdFarmReset: app.state.farmGathered.clear(); return true;                              // re-show passed nodes
        case IdFarmStop:  app.state.farmRunActive = false; app.settingsDirty = true; return true;   // back to the leveling guide
        case IdFishReset: app.state.fishVisited.clear(); return true;                               // re-show reached holes
        case IdFishStop:  app.state.fishRunActive = false; return true;                             // stop the fishing run
        default:         return false;
    }
}
