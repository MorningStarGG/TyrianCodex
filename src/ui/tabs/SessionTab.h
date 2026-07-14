#pragma once
class App;

// The "Sessions" settings tab: the Session / Earnings Tracker. Shows the live current session (per-currency
// wallet deltas + a timer, with Reset / Save), a persisted per-character history log (delete / wipe), and a
// small earnings-over-time bar graph for a chosen currency. The lifecycle + data live in SessionTracker /
// SessionHistoryStore; this is the F1 tab shell + rendering.
void DrawSessionContent(App& app);
// Open the settings window on the Sessions tab. scope >= 0 jumps to a scope chip
// (0 Session / 1 Currencies / 2 Levels / 3 Objectives); scope < 0 leaves the current scope.
void OpenSessionTab(App& app, int scope = -1);
