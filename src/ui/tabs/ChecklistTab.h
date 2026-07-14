#pragma once
class App;

// Checklist tab: the cached Release > Region > Zone > Type > objective tree, with manual ticking, search, and per-node done/total counts.
// Owns the checklist-tree state. Defined in ui/tabs/ChecklistTab.cpp.
void DrawChecklistContent(App& app);
void WarmChecklist(App& app);   // login warm: build the static tree once + prefetch every zone/region map
