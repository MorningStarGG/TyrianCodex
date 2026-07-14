#include "Widgets.h"
#include "app/App.h"
#include "app/ItemCatalog.h"
#include "ui/Gw2Ui.h"
#include "ui/items/ItemRender.h"   // ItemUI::RarityColor
#include "ui/tabs/ItemsTab.h"      // OpenItemsTab (jump to the full Items tab on All Items)
#include "util/Textures.h"         // Tex::GetTextureFromURL (row icon)
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <vector>

// "Items": a compact whole-game item-catalogue search on the dashboard -- the Atlas-style sibling of the
// location Search widget. Type a query, see the top matches (icon + rarity-coloured name), click any row (or
// the footer) to open the full Items tab on All Items with the query prefilled. Reuses ItemCatalog::Query +
// ItemUI rarity colours + the same row icon path as the Items tab; the tab is the full browser.
void DashW::Items(App& app, float w)
{
    static char q[96] = "";

    if (ItemCatalog::Count() == 0)
    {
        Gw2Ui::Label("Loading item database...", Gw2Ui::kTextDim, false, nullptr, 14.f);
        return;
    }

    Gw2Ui::SearchBox("##dwItems", q, sizeof(q), w, w < 170.f ? "Search..." : "Search items...");   // short hint when narrow
    if (q[0] == '\0')
    {
        // wrap the hint to the widget width so it doesn't clip at half width
        const char* hint = "Type to search the whole-game item catalogue.";
        const float ww = ImGui::GetContentRegionAvail().x;
        const float hh = Gw2Ui::MeasureWrappedHeight(hint, 14.f, ww);
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(ww, hh));
        Gw2Ui::LabelIn(hp, ImVec2(hp.x + ww, hp.y + hh), hint, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextDim, false, nullptr, 14.f, ww);
        return;
    }

    const std::vector<int>& res = ItemCatalog::Query(q, std::string(), std::string(), 0u, 0, 80,
                                                     ItemCatalog::Sort::Name, true);
    if (res.empty()) { Gw2Ui::Label("No matches.", Gw2Ui::kTextSub, false, nullptr, 14.f); return; }

    const int n = std::min((int)res.size(), 8);   // top matches; the Items tab shows them all
    for (int i = 0; i < n; ++i)
    {
        const ItemCatalog::Item& it = ItemCatalog::At(res[i]);
        const ItemUI::ResultRow row = ItemUI::DrawResultRow(i, w, it.id, it.icon, it.name, it.rarity, 0, it.chatLink);
        if (row.hovered) DrawItemCatalogTooltip(res[i]);   // same rich tooltip as the tab
        if (row.clicked) OpenItemsTab(app, 0, q);          // open the full catalogue with this search
    }

    if ((int)res.size() > n)
    {
        char more[56]; std::snprintf(more, sizeof(more), "+%d more in the Items tab", (int)res.size() - n);
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##dwItemsMore", -1, 20.f, w);
        if (row.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        Gw2Ui::RowLabel(ImGui::GetWindowDrawList(), row, 0.f, 0.f, more,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
        if (row.clicked) OpenItemsTab(app, 0, q);
    }
}
