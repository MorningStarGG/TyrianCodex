#include "ui/tabs/HomesteadSection.h"

#include "Shared.h"                // Texture_t (Width/Height/Resource) for tooltip images
#include "app/App.h"
#include "app/AccountData.h"
#include "app/DecorationCatalog.h"
#include "app/ItemCatalog.h"      // glyph item name + chat link
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiGallery.h"  // the shared GalleryBrowser shell + GalleryRail
#include "ui/GuideViewer.h"        // SetClipboard + ViewerAlert (copy) + TravelToLocation (cats click-to-travel)
#include "ui/wiki/WikiReader.h"    // Wiki::RequestOpen (open in the in-overlay reader)
#include "util/ChatLink.h"         // ChatLink::Recipe (decoration recipe chat link)
#include "util/ImageCache.h"       // GetDecorationImage / GetCatImage (bundled screenshots)
#include "util/Textures.h"         // Tex::GetTextureFromURL (decoration icon)

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    int  g_hview   = 0;            // 0 = Decorations, 1 = Glyphs, 2 = Cats, 3 = Nodes
    char g_hsearch[96] = "";
    int  g_hcat    = 0;            // selected category-dropdown index (0 = All categories)
    int  g_hlock   = 0;            // 0 = All, 1 = Owned, 2 = Missing

    const char* kLock[] = { "All", "Owned", "Missing" };

    // passes the All/Owned/Missing filter for an item with this owned-state.
    bool LockPass(bool owned) { return g_hlock == 0 || (g_hlock == 1) == owned; }

    std::string Lower(const char* s) { std::string o; if (s) for (const char* p = s; *p; ++p) o += (char)std::tolower((unsigned char)*p); return o; }

    // "alchemy_harvesting" -> "Alchemy Harvesting"; "harvesting" -> "Harvesting".
    std::string Title(const std::string& in)
    {
        std::string o; bool start = true;
        for (char c : in)
        {
            if (c == '_' || c == '-') { o += ' '; start = true; }
            else { o += start ? (char)std::toupper((unsigned char)c) : c; start = false; }
        }
        return o;
    }

    // ---- decoration display order (filtered + sorted), cached behind a change token (F5: no per-frame churn) ----
    std::vector<int> g_order;
    int    g_doCat = -2, g_doLock = -1; std::string g_doSearch = "\x01";
    double g_doAt = -1.0; size_t g_doOwnedN = (size_t)-1, g_doN = (size_t)-1;

    // ---- category LEFT RAIL: "All Decorations" + each category with owned/total, cached behind the owned-state
    // token (rebuilt only when the catalog / owned set changes -- F5 no-per-frame-churn rule). g_hcat = selected
    // category id (0 = All), set by clicking a rail row. ----
    struct CatRow { int id = 0; std::string name; int owned = 0, total = 0; };
    std::vector<CatRow> g_catRail;
    double g_crAt = -1.0; size_t g_crOwnedN = (size_t)-1, g_crN = (size_t)-1;

    void RebuildCatRail(App& app, const AccountData::Model& m)
    {
        DecorationCatalog& cat = app.decorations;
        const std::vector<DecorationCatalog::Decoration>& decos = cat.Decorations();
        if (!g_catRail.empty() && g_crAt == m.homesteadAt && g_crOwnedN == m.decorationsOwned.size() && g_crN == decos.size())
            return;
        auto ownedCount = [&](int id) { auto it = m.decorationsOwned.find(id); return it == m.decorationsOwned.end() ? 0 : it->second; };
        std::unordered_map<int, std::pair<int, int>> agg;   // category id -> (owned, total)
        int allOwned = 0, allTotal = 0;
        for (const DecorationCatalog::Decoration& d : decos)
        {
            const bool owned = ownedCount(d.id) > 0;
            ++allTotal; if (owned) ++allOwned;
            for (int c : d.categories) { std::pair<int, int>& a = agg[c]; ++a.second; if (owned) ++a.first; }
        }
        std::vector<CatRow> rows;
        for (const auto& kv : agg) { std::string n = cat.CategoryName(kv.first); if (!n.empty()) rows.push_back({ kv.first, n, kv.second.first, kv.second.second }); }
        std::sort(rows.begin(), rows.end(), [](const CatRow& a, const CatRow& b) { return a.name < b.name; });
        g_catRail.clear();
        g_catRail.push_back({ 0, "All Decorations", allOwned, allTotal });
        for (CatRow& r : rows) g_catRail.push_back(std::move(r));
        g_crAt = m.homesteadAt; g_crOwnedN = m.decorationsOwned.size(); g_crN = decos.size();
    }

    // Item / Fishing-tooltip parity flow: hero screenshot -> name + "Homestead Decoration" subtitle -> separator ->
    // clean "Label: value" DATA lines (cream TooltipText, like Fishing) -> bulleted ingredients -> separator ->
    // muted action footer. (Was a flat TooltipMuted dump.)
    void DecoTooltip(App& app, const DecorationCatalog::Decoration& d, int owned)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipHeroImage(ImageCache::GetDecorationImage(d.id), 332.f, 224.f);
        Gw2Ui::TooltipTitle(d.name.c_str());
        Gw2Ui::TooltipText("Homestead Decoration");

        // DATA lines (one fact per line, "Label: value", cream like the Fishing tooltip).
        Gw2Ui::TooltipSeparator();
        std::string cats;
        for (int c : d.categories) { std::string n = app.decorations.CategoryName(c); if (n.empty()) continue; if (!cats.empty()) cats += ", "; cats += n; }
        if (!cats.empty()) Gw2Ui::TooltipText(("Category: " + cats).c_str());
        std::string o = owned > 0 ? ("Owned: " + std::to_string(owned)) : std::string("Owned: 0");
        if (d.max > 0) o += "   (max " + std::to_string(d.max) + ")";
        Gw2Ui::TooltipText(o.c_str());
        std::string src = d.source.empty() ? d.desc : d.source;
        if (!src.empty()) Gw2Ui::TooltipText(("Source: " + src).c_str());
        if (d.handiwork > 0) Gw2Ui::TooltipText(("Handiwork: " + std::to_string(d.handiwork)).c_str());
        if (!d.sheet.empty()) Gw2Ui::TooltipText(("Recipe: " + d.sheet).c_str());

        if (!d.recipe.empty())
        {
            Gw2Ui::TooltipText("Ingredients:");
            for (const auto& ri : d.recipe)
                Gw2Ui::TooltipBullet((std::to_string(ri.count) + " x " + ri.name).c_str());
        }

        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipColored(d.premium ? "Premium  -  Black Lion / gem store" : "Free  -  crafted / earned",
                              d.premium ? IM_COL32(214, 150, 224, 255) : IM_COL32(126, 196, 140, 255));
        Gw2Ui::TooltipMuted(owned > 0 ? "Unlocked" : "Not yet unlocked");
        Gw2Ui::TooltipMuted("Left-click: open wiki   -   Right-click: copy");
        Gw2Ui::TooltipEnd();
    }

    void DecoCopyMenu(const DecorationCatalog::Decoration& d, bool openNow)
    {
        const std::string recipeLink = d.recipeId > 0 ? ChatLink::Recipe(d.recipeId) : std::string();
        std::vector<Gw2Ui::MenuNode> nodes;
        nodes.push_back({ "Open in Wiki", 1 });
        if (!d.name.empty())        nodes.push_back({ "Copy name", 2 });
        if (!recipeLink.empty())    nodes.push_back({ "Copy recipe chat link", 3 });
        switch (Gw2Ui::ContextMenuTree("##hdcopy", nodes, openNow))
        {
            case 1: Wiki::RequestOpen(DecorationCatalog::WikiTitle(d)); break;
            case 2: SetClipboard(d.name); ViewerAlert("Copied decoration name."); break;
            case 3: SetClipboard(recipeLink); ViewerAlert("Copied recipe chat link - paste in game."); break;
            default: break;
        }
    }

    // ---- Decorations gallery (the rich primary view) -- on the shared Gw2Ui::GalleryBrowser shell ----
    void DrawDecorations(App& app, const AccountData::Model& m)
    {
        DecorationCatalog& cat = app.decorations;
        const std::vector<DecorationCatalog::Decoration>& decos = cat.Decorations();
        RebuildCatRail(app, m);
        auto ownedCount = [&](int id) { auto it = m.decorationsOwned.find(id); return it == m.decorationsOwned.end() ? 0 : it->second; };
        const int catId = g_hcat;   // selected category id (0 = All), driven by the rail
        auto inCat = [&](const DecorationCatalog::Decoration& d) { return catId == 0 || std::find(d.categories.begin(), d.categories.end(), catId) != d.categories.end(); };
        const std::string q = Lower(g_hsearch);
        const float scale = Gw2Ui::TextScale();

        // ONE change token (category + lock + search + owned-state + catalog size): gates the cached header tally
        // AND the shell's cached display order (no per-frame rebuild).
        uint64_t tok = 1469598103934665603ull;
        auto mix = [&tok](uint64_t v) { tok ^= v; tok *= 1099511628211ull; };
        mix((uint64_t)(unsigned)catId); mix((uint64_t)(unsigned)g_hlock);
        for (const char* p = g_hsearch; *p; ++p) mix((unsigned char)*p);
        mix((uint64_t)(m.homesteadAt * 1000.0)); mix((uint64_t)m.decorationsOwned.size()); mix((uint64_t)decos.size());

        // header tally (whole category, owned-aware) -- cached behind the token (no per-frame 1k+ decos scan).
        static uint64_t s_tallyTok = ~0ull; static int s_unl = 0, s_tot = 0, s_fU = 0, s_fT = 0, s_pU = 0, s_pT = 0;
        if (s_tallyTok != tok)
        {
            s_unl = 0; s_tot = 0; s_fU = 0; s_fT = 0; s_pU = 0; s_pT = 0;
            for (const auto& d : decos)
            {
                if (!inCat(d)) continue;
                ++s_tot; const bool ow = ownedCount(d.id) > 0; if (ow) ++s_unl;
                if (d.premium) { ++s_pT; if (ow) ++s_pU; } else { ++s_fT; if (ow) ++s_fU; }
            }
            s_tallyTok = tok;
        }

        // rail model: a flat mirror of g_catRail (rebuilt only when the cat rail's owned-state token moves).
        static Gw2Ui::GalleryRailNode s_rail; static uint64_t s_railTok = ~0ull;
        const uint64_t railTok = (uint64_t)(m.homesteadAt * 1000.0) ^ ((uint64_t)m.decorationsOwned.size() << 21) ^ ((uint64_t)decos.size() << 42);
        if (s_railTok != railTok)
        {
            s_rail.children.clear();
            for (const CatRow& r : g_catRail) s_rail.children.push_back({ std::to_string(r.id), r.name, r.owned, r.total, {} });
            s_railTok = railTok;
        }
        std::string railKey = std::to_string(g_hcat);

        char titleBuf[80];
        { const std::string cn = catId == 0 ? std::string("Decorations") : cat.CategoryName(catId);
          std::snprintf(titleBuf, sizeof(titleBuf), "%s", cn.c_str()); }
        Gw2Ui::GalleryBrowser::FilterDropdown lockF{ "Lock", kLock, 3, &g_hlock, 116.f };

        Gw2Ui::GalleryBrowser gb;
        gb.id = "home.decorations";
        gb.title = titleBuf;
        gb.have = s_unl; gb.total = s_tot; gb.tallyVerb = "unlocked";
        gb.rightStatus = AccountData::Refreshing() ? "updating..." : nullptr;
        gb.showProgress = (s_pT == 0);   // single bar if this category has no premium; else the Free/Premium 3-bar
        if (s_pT > 0)
            gb.drawProgress = [u = s_unl, fU = s_fU, fT = s_fT, pU = s_pU, pT = s_pT, t = s_tot](float w) {
                Gw2Ui::ProgressSeg segs[2] = {
                    { "Free",    fU, fT, IM_COL32(126, 196, 140, 255) },   // crafted / earned in-game
                    { "Premium", pU, pT, IM_COL32(214, 150, 224, 255) },   // Black Lion / gem store
                };
                Gw2Ui::ProgressBreakdown(segs, 2, "All", u, t, w);
            };
        gb.searchBuf = g_hsearch; gb.searchBufSize = (int)sizeof(g_hsearch); gb.searchHint = "Search decorations...";
        gb.filters = &lockF; gb.filterCount = 1;
        gb.viewMode = &app.config.itemsView; gb.settingsDirty = &app.settingsDirty;
        gb.railRoot = &s_rail; gb.railSelectedKey = &railKey;
        gb.railWidthPx = &app.config.PaneW("homestead.catrail", 210.f); gb.railDefaultW = 210.f;
        gb.orderToken = tok;
        gb.gridCell = 64.f; gb.gridGap = 6.f; gb.listRowH = 40.f;

        gb.rebuildOrder = [&](std::vector<int>& order) {
            for (int i = 0; i < (int)decos.size(); ++i)
            {
                const DecorationCatalog::Decoration& d = decos[i];
                if (!inCat(d)) continue;
                if (!LockPass(ownedCount(d.id) > 0)) continue;
                if (!q.empty() && Lower(d.name.c_str()).find(q) == std::string::npos) continue;
                order.push_back(i);
            }
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                const bool oa = ownedCount(decos[a].id) > 0, ob = ownedCount(decos[b].id) > 0;
                if (oa != ob) return oa;                 // owned first
                return decos[a].name < decos[b].name;
            });
        };
        gb.drawGridCell = [&](int idx, ImVec2 cmin, float cs) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const DecorationCatalog::Decoration& d = decos[idx];
            const int oc = ownedCount(d.id); const bool owned = oc > 0;
            ImGui::InvisibleButton("##dc", ImVec2(cs, cs));
            const bool hov = ImGui::IsItemHovered();
            if (!d.icon.empty())
            {
                char t[40]; std::snprintf(t, sizeof(t), "TC_DECO_%d", d.id);
                if (void* tex = Tex::GetTextureFromURL(t, d.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cs - 1.f, cmin.y + cs - 1.f));
            }
            dl->AddRect(cmin, ImVec2(cmin.x + cs, cmin.y + cs), owned ? Gw2Ui::kGold : IM_COL32(96, 96, 96, 170), 2.f, 0, owned ? 1.5f : 1.f);
            if (!owned) dl->AddRectFilled(ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cs - 1.f, cmin.y + cs - 1.f), IM_COL32(0, 0, 0, 150), 2.f);
            else if (oc > 1)
            {
                char b[12]; std::snprintf(b, sizeof(b), "%d", oc);
                Gw2Ui::LabelDL(dl, ImVec2(cmin.x + 2.f, cmin.y + cs - 17.f), ImVec2(cmin.x + cs - 3.f, cmin.y + cs - 2.f), b,
                               Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(255, 255, 255, 255), true, nullptr, 14.f);
            }
            if (hov)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                DecoTooltip(app, d, oc);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Wiki::RequestOpen(DecorationCatalog::WikiTitle(d));
            }
            DecoCopyMenu(d, hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        };
        gb.drawListRow = [&](int idx, const Gw2Ui::RowHotspot& row) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const DecorationCatalog::Decoration& d = decos[idx];
            const int oc = ownedCount(d.id); const bool owned = oc > 0;
            const float ic = row.height - 8.f, statusW = 130.f * scale;
            const ImVec2 p = row.min;
            if (!d.icon.empty())
            {
                char t[40]; std::snprintf(t, sizeof(t), "TC_DECO_%d", d.id);
                if (void* tex = Tex::GetTextureFromURL(t, d.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic));
            }
            dl->AddRect(ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic), owned ? Gw2Ui::kGold : IM_COL32(96, 96, 96, 170), 0.f, 0, 1.2f);
            char st[40];
            if (owned && oc > 1) std::snprintf(st, sizeof(st), "Owned  x%d", oc);
            else                 std::snprintf(st, sizeof(st), owned ? "Owned" : "Locked");
            Gw2Ui::RowLabel(dl, row, row.width - statusW, 8.f, st, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                            owned ? Gw2Ui::kGold : Gw2Ui::kTextDim, false, nullptr, 16.f);
            Gw2Ui::RowLabel(dl, row, ic + 12.f, statusW + 12.f, d.name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                            owned ? IM_COL32(228, 222, 204, 255) : Gw2Ui::kTextDim, false, nullptr, 18.f);
            if (row.hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); DecoTooltip(app, d, oc); }
            if (row.clicked) Wiki::RequestOpen(DecorationCatalog::WikiTitle(d));
            DecoCopyMenu(d, row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        };

        Gw2Ui::DrawGalleryBrowser(gb);
        g_hcat = std::atoi(railKey.c_str());   // sync the rail's selection back into g_hcat
    }

    // ---- owned/total collection list (glyphs / cats / nodes) ----
    // Static fields are built ONCE per collection (the catalog is immutable after AddonLoad); owned status is a live
    // lambda; the filtered+sorted order is cached behind a change token (the no-per-frame-churn rule). Cats carry the
    // rich extras: left-click TRAVELS to the nearest waypoint, the tooltip shows the location image + details.
    struct CollItem
    {
        std::string name, sub, key, wiki;
        bool        leftClickWiki = false;   // nodes: left-click opens `wiki`. glyphs/cats: false
        std::string travelChat; float cx = 0.f, cy = 0.f; uint32_t mapId = 0;   // cats: left-click travels here
        int         catImageId = 0;          // ImageCache::GetCatImage(catImageId) in the tooltip (0 = none)
        std::vector<std::pair<std::string, std::string>> tip;                    // tooltip label->value rows (cats)
        std::string copyItem;                // right-click "Copy needed item" (cat lure)
        std::string copyChat, copyChatLabel; // right-click "Copy <label>" (glyph item link / cat waypoint link)
    };
    struct CollView { std::vector<CollItem> items; bool built = false; };
    CollView g_glyphView, g_catView, g_nodeView;

    void CollCopyMenu(const CollItem& it, bool openNow)
    {
        std::vector<Gw2Ui::MenuNode> nodes;
        if (!it.wiki.empty())     nodes.push_back({ "Open in Wiki", 1 });
        if (!it.name.empty())     nodes.push_back({ "Copy name", 2 });
        if (!it.copyItem.empty()) nodes.push_back({ "Copy needed item", 3 });
        if (!it.copyChat.empty()) nodes.push_back({ it.copyChatLabel.c_str(), 4 });
        switch (Gw2Ui::ContextMenuTree("##hccopy", nodes, openNow))
        {
            case 1: Wiki::RequestOpen(it.wiki); break;
            case 2: SetClipboard(it.name); ViewerAlert("Copied name."); break;
            case 3: SetClipboard(it.copyItem); ViewerAlert(("Copied: " + it.copyItem).c_str()); break;
            case 4: SetClipboard(it.copyChat); ViewerAlert("Copied chat link - paste in game."); break;
            default: break;
        }
    }

    CollItem MakeColl(std::string name, std::string sub, std::string wiki)
    {
        CollItem it;
        it.key = Lower(name.c_str()); if (!sub.empty()) { it.key += ' '; it.key += Lower(sub.c_str()); }
        it.name = std::move(name); it.sub = std::move(sub); it.wiki = std::move(wiki);
        return it;
    }

    // A homestead completion list (glyphs / cats / nodes) on the shared Gw2Ui::ChecklistBrowser. `id` keys the
    // shell's order cache; `hint` is the search placeholder; viewKey distinguishes the order token. twoLine = name
    // over a dim sub (cats); else "name - sub" on one row. The shell draws the owned dot + name/sub + status; the
    // cat rich tooltip / travel-on-click / copy ride in `onRow`.
    void DrawCollectionList(App& app, const char* id, int viewKey, const char* label, const char* hint,
                            const std::vector<CollItem>& items,
                            const std::function<bool(int)>& owned, double ownedAt, size_t ownedN, bool twoLine = false)
    {
        int unl = 0; for (int i = 0; i < (int)items.size(); ++i) if (owned(i)) ++unl;   // owned across all (cheap)

        uint64_t tok = 1469598103934665603ull;
        auto mix = [&tok](uint64_t v) { tok ^= v; tok *= 1099511628211ull; };
        mix((uint64_t)(unsigned)viewKey); mix((uint64_t)(unsigned)g_hlock);
        for (const char* p = g_hsearch; *p; ++p) mix((unsigned char)*p);
        mix((uint64_t)(ownedAt * 1000.0)); mix((uint64_t)ownedN);
        const std::string q = Lower(g_hsearch);

        Gw2Ui::GalleryBrowser::FilterDropdown lockF{ "Lock", kLock, 3, &g_hlock, 116.f };

        Gw2Ui::ChecklistBrowser cb;
        cb.id = id;
        cb.title = label;
        cb.have = unl; cb.total = (int)items.size(); cb.tallyVerb = "unlocked";
        cb.rightStatus = AccountData::Refreshing() ? "updating..." : nullptr;
        cb.showProgress = true;
        cb.searchBuf = g_hsearch; cb.searchBufSize = (int)sizeof(g_hsearch); cb.searchHint = hint;
        cb.filters = &lockF; cb.filterCount = 1;
        cb.settingsDirty = &app.settingsDirty;
        cb.twoLine = twoLine;
        cb.orderToken = tok;

        cb.rebuildOrder = [&](std::vector<int>& order) {
            for (int i = 0; i < (int)items.size(); ++i)
            {
                const bool ow = owned(i);
                if (!LockPass(ow)) continue;
                if (!q.empty() && items[i].key.find(q) == std::string::npos) continue;
                order.push_back(i);
            }
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                const bool oa = owned(a), ob = owned(b);
                if (oa != ob) return oa;                 // owned first
                return items[a].name < items[b].name;
            });
        };
        cb.rowData = [&](int idx) -> Gw2Ui::ChecklistBrowser::RowData {
            const CollItem& it = items[idx];
            return { owned(idx), it.name.c_str(), it.sub.empty() ? nullptr : it.sub.c_str() };
        };
        cb.onRow = [&](int idx, const Gw2Ui::RowHotspot& row) {
            const CollItem& it = items[idx];
            const bool ow = owned(idx);
            if (row.hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if ((it.catImageId > 0 || !it.tip.empty()) && Gw2Ui::TooltipBegin())   // rich tooltip (cats)
                {
                    // Item / Fishing-tooltip parity: hero image -> title -> subtitle -> separator -> "Label: value"
                    // DATA lines (cream TooltipText) -> separator -> muted status + footer.
                    Gw2Ui::TooltipHeroImage(ImageCache::GetCatImage(it.catImageId), 312.f, 200.f);
                    Gw2Ui::TooltipTitle(it.name.c_str());
                    if (!it.sub.empty()) Gw2Ui::TooltipText(it.sub.c_str());
                    if (!it.tip.empty())
                    {
                        Gw2Ui::TooltipSeparator();
                        for (const auto& kv : it.tip)
                            if (!kv.second.empty()) Gw2Ui::TooltipText((kv.first + ": " + kv.second).c_str());
                    }
                    Gw2Ui::TooltipSeparator();
                    Gw2Ui::TooltipMuted(ow ? "Adopted" : "Not yet adopted");
                    Gw2Ui::TooltipMuted(it.travelChat.empty() ? "Right-click: copy" : "Left-click: travel   -   Right-click: copy");
                    Gw2Ui::TooltipEnd();
                }
            }
            if (row.clicked)
            {
                if (!it.travelChat.empty()) TravelToLocation(app, "waypoint", it.mapId, it.cx, it.cy, it.name, it.travelChat);
                else if (it.leftClickWiki && !it.wiki.empty()) Wiki::RequestOpen(it.wiki);
            }
            CollCopyMenu(it, row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        };

        Gw2Ui::DrawChecklistBrowser(cb);
    }

    void DrawGlyphs(App& app, const AccountData::Model& m)
    {
        if (!g_glyphView.built)
        {
            g_glyphView.built = true;
            for (const auto& g : app.decorations.Glyphs())
            {
                const ItemCatalog::Item& item = g.itemId > 0 ? ItemCatalog::ById(g.itemId) : ItemCatalog::ById(0);
                const bool haveItem = item.id > 0 && !item.name.empty();
                CollItem it = MakeColl(haveItem ? item.name : Title(g.id),
                                       g.slot.empty() ? std::string() : ("Slot: " + Title(g.slot)),
                                       haveItem ? item.name : std::string());   // wiki = the glyph item page (right-click only; no left-click)
                if (haveItem && !item.chatLink.empty()) { it.copyChat = item.chatLink; it.copyChatLabel = "Copy chat link"; }
                g_glyphView.items.push_back(std::move(it));
            }
        }
        const std::vector<DecorationCatalog::Glyph>& gl = app.decorations.Glyphs();
        DrawCollectionList(app, "home.glyphs", 1, "Glyphs", "Search glyphs...", g_glyphView.items,
                           [&](int i) { return m.glyphsOwned.count(gl[i].id) > 0; }, m.homesteadAt, m.glyphsOwned.size());
    }

    void DrawCats(App& app, const AccountData::Model& m)
    {
        if (!g_catView.built)
        {
            g_catView.built = true;
            for (const auto& c : app.decorations.Cats())
            {
                // Row 2: "Feed <lure> in <Zone> (<Region>)" -- a clean reading sentence (region omitted where there
                // isn't one; "Anywhere" cats read "... anywhere"; no-lure cats read "In <Zone> (<Region>)").
                const std::string zone = c.location.substr(0, c.location.find(" - "));
                const bool hasLure = !c.lure.empty() && c.lure != "No item required.";
                const bool anywhere = zone.empty() || zone == "Anywhere";
                std::string place = zone;
                if (!anywhere && !c.region.empty()) place += " (" + c.region + ")";
                std::string sub;
                if (hasLure)        sub = "Feed " + c.lure + (anywhere ? " anywhere" : " in " + place);
                else if (!anywhere) sub = "In " + place;
                else                sub = "Anywhere";
                CollItem it = MakeColl(!c.identity.empty() ? c.identity : (c.name.empty() ? Title(c.hint) : c.name),
                                       sub, "Hungry cat scavenger hunt");   // right-click "Open in Wiki" -> the guide
                it.catImageId = c.id;
                if (!c.lure.empty())     it.tip.push_back({ "Feed", c.lure });
                if (!c.location.empty()) it.tip.push_back({ "Location", c.location });
                if (!c.region.empty())   it.tip.push_back({ "Region", c.region });
                if (!c.waypoint.empty()) it.tip.push_back({ "Waypoint", c.waypoint });
                if (!c.notes.empty())    it.tip.push_back({ "Notes", c.notes });
                if (!c.wpChatLink.empty())   // left-click travels there + a "Copy waypoint chat link" menu entry
                {
                    it.travelChat = c.wpChatLink; it.cx = c.cx; it.cy = c.cy; it.mapId = c.mapId;
                    it.copyChat = c.wpChatLink; it.copyChatLabel = "Copy waypoint chat link";
                }
                if (!c.lure.empty() && c.lure != "No item required.") it.copyItem = c.lure;
                g_catView.items.push_back(std::move(it));
            }
        }
        const std::vector<DecorationCatalog::Cat>& cs = app.decorations.Cats();
        DrawCollectionList(app, "home.cats", 2, "Cats", "Search cats...", g_catView.items,
                           [&](int i) { return m.catsOwned.count(cs[i].id) > 0; }, m.homesteadAt, m.catsOwned.size(), true);
    }

    void DrawNodes(App& app, const AccountData::Model& m)
    {
        if (!g_nodeView.built)
        {
            g_nodeView.built = true;
            for (const auto& n : app.decorations.Nodes())
            {
                CollItem it = MakeColl(n.name, std::string(), n.name);
                it.leftClickWiki = true;   // nodes keep left-click -> the wiki page
                g_nodeView.items.push_back(std::move(it));
            }
        }
        const std::vector<DecorationCatalog::Node>& ns = app.decorations.Nodes();
        DrawCollectionList(app, "home.nodes", 3, "Gathering nodes", "Search nodes...", g_nodeView.items,
                           [&](int i) { return m.nodesOwned.count(ns[i].id) > 0; }, m.homesteadAt, m.nodesOwned.size());
    }
}

