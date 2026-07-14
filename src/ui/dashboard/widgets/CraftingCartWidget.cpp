#include "Widgets.h"
#include "app/App.h"
#include "app/AccountData.h"            // ItemMeta (hover tooltip)
#include "app/CraftCart.h"
#include "app/CraftCartSummary.h"
#include "app/CraftingEngine.h"
#include "app/ItemCatalog.h"
#include "ui/Gw2Ui.h"
#include "ui/items/ItemRender.h"        // ItemUI::RarityColor + DrawItemTooltip
#include "ui/tabs/TradingPostTab.h"     // OpenCraftingCart
#include "render/glyphs/Glyphs.h"       // Render::DrawGlyph (check mark)
#include "util/Textures.h"              // Tex::GetTextureFromURL (row icon)
#include "util/Trading.h"               // Trading::Coins
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    bool CartToggle(const char* id, const char* label, bool* v, const char* tip, float w)
    {
        const Gw2Ui::ActionButtonResult r = Gw2Ui::ActionButtonFrame(id, ImVec2(w, 26.f), Gw2Ui::ActionButtonVariant::Normal, false, tip);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (*v)
        {
            dl->AddRectFilled(r.min, r.max, IM_COL32(88, 62, 22, 62), 3.f);
            dl->AddRect(r.min, r.max, Gw2Ui::Alpha(Gw2Ui::kGold, 220), 3.f, 0, 1.45f);
            dl->AddRect(ImVec2(r.min.x + 1.f, r.min.y + 1.f), ImVec2(r.max.x - 1.f, r.max.y - 1.f), Gw2Ui::Alpha(Gw2Ui::kGold, 70), 2.f, 0, 1.f);
        }
        const ImU32 textCol = *v ? Gw2Ui::kGold
                            : ((r.hovered || r.held) ? IM_COL32(255, 232, 184, 255) : IM_COL32(225, 209, 176, 248));
        Gw2Ui::LabelIn(ImVec2(r.min.x + 8.f, r.min.y), ImVec2(r.max.x - 8.f, r.max.y), label,
                       Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, textCol, true, nullptr, 16.f);
        if (r.clicked) { *v = !*v; return true; }
        return false;
    }

    // Build an ItemMeta from the bundled catalog for the cart-row hover tooltip (same rich tooltip as the view).
    AccountData::ItemMeta CartMeta(const ItemCatalog::Item& it)
    {
        AccountData::ItemMeta m;
        m.have = true; m.id = it.id; m.name = it.name; m.icon = it.icon; m.rarity = it.rarity; m.type = it.type;
        m.level = it.level; m.vendorValue = it.vendorValue; m.chatLink = it.chatLink; m.description = it.description;
        m.flags = it.flags; m.restrictions = it.restrictions;
        if (!it.detailsRaw.empty()) { try { m.details = nlohmann::json::parse(it.detailsRaw); } catch (...) {} }
        return m;
    }
}

