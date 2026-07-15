#include "ui/tabs/RecipesSection.h"

#include "app/App.h"
#include "app/AccountData.h"
#include "app/CraftingEngine.h"     // CraftingEngine::All / Recipe / Version / Ready
#include "app/ItemCatalog.h"        // ItemCatalog::ById / RarityRank / Version (output + ingredient items)
#include "ui/Gw2Ui.h"
#include "ui/ApiReminder.h"
#include "ui/GuideViewer.h"         // SetClipboard + ViewerAlert
#include "ui/wiki/WikiReader.h"     // Wiki::RequestOpen
#include "ui/items/ItemRender.h"    // ItemUI::RarityColor
#include "util/Textures.h"          // Tex::GetTextureFromURL
#include "util/ChatLink.h"          // ChatLink::Recipe / Item

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
    char        g_search[96] = {};
    int         g_learn = 0;                 // 0 = All, 1 = Learned, 2 = Unknown
    const char* kLearn[] = { "All", "Learned", "Unknown" };
    std::string g_selDisc;                   // selected discipline ("" = all disciplines)
    std::string g_selRarity;                 // selected output rarity within the discipline ("" = all rarities)

    std::string Lower(const char* s) { std::string o; if (s) for (const char* p = s; *p; ++p) o += (char)std::tolower((unsigned char)*p); return o; }

    bool IsLearned(const CraftingEngine::Recipe& r, const std::set<int>& learned)
    { return r.id > 0 && learned.count(r.id) != 0; }

    // Mystic Forge is NOT real crafting (no recipe sheet, datawars2-sourced, outputs aren't in the item catalog), so
    // it's excluded from this Collections browser -- the CraftingEngine still keeps it for the TP crafting calculator.
    bool IsMysticForge(const CraftingEngine::Recipe& r) { return r.type == "MysticForge"; }

    std::string OutRarity(const CraftingEngine::Recipe& r)
    { const std::string& rr = ItemCatalog::ById(r.out).rarity; return rr.empty() ? std::string("Basic") : rr; }

    // ---- rail data: Discipline -> output Rarity, with learned/total, cached behind a (recipes, items, learned-set)
    // change token. A recipe appears under EACH of its disciplines (and that disc's output-rarity bucket). ----
    struct RarityBucket { std::string rarity; int rank = 0, learned = 0, total = 0; std::vector<int> idx; };
    struct DiscGroup    { std::string disc; int learned = 0, total = 0; std::vector<RarityBucket> rarities; std::vector<int> idx; };
    std::vector<DiscGroup> g_groups;
    int      g_total = 0, g_totLearned = 0;
    uint64_t g_grpRVer = (uint64_t)-1, g_grpIVer = (uint64_t)-1; double g_grpAt = -1.0; size_t g_grpN = (size_t)-1;

    DiscGroup* FindDisc(const std::string& d) { for (DiscGroup& g : g_groups) if (g.disc == d) return &g; return nullptr; }

    void RebuildGroups(const std::set<int>& learned, double recipesAt)
    {
        if (g_grpRVer == CraftingEngine::Version() && g_grpIVer == ItemCatalog::Version()
            && g_grpAt == recipesAt && g_grpN == learned.size())
            return;
        g_groups.clear();
        const std::vector<CraftingEngine::Recipe>& all = CraftingEngine::All();
        g_total = 0; g_totLearned = 0;
        for (int i = 0; i < (int)all.size(); ++i)
        {
            const CraftingEngine::Recipe& r = all[i];
            if (IsMysticForge(r) || ItemCatalog::ById(r.out).name.empty()) continue;   // skip MF + un-nameable outputs (guild decorations etc. aren't in /v2/items)
            const std::string rar = OutRarity(r);
            const int rank = std::max(0, ItemCatalog::RarityRank(rar));
            const bool learn = IsLearned(r, learned);
            ++g_total; if (learn) ++g_totLearned;
            auto bucket = [&](const std::string& d)
            {
                DiscGroup* g = FindDisc(d);
                if (!g) { g_groups.push_back({ d, 0, 0, {}, {} }); g = &g_groups.back(); }
                ++g->total; if (learn) ++g->learned; g->idx.push_back(i);
                RarityBucket* b = nullptr;
                for (RarityBucket& rb : g->rarities) if (rb.rarity == rar) { b = &rb; break; }
                if (!b) { g->rarities.push_back({ rar, rank, 0, 0, {} }); b = &g->rarities.back(); }
                ++b->total; if (learn) ++b->learned; b->idx.push_back(i);
            };
            if (r.disc.empty()) bucket("Other");
            else for (const std::string& d : r.disc) bucket(d);
        }
        std::sort(g_groups.begin(), g_groups.end(), [](const DiscGroup& a, const DiscGroup& b) { return a.disc < b.disc; });
        for (DiscGroup& g : g_groups)
            std::sort(g.rarities.begin(), g.rarities.end(), [](const RarityBucket& a, const RarityBucket& b) { return a.rank < b.rank; });
        g_grpRVer = CraftingEngine::Version(); g_grpIVer = ItemCatalog::Version(); g_grpAt = recipesAt; g_grpN = learned.size();
    }

    bool RailRow(const char* label, int learned, int total, bool selected, int rowIndex, float w, int level)
    {
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##recrail", rowIndex, 38.f, w, false, selected);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (selected) dl->AddRectFilled(ImVec2(row.min.x + 2.f, row.min.y + 3.f), ImVec2(row.min.x + 5.f, row.min.y + 35.f), Gw2Ui::kGold, 1.f);
        char cb[24]; std::snprintf(cb, sizeof(cb), "%d / %d", learned, total);
        const float cw = Gw2Ui::MeasureWidth(cb, 16.f) + 10.f;
        Gw2Ui::RowLabel(dl, row, w - cw - 4.f, 6.f, cb, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                        selected ? Gw2Ui::kGold : Gw2Ui::kTextDim, false, nullptr, 16.f);
        Gw2Ui::RowLabel(dl, row, 14.f + level * 16.f, cw + 10.f, label, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        selected ? Gw2Ui::kGold : Gw2Ui::kTextSelected, false, nullptr, level ? 16.f : 18.f);
        return row.clicked;
    }

    void DrawRail(float width, float height)
    {
        ImGui::BeginChild("##recraill", ImVec2(width, height), false);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
        const float w = ImGui::GetContentRegionAvail().x;
        int row = 0;
        ImGui::PushID(-1);
        if (RailRow("All Recipes", g_totLearned, g_total, g_selDisc.empty(), row++, w, 0)) { g_selDisc.clear(); g_selRarity.clear(); }
        ImGui::PopID();
        for (int gi = 0; gi < (int)g_groups.size(); ++gi)
        {
            const DiscGroup& g = g_groups[gi];
            ImGui::PushID(gi * 100);
            if (RailRow(g.disc.c_str(), g.learned, g.total, g_selDisc == g.disc && g_selRarity.empty(), row++, w, 0))
            { g_selDisc = g.disc; g_selRarity.clear(); }
            ImGui::PopID();
            if (g_selDisc == g.disc)   // the selected discipline expands to its output rarities
                for (int ri = 0; ri < (int)g.rarities.size(); ++ri)
                {
                    const RarityBucket& b = g.rarities[ri];
                    ImGui::PushID(gi * 100 + ri + 1);
                    if (RailRow(b.rarity.c_str(), b.learned, b.total, g_selRarity == b.rarity, row++, w, 1))
                        g_selRarity = (g_selRarity == b.rarity) ? std::string() : b.rarity;
                    ImGui::PopID();
                }
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    // ---- the filtered display list (recipe indices) for the current selection + search + learned filter, cached. ----
    std::vector<int> g_view;
    uint64_t g_vRVer = (uint64_t)-1, g_vIVer = (uint64_t)-1; double g_vAt = -1.0; size_t g_vN = (size_t)-1;
    int g_vLearn = -1; std::string g_vSearch = "\x01", g_vDisc = "\x02", g_vRarity = "\x03";

    void RebuildView(const std::set<int>& learned, double recipesAt)
    {
        if (g_vRVer == CraftingEngine::Version() && g_vIVer == ItemCatalog::Version() && g_vAt == recipesAt
            && g_vN == learned.size() && g_vLearn == g_learn && g_vSearch == g_search
            && g_vDisc == g_selDisc && g_vRarity == g_selRarity)
            return;
        const std::vector<CraftingEngine::Recipe>& all = CraftingEngine::All();
        const std::string q = Lower(g_search);
        g_view.clear();

        std::vector<int> allIdx;
        const std::vector<int>* base = nullptr;
        if (g_selDisc.empty())
        {
            for (int i = 0; i < (int)all.size(); ++i)
                if (!IsMysticForge(all[i]) && !ItemCatalog::ById(all[i].out).name.empty()) allIdx.push_back(i);
            base = &allIdx;
        }
        else if (DiscGroup* g = FindDisc(g_selDisc))
        {
            base = &g->idx;
            if (!g_selRarity.empty())
                for (RarityBucket& b : g->rarities) if (b.rarity == g_selRarity) { base = &b.idx; break; }
        }
        if (!base) base = &allIdx;

        for (int i : *base)
        {
            const CraftingEngine::Recipe& r = all[i];
            const bool learn = IsLearned(r, learned);
            if (g_learn == 1 && !learn) continue;
            if (g_learn == 2 && learn) continue;
            if (!q.empty() && Lower(ItemCatalog::ById(r.out).name.c_str()).find(q) == std::string::npos) continue;
            g_view.push_back(i);
        }
        std::sort(g_view.begin(), g_view.end(), [&](int a, int b) {
            return ItemCatalog::ById(all[a].out).name < ItemCatalog::ById(all[b].out).name;
        });
        g_vRVer = CraftingEngine::Version(); g_vIVer = ItemCatalog::Version(); g_vAt = recipesAt; g_vN = learned.size();
        g_vLearn = g_learn; g_vSearch = g_search; g_vDisc = g_selDisc; g_vRarity = g_selRarity;
    }

    void RecipeTooltip(const CraftingEngine::Recipe& r, const ItemCatalog::Item& out, bool learned)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        if (!out.icon.empty())
        {
            char t[40]; std::snprintf(t, sizeof(t), "TC_ITEM_%d", r.out);
            if (void* tex = Tex::GetTextureFromURL(t, out.icon.c_str()))
            {
                const float hero = 96.f, x0 = ImGui::GetCursorPosX();
                ImGui::Image((ImTextureID)tex, ImVec2(hero, hero));
                ImGui::SetCursorPosX(x0);
                ImGui::Dummy(ImVec2(hero, 3.f));
            }
        }
        std::string title = out.name.empty() ? ("Item " + std::to_string(r.out)) : out.name;
        if (r.cnt > 1) { title += " x"; title += std::to_string(r.cnt); }
        Gw2Ui::TooltipTitle(title.c_str());
        Gw2Ui::TooltipText("Crafting Recipe");
        Gw2Ui::TooltipSeparator();
        if (!r.disc.empty())
        {
            std::string d; for (size_t i = 0; i < r.disc.size(); ++i) { if (i) d += ", "; d += r.disc[i]; }
            std::string l = "Discipline: " + d; Gw2Ui::TooltipText(l.c_str());
        }
        if (r.rating > 0) { std::string l = "Rating: " + std::to_string(r.rating); Gw2Ui::TooltipText(l.c_str()); }
        if (!out.rarity.empty()) { std::string l = "Rarity: " + out.rarity; Gw2Ui::TooltipText(l.c_str()); }
        if (!r.ing.empty())
        {
            Gw2Ui::TooltipSeparator();
            Gw2Ui::TooltipText("Ingredients:");
            for (const std::pair<int, int>& ing : r.ing)
            {
                const ItemCatalog::Item& it = ItemCatalog::ById(ing.first);
                std::string nm = it.name.empty() ? ("Item " + std::to_string(ing.first)) : it.name;
                char l[176]; std::snprintf(l, sizeof(l), "   %d   %s", ing.second, nm.c_str());
                Gw2Ui::TooltipText(l);
            }
        }
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted(learned ? "Learned" : "Not yet learned");
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Right-click for menu.");
        Gw2Ui::TooltipEnd();
    }

    void RecipeMenu(const CraftingEngine::Recipe& r, const ItemCatalog::Item& out, bool openNow)
    {
        std::vector<Gw2Ui::MenuNode> nodes;
        if (!out.name.empty())     nodes.push_back({ "Open in Wiki", 1 });
        if (r.id > 0)              nodes.push_back({ "Copy recipe chat link", 2 });
        if (!out.chatLink.empty()) nodes.push_back({ "Copy item chat link", 3 });
        if (nodes.empty()) return;
        switch (Gw2Ui::ContextMenuTree("##recmenu", nodes, openNow))
        {
            case 1: Wiki::RequestOpen(out.name); break;
            case 2: SetClipboard(ChatLink::Recipe(r.id)); ViewerAlert("Copied recipe chat link - paste in game."); break;
            case 3: SetClipboard(out.chatLink); ViewerAlert("Copied item chat link - paste in game."); break;
            default: break;
        }
    }
}

