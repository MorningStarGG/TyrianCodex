#include "ui/tabs/LegendaryArmorySection.h"

#include "app/App.h"
#include "app/AccountData.h"
#include "app/ItemCatalog.h"
#include "ui/Gw2Ui.h"
#include "ui/ApiReminder.h"
#include "ui/items/ItemRender.h"   // DrawItemCell / DrawItemTooltip / DrawCopyMenu / RarityColor
#include "util/Textures.h"         // Tex::GetTextureFromURL (list-row icon)
#include "ui/gw2ui/Gw2UiGallery.h" // the shared GalleryBrowser shell + GalleryRail

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    // The catalog (id->max) + owned (id->count) come from AccountData -- prewarmed at login + served stale from
    // the disk cache (DomLegendaryArmory), so this view is a pure renderer. State here is only the filter/sort;
    // the GalleryBrowser shell owns the filtered+sorted order cache (keyed by id + token).
    char g_search[96] = "";
    int  g_sort = 2;        // 0 = Name, 1 = Type, 2 = Owned
    bool g_asc  = false;    // default Owned + desc -> owned-first, then name

    AccountData::ItemMeta LegMeta(const ItemCatalog::Item& it)
    {
        AccountData::ItemMeta m;
        m.have = true; m.id = it.id; m.name = it.name; m.icon = it.icon; m.rarity = it.rarity; m.type = it.type;
        m.level = it.level; m.vendorValue = it.vendorValue; m.chatLink = it.chatLink; m.description = it.description;
        m.flags = it.flags; m.restrictions = it.restrictions;
        if (!it.detailsRaw.empty()) { try { m.details = nlohmann::json::parse(it.detailsRaw); } catch (...) {} }
        return m;
    }

    std::string Lower(const char* s)
    {
        std::string o; if (s) for (const char* p = s; *p; ++p) o += (char)std::tolower((unsigned char)*p); return o;
    }

    // ---- category data: top type (Weapons / Armor / Trinkets / Back Items / Upgrades), each with its sub-kind
    // (weapon type / armor weight / trinket + Rune|Sigil), owned/total counts -- built once behind the armory-data
    // token (F5), then mirrored into the shell's GalleryRailNode tree. g_legCat = selected top category ("" = All);
    // g_legSub = selected sub-kind ("" = whole category); both decoded back from the rail's selection key. ----
    struct LegSub { std::string key, name; int owned = 0, total = 0; };
    struct LegCat { std::string key, name; int owned = 0, total = 0; std::vector<LegSub> subs; };
    std::vector<LegCat> g_legRail;
    std::string g_legCat, g_legSub;
    std::unordered_map<int, std::pair<std::string, std::string>> g_legKind;   // id -> (catKey, subKey) for the body filter
    double g_lrAt = -1.0; size_t g_lrN = (size_t)-1; uint64_t g_lrCatVer = (uint64_t)-1;

    std::string CatName(const std::string& type)
    {
        if (type == "Weapon")           return "Weapons";
        if (type == "Armor")            return "Armor";
        if (type == "Trinket")          return "Trinkets";
        if (type == "Back")             return "Back Items";
        if (type == "UpgradeComponent") return "Upgrades";
        return type.empty() ? std::string("Other") : type;
    }
    int CatRank(const std::string& c)
    {
        if (c == "Weapons")    return 0;
        if (c == "Armor")      return 1;
        if (c == "Trinkets")   return 2;
        if (c == "Back Items") return 3;
        if (c == "Upgrades")   return 4;
        return 5;
    }
    // sub-kind: armor groups by weight (Light/Medium/Heavy); everything else by its details "type" (weapon type /
    // trinket type / Rune|Sigil). "" when the item carries no usable detail (e.g. plain back items).
    std::string SubKind(const ItemCatalog::Item& it)
    {
        if (it.detailsRaw.empty()) return std::string();
        try
        {
            nlohmann::json d = nlohmann::json::parse(it.detailsRaw);
            if (it.type == "Armor" && d.contains("weight_class") && d["weight_class"].is_string())
                return d["weight_class"].get<std::string>();
            if (d.contains("type") && d["type"].is_string())
                return d["type"].get<std::string>();
        }
        catch (...) {}
        return std::string();
    }

    void RebuildLegRail(const AccountData::Model& m)
    {
        if (!g_legRail.empty() && g_lrAt == m.legendaryAt && g_lrN == m.legendaryMax.size() && g_lrCatVer == ItemCatalog::Version())
            return;
        g_legKind.clear();
        std::map<std::string, std::pair<int, int>> catAgg;                          // cat -> (owned, total)
        std::map<std::string, std::map<std::string, std::pair<int, int>>> subAgg;   // cat -> sub -> (owned, total)
        int allOwned = 0, allTotal = 0;
        for (const auto& kv : m.legendaryMax)
        {
            const ItemCatalog::Item& it = ItemCatalog::ById(kv.first);
            const std::string cat = CatName(it.type);
            const std::string sub = SubKind(it);
            g_legKind[kv.first] = { cat, sub };
            auto ot = m.legendaryOwned.find(kv.first);
            const bool owned = ot != m.legendaryOwned.end() && ot->second > 0;
            ++allTotal; if (owned) ++allOwned;
            auto& ca = catAgg[cat]; ++ca.second; if (owned) ++ca.first;
            if (!sub.empty()) { auto& sa = subAgg[cat][sub]; ++sa.second; if (owned) ++sa.first; }
        }
        std::vector<LegCat> cats;
        for (const auto& c : catAgg)
        {
            LegCat lc; lc.key = c.first; lc.name = c.first; lc.owned = c.second.first; lc.total = c.second.second;
            auto sit = subAgg.find(c.first);
            if (sit != subAgg.end())
                for (const auto& s : sit->second) lc.subs.push_back({ s.first, s.first, s.second.first, s.second.second });
            cats.push_back(std::move(lc));
        }
        std::sort(cats.begin(), cats.end(), [](const LegCat& a, const LegCat& b) { return CatRank(a.key) < CatRank(b.key); });
        g_legRail.clear();
        g_legRail.push_back({ std::string(), "All Legendaries", allOwned, allTotal, {} });
        for (LegCat& c : cats) g_legRail.push_back(std::move(c));
        g_lrAt = m.legendaryAt; g_lrN = m.legendaryMax.size(); g_lrCatVer = ItemCatalog::Version();
    }

    constexpr char kSubMark = '\x1f';   // rail key = "cat" (category) or "cat\x1fsub" (sub-kind); "" = All
}

