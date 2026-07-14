#pragma once
#include "ui/Gw2Ui.h"   // RowHotspot, Row/RowLabel, MeasureWidth, kGold/kText*, ImVec2/ImU32, std::string/vector/function

// ---------------------------------------------------------------------------------------------------------------
// The Gallery framework -- a shared left RAIL (here) + a canonical gallery-browser SHELL (added in a later step),
// composing the existing Gw2Ui primitives so the Collections + Items "gallery" surfaces stop hand-rolling the same
// toolbar / rail / order-cache / grid+list. Part of the Gw2Ui framework (namespace Gw2Ui), kept in its OWN header
// so the descriptor can evolve without rebuilding every consumer of the universal Gw2Ui.h.
// ---------------------------------------------------------------------------------------------------------------
namespace Gw2Ui
{
    // A node in a gallery rail tree. `key` is the stable selection id ("" is conventionally the "All" root row).
    // `have`/`total` drive the right-aligned count badge (total < 0 -> no badge). children = 0..N levels (a flat
    // rail, a 2-level category->subkind, or a 3-level weight->slot tree) -- the rail recurses to any depth.
    struct GalleryRailNode
    {
        std::string key, label;
        int have = -1, total = -1;
        std::vector<GalleryRailNode> children;
    };

    // The rail's look (canonical defaults; a caller may tweak per scope). Clean Menomonia downscale ratios only.
    struct GalleryRailStyle
    {
        float rowH       = 34.f;   // row height, all levels
        float labelTop   = 18.f;   // level-0 label font px
        float labelChild = 16.f;   // deeper-level label font px
        float badgeFont  = 16.f;   // "N / M" count-badge font px
        float indentPx   = 16.f;   // per-level indent
        bool  collapseTopLeaf = false;   // re-click a SELECTED top-level leaf -> clear to the root ("" / All); off = it stays selected
    };

    // Draw a left rail (inside its OWN BeginChild `id`) from `root`'s CHILDREN -- `root` itself is the unseen
    // container, so include an "All" node (key "") as the first child if you want one. A node is EXPANDED iff
    // `*selectedKey` is it or a descendant, so selecting a parent opens it and selecting a child keeps the path
    // open (no extra expansion state). Clicking a node sets `*selectedKey` to its key; re-clicking a selected LEAF
    // clears back to its parent. Returns true the frame the selection changed (caller can rebuild on it).
    bool GalleryRail(const char* id, float width, float height,
                     const GalleryRailNode& root, std::string* selectedKey,
                     const GalleryRailStyle& style = GalleryRailStyle{});

    // ---------------------------------------------------------------------------------------------------------
    // The canonical gallery SHELL. The caller fills the descriptor; DrawGalleryBrowser composes the existing
    // Gw2Ui primitives into ONE layout, top to bottom:
    //   [ toolbar: search (fills) ... filters ... [sort][asc] [grid|list] ]   (cluster wraps to row 2 if narrow)
    //   [ optional extra toolbar row ]
    //   [ header: "Title  --  N / M verb" + rightStatus ]
    //   [ optional progress bar ]
    //   [ optional RAIL | VSplitter | grid-or-list body ]
    // It takes NO App& -- the few Config bits (grid/list viewMode, rail width, settingsDirty) come in by pointer.
    // CANONICAL TEXT: header 18px; rail 18/16 (GalleryRailStyle); the caller's grid/list hatches SHOULD use
    // 18 (primary) / 16 (secondary) with COLOUR emphasis (gold / rarity / the gold rail stripe), never faux-bold.
    // ---------------------------------------------------------------------------------------------------------
    struct GalleryBrowser
    {
        const char* id = "";                  // stable, unique per scope -> keys the order cache + the rail width

        // header
        const char* title = "";
        int   have = -1, total = -1;          // -1/-1 => no "N / M" tally
        const char* tallyVerb = "unlocked";   // "unlocked" / "owned" / ...
        const char* rightStatus = nullptr;    // SectionHeader right-side text ("updating...", else nullptr)
        bool  showProgress = false;           // bar under the header (drawn only when have>=0 && total>0)
        std::function<void(float availW)> drawProgress;   // optional: a custom progress viz drawn INSTEAD of the default bar
        float headerFontSize = 18.f;
        // Optional header OVERRIDE: when set, the shell draws this string (a plain dim Label at headerFontSize)
        // INSTEAD of the "Title -- N / M verb" SectionHeader, given the live filtered order count (All Items'
        // "N items (tradeable)" line). The order is computed BEFORE the header so the count is exact.
        std::function<std::string(int orderCount)> headerText;