void DrawRecipesContent(App& app)
{
    if (!AccountData::HasKey())
    { ApiReminder::Card(app, "collections", "viewing your learned recipes", /*gated*/ true); return; }
    if (!AccountData::HasScope(Api::TokenPermission::Unlocks))
    { Gw2Ui::EmptyState("Missing unlocks scope", "The learned-recipe status needs the unlocks permission."); return; }
    if (!CraftingEngine::Ready() || ItemCatalog::Count() == 0)
    { Gw2Ui::Label("Loading recipes...", Gw2Ui::kTextDim, false, nullptr, 16.f); return; }

    const AccountData::Model& m = AccountData::Get();
    const std::set<int>& learned = m.recipesUnlocked;
    RebuildGroups(learned, m.recipesAt);
    RebuildView(learned, m.recipesAt);

    {
        const float ui = Gw2Ui::GlobalScale();
        const float barW = ImGui::GetContentRegionAvail().x, dropW = 120.f * ui, gap = 8.f * ui;
        const float searchW = Gw2Ui::FillWidth(barW, 1, dropW + gap, gap, 160.f * ui);
        Gw2Ui::SearchBox("##recsearch", g_search, (int)sizeof(g_search), searchW, "Search output item...");
        ImGui::SameLine(searchW + gap);
        { int lf = g_learn; if (Gw2Ui::DropdownPx("##reclearn", kLearn, 3, &lf, dropW, nullptr, nullptr, 0, Gw2Ui::InputBoxHeight())) g_learn = lf; }
    }

    { char h[96]; std::snprintf(h, sizeof(h), "Recipes  --  %d / %d learned", g_totLearned, g_total);
      Gw2Ui::SectionHeader(h, AccountData::Refreshing() ? "updating..." : nullptr, 18.f, Gw2Ui::kGold, true); }
    // SectionHeader already draws its own underline -- no extra Divider (that double-lined the header).

    const float scale = Gw2Ui::TextScale();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    const float maxRail = std::clamp(avail.x - 320.f, 160.f, 360.f);
    float& railW = app.config.PaneW("recipes.rail", 220.f);
    railW = std::clamp(railW, 160.f, std::max(160.f, maxRail));
    DrawRail(railW, avail.y);
    if (Gw2Ui::VSplitter("##rec_rail_split", origin.x + railW + 3.f, origin.y, avail.y, &railW, 160.f, std::max(160.f, maxRail)))
        app.settingsDirty = true;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + railW + 6.f, origin.y));
    const float bodyW = avail.x - railW - 6.f;

    ImGui::BeginChild("##recbody", ImVec2(bodyW, 0.f), false);
    const float availW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_view.empty()) { Gw2Ui::Label("No matching recipes.", Gw2Ui::kTextDim, false, nullptr, 16.f); ImGui::EndChild(); return; }

    const std::vector<CraftingEngine::Recipe>& all = CraftingEngine::All();
    // The discipline line only adds info in "All Recipes" (where the discipline varies). When a discipline is
    // selected in the rail it's redundant, so the row collapses to a single line (name centered + rating + status).
    const bool  showDisc = g_selDisc.empty();
    const float rowH = (showDisc ? 52.f : 44.f) * scale, ic = rowH - 14.f, statusW = 92.f * scale;
    ImGuiListClipper clipper; clipper.Begin((int)g_view.size(), rowH);
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const CraftingEngine::Recipe& r = all[g_view[i]];
            const ItemCatalog::Item& out = ItemCatalog::ById(r.out);
            const bool learn = IsLearned(r, learned);
            const ImU32 rc = ItemUI::RarityColor(out.rarity);
            ImGui::PushID(g_view[i]);
            const Gw2Ui::RowHotspot row = Gw2Ui::Row("##recrow", i, rowH, availW);
            const ImVec2 p = row.min;
            if (!out.icon.empty())
            {
                char t[40]; std::snprintf(t, sizeof(t), "TC_ITEM_%d", r.out);
                if (void* tex = Tex::GetTextureFromURL(t, out.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(p.x + 7.f, p.y + 7.f), ImVec2(p.x + 7.f + ic, p.y + 7.f + ic));
                dl->AddRect(ImVec2(p.x + 7.f, p.y + 7.f), ImVec2(p.x + 7.f + ic, p.y + 7.f + ic), rc, 2.f, 0, 1.4f);
            }
            // status (far right, vertically centered)
            Gw2Ui::RowLabel(dl, row, row.width - statusW, 10.f, learn ? "Learned" : "Unknown", Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                            learn ? Gw2Ui::kGold : Gw2Ui::kTextDim, false, nullptr, 16.f * scale);
            // crafting-rating capsule pill, just left of the status
            float nameRightPad = statusW + 14.f;
            if (r.rating > 0)
            {
                char rb[16]; std::snprintf(rb, sizeof(rb), "%d", r.rating);
                const float pfs = 18.f * scale, pw = Gw2Ui::PillWidth(rb, pfs, 20.f), ph = Gw2Ui::PillHeight(pfs);
                Gw2Ui::PillAt(dl, ImVec2(p.x + row.width - nameRightPad - pw, p.y + (rowH - ph) * 0.5f), rb, pfs, Gw2Ui::kPillBorder, Gw2Ui::kPillText, Gw2Ui::kPillFill, 20.f);
                nameRightPad += pw + 14.f;
            }
            // output name (rarity color) -- top when the discipline line shows, else vertically centered
            std::string nm = out.name.empty() ? ("Item " + std::to_string(r.out)) : out.name;
            if (r.cnt > 1) { nm += "  x"; nm += std::to_string(r.cnt); }
            Gw2Ui::RowLabel(dl, row, ic + 18.f, nameRightPad, nm.c_str(), Gw2Ui::HAlign::Left, showDisc ? Gw2Ui::VAlign::Top : Gw2Ui::VAlign::Middle,
                            out.name.empty() ? Gw2Ui::kTextDim : rc, false, nullptr, 20.f * scale);
            if (showDisc)
            {
                std::string disc = r.disc.empty() ? r.type : r.disc[0];
                for (size_t d = 1; d < r.disc.size(); ++d) { disc += ", "; disc += r.disc[d]; }
                Gw2Ui::RowLabel(dl, row, ic + 18.f, nameRightPad, disc.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Bottom,
                                Gw2Ui::kTextDim, false, nullptr, 16.f * scale);
            }
            if (row.hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); RecipeTooltip(r, out, learn); }
            RecipeMenu(r, out, row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
            ImGui::PopID();
        }
    ImGui::EndChild();
}
