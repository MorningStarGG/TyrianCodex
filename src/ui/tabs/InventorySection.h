#pragma once
#include <string>
#include <vector>
class App;
namespace AccountData { struct Model; }

// InventorySection: a multi-export VIEW LIBRARY (NOT a registered tab) -- it provides the Items tab's
// "My Inventory" + "Equipment" scope bodies plus the dashboard's inventory query. Data is backed by
// app/AccountData so the dashboard widgets and this full-size browser share one API/cache model.
void DrawInventoryContent(App& app);

// Set the owned-items (My Inventory) search box + reset its source filter to All. Used when an inventory
// launcher opens the Items tab with a query so the My-Inventory view shows it.
void SetInventorySearch(const char* query);

// A character's equipped gear in a hero-panel layout (armor column, weapon sets, trinket column + relic) --
// each slot a hoverable item cell, center = character header + profession class icon. The Items-tab "Equipment"
// scope. Reuses the inventory item-meta + cell render.
void DrawEquipmentContent(App& app);
// Preselect the character the Equipment view shows (used by the Alt Equipment widget / data-text launchers).
void SetEquipmentChar(const char* name);

// One matching owned item: its id + total count across every location it sits in, plus a short human summary
// of WHERE it is ("Kerah Holl x2, Bank x1") for the tooltip footer.
struct InventoryHit { int itemId = 0; int total = 0; std::string where; };
// Top owned-item matches for a query (best-first), for the dashboard Inventory widget's inline results.
std::vector<InventoryHit> QueryInventoryTop(const AccountData::Model& m, const char* query);
// Rich hover tooltip for an owned item (reuses the inventory meta + name resolver). `where` is the location
// summary line (may be empty). The widget calls this on result-row hover.
void DrawInventoryTooltip(const AccountData::Model& m, int itemId, int total, const std::string& where);
