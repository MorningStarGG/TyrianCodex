#pragma once
#include <functional>
#include <string>
#include <vector>
#include <imgui.h>             // ImU32
#include "app/AccountData.h"   // AccountData::ItemMeta + Api::V2::ItemSlot / MaterialStack

// -----------------------------------------------------------------------------------------------------
// Shared item rendering -- the GW2-styled item cell + rich tooltip + the pure field helpers, used by BOTH the
// inventory tab (owned items) and the all-items catalog (data-source agnostic). Pass the item's ItemMeta (base
// fields + the type-specific `details` json), an optional per-copy Instance (count / location / binding /
// upgrades), and a NameFn that resolves OTHER item ids to names (the upgrade/infusion list + the unknown-item
// fallback title). Lifted verbatim from InventoryTab.cpp so behavior is unchanged.
// -----------------------------------------------------------------------------------------------------
namespace ItemUI
{
    // ---- pure field helpers ----
    ImU32       RarityColor(const std::string& rarity);
    std::string Humanize(std::string s);
    std::string CleanDescription(std::string s);
    std::string JoinStrings(const std::vector<std::string>& values, const char* sep = ", ");
    std::string CoinText(int copper);
    std::string DurationText(int ms);
    std::string DetailStr(const AccountData::ItemMeta& meta, const char* key);
    int         DetailInt(const AccountData::ItemMeta& meta, const char* key);
    std::string ItemTypeLine(const AccountData::ItemMeta& meta);
    std::string ItemDetailLine(const AccountData::ItemMeta& meta);
    bool        HasString(const std::vector<std::string>& values, const char* needle);

    // Per-frame gate (default on): item-cell/tooltip callers set this from Config.showTpPrices so the shared
    // renderers can show live Trading Post prices without an App& reference.
    void SetTpPrices(bool on);

    using NameFn = std::function<std::string(int)>;

    // Per-owned-copy extras; all defaults => a plain catalog item (no count badge / binding / upgrades).
    struct Instance
    {
        int count = 0;
        std::string location;
        const Api::V2::ItemSlot* slot = nullptr;
        const Api::V2::MaterialStack* material = nullptr;
    };

    // Rich tooltip for an item: meta may be null (unknown -> name via nameFor). statSet (optional) adds the
    // attribute-combo prefix line ("Berserker's") for catalog items. `extra` (optional) is invoked just before
    // the copy-menu footer so callers can append their own lines (e.g. the crafting "Known by:" line).
    void DrawItemTooltip(int id, const AccountData::ItemMeta* meta, const Instance& inst, const NameFn& nameFor,
                         const std::string& statSet = "", const std::function<void()>& extra = {});
    // Static item info for an already-open tooltip: type/stat lines, description, and item details/stats only.
    // No account copy data, live TP prices, vendor value, flags, bindings, upgrades, or copy-menu text.
    void DrawItemLoreBlock(const AccountData::ItemMeta& meta, const std::string& statSet = "",
                           bool includeTypeLines = true);
    // Shared right-click menu for every item surface. Open with openNow=true on the right-click frame.
    void DrawCopyMenu(const char* popupId, bool openNow, const std::string& name, const std::string& chatLink);

    // Square item cell: icon + rarity border + count badge + hover tooltip + right-click copy menu.
    // Returns true on a left-click (for selection); callers may ignore it.
    bool DrawItemCell(int id, const AccountData::ItemMeta* meta, const Instance& inst, const NameFn& nameFor,
                      float cell, const std::string& statSet = "");

    // A compact horizontal search-result row for the dashboard Items + Inventory widgets: icon (rarity
    // border) + name + optional "xN" count + the alternating row stripe / hover highlight. Returns click +
    // hover so the caller attaches its own tooltip + open action; count <= 0 hides the count. id keys the
    // icon texture cache + the per-row ImGui id.
    struct ResultRow { bool clicked = false; bool hovered = false; };
    ResultRow DrawResultRow(int rowIndex, float width, int id, const std::string& icon,
                            const std::string& name, const std::string& rarity, int count = 0,
                            const std::string& chatLink = "");
}
