#pragma once
class App;

// The Hub: the landing tab of the Tyrian Codex window. A welcome header + a grid of clickable cards (icon + name
// + one-line description), one per section, that jump to the matching tab. Body in ui/tabs/HubTab.cpp.
void DrawHubContent(App& app);
