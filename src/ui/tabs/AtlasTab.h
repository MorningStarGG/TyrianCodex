#pragma once
class App;

// The "Atlas" settings tab: a searchable, filterable database of every world location we hold in memory
// (waypoints / POIs / vistas / hero points / hearts + the zones), with travel + show-on-map + copy + a
// map-on-hover preview. Body in ui/tabs/AtlasTab.cpp; the data + row come from ui/search/.
void DrawAtlasContent(App& app);

// Open the Atlas settings tab, optionally pre-selecting a browse type filter (Search::Kind + 1; <0 = leave as-is)
// and/or a map-kind ("City", "Dungeon", ...; nullptr = leave). e.g. typeSel = (int)Search::Kind::Zone + 1 for Zones.
void OpenAtlasTab(App& app, int typeSel = -1, const char* kind = nullptr);

// Open the Atlas tab at one of its row-1 browse SCOPES (index parallels the tray's Atlas scope labels).
void OpenAtlasScope(App& app, int index);
