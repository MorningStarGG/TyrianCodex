// ItemsTab: the registered "Items" tab -- a HYBRID ORCHESTRATOR. It owns the "All Items" catalogue body in this
// file, and delegates the "My Inventory" + "Equipment" scopes to ui/tabs/InventorySection.cpp (a multi-export
// view library, NOT a tab).
#include "ui/tabs/ItemsTab.h"

#include "app/App.h"
#include "app/AccountData.h"        // ItemMeta (built from a CatItem for the shared renderers)
#include "app/ItemCatalog.h"
#include "app/TpEligibility.h"
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiGallery.h"  // the shared GalleryRail (type -> subtype rail)
#include "ui/items/ItemRender.h"    // ItemUI::DrawItemCell / DrawItemTooltip / RarityColor / CoinText
#include "ui/items/ItemTpDetail.h"  // ItemTp::Tradeable (filter) + DrawDetail (slide-out price card)
#include "ui/tabs/InventorySection.h"   // DrawInventoryContent (My Inventory scope) + DrawEquipmentContent
// (Legendary Armory scope moved to the Collections > Wardrobe "Legendary" sub-chip.)
#include "ui/SettingsWindow.h"      // OpenSettingsTab (OpenItemsTab opens the window + selects the tab)
#include "util/Textures.h"          // Tex::GetTextureFromURL (list-row icon)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    int  g_scope = 0;                 // 0 = All Items, 1 = My Inventory, 2 = Equipment
    std::string g_type;               // selected item type ("" = All Types)
    std::string g_subtype;            // selected subtype ("" = all of the selected type)
    char g_search[96] = "";
    int  g_rarity = 0;                // 0 = All; else rank+1
    int  g_lvMin = 0, g_lvMax = 80;
    ItemCatalog::Sort g_sort = ItemCatalog::Sort::Name;
    bool g_asc = true;

    // Trading Post: a "tradeable only" filter + the slide-out price card (selection + open/close animation).
    bool        g_tradeOnly = false;
    int         g_tpSel = 0;      // selected item id (0 = none / closing)
    int         g_tpShown = 0;    // item the card is rendering (kept during the close animation)
    std::string g_tpName;         // its display name (header), captured on select
    float       g_tpAnim = 0.f;   // 0 = hidden, 1 = fully slid out
    int         g_tpScrollItem = 0;

    // Parsed-on-demand ItemMeta per catalog item (so the shared cell/tooltip render with full details, offline).
    // Bounded: browsing the whole catalog would otherwise accumulate parsed details for all 74k items.
    std::unordered_map<int, AccountData::ItemMeta> g_metaCache;
    uint64_t g_metaVer = (uint64_t)-1;
    constexpr size_t kMetaCap = 3000;   // a few MB; far more than ever visible at once

    AccountData::ItemMeta& MetaFor(const ItemCatalog::Item& it)
    {
        if (g_metaVer != ItemCatalog::Version()) { g_metaCache.clear(); g_metaVer = ItemCatalog::Version(); }
        auto f = g_metaCache.find(it.id);
        if (f != g_metaCache.end()) return f->second;
        if (g_metaCache.size() >= kMetaCap) g_metaCache.clear();   // cap: drop + re-parse on next view (cheap, ~visible cells)
        AccountData::ItemMeta m;
        m.have = true; m.id = it.id; m.name = it.name; m.icon = it.icon; m.rarity = it.rarity; m.type = it.type;
        m.level = it.level; m.vendorValue = it.vendorValue; m.chatLink = it.chatLink; m.description = it.description;
        m.flags = it.flags; m.restrictions = it.restrictions;
        if (!it.detailsRaw.empty()) { try { m.details = nlohmann::json::parse(it.detailsRaw); } catch (...) {} }
        return g_metaCache.emplace(it.id, std::move(m)).first->second;
    }

    std::string FallbackName(int id) { char b[32]; std::snprintf(b, sizeof(b), "Item %d", id); return b; }

    unsigned RarityMask() { return g_rarity == 0 ? 0u : (1u << (g_rarity - 1)); }

    void ToggleCatalogTpCard(const ItemCatalog::Item& it)
    {
        if (!ItemTp::Tradeable(it.id))
        {
            g_tpSel = 0;
            return;
        }
        const bool closing = (g_tpSel == it.id);
        g_tpSel = closing ? 0 : it.id;
        g_tpShown = it.id;
        g_tpName = it.name;
        if (!closing) g_tpScrollItem = 0;
    }

    // ---- centered progress ring (% in the middle, live count under it) ----
    void DrawProgressRing(ImVec2 center, float radius, const ItemCatalog::ProgressInfo& p)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float thick = std::max(6.f, radius * 0.11f);
        dl->AddCircle(center, radius, IM_COL32(70, 64, 50, 170), 96, thick);
        float frac = p.total > 0 ? (float)p.fetched / (float)p.total : 0.f;
        frac = std::max(0.f, std::min(1.f, frac));
        if (frac > 0.003f)
        {
            dl->PathArcTo(center, radius, -1.57080f, -1.57080f + 6.28319f * frac, 96);
            dl->PathStroke(Gw2Ui::kGold, false, thick);
        }
        char pct[16]; std::snprintf(pct, sizeof(pct), "%.0f%%", frac * 100.f);
        Gw2Ui::LabelIn(ImVec2(center.x - radius, center.y - radius), ImVec2(center.x + radius, center.y + radius),
                       pct, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, radius * 0.55f);
        char cnt[48];
        if (p.total > 0) std::snprintf(cnt, sizeof(cnt), "%d / %d items", p.fetched, p.total);
        else             std::snprintf(cnt, sizeof(cnt), "Starting...");
        Gw2Ui::LabelIn(ImVec2(center.x - radius * 2.f, center.y + radius + 8.f), ImVec2(center.x + radius * 2.f, center.y + radius + 32.f),
                       cnt, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSub, false, nullptr, 16.f);
        Gw2Ui::LabelIn(ImVec2(center.x - radius * 2.f, center.y + radius + 32.f), ImVec2(center.x + radius * 2.f, center.y + radius + 52.f),
                       "Loading item database", Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
    }

    bool DrawTradeableToggle()
    {
        const ImVec2 size(118.f, 26.f);
        const Gw2Ui::ActionButtonResult r = Gw2Ui::ActionButtonFrame(
            "##tponly", size, Gw2Ui::ActionButtonVariant::Normal, false,
            "Show only items that can be sold on the Trading Post");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (g_tradeOnly)
        {
            dl->AddRectFilled(r.min, r.max, IM_COL32(88, 62, 22, 62), 3.f);
            dl->AddRect(r.min, r.max, Gw2Ui::Alpha(Gw2Ui::kGold, 220), 3.f, 0, 1.45f);
            dl->AddRect(ImVec2(r.min.x + 1.f, r.min.y + 1.f), ImVec2(r.max.x - 1.f, r.max.y - 1.f),
                        Gw2Ui::Alpha(Gw2Ui::kGold, 70), 2.f, 0, 1.f);
        }

        const ImU32 textCol = g_tradeOnly ? Gw2Ui::kGold
                            : ((r.hovered || r.held) ? IM_COL32(255, 232, 184, 255)
                                                      : IM_COL32(225, 209, 176, 248));
        Gw2Ui::PushTextScale(1.f);
        Gw2Ui::LabelIn(ImVec2(r.min.x + 8.f, r.min.y), ImVec2(r.max.x - 8.f, r.max.y), "Tradeable",
                       Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, textCol, true, nullptr, 18.f);
        Gw2Ui::PopTextScale();
        return r.clicked;
    }

    // ---- details list (virtualized) with sortable + per-type context columns ----
    // kind: 0 = dim generic, 1 = name (cream), 2 = rarity (per-row colored), 3 = value (gold)
    struct Col
    {
        std::string header; float w = 0.f /*0 = flex (Name)*/; float x = 0.f;
        bool sortable = false; ItemCatalog::Sort sort = ItemCatalog::Sort::Name; int kind = 0;
        std::function<std::string(const ItemCatalog::Item&, AccountData::ItemMeta&)> val;
        float* store = nullptr;   // -> persisted width in Config.uiPaneWidths (null = the flex Name column)
    };

    // The type-specific extra columns (read from the parsed `details`), appended when a single type is selected.
    std::vector<Col> ContextCols(const std::string& t)
    {
        std::vector<Col> v;
        auto stat = [](const ItemCatalog::Item& it, AccountData::ItemMeta&) { return it.statSet; };
        if (t == "Weapon")
        {
            v.push_back({ "Damage", 92.f, 0.f, false, ItemCatalog::Sort::Name, 0, [](const ItemCatalog::Item&, AccountData::ItemMeta& m) {
                const int lo = ItemUI::DetailInt(m, "min_power"), hi = ItemUI::DetailInt(m, "max_power");
                if (hi <= 0) return std::string(); char b[24]; std::snprintf(b, sizeof(b), "%d-%d", lo, hi); return std::string(b); } });
            v.push_back({ "Stat", 128.f, 0.f, false, ItemCatalog::Sort::Name, 0, stat });
        }
        else if (t == "Armor")
        {
            v.push_back({ "Weight", 86.f, 0.f, false, ItemCatalog::Sort::Name, 0, [](const ItemCatalog::Item&, AccountData::ItemMeta& m) {
                return ItemUI::Humanize(ItemUI::DetailStr(m, "weight_class")); } });
            v.push_back({ "Stat", 128.f, 0.f, false, ItemCatalog::Sort::Name, 0, stat });
        }
        else if (t == "Trinket" || t == "Back")
        {
            v.push_back({ "Stat", 150.f, 0.f, false, ItemCatalog::Sort::Name, 0, stat });
        }
        else if (t == "Bag")
        {
            v.push_back({ "Slots", 64.f, 0.f, false, ItemCatalog::Sort::Name, 0, [](const ItemCatalog::Item&, AccountData::ItemMeta& m) {
                const int s = ItemUI::DetailInt(m, "size"); return s > 0 ? std::to_string(s) : std::string(); } });
        }
        else if (t == "Consumable")
        {
            v.push_back({ "Duration", 92.f, 0.f, false, ItemCatalog::Sort::Name, 0, [](const ItemCatalog::Item&, AccountData::ItemMeta& m) {
                const int d = ItemUI::DetailInt(m, "duration_ms"); if (d <= 0) return std::string();
                const int s = (d + 999) / 1000; char b[24];
                if (s >= 3600) std::snprintf(b, sizeof(b), "%dh", s / 3600); else if (s >= 60) std::snprintf(b, sizeof(b), "%dm", s / 60); else std::snprintf(b, sizeof(b), "%ds", s);
                return std::string(b); } });
        }
        else if (t == "Gathering" || t == "Tool")
        {
            v.push_back({ "Charges", 70.f, 0.f, false, ItemCatalog::Sort::Name, 0, [](const ItemCatalog::Item&, AccountData::ItemMeta& m) {
                const int c = ItemUI::DetailInt(m, "charges"); return c > 0 ? std::to_string(c) : std::string(); } });
        }
        return v;
    }

    void DrawList(App& app, const std::vector<int>& res)
    {
        const ItemUI::NameFn nameFn = [](int id) { return FallbackName(id); };
        const float scale = Gw2Ui::TextScale();
        const float rowH = std::max(42.f, 44.f * scale);
        const float nameFs = 18.f;
        const float cellFs = 16.f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const float wIcon = rowH;
        const bool single = !g_type.empty() && g_type != "all";

        // build the column set: Name (flex) | Type/Subtype | [context...] | Rarity | Level | Value
        std::vector<Col> cols;
        cols.push_back({ "Name", 0.f, 0.f, true, ItemCatalog::Sort::Name, 1, [](const ItemCatalog::Item& it, AccountData::ItemMeta&) { return it.name; } });
        cols.push_back({ single ? "Subtype" : "Type", 152.f, 0.f, true, ItemCatalog::Sort::Type, 0, [single](const ItemCatalog::Item& it, AccountData::ItemMeta&) {
            if (single) return it.subtype.empty() ? std::string("-") : ItemUI::Humanize(it.subtype);
            std::string s = ItemUI::Humanize(it.type); if (!it.subtype.empty()) s += " - " + ItemUI::Humanize(it.subtype); return s; } });
        if (single) for (Col& c : ContextCols(g_type)) cols.push_back(std::move(c));
        cols.push_back({ "Rarity", 96.f, 0.f, true, ItemCatalog::Sort::Rarity, 2, [](const ItemCatalog::Item& it, AccountData::ItemMeta&) { return it.rarity; } });
        cols.push_back({ "Level", 50.f, 0.f, true, ItemCatalog::Sort::Level, 0, [](const ItemCatalog::Item& it, AccountData::ItemMeta&) { return it.level > 0 ? std::to_string(it.level) : std::string(); } });
        cols.push_back({ "Value", 112.f, 0.f, true, ItemCatalog::Sort::Value, 3, [](const ItemCatalog::Item& it, AccountData::ItemMeta&) {
            std::string v = ItemUI::CoinText(it.vendorValue); if (!v.empty() && v.rfind("Vendor value: ", 0) == 0) v = v.substr(14);
            if (ItemUI::HasString(it.flags, "NoSell")) v.clear(); return v; } });

        // Persisted draggable widths for the fixed columns (Name, kind==1, stays the flex/fill column).
        for (Col& c : cols)
        {
            if (c.kind == 1) continue;   // Name = flex
            float& sw = app.config.PaneW(std::string("items.col.") + c.header, c.w);
            sw = std::clamp(sw, 56.f, 460.f);
            c.store = &sw; c.w = sw;
        }

        // Drive the shared table shell: it lays out the columns (Name flexes), draws the draggable borders +
        // sortable headers + bottom rule + clipper; we render each row's icon + cells in drawRow from the
        // per-column rects it computes. Sort state (g_sort/g_asc) stays here -- onSort flips it; res re-sorts.
        std::vector<Gw2Ui::TableColumn> tcols(cols.size());
        for (size_t i = 0; i < cols.size(); ++i)
            tcols[i] = { cols[i].header.c_str(), cols[i].store, 56.f, 460.f, Gw2Ui::HAlign::Center,
                         cols[i].sortable ? (int)cols[i].sort : -1 };

        Gw2Ui::DataTableDesc d;
        d.id = "##items"; d.cols = tcols.data(); d.colCount = (int)tcols.size();
        d.availW = availW; d.rowCount = (int)res.size();
        d.rowH = rowH; d.headerH = 31.f * scale; d.headerFs = 16.f;
        d.leftPad = wIcon + 6.f; d.flexMin = 160.f; d.rightReserve = 16.f; d.bodyChild = true;
        d.activeSort = (int)g_sort; d.sortAsc = g_asc;
        d.onSort = [](int id) { const ItemCatalog::Sort s = (ItemCatalog::Sort)id;
                                if (g_sort == s) g_asc = !g_asc; else { g_sort = s; g_asc = true; } };
        d.drawRow = [&](int i, const Gw2Ui::RowHotspot& row, const Gw2Ui::TableRowCols& rc)
        {
            const ItemCatalog::Item& it = ItemCatalog::At(res[i]);
            AccountData::ItemMeta& meta = MetaFor(it);
            ImGui::PushID(res[i]);
            const ImVec2 p = row.min;
            if (row.clicked) ToggleCatalogTpCard(it);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (!it.icon.empty())
            {
                char texId[40]; std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", it.id);
                if (void* tex = Tex::GetTextureFromURL(texId, it.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(p.x + rowH - 2.f, p.y + rowH - 2.f));
            }
            dl->AddRect(ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(p.x + rowH - 2.f, p.y + rowH - 2.f), ItemUI::RarityColor(it.rarity), 0.f, 0, 1.2f);
            for (size_t j = 0; j < cols.size() && (int)j < rc.count; ++j)
            {
                const Col& c = cols[j];
                const std::string s = c.val ? c.val(it, meta) : std::string();
                if (s.empty()) continue;
                const ImU32 col = (c.kind == 1) ? IM_COL32(228, 222, 204, 255)
                                : (c.kind == 2) ? ItemUI::RarityColor(it.rarity)
                                : (c.kind == 3) ? IM_COL32(220, 200, 140, 255) : Gw2Ui::kTextSub;
                Gw2Ui::RowLabel(dl, row, rc.x[j], std::max(0.f, rc.availW - rc.x[j] - rc.w[j] + 8.f), s.c_str(),
                                c.kind == 1 ? Gw2Ui::HAlign::Left : tcols[j].align,   // Name cell left (has the icon), like Flip; data cells follow align
                                Gw2Ui::VAlign::Middle, col, false, nullptr, c.kind == 1 ? nameFs : cellFs);
            }
            if (row.hovered) ItemUI::DrawItemTooltip(it.id, &meta, ItemUI::Instance{}, nameFn, it.statSet);
            ItemUI::DrawCopyMenu("##itemcopymenu", row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), it.name, it.chatLink);
            ImGui::PopID();
        };
        if (Gw2Ui::DataTable(d)) app.settingsDirty = true;
    }

    void DrawAllItems(App& app)
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        const ItemUI::NameFn nameFn = [](int id) { return FallbackName(id); };

        // Loading / empty catalog keeps the centered progress ring (not a gallery body).
        const ItemCatalog::ProgressInfo prog = ItemCatalog::Progress();
        if (ItemCatalog::Count() == 0)
        {
            if (prog.active) DrawProgressRing(ImVec2(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.45f), std::max(40.f, avail.y * 0.16f), prog);
            else Gw2Ui::EmptyState("Item database not loaded", "It downloads in the background on login; reopen shortly.");
            return;
        }

        // rail tree (type -> subtype), rebuilt only when the catalog changes.
        static Gw2Ui::GalleryRailNode s_tree; static uint64_t s_treeTok = ~0ull;
        if (s_treeTok != ItemCatalog::Version())
        {
            s_tree.children.clear();
            s_tree.children.push_back({ "", "All Types", ItemCatalog::Count(), -1, {} });   // total<0 -> single-count badge
            for (const ItemCatalog::TypeCount& t : ItemCatalog::Types())
            {
                Gw2Ui::GalleryRailNode node{ t.name, ItemUI::Humanize(t.name), t.count, -1, {} };
                for (const ItemCatalog::TypeCount& s : ItemCatalog::Subtypes(t.name))
                    node.children.push_back({ t.name + std::string(1, '\x1f') + s.name, ItemUI::Humanize(s.name), s.count, -1, {} });
                s_tree.children.push_back(std::move(node));
            }
            s_treeTok = ItemCatalog::Version();
        }
        static std::string s_railKey; s_railKey = g_subtype.empty() ? g_type : (g_type + std::string(1, '\x1f') + g_subtype);

        static const char* kRarity[] = { "All rarities", "Junk", "Basic", "Fine", "Masterwork", "Rare", "Exotic", "Ascended", "Legendary" };
        static const char* kSort[]   = { "Relevance", "Name", "Level", "Rarity", "Type", "Value" };
        if (app.config.itemsView != 0 && app.config.itemsView != 1) app.config.itemsView = 1;
        const bool grid = (app.config.itemsView == 0);
        static int s_sortSel = (int)g_sort;        // grid-mode sort dropdown; in list mode the DataTable headers sort
        if (!grid) s_sortSel = (int)g_sort;        // list mode: track the header sort so the dropdown shows it on switch

        // order token: every input that changes the filtered/sorted result -> the shell rebuilds the order on a change.
        uint64_t tok = 1469598103934665603ull;
        auto mix = [&tok](uint64_t v) { tok ^= v; tok *= 1099511628211ull; };
        mix(ItemCatalog::Version());
        for (const char* p = g_search; *p; ++p) mix((unsigned char)*p);
        for (char c : s_railKey) mix((unsigned char)c);
        mix((unsigned)g_rarity); mix((unsigned)g_lvMin); mix((unsigned)g_lvMax);
        mix((unsigned)g_sort); mix(g_asc ? 1u : 2u); mix((unsigned)s_sortSel);
        mix(g_tradeOnly ? 0x9E3779B9ull : 0x1ull); mix(TpEligibility::IsReady() ? 0xABCull : 0x1ull);

        Gw2Ui::GalleryBrowser::FilterDropdown rarityF{ "rarity", kRarity, 9, &g_rarity, 132.f };

        Gw2Ui::GalleryBrowser gb;
        gb.id = "items.all";
        gb.headerFontSize = 16.f;
        gb.headerText = [&](int n) {
            char b[80]; std::snprintf(b, sizeof b, "%d items%s%s", n, g_tradeOnly ? " (tradeable)" : "", prog.active ? "  (updating...)" : "");
            return std::string(b);
        };
        gb.searchBuf = g_search; gb.searchBufSize = (int)sizeof(g_search); gb.searchHint = "Search items...";
        gb.filters = &rarityF; gb.filterCount = 1;
        if (grid) { gb.sortOptions = kSort; gb.sortCount = 6; gb.sortSelected = &s_sortSel; gb.sortAsc = &g_asc; }   // list sorts via the DataTable headers
        gb.gridListToggle = true; gb.viewMode = &app.config.itemsView; gb.settingsDirty = &app.settingsDirty;
        gb.extraFilterRow = [&](float) {
            if (DrawTradeableToggle()) g_tradeOnly = !g_tradeOnly;
            Gw2Ui::RangeSliderInt("##itemlevel", &g_lvMin, &g_lvMax, 0, 80);   // full-width row below the Tradeable toggle
            if (g_lvMin > g_lvMax) std::swap(g_lvMin, g_lvMax);
        };
        gb.railRoot = &s_tree; gb.railSelectedKey = &s_railKey;
        gb.railWidthPx = &app.config.PaneW("items.rail", 232.f); gb.railDefaultW = 232.f;
        gb.orderToken = tok;
        gb.gridCell = 54.f; gb.gridGap = 6.f;
        gb.emptyText = (g_tradeOnly && !TpEligibility::IsReady()) ? "Trading Post item data is loading." : "No items match.";
        gb.rebuildOrder = [&](std::vector<int>& order) {
            if (grid) g_sort = (ItemCatalog::Sort)s_sortSel;   // apply the grid sort dropdown before the query
            const std::vector<int>& q = ItemCatalog::Query(g_search, g_type, g_subtype, RarityMask(), g_lvMin, g_lvMax, g_sort, g_asc);
            order.clear();
            if (g_tradeOnly) { for (int idx : q) if (ItemTp::Tradeable(ItemCatalog::At(idx).id)) order.push_back(idx); }
            else order = q;
        };
        gb.drawGridCell = [&](int idx, ImVec2 /*cmin*/, float cs) {
            const ItemCatalog::Item& it = ItemCatalog::At(idx);
            if (ItemUI::DrawItemCell(it.id, &MetaFor(it), ItemUI::Instance{}, nameFn, cs, it.statSet))
                ToggleCatalogTpCard(it);
        };
        gb.drawListBody = [&](const std::vector<int>& order) { DrawList(app, order); };

        Gw2Ui::DrawGalleryBrowser(gb);

        // decode the (possibly clicked) rail selection back into the type/subtype filter for next frame.
        { const size_t sep = s_railKey.find('\x1f');
          if (sep == std::string::npos) { g_type = s_railKey; g_subtype.clear(); }
          else { g_type = s_railKey.substr(0, sep); g_subtype = s_railKey.substr(sep + 1); } }

        // the TP slide-out card overlays the items area from the right edge.
        ItemTp::DrawSlideCard(app, g_tpSel, g_tpShown, g_tpName, g_tpAnim, g_tpScrollItem,
                              origin.x, origin.y, avail.x, avail.y);
    }
}

