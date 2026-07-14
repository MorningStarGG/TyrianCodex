#pragma once
class App;

// The "Timers" settings tab: a GLOBAL, all-maps event board (List + GW2-wiki-style Timeline) with category +
// text filters and click-to-guide. The body delegates to ViewerEvents::DrawTimersTab (one impl, shared with the
// map-scoped event panels); this is just the F1 tab shell.
void DrawTimersContent(App& app);
void OpenTimersTab(App& app);   // open the settings window on the Timers tab
