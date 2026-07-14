#pragma once
class App;

// The "Items" settings tab: a top scope toggle over 3 scopes -- All Items / My Inventory / Equipment. "All Items"
// is the Atlas-styled whole-game item browser (ItemCatalog) with a type/subtype rail + filter bar + icon/list
// views; the others are the existing owned views (DrawInventoryContent / DrawEquipmentContent), unchanged.
// (Wardrobe, Homestead + Legendary Armory moved to the Collections tab.) Dispatched from SettingsWindow's Items tab.
void DrawItemsContent(App& app);
void ShutdownItemsTab();   // free the per-item parsed-meta cache on addon disable/unload (call from AddonUnload)

// AccountData interest mask for the active Items scope. OR'd into the per-frame AccountData::Tick mask in
// entry.cpp, mirroring TradingPostNeededDomains.
unsigned ItemsNeededDomains(App& app);

// Open the Items tab at a scope (0 = All Items, 1 = My Inventory; -1 = leave current) + optionally prefill the
// search (the catalogue search for All Items, the owned-items search for My Inventory). The shared entry point
// for every HUD / Info Panel / dashboard "items" or "inventory" launcher.
void OpenItemsTab(App& app, int scope = -1, const char* search = nullptr);

// Rich hover tooltip for a catalog item by its Query()/At() index (reuses the Items-tab meta cache). The
// dashboard Items widget calls this on row hover.
void DrawItemCatalogTooltip(int catalogIndex);
