#pragma once
#include "app/CosmeticCatalog.h"

class App;

// The Collections-tab visual-cosmetic scopes: a generic browser over a CosmeticCatalog::Kind
// (Mounts / Gliders / Jade Bots / Skiffs / Novelties / Finishers / Mail Carriers / Minis / Mist Champions /
// Titles / Emotes). Mirrors the Wardrobe skins gallery -- a grid/list (honoring app.config.itemsView), a lock
// filter (All/Locked/Unlocked), search, an "N / M unlocked" header, unlocked-first order behind a change token
// (no per-frame scans), per-cell icon + lock dim + parity tooltip + right-click menu (Open in Wiki / Copy name);
// Titles/Emotes are text-only cells. Unlock state comes from AccountData. Mounts (by mount type) and Mist
// Champions (by hero) additionally get a LEFT TYPE RAIL (by the catalog `sub` value); the rest are flat grids.
// Dispatched from DrawCollectionsContent.
void DrawCosmeticScope(App& app, CosmeticCatalog::Kind kind);