void DrawLegendaryArmoryContent(App& app)
{
    const AccountData::Model& m = AccountData::Get();
    if (!AccountData::HasKey())
    { ApiReminder::Card(app, "legendaryarmory", "viewing your Legendary Armory", /*gated*/true); return; }
    if (!AccountData::HasScope(Api::TokenPermission::Unlocks))
    { Gw2Ui::EmptyState("Missing unlocks scope", "Legendary Armory needs the unlocks permission."); return; }
    if (m.legendaryMax.empty())
    { Gw2Ui::Label(m.haveLegendary ? "No legendary armory data." : "Loading legendary armory...", Gw2Ui::kTextDim, false, nullptr, 16.f); return; }

    auto ownedOf = [&m](int id) { auto it = m.legendaryOwned.find(id); return it != m.legendaryOwned.end() ? it->second : 0; };
    int ownedCount = 0; for (const auto& kv : m.legendaryMax) if (ownedOf(kv.first) > 0) ++ownedCount;
    const ItemUI::NameFn nameFn = [](int id) { return ItemCatalog::ById(id).name; };
    const float scale = Gw2Ui::TextScale();
    const std::string q = Lower(g_search);

    RebuildLegRail(m);

    // rail model: a 2-level mirror of g_legRail (rebuilt only when the armory-data token moves). The "All" row
    // keeps key "" (the GalleryRail's conventional root); a sub-kind's key encodes "cat\x1fsub" so it's unique.
    static Gw2Ui::GalleryRailNode s_tree; static uint64_t s_treeTok = ~0ull;
    const uint64_t treeTok = ItemCatalog::Version()
        ^ ((uint64_t)(m.legendaryAt * 1000.0) << 1) ^ ((uint64_t)m.legendaryMax.size() << 20) ^ ((uint64_t)m.legendaryOwned.size() << 40);
    if (s_treeTok != treeTok)
    {
        s_tree.children.clear();
        for (const LegCat& c : g_legRail)
        {
            Gw2Ui::GalleryRailNode node{ c.key, c.name, c.owned, c.total, {} };
            for (const LegSub& s : c.subs)
                node.children.push_back({ c.key + kSubMark + s.key, s.name, s.owned, s.total, {} });
            s_tree.children.push_back(std::move(node));
        }
        s_treeTok = treeTok;
    }
    std::string railKey = g_legSub.empty() ? g_legCat : (g_legCat + kSubMark + g_legSub);

    uint64_t tok = 1469598103934665603ull;
    auto mix = [&tok](uint64_t v) { tok ^= v; tok *= 1099511628211ull; };
    mix(ItemCatalog::Version()); mix((uint64_t)(m.legendaryAt * 1000.0)); mix((uint64_t)m.legendaryMax.size());
    mix((uint64_t)m.legendaryOwned.size()); mix((uint64_t)(unsigned)g_sort); mix((uint64_t)(g_asc ? 1 : 0));
    for (const char* p = g_search; *p; ++p) mix((unsigned char)*p);
    for (char ch : railKey) mix((unsigned char)ch);

    static const char* kSort[] = { "Name", "Type", "Owned" };

    Gw2Ui::GalleryBrowser gb;
    gb.id = "legendary";
    gb.title = "Legendary Armory";
    gb.have = ownedCount; gb.total = (int)m.legendaryMax.size(); gb.tallyVerb = "owned";
    gb.rightStatus = m.haveLegendary ? nullptr : "loading...";
    gb.searchBuf = g_search; gb.searchBufSize = (int)sizeof(g_search); gb.searchHint = "Search legendaries...";
    gb.sortOptions = kSort; gb.sortCount = 3; gb.sortSelected = &g_sort; gb.sortAsc = &g_asc;
    gb.viewMode = &app.config.itemsView; gb.settingsDirty = &app.settingsDirty;
    gb.railRoot = &s_tree; gb.railSelectedKey = &railKey;
    gb.railWidthPx = &app.config.PaneW("legendary.catrail", 200.f); gb.railDefaultW = 200.f;
    gb.orderToken = tok;
    gb.gridCell = 58.f; gb.gridGap = 6.f; gb.listRowH = 38.f;
    gb.emptyText = "No matches.";

    gb.rebuildOrder = [&](std::vector<int>& order) {
        for (const auto& kv : m.legendaryMax)
        {
            if (!g_legCat.empty())   // rail category / sub-kind filter
            {
                auto kit = g_legKind.find(kv.first);
                if (kit == g_legKind.end() || kit->second.first != g_legCat) continue;
                if (!g_legSub.empty() && kit->second.second != g_legSub) continue;
            }
            if (!q.empty() && ItemCatalog::ById(kv.first).keyLower.find(q) == std::string::npos) continue;
            order.push_back(kv.first);
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const ItemCatalog::Item& ia = ItemCatalog::ById(a);
            const ItemCatalog::Item& ib = ItemCatalog::ById(b);
            if (g_sort == 2) { const int oa = ownedOf(a), ob = ownedOf(b); if (oa != ob) return g_asc ? (oa < ob) : (oa > ob); }
            else if (g_sort == 1) { if (ia.type != ib.type) return g_asc ? (ia.type < ib.type) : (ia.type > ib.type); }
            else { if (ia.name != ib.name) return g_asc ? (ia.name < ib.name) : (ia.name > ib.name); }
            return ia.name < ib.name;   // stable tiebreak: always name ascending
        });
    };
    gb.drawGridCell = [&](int id, ImVec2 cmin, float cell) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int have = ownedOf(id);
        const auto mxIt = m.legendaryMax.find(id);
        const int mx = (mxIt != m.legendaryMax.end()) ? mxIt->second : 0;
        const ItemCatalog::Item& it = ItemCatalog::ById(id);
        AccountData::ItemMeta meta = LegMeta(it);
        ItemUI::DrawItemCell(it.id, &meta, ItemUI::Instance{}, nameFn, cell, it.statSet);
        if (have == 0) dl->AddRectFilled(cmin, ImVec2(cmin.x + cell, cmin.y + cell), IM_COL32(0, 0, 0, 110), 4.f);
        char ob[16]; std::snprintf(ob, sizeof(ob), "%d/%d", have, mx);
        const float bw = std::max(26.f, Gw2Ui::MeasureWidth(ob, 16.f) + 8.f), bh = 19.f;
        const ImVec2 bmin(cmin.x + cell - bw - 2.f, cmin.y + cell - bh - 2.f), bmax(cmin.x + cell - 2.f, cmin.y + cell - 2.f);
        dl->AddRectFilled(bmin, bmax, have > 0 ? IM_COL32(34, 56, 36, 242) : IM_COL32(22, 22, 22, 242), 3.f);
        dl->AddRect(bmin, bmax, have > 0 ? Gw2Ui::kGold : IM_COL32(130, 130, 130, 210), 3.f, 0, 1.f);
        Gw2Ui::LabelDL(dl, bmin, bmax, ob, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       have > 0 ? IM_COL32(226, 240, 216, 255) : IM_COL32(208, 208, 208, 255), false, nullptr, 16.f);
    };
    gb.drawListRow = [&](int id, const Gw2Ui::RowHotspot& row) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int have = ownedOf(id);
        const auto mxIt = m.legendaryMax.find(id);
        const int mx = (mxIt != m.legendaryMax.end()) ? mxIt->second : 0;
        const ItemCatalog::Item& it = ItemCatalog::ById(id);
        const float ic = row.height - 8.f, typeW = 150.f * scale;
        const ImVec2 p = row.min;
        if (!it.icon.empty())
        {
            char t[40]; std::snprintf(t, sizeof(t), "TC_ITEM_%d", id);
            if (void* tex = Tex::GetTextureFromURL(t, it.icon.c_str()))
                dl->AddImage((ImTextureID)tex, ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic));
        }
        dl->AddRect(ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic), ItemUI::RarityColor(it.rarity), 0.f, 0, 1.2f);
        char ob[24]; std::snprintf(ob, sizeof(ob), "%d / %d", have, mx);
        const float ow = Gw2Ui::MeasureWidth(ob, 18.f * scale) + 10.f;
        Gw2Ui::RowLabel(dl, row, row.width - ow, 8.f, ob, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                        have > 0 ? Gw2Ui::kGold : Gw2Ui::kTextDim, false, nullptr, 18.f * scale);
        Gw2Ui::RowLabel(dl, row, row.width - ow - typeW, ow + 8.f, it.type.c_str(), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                        Gw2Ui::kTextDim, false, nullptr, 16.f * scale);
        Gw2Ui::RowLabel(dl, row, ic + 12.f, ow + typeW + 16.f, it.name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        have > 0 ? ItemUI::RarityColor(it.rarity) : Gw2Ui::kTextDim, false, nullptr, 18.f * scale);
        if (row.hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            AccountData::ItemMeta meta = LegMeta(it);
            ItemUI::DrawItemTooltip(it.id, &meta, ItemUI::Instance{}, nameFn, it.statSet);
        }
        ItemUI::DrawCopyMenu("##legcopy", row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), it.name, it.chatLink);
    };

    Gw2Ui::DrawGalleryBrowser(gb);

    // decode the (possibly clicked) rail selection back into g_legCat/g_legSub for next frame's filter
    const size_t sep = railKey.find(kSubMark);
    if (sep == std::string::npos) { g_legCat = railKey; g_legSub.clear(); }
    else { g_legCat = railKey.substr(0, sep); g_legSub = railKey.substr(sep + 1); }
}