void ShutdownItemsTab()
{
    std::unordered_map<int, AccountData::ItemMeta>().swap(g_metaCache);   // release the parsed-meta cache
    g_metaVer = (uint64_t)-1;
}

void DrawItemsContent(App& app)
{
    ItemUI::SetTpPrices(app.config.showTpPrices);   // gate the shared renderers' live TP price lines
    // scope toggle: All Items (catalog) / My Inventory (owned) / Equipment (a character's gear). (Legendary
    // Armory moved to the Collections > Wardrobe "Legendary" sub-chip.)
    {
        static const Gw2Ui::ChipItem kScopeChips[] = { {"All Items"}, {"My Inventory"}, {"Equipment"} };
        Gw2Ui::ChipRow("##scope", kScopeChips, 3, &g_scope, 32.f, 18.f);
    }
    Gw2Ui::Divider(0.f);

    if (g_scope == 1) { DrawInventoryContent(app); return; }
    if (g_scope == 2) { DrawEquipmentContent(app); return; }
    DrawAllItems(app);
}

// AccountData domains the active scope needs (polled only while shown), OR'd into the entry.cpp Tick mask.
unsigned ItemsNeededDomains(App&)
{
    return 0u;   // Legendary Armory's domain is now declared by the Collections Wardrobe scope
}

