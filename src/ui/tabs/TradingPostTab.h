#pragma once
class App;

// The "Trading Post" settings tab. A scope-chip shell over four views (see g_tpView in the .cpp):
//   - My TP (needs the tradingpost API scope): the delivery box, your current buy/sell orders with undercut
//     flags, and a profit/loss summary over your recent transaction history.
//   - Flip Finder: scan tradeable items for buy/sell spread + ROI flip opportunities.
//   - Crafting: the buy-vs-craft cart (CraftingEngine).
//   - Charts: a datawars2 price-history chart for a picked item, or the market-wide index.
// Fetches the detail itself via the typed Commerce client (AccountData keeps only the lightweight counts).
void DrawTradingPostContent(App& app);
void OpenTradingPostTab(App& app, int view = -1);   // view 0..3 jumps to that scope chip; -1 = leave current
void OpenCraftingCart(App& app);   // open the Trading Post tab on the Crafting view (cart) -- HUD / Info Panel / widget actions

// Account domains the TP tab currently needs polled (DomMaterials while the Crafting view's "use own materials"
// is on). ORed into the central AccountData::Tick mask in entry.cpp. 0 when nothing extra is needed.
unsigned TradingPostNeededDomains();
