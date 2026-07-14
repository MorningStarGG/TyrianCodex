#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/SettingsWindow.h"
#include "ui/tabs/ItemsTab.h"        // OpenItemsTab (open the full inventory on the My Inventory scope)
#include "ui/tabs/InventorySection.h"    // QueryInventoryTop + DrawInventoryTooltip (inline search results)
#include "ui/items/ItemRender.h"     // ItemUI::DrawResultRow (shared result-row rendering)
#include "ui/Gw2Ui.h"
#include "ui/UiStrings.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// Character bag overview + launcher for the full account inventory browser.
namespace { std::string g_sel; }

namespace
{
    const char* InvStateText(AccountData::InventoryState state)
    {
        switch (state)
        {
        case AccountData::InventoryState::Loading: return "indexing";
        case AccountData::InventoryState::Complete: return "inventory ready";
        case AccountData::InventoryState::Partial: return "inventory partial";
        case AccountData::InventoryState::Error: return "inventory error";
        case AccountData::InventoryState::Idle:
        default: return "inventory idle";
        }
    }

    ImU32 BagFillColor(float frac)
    {
        struct Stop { float at; int r, g, b; };
        static constexpr Stop stops[] = {
            { 0.00f, 105, 190, 145 },
            { 0.35f, 150, 205, 125 },
            { 0.55f, 205, 195,  90 },
            { 0.72f, 225, 165,  70 },
            { 0.86f, 220, 115,  70 },
            { 1.00f, 205,  65,  65 },
        };
        frac = std::max(0.f, std::min(1.f, frac));
        for (int i = 1; i < (int)(sizeof(stops) / sizeof(stops[0])); ++i)
        {
            if (frac > stops[i].at) continue;
            const Stop& a = stops[i - 1];
            const Stop& b = stops[i];
            const float t = (frac - a.at) / std::max(0.001f, b.at - a.at);
            const int r = (int)(a.r + (b.r - a.r) * t);
            const int g = (int)(a.g + (b.g - a.g) * t);
            const int bl = (int)(a.b + (b.b - a.b) * t);
            return IM_COL32(r, g, bl, 255);
        }
        return IM_COL32(stops[5].r, stops[5].g, stops[5].b, 255);
    }
}

