#pragma once
class App;

// Map completion and event catalogs share Gw2Ui's checklist tree, controls and counts.
// Each scope retains its own search/filter/expansion state. Defined in ui/tabs/ChecklistTab.cpp.
void DrawChecklistContent(App& app);
void WarmChecklist(App& app);   // login warm: build the static tree once + prefetch every zone/region map
void OpenEventChecklist(App& app, unsigned mapId);
