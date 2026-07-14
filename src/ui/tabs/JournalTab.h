#pragma once
#include <string>
class App;

// Story Journal tab: a clone of the in-game Story Journal (season tree + episode detail with rewards/maps) plus
// the per-character "My Story" view reconstructed from the API. Owns ALL journal + personal-story state, caches,
// and loaders. The pure file-parser loaders (LoadJournalCache/Overrides/Locations, LoadPersonalStory,
// LoadPersonalStoryCache) are declared in ui/SettingsWindow.h (the lifecycle facade) and defined here.
// Defined in ui/tabs/JournalTab.cpp.
void DrawJournalContent(App& app);          // the tab body (self-gates on data readiness)
void WarmJournal(App& app);                 // login warm: location maps + journal tree + rewards + My Story preload
const std::string& JournalRewardDiag();     // last bulk reward-fetch status (shown in the Diagnostics image-cache card)

// The current character's PERSONAL-STORY progress as reconstructed from the API (/v2/characters/{name}/quests) --
// the same data the "My Story" tree shows. `ready` is false with no key / before the fetch lands (callers fall
// back to the manual store). Lets the dashboard widget + the "Suggested next" banner reflect the real progress.
struct PsProgress { bool ready = false; int done = 0, total = 0; bool allDone = false; std::string nextName; };
PsProgress PersonalStoryProgress(App& app);