void DashW::Inventory(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Inventories, "inventories")) return;
    if (!AccountData::HasScope(Api::TokenPermission::Characters))
    {
        Gw2Ui::Label(UiStr::NeedScope("characters"), IM_COL32(180, 150, 110, 255), false, nullptr, 14.f);
        if (Gw2Ui::ActionButton("Open Inventory", w, 24.f, Gw2Ui::ActionButtonVariant::Primary, "Open the account inventory browser"))
            OpenItemsTab(app, 1);
        return;
    }

    const std::string name = DashApi::CharacterPicker("##inventory", g_sel, w);
    if (name.empty()) return;
    ImGui::Spacing();

    const AccountData::Model& m = AccountData::Get();
    auto it = m.inv.find(name);
    if (it == m.inv.end() || !it->second.have || it->second.totalSlots == 0)
    {
        Gw2Ui::Label("Loading bags...", Gw2Ui::kTextDim, false, nullptr, 14.f);
        return;
    }
    const int used = it->second.usedSlots, total = it->second.totalSlots;
    const float frac = total > 0 ? (float)used / (float)total : 0.f;
    char lbl[48]; std::snprintf(lbl, sizeof(lbl), "%d/%d slots used", used, total);
    Gw2Ui::ProgressBar(frac, lbl, w, 16.f, BagFillColor(frac));
    if (frac > 0.92f)
        Gw2Ui::Label("Bags nearly full!", IM_COL32(240, 150, 120, 255), false, nullptr, 14.f);

    const AccountData::InventoryIndexStatus& st = AccountData::InventoryStatus();
    if (st.state == AccountData::InventoryState::Loading)
    {
        char b[72];
        std::snprintf(b, sizeof(b), "%s %d/%d", InvStateText(st.state), st.completedSources, st.totalSources);
        Gw2Ui::Label(b, Gw2Ui::kTextSub, false, nullptr, 14.f, 0.4f);
    }
    else if (st.state == AccountData::InventoryState::Partial || st.state == AccountData::InventoryState::Error)
    {
        Gw2Ui::Label(InvStateText(st.state), IM_COL32(235, 160, 110, 255), false, nullptr, 14.f, 0.4f);
    }
    ImGui::Dummy(ImVec2(0.f, 4.f));
    // Search-your-inventory bar + a compact Open button on one row. Typing a query and pressing Open jumps to
    // the Items tab on My Inventory with that OWNED-items search applied (distinct from the catalogue search).
    static char invq[96] = "";
    const float btnW = 78.f, gap = 6.f, btnH = 26.f;
    const float sbW  = (w - btnW - gap > 90.f) ? (w - btnW - gap) : 90.f;
    // Just "Search..." -- the Open button + magnifier icon eat the width, so the longer "Search inventory..."
    // clips even at full width; the "Inventory" title + Open button already make the context clear.
    const ImVec2 rowP = ImGui::GetCursorScreenPos();
    Gw2Ui::SearchBox("##invsearch", invq, sizeof(invq), sbW, "Search...");
    const float searchH = ImGui::GetCursorScreenPos().y - rowP.y;             // the box's real height (font + padding)
    ImGui::SetCursorScreenPos(ImVec2(rowP.x + sbW + gap, rowP.y + (searchH - btnH) * 0.5f));   // centre the button on it
    if (Gw2Ui::ActionButton("Open", btnW, btnH, Gw2Ui::ActionButtonVariant::Primary, "Open the inventory browser (with your search)"))
        OpenItemsTab(app, 1, invq[0] ? invq : nullptr);
    ImGui::SetCursorScreenPos(ImVec2(rowP.x, rowP.y + searchH));              // leave the cursor cleanly below the row

    // Inline owned-item results (mirrors the Items / Search widgets): top matches across every source, click a
    // row (or the footer) to open the full My-Inventory view with the same search; hover for the rich tooltip.
    if (invq[0])
    {
        ImGui::Dummy(ImVec2(0.f, 2.f));
        const std::vector<InventoryHit> hits = QueryInventoryTop(m, invq);
        if (hits.empty())
        {
            Gw2Ui::Label("No matching items.", Gw2Ui::kTextSub, false, nullptr, 14.f);
            return;
        }
        const int n = std::min((int)hits.size(), 8);
        for (int i = 0; i < n; ++i)
        {
            const InventoryHit& h = hits[i];
            auto mi = m.itemMeta.find(h.itemId);
            const std::string name = (mi != m.itemMeta.end() && !mi->second.name.empty())
                                   ? mi->second.name : ("Item " + std::to_string(h.itemId));
            const std::string icon   = mi != m.itemMeta.end() ? mi->second.icon   : std::string();
            const std::string rarity = mi != m.itemMeta.end() ? mi->second.rarity : std::string();
            const std::string link   = mi != m.itemMeta.end() ? mi->second.chatLink : std::string();
            const ItemUI::ResultRow row = ItemUI::DrawResultRow(i, w, h.itemId, icon, name, rarity, h.total, link);
            if (row.hovered) DrawInventoryTooltip(m, h.itemId, h.total, h.where);
            if (row.clicked) OpenItemsTab(app, 1, invq);
        }
        if ((int)hits.size() > n)
        {
            char more[56]; std::snprintf(more, sizeof(more), "+%d more in the Items tab", (int)hits.size() - n);
            const Gw2Ui::RowHotspot row = Gw2Ui::Row("##invMore", -1, 20.f, w);
            if (row.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            Gw2Ui::RowLabel(ImGui::GetWindowDrawList(), row, 0.f, 0.f, more,
                            Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
            if (row.clicked) OpenItemsTab(app, 1, invq);
        }
    }
}
