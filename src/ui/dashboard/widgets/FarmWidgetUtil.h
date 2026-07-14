#pragma once
class App;
struct FarmNode;

// Shared row drawer for the farming-aware dashboard widgets (Current Objective + Upcoming). Draws one gathering
// node (bundled material icon + name + distance/ETA + a cyan "needs <mount>" pill) at the cursor, advancing it.
// `big` = the prominent Current-Objective look (larger icon + gold name + ETA); else a compact Upcoming row.
namespace DashFarm
{
    void DrawNodeRow(App& app, const FarmNode* nd, float w, bool big);
}