// Open the Items tab at a given scope (0 = All Items, 1 = My Inventory, 2 = Equipment; <0 = leave as-is) and optionally
// prefill the catalogue search (clearing the rail filters so the query isn't constrained). The one entry
// point every "inventory" / "items" launcher uses, so the right scope is always shown.
void OpenItemsTab(App& app, int scope, const char* search)
{
    OpenSettingsTab(app, SettingsTabItems);
    if (scope >= 0) g_scope = scope;
    if (search)
    {
        if (scope == 1) SetInventorySearch(search);   // My Inventory has its OWN search (owned items)
        else
        {
            std::snprintf(g_search, sizeof(g_search), "%s", search);   // catalogue search
            g_type.clear(); g_subtype.clear();   // a leftover type/subtype filter would hide search hits
        }
    }
}

// Rich hover tooltip for a catalog item (by Query/At index), reusing the Items-tab meta cache + name resolver.
// The dashboard Items widget calls this on row hover so a widget item shows the same tooltip as the tab.
void DrawItemCatalogTooltip(int catalogIndex)
{
    const ItemCatalog::Item& it = ItemCatalog::At(catalogIndex);
    if (it.id == 0) return;
    const ItemUI::NameFn nameFn = [](int id) { return FallbackName(id); };
    ItemUI::DrawItemTooltip(it.id, &MetaFor(it), ItemUI::Instance{}, nameFn, it.statSet);
}
