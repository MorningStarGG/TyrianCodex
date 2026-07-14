#pragma once
#include "app/App.h"
#include <imgui.h>

enum { ViewerStyleCompact = 0, ViewerStyleRich = 1, ViewerStyleNone = 2 };   // None = panel hidden (the dashboard elements pass their own compact flag, so they're unaffected)
enum { RichSecondaryEvents = 0, RichSecondaryNearby = 1 };
enum { CompactTabBase = 0, CompactTabEvents = 1 };   // State.compactTab values (Farming is the shared farmRunActive flag)
enum { TaskChipStyleText = 0, TaskChipStyleIconText = 1, TaskChipStyleIconsOnly = 2 };
enum { TaskIconSizeAuto = 0, TaskIconSizeSmall = 1, TaskIconSizeMedium = 2, TaskIconSizeLarge = 3 };
enum { TaskIconArtSimple = 0, TaskIconArtExpanded = 1 };

struct ViewerWindowSizing
{
    ImVec2 Min;
    ImVec2 Max;
    ImVec2 Preferred;
    float  Aspect = 0.f;     // width / height; 0 means free resize
};

struct ViewerAspectConstraint
{
    float Aspect;
    float MinW;
    float MaxW;
};

int  ViewerStyleIndex(const App& app);
int  TaskChipStyleIndex(const App& app);
int  TaskIconSizeIndex(const App& app);
int  ResolvedTaskIconSize(const App& app, bool compact);
int  TaskIconArtIndex(const App& app);
int  RichSecondaryIndex(const App& app);

bool ViewerCompact(const App& app);
bool ViewerRich(const App& app);
bool TaskIconsExpanded(const App& app);

// Whether a FARMING RUN is active this frame (drives the in-world arrow + suppresses the leveling arrow, and
// switches the dashboard objective widgets to farming). True when the shared State.farmRunActive flag is set AND
// the run is allowed (FarmingTabAvailable). Independent of the viewer panel -- the run works with it hidden.
bool FarmingRunActive(App& app);
bool FarmingMapHasData(App& app);   // current map has a bundled farming dataset
// Is a farming run allowed on this map this frame? (map has data AND not travelling / in a dungeon -- those own
// the arrow, so farming stands down to avoid hijacking it.) Gates the Farming chip + the run.
bool FarmingTabAvailable(App& app);

// Fishing-hole run gates (mirror the farming ones). A fishing run stands down for travel / dungeons / a farming run.
bool FishingRunActive(App& app);
bool FishingMapHasData(App& app);   // current map has a bundled fishing-hole dataset
bool FishingTabAvailable(App& app);

float ViewerPanelAutoWidth(const App& app);
// Uniform guide-viewer zoom multiplier (clamped). Multiply layout px (icons, row heights, insets, window
// bounds) by this; viewer TEXT + Gw2Ui measurements are scaled separately by a PushTextScale in the viewer.
float ViewerScale(const App& app);
ImVec2 ViewerWindowPadding(const App& app);
ViewerWindowSizing StepWindowSizing(const App& app);
ImVec2 ClampStepWindowSize(ImVec2 size, const ViewerWindowSizing& s);
ImVec2 InitialStepWindowSize(ImVec2 saved, const ViewerWindowSizing& s);
void FixedAspectResizeCallback(ImGuiSizeCallbackData* data);
ImVec2 ResetGuidePanelSize(const App& app);
