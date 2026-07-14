#pragma once
#include <string>
class App;

// The GW2-framed settings window + its main tabs.
// ui/SettingsWindow.cpp is now just the SHELL (window frame + tab dispatch + font toggle + login cache warm);
// each tab body, the settings model + serialization, and the journal / checklist subsystems live in their own
// modules under ui/tabs/ (Diagnostics is a section inside Options). These are the entry points the entry.cpp
// lifecycle calls; the LoadJournal* / LoadPersonalStory* loaders are pure file parsers (defined in JournalTab)
// and take only a path. State is otherwise injected explicitly as App&.
enum SettingsTabId
{
    SettingsTabHub = 0,        // landing page (clickable cards to every tab); opens first
    SettingsTabAtlas,
    SettingsTabItems,
    SettingsTabCollections,    // account-unlock browsers (Wardrobe + Homestead + Fishing) hosted as scopes
    SettingsTabTradingPost,    // live Trading Post: item lookup + My TP (orders / delivery / P&L)
    SettingsTabJournal,
    SettingsTabChecklist,
    SettingsTabDungeons,
    SettingsTabTimers,         // global event/world-boss timer board (list + timeline)
    SettingsTabSessions,       // session/earnings tracker (current session + history log + earnings graph)
    SettingsTabOptions,        // settings -> demoted to second-to-last
    SettingsTabAbout,
};

void DrawSettingsWindow(App& app);                   // Render: the tabbed window (self-gates on showSettings)
void OpenSettingsTab(App& app, SettingsTabId tab);   // Open the main window and select a tab
void OpenOptionsSection(int section, const char* group = nullptr);  // Options tab: jump to a SEC_* section (+ optional subsection group)
void SaveSettings(App& app);                         // Render commit + AddonUnload
void LoadSettings(App& app);                         // AddonLoad
void LoadJournalCache(const std::string& cachePath); // AddonLoad: last session's journal tree (cache-then-refresh)
void LoadJournalOverrides(const std::string& path);  // AddonLoad: bundled fallback blurbs
void LoadJournalLocations(const std::string& path);  // AddonLoad: per-episode wiki locations
void LoadStoryTimes(const std::string& path);         // AddonLoad: bundled gw2storytimes completion-time estimates
void LoadPersonalStory(const std::string& path);     // AddonLoad: curated questId->chapter map (My Story view)
void LoadPersonalStoryCache(const std::string& path);// AddonLoad: last session's reconstructed per-character My Story
void WarmCaches(App& app);                           // Render first in-game frame: warm tab maps/icons/journal
void ApplyFontChoice(App& app);                      // OnFontLoaded + Options tab font toggle