        // toolbar
        bool  search = true; const char* searchHint = "Search..."; char* searchBuf = nullptr; int searchBufSize = 0;
        struct FilterDropdown { const char* label; const char* const* options; int count; int* selected; float width = 132.f; };
        const FilterDropdown* filters = nullptr; int filterCount = 0;
        const char* const* sortOptions = nullptr; int sortCount = 0; int* sortSelected = nullptr; bool* sortAsc = nullptr;
        bool  gridListToggle = true; int* viewMode = nullptr; bool* settingsDirty = nullptr; bool textOnlyList = false;
        std::function<void(float availW)> extraFilterRow;   // optional extra toolbar row (e.g. All Items level range)

        // rail (optional)
        const GalleryRailNode* railRoot = nullptr; std::string* railSelectedKey = nullptr;
        float* railWidthPx = nullptr; float railDefaultW = 200.f; GalleryRailStyle railStyle;

        // body: the shell owns the cached order (rebuilt only when orderToken moves) + the grid/list
        // virtualization; the hatches draw each item (so APNG renders, swatches, badges, click + tooltip stay
        // per-scope). The hatches receive the CATALOG index (order[i]) -- they never see the order vector.
        uint64_t orderToken = 0;
        std::function<void(std::vector<int>& order)> rebuildOrder;   // filter+sort the catalog indices into `order`
        std::function<void(int catalogIndex, ImVec2 cellMin, float cell)> drawGridCell;
        std::function<void(int catalogIndex, const RowHotspot& row)> drawListRow;
        // List-mode WHOLE-BODY hatch (takes precedence over drawListRow in list mode): the caller draws the entire
        // list body from `order` itself -- for a table (Gw2Ui::DataTable) that owns its own columns + clipper, which
        // the shell's per-row drawListRow can't express. Runs inside the shell's body child.
        std::function<void(const std::vector<int>& order)> drawListBody;
        float gridCell = 58.f, gridGap = 6.f, listRowH = 38.f;
        const char* emptyText = "No matches.";
    };

    void DrawGalleryBrowser(const GalleryBrowser& d);   // no App& -- Config bits arrive via the descriptor pointers

    // ---------------------------------------------------------------------------------------------------------
    // A sibling of GalleryBrowser for COMPLETION CHECKLISTS (owned/locked collections -- homestead glyphs/cats/
    // nodes, achievement-style lists). It SHARES the gallery frame (toolbar + header + progress + order cache)
    // but renders a "checked-off" body: the framework draws the owned dot + status + name/(2-line sub) CENTRALLY;
    // the caller supplies the row data (rowData) + optional per-row interaction (onRow: tooltip / click / copy).
    // No grid / sort / rail (a checklist is one owned-first list). CANONICAL TEXT: name 18 (or 18/15 two-line),
    // status 15, one-line 16 -- clean Menomonia ratios. Reuses GalleryBrowser::FilterDropdown for its filters.
    // ---------------------------------------------------------------------------------------------------------
    struct ChecklistBrowser
    {
        const char* id = "";                  // stable, unique per scope -> keys the shared order cache
        // header
        const char* title = ""; int have = -1, total = -1; const char* tallyVerb = "unlocked";
        const char* rightStatus = nullptr; bool showProgress = true;
        std::function<void(float availW)> drawProgress;   // optional custom viz instead of the default bar
        float headerFontSize = 18.f;
        // toolbar (search + filter dropdowns; no sort / grid)
        bool search = true; const char* searchHint = "Search..."; char* searchBuf = nullptr; int searchBufSize = 0;
        const GalleryBrowser::FilterDropdown* filters = nullptr; int filterCount = 0;
        bool* settingsDirty = nullptr;
        // body
        bool  twoLine = false;                // name over a dim sub-line (cats); else "name  -  sub" on one row
        const char* statusOwned = "Owned"; const char* statusLocked = "Locked";
        uint64_t orderToken = 0;
        std::function<void(std::vector<int>& order)> rebuildOrder;   // filter+sort catalog indices into `order`
        struct RowData { bool owned = false; const char* name = ""; const char* sub = nullptr; };   // stable strings
        std::function<RowData(int catalogIndex)> rowData;            // framework draws dot + name/sub + status from this
        std::function<void(int catalogIndex, const RowHotspot& row)> onRow;   // optional: hover tooltip / click / copy
        float rowH = 38.f, rowHTwoLine = 54.f;
        const char* emptyText = "No matches.";
    };
    void DrawChecklistBrowser(const ChecklistBrowser& d);
}
