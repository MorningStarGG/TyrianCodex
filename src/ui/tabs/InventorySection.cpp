#include "ui/tabs/InventorySection.h"

#include "app/App.h"
#include "api/core/Permissions.h"
#include "render/glyphs/Glyphs.h"
#include "ui/Gw2Ui.h"
#include "ui/ApiReminder.h"
#include "ui/GuideViewer.h"
#include "app/AccountData.h"
#include "app/StaticData.h"
#include "ui/items/ItemRender.h"   // shared item cell + tooltip + field helpers (RarityColor, CoinText, ...)
#include "ui/items/ItemScore.h"    // shared search primitives (Lower, QueryTerms, TokenFieldScore)
#include "ui/items/ItemTpDetail.h" // shared Trading Post price slide-out card
#include "util/Textures.h"
#include "util/Trading.h"          // Trading::Coins (cleanup vendor-value total)
#include "ui/dashboard/widgets/ApiWidgetUtil.h"   // DashApi::CharacterPicker / Gate (Equipment view)
#include <map>

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    enum SourceFilter { FilterAll = 0, FilterCharacters, FilterShared, FilterBank, FilterMaterials };

    struct LocationRecord
    {
        int itemId = 0;
        int count = 0;
        std::string source;
        std::string detail;
        const Api::V2::ItemSlot* slot = nullptr;
        const Api::V2::MaterialStack* material = nullptr;
    };

    struct SearchGroup
    {
        int itemId = 0;
        int score = 0;
        std::string sortName;
        std::vector<LocationRecord> records;
    };

    static char g_search[96] = "";
    static int  g_filter = FilterAll;
    static std::string g_source = "all";
    static int         g_tpSel = 0;
    static int         g_tpShown = 0;
    static std::string g_tpName;
    static float       g_tpAnim = 0.f;
    static int         g_tpScrollItem = 0;

    using namespace ItemUI;   // Lower / QueryTerms / TokenFieldScore / RarityColor / CoinText / ItemTypeLine / ...

    std::string ItemName(const AccountData::Model& m, int id)
    {
        auto it = m.itemMeta.find(id);
        if (it != m.itemMeta.end() && !it->second.name.empty()) return it->second.name;
        char b[32];
        std::snprintf(b, sizeof(b), "Item %d", id);
        return b;
    }

    const AccountData::ItemMeta* Meta(const AccountData::Model& m, int id)
    {
        auto it = m.itemMeta.find(id);
        return it == m.itemMeta.end() ? nullptr : &it->second;
    }

    // Name resolver the shared ItemUI renderers use for upgrade/infusion names + the unknown-item fallback title.
    ItemUI::NameFn NameFnFor(const AccountData::Model& m) { return [&m](int id) { return ItemName(m, id); }; }

    bool InventoryItemTradeable(int id)
    {
        return ItemTp::Tradeable(id);
    }

    void ToggleInventoryTpCard(const AccountData::Model& m, int id)
    {
        if (id <= 0 || !InventoryItemTradeable(id))
        {
            g_tpSel = 0;
            return;
        }
        const bool closing = (g_tpSel == id);
        g_tpSel = closing ? 0 : id;
        g_tpShown = id;
        g_tpName = ItemName(m, id);
        if (!closing) g_tpScrollItem = 0;
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

    float InventoryCellSize()
    {
        return std::max(48.f, 52.f * Gw2Ui::TextScale());
    }

    float SearchResultCellSize()
    {
        return std::max(40.f, 44.f * Gw2Ui::TextScale());
    }

    float InventoryCellGap()
    {
        return std::max(4.f, 5.f * Gw2Ui::TextScale());
    }

    const char* StatusText(AccountData::InventoryState state)
    {
        switch (state)
        {
        case AccountData::InventoryState::Loading: return "indexing";
        case AccountData::InventoryState::Complete: return "ready";
        case AccountData::InventoryState::Partial: return "partial";
        case AccountData::InventoryState::Error: return "error";
        case AccountData::InventoryState::Idle:
        default: return "idle";
        }
    }

    bool SourceAllowed(SourceFilter filter, const char* sourceType)
    {
        if (filter == FilterAll) return true;
        if (filter == FilterCharacters) return std::strcmp(sourceType, "character") == 0;
        if (filter == FilterShared) return std::strcmp(sourceType, "shared") == 0;
        if (filter == FilterBank) return std::strcmp(sourceType, "bank") == 0;
        if (filter == FilterMaterials) return std::strcmp(sourceType, "materials") == 0;
        return true;
    }

    void DrawStatusLine(const AccountData::InventoryIndexStatus& st)
    {
        char b[160];
        if (st.state == AccountData::InventoryState::Loading)
            std::snprintf(b, sizeof(b), "%s  %d/%d sources  %d item details",
                          StatusText(st.state), st.completedSources, st.totalSources, st.pendingItemMeta);
        else if (st.lastRefreshAt > 0)
            std::snprintf(b, sizeof(b), "%s  updated %.0fs ago", StatusText(st.state), ImGui::GetTime() - st.lastRefreshAt);
        else
            std::snprintf(b, sizeof(b), "%s", StatusText(st.state));
        Gw2Ui::Label(b, Gw2Ui::kTextSub, false, nullptr, 16.f);
    }

    bool SourceRow(const char* id, const char* name, const char* sub, bool selected, int row)
    {
        const float titleFs = 20.f;
        const float subFs = 16.f;
        const float h = 54.f;
        const Gw2Ui::RowHotspot hit = Gw2Ui::Row(id, row, h, 0.f, false, selected);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Gw2Ui::RowHotspot titleRow{ ImVec2(hit.min.x, hit.min.y + 5.f), hit.width, 23.f, false, false };
        Gw2Ui::RowLabel(dl, titleRow, 8.f, 8.f, name, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        selected ? Gw2Ui::kGold : IM_COL32(220, 214, 196, 255), true, nullptr, titleFs);
        if (sub && *sub)
        {
            const Gw2Ui::RowHotspot subRow{ ImVec2(hit.min.x, hit.min.y + 30.f), hit.width, h - 33.f, false, false };
            Gw2Ui::RowLabel(dl, subRow, 8.f, 8.f, sub, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                            Gw2Ui::kTextSub, false, nullptr, subFs);
        }
        return hit.clicked;
    }

    void SameLineGrid(int col, int cols, float gap)
    {
        if (col + 1 < cols) ImGui::SameLine(0.f, gap);
    }

    void DrawSlotGrid(const AccountData::Model& m, const std::vector<Api::V2::ItemSlot>& slots,
                      const std::string& source, float width)
    {
        const float gap = InventoryCellGap();
        const float cell = InventoryCellSize();
        const int cols = std::max(1, (int)((width + gap) / (cell + gap)));
        for (int i = 0; i < (int)slots.size(); ++i)
        {
            ImGui::PushID(i);
            const Api::V2::ItemSlot& s = slots[i];
            char loc[160];
            std::snprintf(loc, sizeof(loc), "%s slot %d", source.c_str(), i + 1);
            if (ItemUI::DrawItemCell(s.id, Meta(m, s.id), ItemUI::Instance{ s.count, loc, s.id > 0 ? &s : nullptr, nullptr }, NameFnFor(m), cell))
                ToggleInventoryTpCard(m, s.id);
            ImGui::PopID();
            SameLineGrid(i % cols, cols, gap);
        }
    }

    void DrawBagModeIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, bool singleBag, ImU32 col)
    {
        const float s = Gw2Ui::TextScale();
        const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float cell = 4.6f * s;
        const float gap = 2.2f * s;
        const float stroke = std::max(1.f, 1.2f * s);
        if (singleBag)
        {
            const float startX = c.x - (cell * 3.f + gap * 2.f) * 0.5f;
            const float startY = c.y - (cell * 3.f + gap * 2.f) * 0.5f;
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                {
                    const ImVec2 a(startX + x * (cell + gap), startY + y * (cell + gap));
                    dl->AddRect(a, ImVec2(a.x + cell, a.y + cell), col, 0.f, 0, stroke);
                }
            return;
        }

        const float rowW = 19.f * s;
        const float rowH = 4.6f * s;
        const float startX = c.x - rowW * 0.5f;
        const float startY = c.y - (rowH * 3.f + gap * 2.f) * 0.5f;
        for (int y = 0; y < 3; ++y)
        {
            const float yy = startY + y * (rowH + gap);
            dl->AddRect(ImVec2(startX, yy), ImVec2(startX + rowW, yy + rowH), col, 0.f, 0, stroke);
            dl->AddLine(ImVec2(startX + 6.f * s, yy), ImVec2(startX + 6.f * s, yy + rowH), col, stroke);
        }
    }

    bool SourceSingleGrid(const App& app, const std::string& source)
    {
        auto it = app.config.inventorySingleGridBySource.find(source);
        return it != app.config.inventorySingleGridBySource.end() && it->second;
    }

    bool DrawSourceLayoutToggle(App& app, const std::string& source, const char* groupedName, const char* singleName)
    {
        bool singleGrid = SourceSingleGrid(app, source);
        char tip[160];
        std::snprintf(tip, sizeof(tip), "Showing %s. Click to show %s.", singleGrid ? singleName : groupedName, singleGrid ? groupedName : singleName);
        Gw2Ui::ActionButtonResult r = Gw2Ui::ActionButtonFrame("##sourceGridMode", ImVec2(32.f, 26.f), Gw2Ui::ActionButtonVariant::Normal, false, tip);
        if (r.clicked)
        {
            singleGrid = !singleGrid;
            app.config.inventorySingleGridBySource[source] = singleGrid;
            app.settingsDirty = true;
        }
        const ImU32 col = r.hovered || singleGrid ? Gw2Ui::kGold : IM_COL32(205, 190, 150, 230);
        DrawBagModeIcon(ImGui::GetWindowDrawList(), r.min, r.max, singleGrid, col);
        return singleGrid;
    }

    void DrawCharacterSource(const AccountData::Model& m, const std::string& name, float width, bool singleBag)
    {
        auto it = m.inv.find(name);
        if (it == m.inv.end() || !it->second.have)
        {
            Gw2Ui::Label("Loading character bags...", Gw2Ui::kTextSub, false, nullptr, 16.f);
            return;
        }
        const AccountData::CharInv& ci = it->second;
        if (!ci.error.empty())
        {
            Gw2Ui::Label(ci.error.c_str(), IM_COL32(235, 130, 110, 255), false, nullptr, 16.f);
            return;
        }
        char sum[64];
        std::snprintf(sum, sizeof(sum), "%d/%d slots used", ci.usedSlots, ci.totalSlots);
        const float frac = ci.totalSlots > 0 ? (float)ci.usedSlots / (float)ci.totalSlots : 0.f;
        Gw2Ui::ProgressBar(frac, sum, width, 16.f, BagFillColor(frac));
        ImGui::Dummy(ImVec2(0.f, 6.f));

        if (singleBag)
        {
            std::vector<Api::V2::ItemSlot> slots;
            slots.reserve((size_t)std::max(0, ci.totalSlots));
            for (const AccountData::CharBag& bag : ci.bags)
            {
                if (!bag.present) continue;
                slots.insert(slots.end(), bag.slots.begin(), bag.slots.end());
                for (int pad = (int)bag.slots.size(); pad < bag.size; ++pad)
                    slots.push_back(Api::V2::ItemSlot{});
            }
            if ((int)slots.size() < ci.totalSlots)
                slots.resize((size_t)ci.totalSlots);
            Gw2Ui::SectionHeader("All bags", nullptr, 18.f, Gw2Ui::kGold, false);
            DrawSlotGrid(m, slots, name + " inventory", width);
            return;
        }

        for (int bi = 0; bi < (int)ci.bags.size(); ++bi)
        {
            const AccountData::CharBag& bag = ci.bags[bi];
            if (!bag.present) continue;
            int used = 0;
            for (const Api::V2::ItemSlot& s : bag.slots) if (s.id > 0) ++used;
            std::string bagName = ItemName(m, bag.id);
            char hdr[160];
            std::snprintf(hdr, sizeof(hdr), "%s  %d/%d", bagName.c_str(), used, bag.size);
            Gw2Ui::SectionHeader(hdr, nullptr, 18.f, Gw2Ui::kGold, false);
            DrawSlotGrid(m, bag.slots, name + " bag " + std::to_string(bi + 1), width);
            ImGui::Dummy(ImVec2(0.f, 8.f));
        }
    }

    void DrawMaterials(const AccountData::Model& m, float width, bool singleGrid)
    {
        if (!m.haveMats) { Gw2Ui::Label("Loading material storage...", Gw2Ui::kTextSub, false, nullptr, 16.f); return; }
        const float gap = InventoryCellGap();
        const float cell = InventoryCellSize();
        const int cols = std::max(1, (int)((width + gap) / (cell + gap)));
        if (singleGrid)
        {
            Gw2Ui::SectionHeader("All materials", nullptr, 18.f, Gw2Ui::kGold, false);
            int visible = 0;
            for (const Api::V2::MaterialStack& mat : m.materials)
            {
                if (mat.count <= 0) continue;
                ImGui::PushID(mat.id);
                char loc[160];
                std::snprintf(loc, sizeof(loc), "Material Storage: %s", StaticData::MaterialCategoryName(mat.category).c_str());
                if (ItemUI::DrawItemCell(mat.id, Meta(m, mat.id), ItemUI::Instance{ mat.count, loc, nullptr, &mat }, NameFnFor(m), cell))
                    ToggleInventoryTpCard(m, mat.id);
                ImGui::PopID();
                SameLineGrid(visible % cols, cols, gap);
                ++visible;
            }
            return;
        }

        std::map<int, std::vector<const Api::V2::MaterialStack*>> byCat;
        for (const Api::V2::MaterialStack& mat : m.materials)
            if (mat.count > 0) byCat[mat.category].push_back(&mat);
        for (const auto& kv : byCat)
        {
            Gw2Ui::SectionHeader(StaticData::MaterialCategoryName(kv.first).c_str(), nullptr, 18.f, Gw2Ui::kGold, false);
            for (int i = 0; i < (int)kv.second.size(); ++i)
            {
                ImGui::PushID(kv.second[i]->id);
                char loc[160];
                std::snprintf(loc, sizeof(loc), "Material Storage: %s", StaticData::MaterialCategoryName(kv.first).c_str());
                if (ItemUI::DrawItemCell(kv.second[i]->id, Meta(m, kv.second[i]->id), ItemUI::Instance{ kv.second[i]->count, loc, nullptr, kv.second[i] }, NameFnFor(m), cell))
                    ToggleInventoryTpCard(m, kv.second[i]->id);
                ImGui::PopID();
                SameLineGrid(i % cols, cols, gap);
            }
            ImGui::Dummy(ImVec2(0.f, 8.f));
        }
    }

    void AddSearchRecords(std::vector<LocationRecord>& out, const AccountData::Model& m, SourceFilter filter)
    {
        if (SourceAllowed(filter, "shared"))
            for (int i = 0; i < (int)m.sharedSlots.size(); ++i)
                if (m.sharedSlots[i].id > 0) out.push_back({ m.sharedSlots[i].id, std::max(1, m.sharedSlots[i].count), "Shared Inventory", "slot " + std::to_string(i + 1), &m.sharedSlots[i] });
        if (SourceAllowed(filter, "bank"))
            for (int i = 0; i < (int)m.bankSlots.size(); ++i)
                if (m.bankSlots[i].id > 0) out.push_back({ m.bankSlots[i].id, std::max(1, m.bankSlots[i].count), "Bank", "slot " + std::to_string(i + 1), &m.bankSlots[i] });
        if (SourceAllowed(filter, "materials"))
            for (const Api::V2::MaterialStack& mat : m.materials)
                if (mat.id > 0 && mat.count > 0) out.push_back({ mat.id, mat.count, "Material Storage", StaticData::MaterialCategoryName(mat.category), nullptr, &mat });
        if (SourceAllowed(filter, "character"))
            for (const auto& kv : m.inv)
                for (int bi = 0; bi < (int)kv.second.bags.size(); ++bi)
                    for (int si = 0; si < (int)kv.second.bags[bi].slots.size(); ++si)
                    {
                        const Api::V2::ItemSlot& s = kv.second.bags[bi].slots[si];
                        if (s.id > 0) out.push_back({ s.id, std::max(1, s.count), kv.first, "bag " + std::to_string(bi + 1) + ", slot " + std::to_string(si + 1), &s });
                    }
    }

    int InventorySearchScore(const AccountData::Model& m, const LocationRecord& r,
                             const std::vector<std::string>& terms, const std::string& phrase, bool includeLocation)
    {
        if (terms.empty()) return 1;

        const std::string name = Lower(ItemName(m, r.itemId));
        const std::string idStr = std::to_string(r.itemId);
        const AccountData::ItemMeta* meta = Meta(m, r.itemId);
        std::string rarity, typeLine, detailLine, desc, flags, restrictions, matCategory, location;
        if (meta)
        {
            rarity = Lower(meta->rarity);
            typeLine = Lower(ItemTypeLine(*meta));
            detailLine = Lower(ItemDetailLine(*meta));
            desc = Lower(CleanDescription(!DetailStr(*meta, "description").empty() ? DetailStr(*meta, "description") : meta->description));
            flags = Lower(JoinStrings(meta->flags));
            restrictions = Lower(JoinStrings(meta->restrictions));
        }
        if (r.material) matCategory = Lower(StaticData::MaterialCategoryName(r.material->category));
        if (includeLocation) location = Lower(r.source + " " + r.detail);

        int score = 0;
        for (const std::string& term : terms)
        {
            int best = 0;
            best = std::max(best, TokenFieldScore(name, term, 1400, 1250, 1000));
            best = std::max(best, TokenFieldScore(idStr, term, 1300, 1150, 900));
            best = std::max(best, TokenFieldScore(rarity, term, 760, 700, 620));
            best = std::max(best, TokenFieldScore(typeLine, term, 720, 660, 580));
            best = std::max(best, TokenFieldScore(detailLine, term, 700, 640, 560));
            best = std::max(best, TokenFieldScore(matCategory, term, 640, 590, 520));
            best = std::max(best, TokenFieldScore(flags, term, 520, 470, 410));
            best = std::max(best, TokenFieldScore(restrictions, term, 500, 450, 390));
            best = std::max(best, TokenFieldScore(desc, term, 430, 390, 330));
            best = std::max(best, TokenFieldScore(location, term, 360, 330, 280));
            if (best <= 0) return -1;
            score += best;
        }

        if (!phrase.empty())
        {
            score += TokenFieldScore(name, phrase, 5000, 4200, 3200);
            score += TokenFieldScore(idStr, phrase, 4600, 3900, 3000);
            score += TokenFieldScore(typeLine, phrase, 1800, 1500, 1200);
            score += TokenFieldScore(detailLine, phrase, 1600, 1300, 1000);
            score += TokenFieldScore(desc, phrase, 800, 650, 500);
            score += TokenFieldScore(location, phrase, 650, 540, 430);
        }
        return score;
    }

    // Render item groups as the All-Storage view does: an item cell + its per-location lines ("source - detail xN").
    // nameHeader=true (search): each item gets its own gold "Name xN" header. nameHeader=false (cleanup): the item
    // name is an inline label line instead, so the only headers are the caller's category headers.
    void RenderSearchGroups(const AccountData::Model& m, const std::vector<SearchGroup>& ordered, float width,
                            bool nameHeader = true, const char* totalLabel = "Search result total")
    {
        for (const SearchGroup& g : ordered)
        {
            int total = 0;
            for (const LocationRecord& r : g.records) total += r.count;
            if (nameHeader)
            {
                char title[220];
                std::snprintf(title, sizeof(title), "%s  x%d", ItemName(m, g.itemId).c_str(), total);
                Gw2Ui::SectionHeader(title, nullptr, 20.f, Gw2Ui::kGold, false);
            }
            const float cell = SearchResultCellSize();
            ImGui::PushID(g.itemId);
            if (ItemUI::DrawItemCell(g.itemId, Meta(m, g.itemId), ItemUI::Instance{ total, totalLabel, nullptr, nullptr }, NameFnFor(m), cell))
                ToggleInventoryTpCard(m, g.itemId);
            ImGui::SameLine();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float lineH = 24.f;
            const int   lineCount = (int)g.records.size() + (nameHeader ? 0 : 1);
            const float rowH = std::max(cell, (float)lineCount * lineH);
            ImGui::Dummy(ImVec2(std::max(40.f, width - cell - 10.f), rowH));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float lineRight = p.x + width - cell - 14.f;
            float y = p.y;
            if (lineCount == 1) y += std::max(0.f, (rowH - lineH) * 0.5f);
            if (!nameHeader)   // item name as the first line + a right-aligned xTotal (the category is the header)
            {
                const AccountData::ItemMeta* meta = Meta(m, g.itemId);
                char tot[16]; std::snprintf(tot, sizeof(tot), "x%d", total);
                const float tw = Gw2Ui::MeasureWidth(tot, 18.f) + 10.f;
                Gw2Ui::LabelDL(dl, ImVec2(p.x, y), ImVec2(lineRight - tw, y + lineH), ItemName(m, g.itemId).c_str(),
                               Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, ItemUI::RarityColor(meta ? meta->rarity : std::string()), false, nullptr, 18.f);
                Gw2Ui::LabelDL(dl, ImVec2(lineRight - tw, y), ImVec2(lineRight, y + lineH), tot,
                               Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 18.f);
                y += lineH;
            }
            for (const LocationRecord& r : g.records)
            {
                std::string line = r.source + " - " + r.detail + "  x" + std::to_string(r.count);
                Gw2Ui::LabelDL(dl, ImVec2(p.x + (nameHeader ? 0.f : 14.f), y), ImVec2(lineRight, y + lineH), line.c_str(),
                               Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(220, 214, 196, 255), false, nullptr, nameHeader ? 18.f : 16.f);
                y += lineH;
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0.f, 6.f));
        }
    }

    // Freshness token for the whole-account storage scan: changes whenever AccountData refreshes shared / bank /
    // materials / any character's bags, or as item metadata streams in (which changes names -> sort order). Cheap
    // to fold each frame; it gates the expensive collect + score + group + sort so that runs only on a real change.
    uint64_t InvDataToken(const AccountData::Model& m)
    {
        uint64_t h = 1469598103934665603ull;
        auto mix  = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        auto mixd = [&](double d) { uint64_t b = 0; std::memcpy(&b, &d, sizeof(b)); mix(b); };
        mixd(m.sharedAt); mixd(m.bankAt); mixd(m.matsAt);
        mix((uint64_t)m.inv.size());
        for (const auto& kv : m.inv) mixd(kv.second.at);
        mix((uint64_t)(unsigned)m.inventoryStatus.pendingItemMeta);
        return h;
    }
    // Fold a source filter + query string into the data token -> a per-view cache key.
    uint64_t InvViewToken(uint64_t dataTok, int filter, const char* query)
    {
        uint64_t h = dataTok;
        auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix((uint64_t)(unsigned)filter);
        for (const char* p = query ? query : ""; *p; ++p) mix((unsigned char)*p);
        return h;
    }

    void DrawSearchResults(const AccountData::Model& m, const char* query, SourceFilter filter, float width)
    {
        // Cache the grouped+sorted result behind a (data + filter + query) token; the expensive collect/score/
        // group/sort below runs only on a change, the per-frame path just renders the cached groups. The cached
        // records carry value data only -- RenderSearchGroups never derefs the slot/material pointers, and the data
        // token tracks the *At stamps that would invalidate them, so the cache stays valid across refreshes.
        static std::vector<SearchGroup> s_ordered;
        static uint64_t s_tok = ~0ull;
        static bool s_empty = true;

        const uint64_t tok = InvViewToken(InvDataToken(m), (int)filter, query);
        if (tok != s_tok)
        {
            s_tok = tok;
            s_ordered.clear();

            std::vector<LocationRecord> records;
            AddSearchRecords(records, m, filter);
            const std::vector<std::string> terms = QueryTerms(query);
            const std::string phrase = JoinTerms(terms);
            std::map<int, bool> includeAllLocations;
            std::map<int, int> scores;
            for (const LocationRecord& r : records)
            {
                const int itemScore = InventorySearchScore(m, r, terms, phrase, false);
                if (terms.empty() || itemScore >= 0)
                {
                    includeAllLocations[r.itemId] = true;
                    scores[r.itemId] = std::max(scores[r.itemId], itemScore);
                    continue;
                }
                const int locationScore = InventorySearchScore(m, r, terms, phrase, true);
                if (locationScore >= 0)
                    scores[r.itemId] = std::max(scores[r.itemId], locationScore);
            }

            std::map<int, SearchGroup> grouped;
            for (const LocationRecord& r : records)
            {
                const bool itemMatched = includeAllLocations.find(r.itemId) != includeAllLocations.end();
                const bool locationMatched = !itemMatched && InventorySearchScore(m, r, terms, phrase, true) >= 0;
                if (!itemMatched && !locationMatched) continue;
                SearchGroup& g = grouped[r.itemId];
                g.itemId = r.itemId;
                g.score = std::max(g.score, scores[r.itemId]);
                g.sortName = Lower(ItemName(m, r.itemId));
                g.records.push_back(r);
            }

            s_ordered.reserve(grouped.size());
            for (auto& kv : grouped)
                s_ordered.push_back(std::move(kv.second));
            std::sort(s_ordered.begin(), s_ordered.end(), [](const SearchGroup& a, const SearchGroup& b)
            {
                if (a.score != b.score) return a.score > b.score;
                if (a.sortName != b.sortName) return a.sortName < b.sortName;
                return a.itemId < b.itemId;
            });
            s_empty = s_ordered.empty();
        }

        if (s_empty)
        {
            Gw2Ui::EmptyState("No items found", "Try a different item name or source filter.");
            return;
        }
        RenderSearchGroups(m, s_ordered, width);
    }

    // Cleanup advisor (My Inventory "Cleanup" source): read-only tidy-up suggestions, rendered exactly like the
    // All-Storage view (item + per-location lines), but grouped under cleanup-TYPE headers (Vendor junk / Deposit /
    // Salvage) instead of per-item name headers. Built from the same LocationRecords (no new API) and honoring the
    // source filter chips, so it shows who/where + filters per source like the rest of inventory.
    void DrawCleanup(const AccountData::Model& m, float width)
    {
        // Cache the three bucketed+sorted groups (+ junk value) behind a (data + filter) token -- rebuild only on
        // a real inventory change, then render the cached groups every frame. Same pointer-safety as the search
        // view (the render path reads value data + fresh metas by id only).
        static uint64_t s_tok = ~0ull;
        static std::vector<SearchGroup> s_g[3];
        static long long s_junkValue = 0;
        static bool s_empty = true;

        const uint64_t tok = InvViewToken(InvDataToken(m), g_filter, "");
        if (tok != s_tok)
        {
            s_tok = tok;
            for (auto& v : s_g) v.clear();
            s_junkValue = 0;

            std::vector<LocationRecord> records;
            AddSearchRecords(records, m, (SourceFilter)g_filter);

            // bucket: 0 = vendor junk, 1 = deposit-to-materials, 2 = salvage low-rarity gear, -1 = none.
            // "in bags" = anywhere a Deposit/Salvage would apply (a character bag or shared) -- not Bank/Material Storage.
            auto bucketOf = [&](const LocationRecord& r) -> int {
                const AccountData::ItemMeta* meta = Meta(m, r.itemId);
                if (!meta || !meta->have) return -1;
                const bool inBags = r.source != "Bank" && r.source != "Material Storage";
                if (meta->rarity == "Junk") return 0;
                if (inBags && meta->type == "CraftingMaterial") return 1;
                if (inBags && (meta->type == "Armor" || meta->type == "Weapon") && (meta->rarity == "Basic" || meta->rarity == "Fine")) return 2;
                return -1;
            };

            std::map<int, SearchGroup> g[3];
            for (const LocationRecord& r : records)
            {
                const int b = bucketOf(r);
                if (b < 0) continue;
                SearchGroup& sg = g[b][r.itemId];
                sg.itemId = r.itemId; sg.sortName = Lower(ItemName(m, r.itemId)); sg.records.push_back(r);
                if (b == 0) { const AccountData::ItemMeta* meta = Meta(m, r.itemId); if (meta) s_junkValue += (long long)meta->vendorValue * r.count; }
            }

            for (int b = 0; b < 3; ++b)
            {
                s_g[b].reserve(g[b].size());
                for (auto& kv : g[b]) s_g[b].push_back(std::move(kv.second));
                std::sort(s_g[b].begin(), s_g[b].end(), [](const SearchGroup& a, const SearchGroup& bb) { return a.sortName < bb.sortName; });
            }
            s_empty = s_g[0].empty() && s_g[1].empty() && s_g[2].empty();
        }

        if (s_empty)
        { Gw2Ui::Label("No cleanup suggestions -- your bags look tidy.", Gw2Ui::kTextDim, false, nullptr, 16.f); return; }

        if (!s_g[0].empty())
        {
            char h[96]; std::snprintf(h, sizeof(h), "Vendor junk  --  %d item%s", (int)s_g[0].size(), s_g[0].size() == 1 ? "" : "s");
            char sub[64]; std::snprintf(sub, sizeof(sub), "%s at any vendor", Trading::Coins(s_junkValue).c_str());
            Gw2Ui::SectionHeader(h, sub, 20.f, Gw2Ui::kGold, true);
            RenderSearchGroups(m, s_g[0], width, /*nameHeader*/ false, "junk in storage");
            ImGui::Dummy(ImVec2(0.f, 6.f));
        }
        if (!s_g[1].empty())
        {
            char h[96]; std::snprintf(h, sizeof(h), "Deposit to material storage  --  %d type%s", (int)s_g[1].size(), s_g[1].size() == 1 ? "" : "s");
            Gw2Ui::SectionHeader(h, "materials in your bags (Deposit All Materials)", 20.f, Gw2Ui::kGold, true);
            RenderSearchGroups(m, s_g[1], width, /*nameHeader*/ false, "in your bags");
            ImGui::Dummy(ImVec2(0.f, 6.f));
        }
        if (!s_g[2].empty())
        {
            char h[96]; std::snprintf(h, sizeof(h), "Salvage low-rarity gear  --  %d item%s", (int)s_g[2].size(), s_g[2].size() == 1 ? "" : "s");
            Gw2Ui::SectionHeader(h, "Basic/Fine armor & weapons -> materials", 20.f, Gw2Ui::kGold, true);
            RenderSearchGroups(m, s_g[2], width, /*nameHeader*/ false, "in your bags");
        }
    }

    void DrawFilterChips()
    {
        static const Gw2Ui::ChipItem kFilters[] = {
            { "All" }, { "Characters" }, { "Shared" }, { "Bank" }, { "Materials" } };
        Gw2Ui::ChipRow("##invfilter", kFilters, 5, &g_filter, 30.f, 16.f, 6.f, 60.f, true);
    }
}