// "Crafting Cart": the dashboard face of the persistent cart (CraftCart). Switch projects, toggle the materials
// view flat/by-item, tick materials off (in sync with the Crafting view + Info Panel), see the still-need total,
// and jump into the full cart. Reads the shared CraftCartSummary so the recursive plan is computed once. Adapts
// to a narrow (half-width) layout.
void DashW::CraftingCart(App& app, float w)
{
    if (!CraftingEngine::Ready()) { Gw2Ui::Label("Loading recipes...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    const std::string active = CraftCart::Active();
    const bool narrow = w < 230.f;

    // -- project picker + Open --
    {
        const std::vector<std::string> names = CraftCart::Names();
        std::vector<const char*> cn; cn.reserve(names.size());
        int sel = 0;
        for (int i = 0; i < (int)names.size(); ++i) { cn.push_back(names[i].c_str()); if (names[i] == active) sel = i; }
        const float openW = 58.f;
        if (Gw2Ui::Dropdown("##dwcartproj", cn.data(), (int)cn.size(), &sel, w - openW - 6.f) && sel >= 0 && sel < (int)names.size())
            CraftCart::SetActive(names[sel]);
        ImGui::SameLine(0.f, 6.f);
        if (Gw2Ui::ActionButton("Open", openW, 26.f, Gw2Ui::ActionButtonVariant::Primary, "Open the cart in Trading Post -> Crafting")) OpenCraftingCart(app);
    }

    const CraftCartSummary::Summary& s = CraftCartSummary::Get(app);

    // -- summary line + by-item toggle --
    {
        char ln[96];
        std::snprintf(ln, sizeof(ln), "%d item%s   to buy %s", s.itemCount, s.itemCount == 1 ? "" : "s", Trading::Coins(s.stillNeed).c_str());
        Gw2Ui::Label(ln, IM_COL32(228, 222, 204, 255), false, nullptr, 14.f);
        const float toggleW = std::min(w, narrow ? 118.f : 150.f);
        if (CartToggle("##dwcartbyitem", narrow ? "By item" : "Group by item", &app.config.craftShopByItem,
                       "Show materials grouped per cart item instead of one flat list", toggleW))
            app.settingsDirty = true;
    }

    if (s.itemCount == 0)
    {
        Gw2Ui::Label("Cart is empty.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        return;
    }

    Gw2Ui::Divider(0.f);

    // -- materials with check-off --
    const float rowH = 22.f;
    const float rowFs = 14.f;
    const float minTotW = std::min(78.f, w * 0.34f);
    const float valueGap = 10.f;
    auto valueReserve = [&](const std::string& value) {
        const float want = std::max(minTotW, Gw2Ui::MeasureWidth(value.c_str(), rowFs) + valueGap);
        return std::min(std::max(0.f, w - 4.f), want);
    };
    const int   kMaxRows = 16;
    int drawn = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto matRow = [&](const CraftCartSummary::Mat& m) {
        if (drawn >= kMaxRows) return;
        const ItemCatalog::Item& it = ItemCatalog::ById(m.itemId);
        const bool got = CraftCart::IsGot(active, m.itemId);
        ImGui::PushID(drawn);
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##m", drawn, rowH, w);
        const bool clicked = row.clicked;
        const bool hov = row.hovered;
        const ImVec2 p = row.min;

        const float cbS = rowH - 9.f;
        const ImVec2 cbMin(p.x + 2.f, p.y + (rowH - cbS) * 0.5f), cbMax(cbMin.x + cbS, cbMin.y + cbS);
        dl->AddRect(cbMin, cbMax, got ? Gw2Ui::kGold : IM_COL32(150, 140, 110, 200), 2.f, 0, 1.3f);
        if (got) Render::DrawGlyph(dl, ImVec2((cbMin.x + cbMax.x) * 0.5f, (cbMin.y + cbMax.y) * 0.5f), cbS * 0.95f, Render::Glyph::Check, Gw2Ui::kGold, Render::GlyphStyle{});

        const float ix = p.x + rowH;
        if (!it.icon.empty())
        {
            char t[40]; std::snprintf(t, sizeof(t), "TC_ITEM_%d", m.itemId);
            if (void* tex = Tex::GetTextureFromURL(t, it.icon.c_str()))
                dl->AddImage((ImTextureID)tex, ImVec2(ix + 1.f, p.y + 2.f), ImVec2(ix + rowH - 3.f, p.y + rowH - 2.f));
        }
        dl->AddRect(ImVec2(ix + 1.f, p.y + 2.f), ImVec2(ix + rowH - 3.f, p.y + rowH - 2.f), ItemUI::RarityColor(it.rarity), 0.f, 0, 1.f);

        { const std::string r = got ? std::string("got") : (m.priced ? Trading::Coins(m.total) : std::string("--"));
          const float valueW = valueReserve(r);
          const float nameOff = rowH * 2.f + 2.f;
          Gw2Ui::RowLabel(dl, row, nameOff, valueW + valueGap, it.name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                          got ? Gw2Ui::kTextDim : ItemUI::RarityColor(it.rarity), false, nullptr, rowFs);
          Gw2Ui::RowLabel(dl, row, std::max(0.f, w - valueW), 2.f, r.c_str(),
                          Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, got ? Gw2Ui::kTextDim : IM_COL32(255, 230, 170, 255), false, nullptr, rowFs); }

        if (hov)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const AccountData::ItemMeta meta = CartMeta(it);
            ItemUI::DrawItemTooltip(it.id, &meta, ItemUI::Instance{}, [](int id) { return ItemCatalog::ById(id).name; }, it.statSet);
        }
        ItemUI::DrawCopyMenu("##cartcopy", hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right), it.name, it.chatLink);
        if (clicked)
        {
            if (ImGui::GetIO().MousePos.x < p.x + rowH) CraftCart::SetGot(active, m.itemId, !got);
            else OpenCraftingCart(app);
        }
        ImGui::PopID();
        ++drawn;
    };

    int totalMats = 0;
    if (!app.config.craftShopByItem)
    {
        for (const CraftCartSummary::Mat& m : s.flat) { ++totalMats; matRow(m); }
    }
    else
    {
        for (const CraftCartSummary::Group& g : s.byItem)
        {
            if (drawn >= kMaxRows) { totalMats += (int)g.mats.size(); continue; }
            const ItemCatalog::Item& out = ItemCatalog::ById(g.outId);
            char gh[96]; std::snprintf(gh, sizeof(gh), "%s  x%lld", out.name.c_str(), g.outQty);
            const std::string subtotal = Trading::Coins(g.subtotal);
            const float valueW = valueReserve(subtotal);
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(w, rowH));
            const Gw2Ui::RowHotspot row{ hp, w, rowH, false, false };
            Gw2Ui::RowLabel(dl, row, 0.f, valueW + valueGap, gh, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, rowFs);
            Gw2Ui::RowLabel(dl, row, std::max(0.f, w - valueW), 2.f, subtotal.c_str(),
                            Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(255, 230, 170, 255), false, nullptr, rowFs);
            for (const CraftCartSummary::Mat& m : g.mats) { ++totalMats; matRow(m); }
        }
    }

    if (drawn < totalMats)
    {
        char more[64]; std::snprintf(more, sizeof(more), "+%d more -- open the cart", totalMats - drawn);
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##dwcartmore", -1, 20.f, w);
        if (row.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        Gw2Ui::RowLabel(ImGui::GetWindowDrawList(), row, 0.f, 0.f, more,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
        const bool click = row.clicked;
        if (click) OpenCraftingCart(app);
    }
    if (!s.pricesComplete) Gw2Ui::Label("Prices still loading...", Gw2Ui::kTextDim, false, nullptr, 14.f);
}
