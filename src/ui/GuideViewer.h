#pragma once
#include <cstdint>
#include <string>
#include "app/State.h"
#include "model/Dataset.h"
class App;

// The in-game guide viewer HUD panel (steps / current objective / checklist / "next up" recommendations) +
// its mode sub-panels, in ui/GuideViewer.cpp. These are the entry points entry.cpp calls; the panels + the
// recommend-list helper are internal. State is injected explicitly as App&.
void DrawGuideWindow(App& app);               // Render: the guide HUD panel (master toggle + in-game gated)
void DrawWaypointTravelOffer(App& app);       // Render: step-change prompt for closer confirmed waypoint travel
void CopyCurrentWaypoint(App& app);           // copy the current objective's nearest-waypoint chat link (arrow/tray/keybind)
void CopyWaypointForStep(App& app, const Step& step);
void ShowStepOnMap(App& app, const Step& step);
void ShowTravelTargetOnMap(App& app);
void CopyTravelTargetWaypoint(App& app);
void OpenZoneTravelChoice(App& app, uint32_t mapId);   // recommended-zone click -> Travel/Show target/Not now card
void OpenChatWaypointTravel(App& app, float cx, float cy); // clicked chat-link waypoint -> resolve + offer travel card
// A clicked Atlas / Search / waypoint-list result -> set it as the travel target and pop the SAME distance-gated
// card the recommended list + chat waypoints use (zones go through OpenZoneTravelChoice). A waypoint uses its
// chat link directly; the other objective kinds drop a personal target at the spot. Cross-zone aware + confirmed.
void TravelToLocation(App& app, const std::string& type, uint32_t mapId, float cx, float cy,
                      const std::string& name, const std::string& chatLink);
void OnObjectiveTargetClicked(App& app, const Step& step);
TargetOverrideKind CurrentTargetOverride(const App& app);
const char* TargetClearLabel(TargetOverrideKind kind);
void ClearTargetOverride(App& app, TargetOverrideKind kind);
void SetClipboard(const std::string& s);      // generic clipboard set (pure Win32; also the AddonLoad travel sink)
void ViewerAlert(const char* message);        // generic notification toast (Notify::Push, Action kind)
