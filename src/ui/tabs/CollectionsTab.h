#pragma once
class App;

// The "Collections" settings tab: a top scope-chip shell (modelled on the Items tab) that HOSTS the
// account-unlock browsers as scopes (see the Scope enum in CollectionsTab.cpp):
//   0 = Wardrobe  (skins / dyes / outfits / legendary)  -- moved out of the Items tab
//   1 = Homestead (decorations / glyphs / cats / nodes) -- moved out of the Items tab
//   2 = Fishing   (caught-fish collection browser)      -- moved out of its own former Settings tab
//   3-13 = visual/icon cosmetics (Mounts, Gliders, Jade Bots, Skiffs, Novelties, Finishers, Mail Carriers,
//          Minis, Mist Champions, Titles, Emotes) -- via the generic DrawCosmeticScope
//   14 = Recipes  (a custom, non-cosmetic body)
// Dispatched from SettingsWindow's Collections tab.
void DrawCollectionsContent(App& app);

// Open the Collections tab at a scope (scope id 0..ScCount-1; see the Scope enum in CollectionsTab.cpp;
// -1 = leave current).
void OpenCollectionsTab(App& app, int scope = -1);

// AccountData interest mask for the active Collections scope (polled only while the tab is shown). OR'd into the
// per-frame AccountData::Tick mask in entry.cpp, mirroring ItemsNeededDomains / TradingPostNeededDomains.
unsigned CollectionsNeededDomains(App& app);
