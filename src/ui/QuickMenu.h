#pragma once
#include <vector>

class App;
struct Config;
enum class TargetOverrideKind;
namespace Gw2Ui { struct MenuNode; }

// Shared right-click menu bits for the nav arrow (in-world / dashboard widget / Info Panel data text) and the tray.
// Two layers: Config-level submenus reused by both the arrow menu and the tray, and the full arrow menu (built once,
// used by all three arrow surfaces). All node ids live in fixed ranges so a single dispatch decodes a pick.
namespace QuickMenu
{
    // Shared menu-action ids emitted by ArrowNodes / CurrentStepNodes and run by ArrowDispatch. (Config picks use the
    // separate routing 10000+ / toggle 11000+ ranges, also handled by ApplyConfigPick / ArrowDispatch.) A surface that
    // adds its OWN items (e.g. the tray) must use ids outside [1, 11999] so they never collide with these.
    enum { IdClear = 1, IdMarkDone, IdCopy, IdLockArrow, IdResetArrow, IdSettings, IdSkip, IdUndo, IdHub,
           IdFarmReset, IdFarmStop,     // farming-run actions (shown only while a gathering run is active)
           IdFishReset, IdFishStop };   // fishing-run actions (shown only while a fishing run is active)

    // Config-level shared submenus (used by the arrow menu AND the tray).
    void RoutingNodes(const Config& cfg, std::vector<Gw2Ui::MenuNode>& out);   // appends a "Routing" submenu (current marked)
    void TogglesNodes(const Config& cfg, std::vector<Gw2Ui::MenuNode>& out);   // appends an "In-world toggles" submenu
    void CurrentStepNodes(std::vector<Gw2Ui::MenuNode>& out);                  // appends a "Current step" submenu (Mark done / Back / Copy)
    bool ApplyConfigPick(Config& cfg, int id, bool& settingsDirty);           // routing/toggle id -> apply; else false

    // The full nav-arrow menu, shared by all three arrow surfaces. App-FREE build (caller passes the target kind;
    // the label is derived). `inWorld` includes the Lock/Reset-position items (the floating arrow only -- the widget
    // and data text own their placement via the dashboard / Info Panel).
    void ArrowNodes(const Config& cfg, TargetOverrideKind kind, bool dungeon, bool farming, bool fishing, bool inWorld, std::vector<Gw2Ui::MenuNode>& out);
    bool ArrowDispatch(App& app, int id);   // runs a picked arrow-menu id (Config picks + the App-level actions)
}
