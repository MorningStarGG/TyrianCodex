#pragma once
class App;

// The Content tab (grown from Dungeons): scope chips (Dungeons/Story/Fractals/Raids/Strikes/Home) -> a live
// progress-tracker card + the instances/places. Trackers read the shared AccountData model. Defined in
// ui/tabs/DungeonsTab.cpp.
void DrawDungeonsContent(App& app);

// Open the Content (Dungeons) tab, optionally jumping straight to a scope (0 Dungeons / 1 Story / 2 Fractals /
// 3 Raids / 4 Strikes / 5 Home); scope = -1 leaves the current scope. Mirrors OpenItemsTab/OpenCollectionsTab.
void OpenDungeonsTab(App& app, int scope = -1);

// The AccountData domains the Content tab's trackers need -- folded into the central per-frame Tick mask
// (entry.cpp) so they refresh on a TTL while the tab is open; returns 0 when the tab isn't on screen.
unsigned ContentNeededDomains(App& app);
