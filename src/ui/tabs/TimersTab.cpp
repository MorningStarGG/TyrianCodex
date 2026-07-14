#include "ui/tabs/TimersTab.h"
#include "ui/SettingsWindow.h"            // OpenSettingsTab + SettingsTabId
#include "ui/viewer/ViewerEventPanels.h"  // ViewerEvents::DrawTimersTab (the shared global event board)
#include "app/App.h"

// Thin shell: the whole board (List + Timeline + filters) lives in ViewerEventPanels.cpp alongside the existing
// map-scoped DrawTimers, so the tab and the viewer share ONE event-timer implementation.
void DrawTimersContent(App& app) { ViewerEvents::DrawTimersTab(app); }

void OpenTimersTab(App& app) { OpenSettingsTab(app, SettingsTabTimers); }
