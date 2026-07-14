#pragma once
#include "model/Dataset.h"          // Step
#include "model/ObjectiveTypes.h"   // Objective::MarkerKind
#include <imgui.h>            // ImU32, ImDrawList, ImVec2
#include <string>

// -----------------------------------------------------------------------------------------------------
// util/Draw: shared draw + formatting helpers and the UI enum-label tables, centralized so the renderers
// (TrailRenderer, Markers, GpsArrow), the guide panel and the settings window all use ONE copy. Pure -- no
// globals; every input is an argument (e.g. FormatEta takes the smoothed speed rather than reading it).
// -----------------------------------------------------------------------------------------------------

// Enum-label tables, parallel to the Config enum indices. inline => a single shared definition across TUs.
inline const char* const kRouteModeNames[]  = { "Trail", "Hybrid Trail", "Hybrid Nearest", "Direct Trail", "Direct Nearest" };
inline const char* const kObjCircleSurfaceNames[] = { "Map + minimap", "Map only", "Minimap only", "Off" };
inline const char* const kPathTypeNames[]   = { "On Foot", "Mount" };
inline const char* const kUpcomingNames[]   = { "All", "Open-world only", "Dungeon only", "None" };
inline const char* const kTrailColorNames[] = { "Gold", "Amber", "Cyan", "Green", "Magenta", "White" };
inline const char* const kTrailStyleNames[] = { "Footprints", "Arrows", "Dashed", "Line", "Dots" };
inline const char* const kMapTrailWidthNames[] = { "1 px", "2 px", "3 px", "4 px", "5 px", "6 px" };   // index + 1 = px
inline const char* const kViewerStyleNames[] = { "Normal Viewer", "Expanded Viewer", "None (hidden)" };
inline const char* const kRichSecondaryNames[] = { "Timed + Event Areas", "Nearby Objectives" };
inline const char* const kStepHoverStyleNames[] = { "Details", "Details & Map" };
inline const char* const kTaskChipStyleNames[] = { "Text", "Icon + Text", "Icons Only" };
inline const char* const kTaskIconSizeNames[] = { "Auto", "Small", "Medium", "Large" };
inline const char* const kTaskIconArtNames[] = { "Simple", "Expanded" };
inline const char* const kNotifyCornerNames[] = { "Top-left", "Top-center", "Top-right", "Center", "Bottom-left", "Bottom-center", "Bottom-right" };
inline const char* const kNotifyFontNames[] = { "GW2 (Menomonia)", "Stock" };
inline const char* const kDashEdgeNames[]   = { "Top", "Bottom", "Left", "Right" };
inline const char* const kHudClockPrimaryNames[]   = { "Local time", "Server time", "Tyrian time" };
inline const char* const kHudClockSecondaryNames[] = { "None", "Local time", "Server time", "Tyrian time" };
inline const char* const kQuickNotesEditorModeNames[] = { "Auto", "Popup only" };
inline const char* const kZhPillsNames[]    = { "Side-by-side", "Stacked" };
inline const char* const kZhBarNames[]      = { "Off", "Integrated", "Below banner" };
inline const char* const kZhBarTextNames[]  = { "On bar", "Above bar", "Below bar", "None" };
inline constexpr int     kZhPillsCount = 2, kZhBarCount = 3, kZhBarTextCount = 4;
inline constexpr int     kNotifyCornerCount = 7;
inline const char* const kZdAreaStyleNames[]   = { "One line", "Big banner" };
inline const char* const kZdWidthNames[]       = { "Normal", "Wide" };
inline constexpr int     kZdWidthCount = 2;
inline const char* const kZdAnimStyleNames[]   = { "Heralded (cascade + glint)", "Clean (fade + drift)", "Decode (Krytan reveal)", "Slide (sweep in)", "Twist (Krytan swipe)" };
inline const char* const kZdExitStyleNames[]   = { "Fade + lift", "Letter fade", "Dissolve", "Crumble", "Burn", "Slide" };
inline const char* const kZdShineNames[]       = { "Letter glint", "Thin sweep", "Thick sweep", "None" };
inline const char* const kZdBackdropNames[]    = { "Band", "Soft vignette", "Per-line glow", "None" };
inline constexpr int     kZdAreaStyleCount = 2, kZdAnimStyleCount = 5, kZdExitStyleCount = 6;
inline constexpr int     kZdShineCount = 4, kZdBackdropCount = 4;
inline const char* const kFontSizeNames[]   = { "14", "16", "18", "20", "22", "24", "28", "32" };  // labels = true px
inline const float       kFontSizePx[]      = { 14.f, 16.f, 18.f, 20.f, 22.f, 24.f, 28.f, 32.f };  // parallel render px
inline constexpr int     kFontSizeCount     = 8;
inline constexpr int     kViewerStyleCount  = 3;
inline constexpr int     kRichSecondaryCount = 2;
inline constexpr int     kStepHoverStyleCount = 2;
inline constexpr int     kTaskChipStyleCount = 3;
inline constexpr int     kTaskIconSizeCount = 4;
inline constexpr int     kTaskIconArtCount = 2;
inline constexpr int     kQuickNotesEditorModeCount = 2;

int         KindIndex(Objective::MarkerKind k);            // MarkerKind -> 0..4 (waypoint/poi/vista/hero/heart), else -1
ImU32       TrailColorFor(int choice, int a);              // TrailColorChoice index + alpha -> RGBA
std::string DetailText(const Step& s);                     // wiki summary, then a "Tasks:"/"Answer:" line when present
std::string FormatEta(float distMeters, float speedEma);   // ETA "m:ss" at the smoothed speed, "--" when stationary
std::string EstimateText(float minutes);                   // rough "~52m" / "~3h 40m" completion estimate ("" when <=0)
void        FormatDistance(char* buf, size_t bufSize, float meters);   // canonical "<n> m" label (buffer-filling, no alloc)
void        DrawPersonalPin(ImDrawList* dl, ImVec2 ctr, float halfExtent, bool pulse = false);  // shared orange personal-target pin
void        DrawWaypointDestIcon(ImDrawList* dl, ImVec2 center, float radius, float texHalf, ImU32 accent);  // travel-destination disc + GW2 waypoint glyph (dat 157353)
void        TypeIcon(const std::string& type, float size); // one small gw2dat type icon (advances the ImGui cursor)
