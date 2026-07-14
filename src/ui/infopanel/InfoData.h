#pragma once
#include "render/glyphs/Glyphs.h"
#include <map>
#include <string>
#include <vector>
class App;
namespace Gw2Ui { struct MenuNode; }   // right-click menu node (defined in Gw2Ui.h; only referenced by pointer/ref here)

// -----------------------------------------------------------------------------------------------------
// InfoData: the Info Panel's data-text catalog (declarative, like the dashboard widget registry). Each text is
// a stable key + a compute() returning a segment, plus optional tooltip / click actions / per-text options. The
// shared clock/time helpers live in ui/InfoHudShared.h; the HUD launcher's buttons in ui/hud/HudButtons.h.
// -----------------------------------------------------------------------------------------------------
namespace InfoData
{
    // ---- Info Panel data-text catalog ----
    enum class HudFam { Location, Character, Economy, System };

    // Per-text options: the Info Panel stores a {key->int} map PER text PER profile; compute() reads it through
    // this accessor, and the settings UI renders one control per declared HudOption (Bool/Enum).
    struct HudOpts
    {
        const std::map<std::string, int>* m = nullptr;
        int Get(const char* k, int def) const { if (!m) return def; auto it = m->find(k); return it == m->end() ? def : it->second; }
    };

    struct HudSeg
    {
        std::string label;
        std::string value;                                  // empty value AND no paint -> segment hidden this frame
        ImU32       color = IM_COL32(236, 230, 212, 255);   // bright cream value by default
        // optional: paint a square icon (e.g. the route arrow) instead of the value text. When set, the panel
        // reserves a `paintSize` (or bar-height) square and calls this at draw time with the live App/opts.
        void      (*paint)(App&, const HudOpts&, ImDrawList*, ImVec2 center, float size) = nullptr;
        float       paintSize = 0.f;
        bool        locked = false;   // draw the shared travel padlock (Render::DrawLock) before the value
        void*       icon = nullptr;   // optional TYPE icon (heart/POI/material) drawn just before the value
    };

    struct HudOption
    {
        const char*        key;        // stable per-text option key
        const char*        label;
        enum class Kind { Bool, Enum, Int } kind;
        int                def = 0;
        const char* const* enames = nullptr;   // Enum: choice labels
        int                ecount = 0;
        int                imin = 0, imax = 0;  // Int: slider range
    };

    struct HudText
    {
        const char* key;        // STABLE id (persistence)
        const char* title;      // settings label
        HudFam      family;
        unsigned    domains;    // AccountData::Domain bits this text needs (0 = local-only)
        bool        defaultOn;    // in the default profile (shown out of the box)
        int         defaultZone;  // where it lands when placed: 0 Left / 1 Center / 2 Right
        int         defaultOrder; // bar order among default-on texts (0 = unspecified / not-default)
        HudSeg    (*compute)(App&, const HudOpts& opts);
        void      (*tip)(App&, const HudOpts& opts) = nullptr;   // optional rich hover tooltip (builds its own Gw2Ui tooltip)
        // Optional interactions. onClick = the left-click primary action (travel / copy / open a tab). actions =
        // push right-click menu labels (the panel prepends them ABOVE its layout items); onAction dispatches the
        // chosen one by index. All re-derive their live data on demand (fired only on click, never per frame).
        void      (*onClick)(App&, const HudOpts& opts) = nullptr;
        void      (*actions)(App&, const HudOpts& opts, std::vector<const char*>& out) = nullptr;
        void      (*onAction)(App&, const HudOpts& opts, int idx) = nullptr;
        // Optional: a left-click POPUP body (e.g. a waypoint list). When set, left-click opens a popup whose
        // contents this renders (takes precedence over onClick). The panel frames + positions the popup.
        void      (*popup)(App&, const HudOpts& opts) = nullptr;
        // Optional: a text's OWN right-click items (e.g. the Loadout switcher with submenus). The panel composes
        // these ABOVE a shared "Bar Control" submenu into one ContextMenuTree. menuNodes appends the text's nodes
        // (leaves/submenus, with ids in the text's own space); menuPick handles a pick of one of those ids. A text
        // uses EITHER menuNodes (takes precedence) OR actions/onAction for its special items, not both.
        void      (*menuNodes)(App&, const HudOpts& opts, std::vector<Gw2Ui::MenuNode>& out) = nullptr;
        void      (*menuPick)(App&, const HudOpts& opts, int picked) = nullptr;
    };
    const std::vector<HudText>&   HudTexts();      // built once
    const HudText*                FindText(const char* key);
    const char*                   FamilyName(HudFam f);
    const std::vector<HudOption>& OptionsFor(const char* key);   // per-text option descriptors (empty if none)
}