void DrawHomesteadContent(App& app)
{
    const AccountData::Model& m = AccountData::Get();
    DecorationCatalog& cat = app.decorations;
    if (cat.Empty()) { Gw2Ui::EmptyState("Homestead data not loaded", "data/decorations.json is missing or empty."); return; }

    // The catalog browses without a key; only the owned status needs the unlocks scope -> a soft note, not a gate.
    if (!AccountData::HasKey() || !AccountData::HasScope(Api::TokenPermission::Unlocks))
        Gw2Ui::Label("Add an API key with the 'unlocks' scope (Options tab) to track which homestead items you own.",
                     Gw2Ui::kTextDim, false, nullptr, 14.f);

    // sub-view chips (Decorations / Glyphs / Cats / Nodes). Each scope draws its OWN toolbar (search + filters):
    // Decorations on the GalleryBrowser shell, Glyphs/Cats/Nodes on the ChecklistBrowser.
    static const Gw2Ui::ChipItem kView[] = { {"Decorations"}, {"Glyphs"}, {"Cats"}, {"Nodes"} };
    Gw2Ui::ChipRow("##hv", kView, 4, &g_hview, 30.f, 16.f);

    switch (g_hview)
    {
        case 1:  DrawGlyphs(app, m); break;
        case 2:  DrawCats(app, m);   break;
        case 3:  DrawNodes(app, m);  break;
        default: DrawDecorations(app, m); break;
    }
}