void SetInventorySearch(const char* query)
{
    std::snprintf(g_search, sizeof(g_search), "%s", query ? query : "");
    g_filter = FilterAll;
}

// ---- Equipment view (Items tab "Equipment" scope): a hero-panel layout of a character's equipped gear. ----
namespace { std::string g_equipChar; }
void SetEquipmentChar(const char* name) { g_equipChar = name ? name : ""; }

void DrawEquipmentContent(App& app)
{
    const AccountData::Model& m = AccountData::Get();
    if (!AccountData::HasKey())
    { ApiReminder::Card(app, "equipment", "viewing a character's equipped gear", /*gated*/true); return; }
    if (!AccountData::HasScope(Api::TokenPermission::Characters))
    { Gw2Ui::EmptyState("Missing characters scope", "Equipment needs the characters permission."); return; }

    Gw2Ui::SectionHeader("Equipment", nullptr, 20.f, Gw2Ui::kGold, true);
    const std::string name = DashApi::CharacterPicker("##equipchar", g_equipChar, 240.f);  // defaults to active char
    if (name.empty()) { Gw2Ui::Label("No characters cached yet.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }
    g_equipChar = name;
    AccountData::EnsureCharDetail(name);
    ImGui::Dummy(ImVec2(0.f, 6.f));

    auto eit = m.equip.find(name);
    if (eit == m.equip.end() || !eit->second.have)
    { Gw2Ui::Label("Loading equipment...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }
    const AccountData::CharEquip& eq = eit->second;

    std::map<std::string, const Api::V2::ItemSlot*> bySlot;
    for (const Api::V2::ItemSlot& s : eq.pieces) bySlot[s.slot] = &s;

    const AccountData::AcctChar* ch = nullptr;
    for (const AccountData::AcctChar& c : m.characters) if (c.name == name) { ch = &c; break; }

    const float gap = 6.f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail  = ImGui::GetContentRegionAvail();   // the host child is fixed (~821 x ~470) and scrolls if we overflow
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y0 = origin.y;
    // The left strip is 8 rows tall (6 armor + 2 weapon). Size the cell so it fits the available HEIGHT at (near)
    // full size -- this is what keeps the panel from scrolling without shrinking everything down.
    const float cellMax = InventoryCellSize();
    float cell = (avail.y - 40.f) / 8.f - gap;
    cell = std::max(34.f, std::min(cell, cellMax));
    const float leftX = origin.x + 4.f;
    const float colA = leftX, colB = leftX + cell + gap;   // weapon set 1 | set 2 columns (armor uses colA)

    auto slot = [&](const char* sn, float x, float y)
    {
        ImGui::PushID(sn);   // disambiguate duplicate item ids (e.g. two identical rings)
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        auto sit = bySlot.find(sn);
        const Api::V2::ItemSlot* s = (sit != bySlot.end()) ? sit->second : nullptr;
        if (s && s->id > 0)
            ItemUI::DrawItemCell(s->id, Meta(m, s->id), ItemUI::Instance{ 0, s->slot, s, nullptr }, NameFnFor(m), cell);
        else
        {
            dl->AddRectFilled(ImVec2(x + 1, y + 1), ImVec2(x + cell - 1, y + cell - 1), IM_COL32(255, 255, 255, 6), 3.f);
            dl->AddRect(ImVec2(x, y), ImVec2(x + cell, y + cell), IM_COL32(120, 110, 80, 110), 3.f, 0, 1.4f);
            ImGui::SetCursorScreenPos(ImVec2(x, y)); ImGui::Dummy(ImVec2(cell, cell));
        }
        ImGui::PopID();
    };
    auto tag = [&](const char* t, float x, float y)
    { Gw2Ui::LabelDL(dl, ImVec2(x, y), ImVec2(x + 30.f, y + 16.f), t, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f); };

    // ---- LEFT: armor column + the two weapon sets below (mainhand over offhand), swap glyph between. ----
    static const char* kArmor[6] = { "Helm", "Shoulders", "Coat", "Gloves", "Leggings", "Boots" };
    for (int i = 0; i < 6; ++i) slot(kArmor[i], colA, y0 + i * (cell + gap));
    const float wTop = y0 + 6 * (cell + gap) + 12.f;
    slot("WeaponA1", colA, wTop);                slot("WeaponB1", colB, wTop);
    slot("WeaponA2", colA, wTop + (cell + gap)); slot("WeaponB2", colB, wTop + (cell + gap));
    {   // swap glyph (-> over <-) in the gap between the two sets, just above the mainhand row
        const float gx = colA + cell + gap * 0.5f, gy = wTop - 9.f;
        const ImU32 sc = IM_COL32(184, 170, 132, 225);
        dl->AddLine(ImVec2(gx - 6, gy - 3), ImVec2(gx + 6, gy - 3), sc, 1.6f); dl->AddLine(ImVec2(gx + 2, gy - 6), ImVec2(gx + 6, gy - 3), sc, 1.6f); dl->AddLine(ImVec2(gx + 2, gy), ImVec2(gx + 6, gy - 3), sc, 1.6f);
        dl->AddLine(ImVec2(gx - 6, gy + 3), ImVec2(gx + 6, gy + 3), sc, 1.6f); dl->AddLine(ImVec2(gx - 6, gy + 3), ImVec2(gx - 2, gy), sc, 1.6f); dl->AddLine(ImVec2(gx - 6, gy + 3), ImVec2(gx - 2, gy + 6), sc, 1.6f);
    }
    tag("I",  colA + cell * 0.5f - 4.f, wTop + 2 * (cell + gap) + 1.f);
    tag("II", colB + cell * 0.5f - 4.f, wTop + 2 * (cell + gap) + 1.f);
    const float leftBottom = wTop + 2 * (cell + gap) + 18.f;
    const float leftRight  = colB + cell;   // right edge of the gear strip

    // ---- RIGHT: attributes (2-col icon + value) + a trinket cluster below, right-anchored (like the game). ----
    const float colW = 132.f, rowH = 26.f, aIsz = 24.f;
    const float rightW = colW * 2.f;
    const float rightX = origin.x + avail.x - rightW - 6.f;   // left edge of the right block
    const float attrColA = rightX, attrColB = rightX + colW;
    const float racx = rightX + rightW * 0.5f;                // right-block center (header)
    float ay = y0 + 6.f, rightBottom = ay;
    AccountData::EnsureEquipAttributes(name);
    if (eq.haveAttrs)
    {
        dl->AddLine(ImVec2(rightX + 6.f, ay), ImVec2(rightX + rightW - 6.f, ay), IM_COL32(150, 124, 72, 90), 1.f); ay += 8.f;
        Gw2Ui::LabelDL(dl, ImVec2(rightX, ay), ImVec2(rightX + rightW, ay + 18.f), "Equipment attributes", Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Top, Gw2Ui::kGold, false, nullptr, 16.f); ay += 26.f;
        struct AR { const char* key; const char* label; bool pct; };
        static const AR kRows[] = {
            { "Power", "Power", false }, { "Precision", "Precision", false }, { "Toughness", "Toughness", false }, { "Vitality", "Vitality", false },
            { "Ferocity", "Ferocity", false }, { "ConditionDamage", "Condition Damage", false }, { "HealingPower", "Healing Power", false }, { "Concentration", "Concentration", false },
            { "Expertise", "Expertise", false }, { "Armor", "Armor", false }, { "Health", "Health", false },
            { "CritChance", "Critical Chance", true }, { "CritDamage", "Critical Damage", true }, { "BoonDuration", "Boon Duration", true }, { "ConditionDuration", "Condition Duration", true } };
        const int nAttr = (int)(sizeof(kRows) / sizeof(kRows[0]));
        const int half = (nAttr + 1) / 2;                 // column-major: col A = first `half`, col B = the rest
        const float gridTop = ay;
        for (int i = 0; i < nAttr; ++i)
        {
            const int col = i < half ? 0 : 1, r = i < half ? i : i - half;
            const AR& a = kRows[i];
            const float x = (col == 0) ? attrColA : attrColB;
            const float y = gridTop + r * rowH;
            void* tx = Tex::GetBundledTexture(("data\\textures\\ui\\attributes\\" + std::string(a.key) + ".png").c_str());
            if (tx) dl->AddImage(tx, ImVec2(x, y + 1.f), ImVec2(x + aIsz, y + 1.f + aIsz));
            auto ai = eq.attributes.find(a.key); const double v = (ai != eq.attributes.end()) ? ai->second : 0.0;
            char vb[24]; if (a.pct) std::snprintf(vb, sizeof(vb), "%.1f%%", v * 100.0); else std::snprintf(vb, sizeof(vb), "%.0f", v);
            Gw2Ui::LabelDL(dl, ImVec2(x + aIsz + 6.f, y), ImVec2(x + colW - 6.f, y + aIsz), vb, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(236, 230, 212, 255), false, nullptr, 18.f);
            ImGui::PushID(a.key);
            ImGui::SetCursorScreenPos(ImVec2(x, y)); ImGui::InvisibleButton("##at", ImVec2(colW, aIsz));
            if (ImGui::IsItemHovered()) Gw2Ui::Tooltip(a.label);
            ImGui::PopID();
        }
        ay = gridTop + half * rowH + 12.f;
        // Trinkets + relic, as a compact cluster (rows of 4) below the attributes.
        static const char* kTrinkets[7] = { "Backpack", "Amulet", "Accessory1", "Accessory2", "Ring1", "Ring2", "Relic" };
        for (int i = 0; i < 7; ++i) slot(kTrinkets[i], rightX + (i % 4) * (cell + gap), ay + (i / 4) * (cell + gap));
        rightBottom = ay + 2 * (cell + gap);
    }

    // ---- CENTER: a LARGE class icon (stands in for the character model), centered in the freed area and sized
    // to the full center width; the name/level/race are drawn over the top. ----
    const float cLeft = leftRight + 10.f, cRight = rightX - 10.f;
    const float cx = (cLeft + cRight) * 0.5f;
    const float halfW = std::max(100.f, (cRight - cLeft) * 0.5f - 6.f);
    float centerBottom = y0;
    void* tex = (ch && !ch->profession.empty()) ? Tex::GetBundledTexture(("data\\textures\\ui\\professions\\" + ch->profession + ".png").c_str()) : nullptr;
    if (tex)
    {
        float isc = std::min((cRight - cLeft) - 6.f, avail.y - 12.f);   // fill the center width, bounded by the panel height (no clip)
        isc = std::min(isc, 560.f); if (isc < 120.f) isc = 120.f;
        const float ix = cx - isc * 0.5f, iy = y0 + (avail.y - isc) * 0.5f;
        dl->AddImage(tex, ImVec2(ix, iy), ImVec2(ix + isc, iy + isc));
        centerBottom = iy + isc;
    }
    float cy = y0 + 6.f;
    Gw2Ui::LabelDL(dl, ImVec2(cx - halfW, cy), ImVec2(cx + halfW, cy + 36.f), name.c_str(), Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Top, IM_COL32(255, 244, 207, 255), false, nullptr, 30.f, 0.f, 1.4f); cy += 40.f;
    if (ch)
    {
        char b[96]; std::snprintf(b, sizeof(b), "Level %d %s", ch->level, ch->profession.c_str());
        Gw2Ui::LabelDL(dl, ImVec2(cx - halfW, cy), ImVec2(cx + halfW, cy + 24.f), b, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Top, Gw2Ui::kTextSub, false, nullptr, 20.f); cy += 26.f;
        if (!ch->race.empty()) { Gw2Ui::LabelDL(dl, ImVec2(cx - halfW, cy), ImVec2(cx + halfW, cy + 20.f), ch->race.c_str(), Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Top, Gw2Ui::kTextDim, false, nullptr, 16.f); cy += 24.f; }
    }
    centerBottom = std::max(centerBottom, cy);

    // Reserve height -- clamp to the available height so the host child never scrolls (the icon is draw-list only,
    // so it can extend toward the bottom edge without driving a scrollbar; the slot/button items all fit by design).
    const float bottom = std::max({ leftBottom, rightBottom, centerBottom });
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(avail.x, std::min(bottom - y0 + 4.f, avail.y - 2.f)));
}

// Top owned-item matches for a query, ordered best-first -- the same record-collect + score + group pass as
// DrawSearchResults, but it returns {itemId, total} pairs (one per distinct owned item, summed across every
// location) for the dashboard Inventory widget's inline results. The full tab remains the grouped browser.
std::vector<InventoryHit> QueryInventoryTop(const AccountData::Model& m, const char* query)
{
    // Cache behind a (data + query) token; the dashboard Inventory widget calls this every frame while its search
    // box has text, so without the gate the whole-account collect+score+group+sort would run per frame.
    static uint64_t s_tok = ~0ull;
    static std::vector<InventoryHit> s_out;

    const uint64_t tok = InvViewToken(InvDataToken(m), FilterAll, query);
    if (tok == s_tok) return s_out;
    s_tok = tok;
    s_out.clear();

    std::vector<LocationRecord> records;
    AddSearchRecords(records, m, FilterAll);
    const std::vector<std::string> terms  = ItemUI::QueryTerms(query ? query : "");
    const std::string              phrase = ItemUI::JoinTerms(terms);

    std::map<int, bool> itemMatched;
    std::map<int, int>  scores;
    for (const LocationRecord& r : records)
    {
        const int s = InventorySearchScore(m, r, terms, phrase, false);
        if (terms.empty() || s >= 0) { itemMatched[r.itemId] = true; scores[r.itemId] = std::max(scores[r.itemId], s); }
        else { const int loc = InventorySearchScore(m, r, terms, phrase, true); if (loc >= 0) scores[r.itemId] = std::max(scores[r.itemId], loc); }
    }

    struct G { int id = 0, total = 0, score = 0; std::string sortName;
               std::vector<std::pair<std::string, int>> sources; };   // per-source counts, in first-seen order
    std::map<int, G> grouped;
    for (const LocationRecord& r : records)
    {
        const bool im = itemMatched.find(r.itemId) != itemMatched.end();
        const bool lm = !im && InventorySearchScore(m, r, terms, phrase, true) >= 0;
        if (!im && !lm) continue;
        G& g = grouped[r.itemId];
        g.id = r.itemId;
        g.total += r.count;
        g.score = std::max(g.score, scores[r.itemId]);
        if (g.sortName.empty()) g.sortName = ItemUI::Lower(ItemName(m, r.itemId));
        auto sit = std::find_if(g.sources.begin(), g.sources.end(),
                                [&](const std::pair<std::string, int>& s) { return s.first == r.source; });
        if (sit == g.sources.end()) g.sources.push_back({ r.source, r.count });
        else                        sit->second += r.count;
    }

    std::vector<G> ord;
    ord.reserve(grouped.size());
    for (auto& kv : grouped) ord.push_back(kv.second);
    std::sort(ord.begin(), ord.end(), [](const G& a, const G& b)
    {
        if (a.score != b.score) return a.score > b.score;
        if (a.sortName != b.sortName) return a.sortName < b.sortName;
        return a.id < b.id;
    });

    s_out.reserve(ord.size());
    for (const G& g : ord)
    {
        std::string where;
        const int kMax = 3;   // keep the footer readable; collapse the rest into "+N more"
        for (int i = 0; i < (int)g.sources.size() && i < kMax; ++i)
        {
            if (!where.empty()) where += ", ";
            where += g.sources[i].first;
            if (g.sources[i].second > 1) where += " x" + std::to_string(g.sources[i].second);
        }
        if ((int)g.sources.size() > kMax) where += ", +" + std::to_string((int)g.sources.size() - kMax) + " more";
        s_out.push_back({ g.id, g.total, where });
    }
    return s_out;
}

void DrawInventoryTooltip(const AccountData::Model& m, int itemId, int total, const std::string& where)
{
    ItemUI::DrawItemTooltip(itemId, Meta(m, itemId), ItemUI::Instance{ total, where, nullptr, nullptr }, NameFnFor(m));
}

void DrawInventoryContent(App& app)
{
    ItemUI::SetTpPrices(app.config.showTpPrices);   // gate the shared renderers' live TP price lines
    const bool hasKey = AccountData::HasKey();
    const bool hasInv = AccountData::HasScope(Api::TokenPermission::Inventories);
    const bool hasChars = AccountData::HasScope(Api::TokenPermission::Characters);
    AccountData::InventoryIndexStatus st = AccountData::InventoryStatus();
    if (hasKey && hasInv && st.state == AccountData::InventoryState::Idle)
        AccountData::WarmInventoryIndex(false);

    const AccountData::Model& m = AccountData::Get();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fullW = avail.x;

    char summary[160];
    std::snprintf(summary, sizeof(summary), "%d/%d bags  %d/%d bank  %d materials",
                  [&] { int u = 0; for (const auto& kv : m.inv) u += kv.second.usedSlots; return u; }(),
                  [&] { int t = 0; for (const auto& kv : m.inv) t += kv.second.totalSlots; return t; }(),
                  m.bankUsed, m.bankTotal, m.matTypes);
    Gw2Ui::SectionHeader("Inventory", summary, 22.f, Gw2Ui::kGold, true);

    if (!hasKey)
    {
        ApiReminder::Card(app, "inventory", "browsing account storage (your bank, materials and bags)", /*gated*/true);
        return;
    }
    if (!hasInv)
    {
        Gw2Ui::EmptyState("Missing inventories scope", "Inventory browsing needs the inventories permission.");
        return;
    }

    const float refreshW = std::min(110.f, fullW);
    const ImVec2 statusP = ImGui::GetCursorScreenPos();
    DrawStatusLine(AccountData::InventoryStatus());
    ImGui::SetCursorScreenPos(ImVec2(statusP.x + fullW - refreshW, statusP.y));
    if (Gw2Ui::ActionButton("Refresh all", refreshW, 26.f, Gw2Ui::ActionButtonVariant::Normal, "Refresh all inventory sources"))
        AccountData::WarmInventoryIndex(true);
    const float toolbarBottom = std::max(ImGui::GetItemRectMax().y, statusP.y + 24.f);
    ImGui::SetCursorScreenPos(ImVec2(statusP.x, toolbarBottom + 5.f));

    Gw2Ui::SearchBox("##invsearch", g_search, sizeof(g_search), fullW, "Search account inventory...");
    DrawFilterChips();
    if (!hasChars)
        Gw2Ui::Label("Character bags need the characters scope; account storage can still be browsed.", Gw2Ui::kTextSub, false, nullptr, 14.f);
    for (const std::string& err : AccountData::InventoryStatus().errors)
        Gw2Ui::Label(err.c_str(), IM_COL32(235, 130, 110, 255), false, nullptr, 14.f);

    ImGui::Dummy(ImVec2(0.f, 6.f));
    const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    // Draggable, persisted source-rail width (shared Gw2Ui::VSplitter); seeds to the old responsive default.
    const float invMaxRail = std::clamp(bodyAvail.x - 280.f, 150.f, 360.f);
    float& railW = app.config.PaneW("inv.rail", std::min(220.f, std::max(170.f, bodyAvail.x * 0.25f)));
    railW = std::clamp(railW, 150.f, invMaxRail);
    if (Gw2Ui::BeginPanel("##invsrc", railW, bodyAvail.y, nullptr))
    {
        int row = 0;
        if (SourceRow("##src_all", "All Storage", "search all sources", g_source == "all", row++)) g_source = "all";
        if (SourceRow("##src_cleanup", "Cleanup", "tidy-up suggestions", g_source == "cleanup", row++)) g_source = "cleanup";
        char sub[64];
        std::snprintf(sub, sizeof(sub), "%d/%d slots", m.sharedUsed, m.sharedTotal);
        if (SourceRow("##src_shared", "Shared Inventory", sub, g_source == "shared", row++)) g_source = "shared";
        std::snprintf(sub, sizeof(sub), "%d/%d slots", m.bankUsed, m.bankTotal);
        if (SourceRow("##src_bank", "Bank", sub, g_source == "bank", row++)) g_source = "bank";
        std::snprintf(sub, sizeof(sub), "%d material types", m.matTypes);
        if (SourceRow("##src_mats", "Materials", sub, g_source == "materials", row++)) g_source = "materials";
        for (const AccountData::AcctChar& ch : m.characters)
        {
            auto it = m.inv.find(ch.name);
            if (it != m.inv.end()) std::snprintf(sub, sizeof(sub), "%d/%d slots", it->second.usedSlots, it->second.totalSlots);
            else std::snprintf(sub, sizeof(sub), "not indexed");
            ImGui::PushID(ch.name.c_str());
            if (SourceRow("##src_char", ch.name.c_str(), sub, g_source == ("char:" + ch.name), row++))
                g_source = "char:" + ch.name;
            ImGui::PopID();
        }
        Gw2Ui::EndPanel();
    }
    ImGui::SameLine(0.f, 10.f);
    const ImVec2 mainPanelPos = ImGui::GetCursorScreenPos();
    const ImVec2 mainPanelSize(std::max(80.f, bodyAvail.x - railW - 10.f), bodyAvail.y);
    if (Gw2Ui::BeginPanel("##invmain", mainPanelSize.x, mainPanelSize.y, nullptr))
    {
        bool sourceSingleGrid = SourceSingleGrid(app, g_source);
        const bool characterPage = g_source.rfind("char:", 0) == 0;
        const bool materialPage = g_source == "materials";
        const bool layoutTogglePage = (g_search[0] == '\0' && (characterPage || materialPage));
        if (layoutTogglePage)
        {
            const ImVec2 toolP = ImGui::GetCursorScreenPos();
            const float contentW = ImGui::GetContentRegionAvail().x;
            const float btnW = 32.f, btnH = 26.f;
            ImGui::SetCursorScreenPos(ImVec2(toolP.x + std::max(0.f, contentW - btnW - ImGui::GetStyle().ScrollbarSize - 12.f), toolP.y));
            sourceSingleGrid = characterPage
                ? DrawSourceLayoutToggle(app, g_source, "bags separately", "one combined bag")
                : DrawSourceLayoutToggle(app, g_source, "materials by category", "one material grid");
            ImGui::SetCursorScreenPos(ImVec2(toolP.x, toolP.y + btnH + 5.f));
        }
        ImGui::BeginChild("##invscroll", ImGui::GetContentRegionAvail(), false);
        const float gutter = ImGui::GetStyle().ScrollbarSize + 12.f;
        const float mainW = std::max(80.f, ImGui::GetContentRegionAvail().x - gutter);
        if (g_search[0] != '\0' || g_source == "all")
        {
            DrawSearchResults(m, g_search, (SourceFilter)g_filter, mainW);
        }
        else if (g_source == "shared")
        {
            Gw2Ui::SectionHeader("Shared Inventory", nullptr, 20.f, Gw2Ui::kGold, false);
            DrawSlotGrid(m, m.sharedSlots, "Shared Inventory", mainW);
        }
        else if (g_source == "bank")
        {
            Gw2Ui::SectionHeader("Bank", nullptr, 20.f, Gw2Ui::kGold, false);
            DrawSlotGrid(m, m.bankSlots, "Bank", mainW);
        }
        else if (g_source == "cleanup")
        {
            DrawCleanup(m, mainW);
        }
        else if (g_source == "materials")
        {
            DrawMaterials(m, mainW, sourceSingleGrid);
        }
        else if (g_source.rfind("char:", 0) == 0)
        {
            DrawCharacterSource(m, g_source.substr(5), mainW, sourceSingleGrid);
        }
        ImGui::EndChild();
        Gw2Ui::EndPanel();
    }
    // Splitter handle in the gap between the source rail and the main panel (drawn last so it doesn't disturb
    // the SameLine flow above).
    if (Gw2Ui::VSplitter("##inv_rail_split", bodyOrigin.x + railW + 5.f, bodyOrigin.y, bodyAvail.y, &railW, 150.f, invMaxRail))
        app.settingsDirty = true;
    ItemTp::DrawSlideCard(app, g_tpSel, g_tpShown, g_tpName, g_tpAnim, g_tpScrollItem,
                          mainPanelPos.x, mainPanelPos.y, mainPanelSize.x, mainPanelSize.y);
}
