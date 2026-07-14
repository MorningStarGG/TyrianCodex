#include "ui/tabs/TradingPostTab.h"
#include "ui/SettingsWindow.h"        // OpenSettingsTab + SettingsTabId
#include "ui/ApiReminder.h"           // no-key / missing-scope nudge
#include "ui/Gw2Ui.h"
#include "ui/items/ItemRender.h"      // ItemUI::DrawResultRow (order / delivery rows)
#include "ui/items/ItemScore.h"       // ItemUI::Lower (Flip Finder search)
#include "ui/items/ItemTpDetail.h"    // shared slide-out TP detail card
#include "app/App.h"
#include "app/AccountData.h"          // item metadata for order/delivery rows
#include "app/ItemCatalog.h"          // item names/icons/rarity for Flip Finder rows
#include "app/CraftingEngine.h"       // recipe catalog + buy-vs-craft engine (Crafting Calculator view)
#include "app/CraftKnown.h"           // learned-recipe sets (account + per character) for the rail / badges
#include "app/CraftCart.h"            // persistent crafting cart (named projects + materials check-off)
#include "app/MarketData.h"           // datawars2 whole-market snapshot cache
#include "app/PriceHistory.h"         // datawars2 per-item + market-wide price history (Charts view)
#include "app/TpEligibility.h"        // official TP id gate
#include "app/TpPrices.h"             // live buy/sell for the undercut/outbid comparison
#include "ui/dashboard/widgets/ApiWidgetUtil.h"  // DashApi::CharacterPicker (known-recipes rail)
#include "render/glyphs/Glyphs.h"     // Render::DrawGlyph (known badge + rail caret)
#include "util/Trading.h"             // TP fee math + shared coin label
#include "util/Textures.h"            // row item icons
#include "api/Client.h"
#include "api/v2/Commerce.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------------------------------------
// The Trading Post tab: a scope-chip shell over four views (g_tpView) -- My TP (the account-side surface:
// delivery box, current orders with undercut/outbid flags, recent-history P&L), Flip Finder, Crafting (the
// buy-vs-craft cart), and Charts. The per-item price lookup lives in the Items tab (its slide-out price card).
// Detail is fetched via the typed Commerce client into tab-local state (cache-on-open + manual Refresh);
// AccountData keeps only the lightweight counts.
// ---------------------------------------------------------------------------------------------------------
namespace
{
    bool g_myInflight = false;
    int  g_myLoaded = 0;              // sub-fetches returned for the current load (of 5)
    bool g_myStarted = false;
    long long g_delCoins = 0;
    std::vector<std::pair<int, int>> g_delItems;        // (id, count)
    struct Order { int itemId = 0, price = 0, qty = 0; };
    std::vector<Order> g_curBuys, g_curSells;
    long long g_histSpent = 0, g_histRecvNet = 0;       // history buys total / history sells net (after fee)
    int g_tpView = 0;                                   // 0 = My TP, 1 = Flip Finder, 2 = Crafting, 3 = Charts

    // Charts view (g_tpView == 3): pick a tradeable item -> big datawars2 price-history chart, or the market index.
    char        g_chartSearch[96] = "";
    int         g_chartItemId = 0;                      // selected item to chart (0 = none)
    std::string g_chartItemName;
    int         g_chartRangeDays = 180;                 // 30 / 90 / 180 / 365
    bool        g_chartMarket = true;                   // market-wide index mode (else the per-item price chart); default true so Charts opens on the index instead of blank
    std::vector<int> g_chartResults;                    // tradeable catalog indices for the picker (change-tokened)
    std::string g_chartSearchBuilt;
    uint64_t    g_chartCatalogVer = ~0ull;
    bool        g_chartTpReadyBuilt = false;

    // ---- Crafting Cart state (the cart itself lives in CraftCart; the compute/display opts live in Config) ----
    bool  g_craftKnownOnly = false;                        // search-filter: only recipes the selected character knows
    char  g_craftSearch[96] = "";
    std::vector<CraftingEngine::Plan> g_craftPlans;        // cached plan per cart entry (change-token gated)
    std::vector<std::pair<int, long long>> g_craftItemsBuilt;  // the cart items the cached plans were built from
    bool      g_craftDirty = true;
    uint64_t  g_craftRecipeVer = ~0ull;
    double    g_craftNextRecalc = 0.0;
    bool      g_craftWantMats = false;                     // published to entry.cpp's AccountData::Tick mask
    char  g_craftProjEdit[64] = "";                        // inline project new/rename buffer
    int   g_craftProjMode = 0;                             // 0 = none, 1 = new, 2 = rename
    std::unordered_map<int, bool> g_craftShopCollapsed;    // by-item shopping list: output id -> collapsed

    // ---- recipe preview: clicking a search/rail row opens that recipe's breakdown on the RIGHT (the inline
    // Add / + buttons still quick-add to the cart). 0 = show the cart instead. ----
    int       g_craftPreview = 0;
    long long g_craftPreviewQty = 1;
    CraftingEngine::Plan g_craftPreviewPlan;               // cached preview plan
    int       g_craftPreviewBuiltId = 0; long long g_craftPreviewBuiltQty = -1;
    uint64_t  g_craftPreviewVer = ~0ull; unsigned g_craftPreviewOpts = ~0u; double g_craftPreviewNext = 0.0;

    // ---- known-recipes rail (per character, grouped by discipline) ----
    std::string g_craftChar;                               // selected character for the rail + "known" badges
    struct RailEntry { bool header = false; std::string disc; int id = 0; int count = 0; };  // header: disc+count; row: out id
    std::vector<RailEntry> g_railFlat;                     // flat header+row list (collapse-aware) for one clipper
    std::string g_railCharBuilt = "\x01";                  // sentinel != any real character name
    uint64_t    g_railKnownVer = ~0ull, g_railRecipeVer = ~0ull, g_railCatVer = ~0ull;
    std::unordered_map<std::string, bool> g_railCollapsed; // discipline -> collapsed
    uint32_t    g_railCollapseVer = 0, g_railCollapseBuilt = ~0u;

    // ---- slide-in Trading Post price card (shared with Flip Finder / Inventory), for clicking a material ----
    int         g_craftTpSel = 0;        // open card item id (0 = none / closing)
    int         g_craftTpShown = 0;      // retained while the close animation plays
    std::string g_craftTpName;
    float       g_craftTpAnim = 0.f;
    int         g_craftTpScrollItem = 0;

    // Build an ItemMeta from the bundled catalog for crafting-row tooltips (small cache, like ItemsTab::MetaFor).
    AccountData::ItemMeta& CraftMeta(const ItemCatalog::Item& it)
    {
        static std::unordered_map<int, AccountData::ItemMeta> s_meta;
        static uint64_t s_ver = ~0ull;
        if (s_ver != ItemCatalog::Version()) { s_meta.clear(); s_ver = ItemCatalog::Version(); }
        auto f = s_meta.find(it.id);
        if (f != s_meta.end()) return f->second;
        if (s_meta.size() >= 2000) s_meta.clear();
        AccountData::ItemMeta m;
        m.have = true; m.id = it.id; m.name = it.name; m.icon = it.icon; m.rarity = it.rarity; m.type = it.type;
        m.level = it.level; m.vendorValue = it.vendorValue; m.chatLink = it.chatLink; m.description = it.description;
        m.flags = it.flags; m.restrictions = it.restrictions;
        if (!it.detailsRaw.empty()) { try { m.details = nlohmann::json::parse(it.detailsRaw); } catch (...) {} }
        return s_meta.emplace(it.id, std::move(m)).first->second;
    }
    const ItemUI::NameFn g_craftNameFn = [](int id) { return ItemCatalog::ById(id).name; };

    // ---- crafting recipe knowledge (which characters can craft an item) ----
    // selKnows = the rail-selected character can craft it; accountUnlock = it's an account-wide recipe sheet;
    // chars = every character that can currently craft it. Derived over ALL official recipes producing `itemId`.
    struct KnownInfo { bool selKnows = false; bool accountUnlock = false; std::vector<std::string> chars; };
    KnownInfo CraftKnownFor(int itemId, const std::string& selChar)
    {
        KnownInfo k;
        const std::vector<CraftingEngine::Recipe>* recs = CraftingEngine::RecipesFor(itemId);
        if (!recs) return k;
        std::set<std::string> who;
        for (const CraftingEngine::Recipe& r : *recs)
        {
            if (r.id <= 0) continue;   // Mystic Forge / no official id -> not a learnable recipe
            if (CraftKnown::KnownByAccount(r.id)) k.accountUnlock = true;
            if (!selChar.empty() && CraftKnown::CharKnows(selChar, r.id)) k.selKnows = true;
            for (const std::string& c : CraftKnown::CharsKnowing(r.id)) who.insert(c);
        }
        k.chars.assign(who.begin(), who.end());
        return k;
    }
    // Cheap per-frame check (set lookups only) for the row "known" badge.
    bool SelKnowsCraft(int itemId, const std::string& selChar)
    {
        if (selChar.empty() || !CraftKnown::IsReady()) return false;
        const std::vector<CraftingEngine::Recipe>* recs = CraftingEngine::RecipesFor(itemId);
        if (!recs) return false;
        for (const CraftingEngine::Recipe& r : *recs) if (r.id > 0 && CraftKnown::CharKnows(selChar, r.id)) return true;
        return false;
    }

    // Item tooltip for a crafting row -- the rich catalog tooltip plus a "Known by:" line. The known-by line is
    // added ONLY for items with a learnable (official-id) recipe, so raw shopping materials don't show it.
    void CraftRowTooltip(const ItemCatalog::Item& it)
    {
        const std::vector<CraftingEngine::Recipe>* recs = CraftingEngine::RecipesFor(it.id);
        bool learnable = false; if (recs) for (const CraftingEngine::Recipe& r : *recs) if (r.id > 0) { learnable = true; break; }
        const KnownInfo info = learnable ? CraftKnownFor(it.id, g_craftChar) : KnownInfo{};
        const size_t allN = CraftKnown::CharacterNames().size();
        std::function<void()> extra;
        if (learnable)
            extra = [info, allN]() {
                Gw2Ui::TooltipSeparator();
                if (!CraftKnown::HasFetched()) { Gw2Ui::TooltipMuted("Known recipes still loading..."); return; }
                if (info.chars.empty())
                {
                    Gw2Ui::TooltipMuted(info.accountUnlock ? "Account unlock - no character can craft it yet"
                                                           : "Not yet learned by any character");
                    return;
                }
                const std::string who = (allN > 0 && info.chars.size() == allN)
                    ? std::string("All characters") : ItemUI::JoinStrings(info.chars, ", ");
                Gw2Ui::TooltipText(("Known by: " + who).c_str());
            };
        ItemUI::DrawItemTooltip(it.id, &CraftMeta(it), ItemUI::Instance{}, g_craftNameFn, it.statSet, extra);
    }

    void CraftQueueAdd(int id)
    {
        CraftCart::Add(CraftCart::Active(), id, 1);   // +1 to the active project (merges duplicates)
        g_craftDirty = true;
    }

    // Open / close the slide-in TP price card for a clicked material (no-op for non-tradeable items).
    void ToggleCraftTpCard(int id, const std::string& name)
    {
        if (!ItemTp::Tradeable(id)) { g_craftTpSel = 0; return; }
        const bool closing = (g_craftTpSel == id);
        g_craftTpSel = closing ? 0 : id;
        g_craftTpShown = id;
        g_craftTpName = name;
        if (!closing) g_craftTpScrollItem = 0;
    }

    // A light-up toggle BUTTON (the Items-tab "Tradeable" style); flips *v on click and returns true then.
    bool CraftToggle(const char* id, const char* label, bool* v, const char* tip, float w)
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
        Gw2Ui::PushTextScale(1.f);
        Gw2Ui::LabelIn(ImVec2(r.min.x + 8.f, r.min.y), ImVec2(r.max.x - 8.f, r.max.y), label,
                       Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, textCol, true, nullptr, 16.f);
        Gw2Ui::PopTextScale();
        if (r.clicked) { *v = !*v; return true; }
        return false;
    }

    // A craftable result row (search / queue): result row + a gold "known" check when the selected character can
    // craft it + the known-by tooltip. Returns the row hit (caller decides the click action).
    ItemUI::ResultRow CraftItemRow(int rowIdx, float w, const ItemCatalog::Item& it)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rowW = Gw2Ui::ContentWidth(w);
        const float rowH = 24.f * Gw2Ui::TextScale();
        const ItemUI::ResultRow rr = ItemUI::DrawResultRow(rowIdx, rowW, it.id, it.icon, it.name, it.rarity, 0, it.chatLink);
        if (SelKnowsCraft(it.id, g_craftChar))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            Render::DrawGlyph(dl, ImVec2(p.x + rowW - rowH * 0.55f, p.y + rowH * 0.5f), rowH * 0.46f,
                              Render::Glyph::Check, Gw2Ui::kGold, Render::GlyphStyle{});
        }
        if (rr.hovered) CraftRowTooltip(it);
        return rr;
    }

    // Rebuild the rail's flat header+row list for `charName`: that character's known recipes -> output items,
    // grouped by discipline (collapse-aware). Cheap-to-render afterwards via one clipper.
    void RebuildRail(const std::string& charName)
    {
        g_railFlat.clear();
        std::map<std::string, std::set<int>> groups;   // discipline -> unique output item ids
        for (int rid : CraftKnown::KnownRecipeIds(charName))
        {
            const CraftingEngine::Recipe* r = CraftingEngine::RecipeById(rid);
            if (!r || r->out <= 0) continue;
            const std::string disc = !r->disc.empty() ? ItemUI::Humanize(r->disc.front())
                                   : (!r->type.empty() ? r->type : std::string("Other"));
            groups[disc].insert(r->out);
        }
        for (auto& kv : groups)
        {
            std::vector<int> ids(kv.second.begin(), kv.second.end());
            std::sort(ids.begin(), ids.end(), [](int a, int b) {
                return ItemUI::Lower(ItemCatalog::ById(a).name) < ItemUI::Lower(ItemCatalog::ById(b).name);
            });
            g_railFlat.push_back(RailEntry{ true, kv.first, 0, (int)ids.size() });
            if (!g_railCollapsed[kv.first])
                for (int id : ids) g_railFlat.push_back(RailEntry{ false, kv.first, id, 0 });
        }
    }

    void DrawCraftRail(App& app, float width, float height)
    {
        (void)app;
        ImGui::BeginChild("##craftrail", ImVec2(width, height), false);
        Gw2Ui::SectionHeader("Known recipes");
        const std::string sel = DashApi::CharacterPicker("##craftchar", g_craftChar, std::max(120.f, width - 10.f));
        if (!sel.empty()) g_craftChar = sel;

        if (!AccountData::HasKey())
            Gw2Ui::EmptyState("No API key", "Add a key in Options to list a character's learned recipes.");
        else if (sel.empty())
            Gw2Ui::Label("No characters cached yet.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        else if (!CraftKnown::IsReady() && !CraftKnown::HasFetched())
            Gw2Ui::Label("Loading known recipes...", Gw2Ui::kTextDim, false, nullptr, 14.f);
        else
        {
            const uint64_t kv = CraftKnown::Version(), rv = CraftingEngine::Version(), cv = ItemCatalog::Version();
            if (g_railCharBuilt != g_craftChar || g_railKnownVer != kv || g_railRecipeVer != rv
                || g_railCatVer != cv || g_railCollapseBuilt != g_railCollapseVer)
            {
                RebuildRail(g_craftChar);
                g_railCharBuilt = g_craftChar; g_railKnownVer = kv; g_railRecipeVer = rv;
                g_railCatVer = cv; g_railCollapseBuilt = g_railCollapseVer;
            }

            if (g_railFlat.empty())
                Gw2Ui::EmptyState("No known recipes", CraftKnown::IsRefreshing()
                    ? "Still fetching this character's recipes."
                    : "This character has no crafting professions, or hasn't learned any recipes yet.");
            else
            {
                Gw2Ui::Divider(0.f);
                const float w = Gw2Ui::ContentWidth();
                const float rowH = 24.f * Gw2Ui::TextScale();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImGuiListClipper clip;
                clip.Begin((int)g_railFlat.size(), rowH);
                while (clip.Step())
                {
                    const ImVec2 base = ImGui::GetCursorScreenPos();   // clipper-pinned start of this batch
                    for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    {
                        ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + (i - clip.DisplayStart) * rowH));
                        const RailEntry& e = g_railFlat[i];
                        if (e.header)
                        {
                            ImGui::PushID(i);
                            const Gw2Ui::RowHotspot row = Gw2Ui::Row("##disc", -1, rowH, w);
                            ImGui::PopID();
                            const ImVec2 p = row.min;
                            if (row.clicked) { g_railCollapsed[e.disc] = !g_railCollapsed[e.disc]; ++g_railCollapseVer; }
                            if (row.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            const bool collapsed = g_railCollapsed[e.disc];
                            Render::DrawGlyph(dl, ImVec2(p.x + rowH * 0.5f, p.y + rowH * 0.5f), rowH * 0.46f,
                                              collapsed ? Render::Glyph::CaretRight : Render::Glyph::CaretDown, Gw2Ui::kTextSub, Render::GlyphStyle{});
                            char hb[96]; std::snprintf(hb, sizeof(hb), "%s  (%d)", e.disc.c_str(), e.count);
                            Gw2Ui::RowLabel(dl, row, rowH + 2.f, 4.f, hb,
                                            Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, 16.f);
                        }
                        else
                        {
                            const ItemCatalog::Item& it = ItemCatalog::ById(e.id);
                            const ImVec2 rrp = ImGui::GetCursorScreenPos();
                            const float raddW = 26.f;
                            const ItemUI::ResultRow rr = ItemUI::DrawResultRow(i, w - raddW - 4.f, e.id, it.icon, it.name, it.rarity, 0, it.chatLink);
                            if (rr.hovered) CraftRowTooltip(it);
                            if (rr.clicked) { g_craftPreview = e.id; g_craftPreviewQty = 1; }   // open preview on the right (+ adds)
                            ImGui::SetCursorScreenPos(Gw2Ui::RowRight(Gw2Ui::RowHotspot{ rrp, w, rowH, false, false }, raddW, 22.f));
                            ImGui::PushID(20000 + i);
                            if (Gw2Ui::ActionButton("+", raddW, 22.f, Gw2Ui::ActionButtonVariant::Primary, "Add to cart")) CraftQueueAdd(e.id);
                            ImGui::PopID();
                        }
                    }
                }
            }
        }
        ImGui::EndChild();
    }

    char g_flipSearch[96] = "";
    int  g_flipSort = 0;             // 0 score, 1 profit, 2 ROI, 3 spread, 4 supply, 5 demand, 6 name, 7 buy, 8 sell, 9 depth
    bool g_flipAsc = false;
    int  g_flipMaxBuyGold = 50;      // 0 = no cap
    int  g_flipMinProfitSilver = 10;
    int  g_flipMinRoiPct = 5;
    int  g_flipMinDepth = 50;        // min of sell supply / buy demand
    int  g_flipRowCount = 100;

    int         g_flipTpSel = 0;
    int         g_flipTpShown = 0;
    std::string g_flipTpName;
    float       g_flipTpAnim = 0.f;
    int         g_flipTpScrollItem = 0;

    struct FlipRow
    {
        int id = 0;
        int buy = 0;
        int sell = 0;
        int spread = 0;
        int profit = 0;
        int buyQty = 0;
        int sellQty = 0;
        int depth = 0;
        double roi = 0.0;
        double score = 0.0;
        int level = 0;
        std::string name, icon, rarity, type, subtype, chatLink;
        std::string description, statSet, detailsRaw;
    };

    std::vector<MarketData::Item> g_marketCopy;
    std::vector<FlipRow> g_flipRows;
    uint64_t g_flipMarketVer = 0, g_flipTpVer = 0, g_flipCatalogVer = 0;
    std::string g_flipSearchBuilt;
    int g_flipSortBuilt = -1, g_flipMaxBuyBuilt = -1, g_flipMinProfitBuilt = -1;
    int g_flipMinRoiBuilt = -1, g_flipMinDepthBuilt = -1, g_flipRowsBuilt = -1;
    bool g_flipAscBuilt = false;

    // Resolve an item id -> (name, icon, rarity) via the AccountData metadata cache (shared with Inventory).
    void ItemRow(int rowIdx, float w, int id, int count)
    {
        const AccountData::Model& m = AccountData::Get();
        auto it = m.itemMeta.find(id);
        std::string name = it != m.itemMeta.end() && !it->second.name.empty() ? it->second.name : ("Item " + std::to_string(id));
        std::string icon = it != m.itemMeta.end() ? it->second.icon : std::string();
        std::string rar  = it != m.itemMeta.end() ? it->second.rarity : std::string();
        std::string link = it != m.itemMeta.end() ? it->second.chatLink : std::string();
        ItemUI::DrawResultRow(rowIdx, w, id, icon, name, rar, count, link);
    }

    void LoadMyTp(App& app, bool force)
    {
        if (g_myInflight) return;
        if (g_myStarted && !force) return;
        g_myStarted = true; g_myInflight = true; g_myLoaded = 0;
        g_delCoins = 0; g_delItems.clear(); g_curBuys.clear(); g_curSells.clear();
        g_histSpent = g_histRecvNet = 0;
        App* ap = &app;
        auto done = [](App*) { if (++g_myLoaded >= 5) g_myInflight = false; };

        app.api.V2().Commerce().Delivery([ap, done](Api::Result<Api::V2::Delivery> r) {
            if (r.ok)
            {
                g_delCoins = r.value.coins;
                const nlohmann::json& items = r.value.raw.contains("items") ? r.value.raw["items"] : nlohmann::json();
                std::vector<int> ids;
                if (items.is_array()) for (const auto& it : items) if (it.is_object()) { int id = it.value("id", 0), c = it.value("count", 0); if (id) { g_delItems.emplace_back(id, c); ids.push_back(id); } }
                if (!ids.empty()) AccountData::EnsureItemMetadata(ids);
            }
            done(ap);
        });
        auto orders = [ap](std::vector<Order>& out) {
            return [ap, &out](Api::Result<std::vector<Api::V2::Transaction>> r) {
                if (r.ok) { std::vector<int> ids;
                    for (const auto& t : r.value) { out.push_back({ t.itemId, t.price, t.quantity }); ids.push_back(t.itemId); TpPrices::Want(t.itemId); }
                    if (!ids.empty()) AccountData::EnsureItemMetadata(ids);
                }
                if (++g_myLoaded >= 5) g_myInflight = false;
            };
        };
        app.api.V2().Commerce().Transactions().CurrentBuys(orders(g_curBuys));
        app.api.V2().Commerce().Transactions().CurrentSells(orders(g_curSells));
        app.api.V2().Commerce().Transactions().HistoryBuys([ap, done](Api::Result<std::vector<Api::V2::Transaction>> r) {
            if (r.ok) for (const auto& t : r.value) g_histSpent += (long long)t.price * t.quantity;
            done(ap);
        });
        app.api.V2().Commerce().Transactions().HistorySells([ap, done](Api::Result<std::vector<Api::V2::Transaction>> r) {
            if (r.ok) for (const auto& t : r.value) g_histRecvNet += (long long)Trading::TpNet(t.price) * t.quantity;
            done(ap);
        });
    }

    std::string AgeText(long long fetchedUtc)
    {
        if (fetchedUtc <= 0) return "never";
        long long age = (long long)std::time(nullptr) - fetchedUtc;
        if (age < 0) age = 0;
        char b[48];
        if (age < 90) std::snprintf(b, sizeof(b), "%llds ago", age);
        else if (age < 5400) std::snprintf(b, sizeof(b), "%lldm ago", age / 60);
        else if (age < 172800) std::snprintf(b, sizeof(b), "%lldh ago", age / 3600);
        else std::snprintf(b, sizeof(b), "%lldd ago", age / 86400);
        return b;
    }

    bool FlipDefaultAsc(int sort)
    {
        return sort == 6 || sort == 7;
    }

    void SetFlipSort(int sort)
    {
        if (g_flipSort == sort) g_flipAsc = !g_flipAsc;
        else { g_flipSort = sort; g_flipAsc = FlipDefaultAsc(sort); }
    }

    void ToggleFlipTpCard(const FlipRow& r)
    {
        if (!TpEligibility::IsTradeable(r.id))
        {
            g_flipTpSel = 0;
            return;
        }
        const bool closing = (g_flipTpSel == r.id);
        g_flipTpSel = closing ? 0 : r.id;
        g_flipTpShown = r.id;
        g_flipTpName = r.name;
        if (!closing) g_flipTpScrollItem = 0;
    }

    bool HasFlipLore(const FlipRow& r)
    {
        return r.level > 0 || !r.rarity.empty() || !r.type.empty() || !r.subtype.empty()
            || !r.description.empty() || !r.statSet.empty() || !r.detailsRaw.empty();
    }

    AccountData::ItemMeta MetaForFlipLore(const FlipRow& r)
    {
        AccountData::ItemMeta meta;
        meta.have = true;
        meta.id = r.id;
        meta.name = r.name;
        meta.icon = r.icon;
        meta.rarity = r.rarity;
        meta.type = r.type;
        meta.level = r.level;
        meta.description = r.description;
        meta.chatLink = r.chatLink;
        if (!r.detailsRaw.empty())
        {
            try { meta.details = nlohmann::json::parse(r.detailsRaw); } catch (...) {}
        }
        return meta;
    }

    void EnsureFlipRows()
    {
        const uint64_t mv = MarketData::Version();
        const uint64_t tv = TpEligibility::Version();
        const uint64_t cv = ItemCatalog::Version();
        const std::string search = g_flipSearch;
        if (g_flipMarketVer == mv && g_flipTpVer == tv && g_flipCatalogVer == cv
            && g_flipSearchBuilt == search && g_flipSortBuilt == g_flipSort && g_flipAscBuilt == g_flipAsc
            && g_flipMaxBuyBuilt == g_flipMaxBuyGold && g_flipMinProfitBuilt == g_flipMinProfitSilver
            && g_flipMinRoiBuilt == g_flipMinRoiPct && g_flipMinDepthBuilt == g_flipMinDepth
            && g_flipRowsBuilt == g_flipRowCount)
            return;

        g_flipMarketVer = mv; g_flipTpVer = tv; g_flipCatalogVer = cv;
        g_flipSearchBuilt = search; g_flipSortBuilt = g_flipSort; g_flipAscBuilt = g_flipAsc; g_flipMaxBuyBuilt = g_flipMaxBuyGold;
        g_flipMinProfitBuilt = g_flipMinProfitSilver; g_flipMinRoiBuilt = g_flipMinRoiPct;
        g_flipMinDepthBuilt = g_flipMinDepth; g_flipRowsBuilt = g_flipRowCount;
        g_flipRows.clear();

        if (!MarketData::IsReady() || !TpEligibility::IsReady()) return;

        MarketData::CopyItems(g_marketCopy);
        const std::string q = ItemUI::Lower(search);
        const int maxBuy = std::max(0, g_flipMaxBuyGold) * 10000;
        const int minProfit = std::max(0, g_flipMinProfitSilver) * 100;
        const double minRoi = std::max(0, g_flipMinRoiPct) / 100.0;
        const int minDepth = std::max(0, g_flipMinDepth);

        g_flipRows.reserve(std::min<size_t>(g_marketCopy.size(), 4000));
        for (const MarketData::Item& m : g_marketCopy)
        {
            if (m.buyPrice <= 0 || m.sellPrice <= 0 || m.buyQuantity <= 0 || m.sellQuantity <= 0) continue;
            if (!TpEligibility::IsTradeable(m.id)) continue;
            if (maxBuy > 0 && m.buyPrice > maxBuy) continue;

            const int profit = Trading::FlipProfit(m.buyPrice, m.sellPrice);
            if (profit < minProfit) continue;
            const double roi = Trading::Roi(profit, m.buyPrice);
            if (roi < minRoi) continue;

            const int depth = std::min(m.buyQuantity, m.sellQuantity);
            if (depth < minDepth) continue;

            const ItemCatalog::Item& it = ItemCatalog::ById(m.id);
            FlipRow r;
            r.id = m.id;
            r.buy = m.buyPrice;
            r.sell = m.sellPrice;
            r.spread = std::max(0, m.sellPrice - m.buyPrice);
            r.profit = profit;
            r.buyQty = m.buyQuantity;
            r.sellQty = m.sellQuantity;
            r.depth = depth;
            r.roi = roi;
            r.name = !it.name.empty() ? it.name : (!m.name.empty() ? m.name : ("Item " + std::to_string(m.id)));
            r.icon = it.icon;
            r.rarity = it.rarity;
            r.type = it.type;
            r.subtype = it.subtype;
            r.chatLink = it.chatLink;
            r.level = it.level;
            r.description = it.description;
            r.statSet = it.statSet;
            r.detailsRaw = it.detailsRaw;
            if (!q.empty())
            {
                std::string hay = ItemUI::Lower(r.name + " " + r.type + " " + r.subtype + " " + std::to_string(r.id));
                if (hay.find(q) == std::string::npos) continue;
            }

            const double profitGold = (double)profit / 10000.0;
            const double liquidity = std::log10((double)m.buyQuantity + 10.0) + std::log10((double)m.sellQuantity + 10.0);
            const double roiFactor = 1.0 + std::min(4.0, std::max(0.0, roi) * 8.0);
            r.score = profitGold * liquidity * roiFactor;
            g_flipRows.push_back(std::move(r));
        }

        std::sort(g_flipRows.begin(), g_flipRows.end(), [](const FlipRow& a, const FlipRow& b) {
            int cmp = 0;
            switch (g_flipSort)
            {
                case 1: cmp = (a.profit < b.profit) ? -1 : (a.profit > b.profit ? 1 : 0); break;
                case 2: cmp = (a.roi < b.roi) ? -1 : (a.roi > b.roi ? 1 : 0); break;
                case 3: cmp = (a.spread < b.spread) ? -1 : (a.spread > b.spread ? 1 : 0); break;
                case 4: cmp = (a.sellQty < b.sellQty) ? -1 : (a.sellQty > b.sellQty ? 1 : 0); break;
                case 5: cmp = (a.buyQty < b.buyQty) ? -1 : (a.buyQty > b.buyQty ? 1 : 0); break;
                case 6: cmp = (ItemUI::Lower(a.name) < ItemUI::Lower(b.name)) ? -1 : (ItemUI::Lower(a.name) > ItemUI::Lower(b.name) ? 1 : 0); break;
                case 7: cmp = (a.buy < b.buy) ? -1 : (a.buy > b.buy ? 1 : 0); break;
                case 8: cmp = (a.sell < b.sell) ? -1 : (a.sell > b.sell ? 1 : 0); break;
                case 9: cmp = (a.depth < b.depth) ? -1 : (a.depth > b.depth ? 1 : 0); break;
                case 0:
                default: cmp = (a.score < b.score) ? -1 : (a.score > b.score ? 1 : 0); break;
            }
            if (cmp == 0) cmp = (a.profit < b.profit) ? -1 : (a.profit > b.profit ? 1 : 0);
            if (cmp == 0) cmp = (a.id < b.id) ? -1 : (a.id > b.id ? 1 : 0);
            return g_flipAsc ? (cmp < 0) : (cmp > 0);
        });

        const int cap = std::max(25, std::min(500, g_flipRowCount));
        if ((int)g_flipRows.size() > cap) g_flipRows.resize(cap);

        g_marketCopy.clear();
        g_marketCopy.shrink_to_fit();   // the ~30k-row snapshot copy is only needed transiently during this rebuild
    }
}

static void DrawMyTpContent(App& app)
{
    if (!AccountData::HasKey() || !AccountData::HasScope(Api::TokenPermission::Tradingpost))
    {
        ApiReminder::Card(app, "tradingpost", "your delivery box, open orders, and a profit/loss summary", /*gated*/true);
        return;
    }
    LoadMyTp(app, false);
    Gw2Ui::PushTextScale(1.2f);

    if (Gw2Ui::ActionButton("Refresh", 120.f, 32.f)) LoadMyTp(app, true);
    if (g_myInflight) { ImGui::SameLine(); Gw2Ui::Label("Updating...", Gw2Ui::kTextDim, false, nullptr, 14.f); }
    Gw2Ui::Divider(0.f);

    auto row = [&](const char* k, const std::string& v, ImU32 col) {
        Gw2Ui::TextValueRow(k, v.c_str(), 0.f, 24.f, 17.f, IM_COL32(200, 192, 170, 255), col);
    };
    auto orderRow = [&](int rowIdx, const Order& o, bool sell) {
        ItemRow(rowIdx, Gw2Ui::ContentWidth(), o.itemId, o.qty);
        TpPrices::Price tp; const bool have = TpPrices::Get(o.itemId, tp);
        const bool flag = have && tp.tradeable && (sell ? (tp.sellUnit > 0 && tp.sellUnit < o.price) : (tp.buyUnit > o.price));
        char b[96]; std::snprintf(b, sizeof(b), "      @ %s%s", Trading::Coins(o.price).c_str(), flag ? (sell ? "   - undercut" : "   - outbid") : "");
        Gw2Ui::Label(b, flag ? IM_COL32(235, 170, 90, 255) : IM_COL32(170, 200, 170, 255), false, nullptr, 14.f);
    };

    ImGui::BeginChild("##mytp", ImVec2(0.f, 0.f), false);

    Gw2Ui::BeginCard("mytp-pickup");
    Gw2Ui::SectionHeader("Pickup");
    row("Coins", Trading::Coins(g_delCoins), IM_COL32(255, 230, 170, 255));
    if (g_delItems.empty()) Gw2Ui::Label("No items to collect.", Gw2Ui::kTextDim, false, nullptr, 14.f);
    else { int i = 0; for (auto& d : g_delItems) ItemRow(i++, Gw2Ui::ContentWidth(), d.first, d.second); }
    Gw2Ui::EndCard();

    char hs[48]; std::snprintf(hs, sizeof(hs), "Sell orders (%d)", (int)g_curSells.size());
    Gw2Ui::BeginCard("mytp-sells");
    Gw2Ui::SectionHeader(hs);
    if (g_curSells.empty()) Gw2Ui::Label("No active sell orders.", Gw2Ui::kTextDim, false, nullptr, 14.f);
    else { int i = 0; for (const Order& o : g_curSells) orderRow(i++, o, true); }
    Gw2Ui::EndCard();

    char hb[48]; std::snprintf(hb, sizeof(hb), "Buy orders (%d)", (int)g_curBuys.size());
    Gw2Ui::BeginCard("mytp-buys");
    Gw2Ui::SectionHeader(hb);
    if (g_curBuys.empty()) Gw2Ui::Label("No active buy orders.", Gw2Ui::kTextDim, false, nullptr, 14.f);
    else { int i = 0; for (const Order& o : g_curBuys) orderRow(i++, o, false); }
    Gw2Ui::EndCard();

    Gw2Ui::BeginCard("mytp-pl");
    Gw2Ui::SectionHeader("Recent history (profit / loss)");
    row("Bought", Trading::Coins(g_histSpent), IM_COL32(220, 180, 170, 255));
    row("Sold (after fee)", Trading::Coins(g_histRecvNet), IM_COL32(180, 220, 180, 255));
    const long long net = g_histRecvNet - g_histSpent;
    row("Net", (net < 0 ? "-" : "") + Trading::Coins(net < 0 ? -net : net), net >= 0 ? Gw2Ui::kSuccessGreen : Gw2Ui::kLossRed);
    Gw2Ui::EndCard();

    ImGui::EndChild();
    Gw2Ui::PopTextScale();
}

static void DrawFlipFinderContent(App& app)
{
    (void)app;
    ItemUI::SetTpPrices(true);
    MarketData::Warm();

    Gw2Ui::PushTextScale(1.16f);
    Gw2Ui::BeginCard("flip-controls");
    Gw2Ui::SectionHeader("Flip Finder");
    const float w  = Gw2Ui::ContentWidth();
    const float bh = Gw2Ui::InputBoxHeight();   // the search-box height; the dropdown + Refresh match it so the row lines up
    // Reserve the fixed sort+refresh tail, then size the search box from what's left so the row never overflows.
    const float flipTail = 8.f + 120.f + 8.f + 110.f;
    const float  searchW    = std::max(140.f, std::min(320.f, w - flipTail));
    const ImVec2 flipRowTop = ImGui::GetCursorScreenPos();
    Gw2Ui::SearchBox("##flipsearch", g_flipSearch, sizeof(g_flipSearch), searchW, "Search items...");
    // SearchBox parks the cursor below-left, so SameLine would land the dropdown INSIDE the box -- position it
    // explicitly to the right of the full box width instead.
    ImGui::SetCursorScreenPos(ImVec2(flipRowTop.x + searchW + 10.f, flipRowTop.y));
    static const char* kSorts[] = { "Score", "Profit", "ROI", "Spread", "Supply", "Demand", "Name", "Buy", "Sell", "Depth" };
    const int prevSort = g_flipSort;
    if (Gw2Ui::Dropdown("##flipsort", kSorts, 10, &g_flipSort, 120.f, nullptr, nullptr, 0, bh) && g_flipSort != prevSort)
        g_flipAsc = FlipDefaultAsc(g_flipSort);
    ImGui::SameLine(0.f, 8.f);
    if (Gw2Ui::ActionButton("Refresh", 110.f, bh, Gw2Ui::ActionButtonVariant::Normal, "Refresh the datawars2 market snapshot"))
        MarketData::Warm(true);
    if (MarketData::IsRefreshing()) { ImGui::SameLine(); Gw2Ui::Label("Updating...", Gw2Ui::kTextDim, false, nullptr, 14.f); }

    Gw2Ui::SliderInt("Max buy (gold, 0 = no cap)", &g_flipMaxBuyGold, 0, 500, 260.f);
    Gw2Ui::SliderInt("Min profit (silver)", &g_flipMinProfitSilver, 0, 500, 260.f);
    Gw2Ui::SliderInt("Min ROI (%)", &g_flipMinRoiPct, 0, 200, 260.f);
    Gw2Ui::SliderInt("Min depth", &g_flipMinDepth, 0, 5000, 260.f);
    Gw2Ui::SliderInt("Rows", &g_flipRowCount, 25, 500, 260.f);

    char st[192];
    std::snprintf(st, sizeof(st), "%d snapshot rows%s, fetched %s",
                  MarketData::Count(), MarketData::IsReady() ? "" : " (no cache yet)",
                  AgeText(MarketData::FetchedUtc()).c_str());
    Gw2Ui::Label(st, Gw2Ui::kTextDim, false, nullptr, 14.f);
    const std::string refreshError = MarketData::LastError();
    if (!refreshError.empty())
        Gw2Ui::Label(("Last refresh failed: " + refreshError).c_str(), IM_COL32(210, 155, 120, 255), false, nullptr, 14.f);
    Gw2Ui::EndCard();
    Gw2Ui::PopTextScale();

    const ImVec2 bodyPos = ImGui::GetCursorScreenPos();
    const ImVec2 bodySize = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##flipbody", bodySize, false);

    EnsureFlipRows();
    if (!TpEligibility::IsReady())
    {
        Gw2Ui::EmptyState("Trading Post item data is loading.", "The official TP id cache gates Flip Finder results.");
    }
    else if (!MarketData::IsReady())
    {
        const std::string refreshError = MarketData::LastError();
        const std::string detail = refreshError.empty()
            ? "The first datawars2 fetch will be cached for future logins."
            : ("Last refresh failed: " + refreshError + ". Click Refresh to retry.");
        Gw2Ui::EmptyState("Market snapshot is loading.", detail.c_str());
    }
    else if (g_flipRows.empty())
    {
        Gw2Ui::EmptyState("No flips match.", "Relax the filters or refresh the market snapshot.");
    }
    else
    {
        const float rowH = 38.f * Gw2Ui::TextScale();
        const float rowFs = 18.f;
        const float availW = Gw2Ui::ContentWidth();
        // Persisted, draggable column widths (Item is the flex/fill column on the left). Shared PaneW store.
        float& buyW    = app.config.PaneW("flip.col.buy", 96.f);
        float& sellW   = app.config.PaneW("flip.col.sell", 96.f);
        float& profitW = app.config.PaneW("flip.col.profit", 102.f);
        float& roiW    = app.config.PaneW("flip.col.roi", 66.f);
        float& depthW  = app.config.PaneW("flip.col.depth", 80.f);
        for (float* w : { &buyW, &sellW, &profitW, &roiW, &depthW }) *w = std::clamp(*w, 50.f, 240.f);

        // Shared table shell: Item flexes; the five fixed columns drag + persist; headers sort via SetFlipSort.
        // Left-aligned like the All-Items table: the text sits at each column's LEFT, clear of the right-edge
        // resize handle (right-aligned text crammed against the handle was the complaint).
        Gw2Ui::TableColumn fcols[6] = {
            { "Item",   nullptr,  120.f, 0.f,   Gw2Ui::HAlign::Center, 6 },
            { "Buy",    &buyW,    50.f,  240.f, Gw2Ui::HAlign::Center, 7 },
            { "Sell",   &sellW,   50.f,  240.f, Gw2Ui::HAlign::Center, 8 },
            { "Profit", &profitW, 50.f,  240.f, Gw2Ui::HAlign::Center, 1 },
            { "ROI",    &roiW,    50.f,  240.f, Gw2Ui::HAlign::Center, 2 },
            { "Depth",  &depthW,  50.f,  240.f, Gw2Ui::HAlign::Center, 9 },
        };
        Gw2Ui::DataTableDesc d;
        d.id = "##flip"; d.cols = fcols; d.colCount = 6;
        d.availW = availW; d.rowCount = (int)g_flipRows.size();
        d.rowH = rowH; d.headerH = 31.f * Gw2Ui::TextScale(); d.headerFs = 16.f;
        d.leftPad = 0.f; d.flexMin = 120.f; d.rightReserve = 0.f; d.bodyChild = false;   // already inside a child
        d.activeSort = g_flipSort; d.sortAsc = g_flipAsc;
        d.onSort = [](int id) { SetFlipSort(id); };

        d.drawRow = [&](int i, const Gw2Ui::RowHotspot& row, const Gw2Ui::TableRowCols& rc)
        {
            const FlipRow& r = g_flipRows[i];
            ImGui::PushID(r.id);
            const ImVec2 p = row.min;
            if (row.clicked) ToggleFlipTpCard(r);
            const bool hov = row.hovered;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            if (!r.icon.empty())
            {
                char texId[40]; std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", r.id);
                if (void* tex = Tex::GetTextureFromURL(texId, r.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(p.x + 3.f, p.y + 3.f), ImVec2(p.x + rowH - 3.f, p.y + rowH - 3.f));
            }
            dl->AddRect(ImVec2(p.x + 3.f, p.y + 3.f), ImVec2(p.x + rowH - 3.f, p.y + rowH - 3.f), ItemUI::RarityColor(r.rarity), 0.f, 0, 1.1f);

            auto cell = [&](int ci, const std::string& s, ImU32 col) {
                Gw2Ui::RowLabel(dl, row, rc.x[ci], std::max(0.f, rc.availW - rc.x[ci] - rc.w[ci] + 8.f), s.c_str(),
                                fcols[ci].align, Gw2Ui::VAlign::Middle, col, false, nullptr, rowFs);
            };
            // Item name lives in the flex column (0) but starts after the icon gutter.
            Gw2Ui::RowLabel(dl, row, rowH + 6.f, std::max(0.f, rc.availW - rc.w[0] + 8.f), r.name.c_str(),
                            Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, ItemUI::RarityColor(r.rarity), false, nullptr, rowFs);
            cell(1, Trading::Coins(r.buy),    IM_COL32(235, 218, 170, 255));
            cell(2, Trading::Coins(r.sell),   IM_COL32(255, 230, 170, 255));
            cell(3, Trading::Coins(r.profit), IM_COL32(150, 225, 150, 255));
            { char b[32]; std::snprintf(b, sizeof(b), "%.0f%%", r.roi * 100.0); cell(4, b, IM_COL32(150, 225, 150, 255)); }
            cell(5, std::to_string(r.depth), Gw2Ui::kTextSub);

            if (hov)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (Gw2Ui::TooltipBegin())
                {
                    Gw2Ui::TooltipTitle(r.name.c_str());
                    Gw2Ui::TooltipSeparator();
                    Gw2Ui::TooltipText(("Buy: " + Trading::Coins(r.buy) + " x" + std::to_string(r.buyQty)).c_str());
                    Gw2Ui::TooltipText(("Sell: " + Trading::Coins(r.sell) + " x" + std::to_string(r.sellQty)).c_str());
                    Gw2Ui::TooltipText(("Profit: " + Trading::Coins(r.profit)).c_str());
                    char b[48]; std::snprintf(b, sizeof(b), "ROI: %.1f%%", r.roi * 100.0);
                    Gw2Ui::TooltipText(b);
                    if (HasFlipLore(r))
                    {
                        Gw2Ui::TooltipSeparator();
                        AccountData::ItemMeta meta = MetaForFlipLore(r);
                        ItemUI::DrawItemLoreBlock(meta, r.statSet, true);
                    }
                    Gw2Ui::TooltipMuted("Click to open the Trading Post detail card.");
                    Gw2Ui::TooltipMuted("Right-click for copy menu.");
                    Gw2Ui::TooltipEnd();
                }
            }
            ItemUI::DrawCopyMenu("##flipcopymenu", hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right), r.name, r.chatLink);
            ImGui::PopID();
        };
        if (Gw2Ui::DataTable(d)) app.settingsDirty = true;
    }
    ImGui::EndChild();

    ItemTp::DrawSlideCard(app, g_flipTpSel, g_flipTpShown, g_flipTpName, g_flipTpAnim, g_flipTpScrollItem,
                          bodyPos.x, bodyPos.y, bodySize.x, bodySize.y);
}

// The shared right-side breakdown: financial ledger + shopping list (flat / by-item, with optional check-off) +
// numbered crafting steps, computed from `plans` (aligned with `items`). Used by BOTH the cart and the single-
// item recipe preview, so they share one design. allowCheckoff = per-material "got" (cart only); allowByItem =
// offer the Flat/By-item toggle (cart only; the preview is always flat).
static void DrawCraftResults(App& app, const std::vector<CraftingEngine::Plan>& plans,
                             const std::vector<std::pair<int, long long>>& items,
                             const std::string& gotKey, bool allowCheckoff, bool allowByItem)
{
    const ImU32 kGoldV = IM_COL32(255, 230, 170, 255), kGreen = IM_COL32(150, 220, 150, 255), kRed = IM_COL32(220, 150, 150, 255);

    auto finRow = [&](const char* k, const std::string& v, ImU32 col, bool emph) {
        const float w = Gw2Ui::ContentWidth();
        const float h = emph ? 30.f : 25.f;
        const ImVec2 p = ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(w, h)); ImDrawList* dl = ImGui::GetWindowDrawList();
        const Gw2Ui::RowHotspot r{ p, w, h, false, false };
        if (emph)
        {
            dl->AddRectFilled(ImVec2(p.x - 3.f, p.y + 1.f), ImVec2(p.x + w + 3.f, p.y + h), IM_COL32(82, 60, 22, 70), 3.f);
            dl->AddLine(ImVec2(p.x - 3.f, p.y + 1.f), ImVec2(p.x + w + 3.f, p.y + 1.f), Gw2Ui::Alpha(Gw2Ui::kGold, 60), 1.f);
        }
        const float fs = emph ? 18.5f : 17.f;
        const ImU32 kcol = emph ? IM_COL32(230, 218, 186, 255) : IM_COL32(200, 192, 170, 255);
        const float valueW = Gw2Ui::MeasureWidth(v.c_str(), fs) + 14.f * Gw2Ui::TextScale();
        Gw2Ui::RowLabel(dl, r, 0.f, valueW + 6.f, k, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, kcol, false, nullptr, fs);
        Gw2Ui::RowLabel(dl, r, 0.f, 6.f, v.c_str(), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, col, false, nullptr, fs);
    };
    auto signedCoins = [](long long c) { return (c < 0 ? std::string("-") : std::string()) + Trading::Coins(c < 0 ? -c : c); };

    // -- aggregate the plans --
    long long craftCost = 0, tpCost = 0, revSell = 0; bool anyTpCost = false, pricesDone = true, allTradeable = true;
    std::unordered_map<int, CraftingEngine::ShopRow> shopAgg;
    std::vector<CraftingEngine::StepRow> steps;
    for (const auto& p : plans)
    {
        if (!p.ok) continue;
        craftCost += p.craftCost;
        if (p.tpCost >= 0) { tpCost += p.tpCost; anyTpCost = true; }
        revSell += p.revenueSell;
        if (!p.pricesComplete) pricesDone = false;
        if (!p.outputTradeable) allTradeable = false;
        for (const auto& s : p.shopping) { auto& a = shopAgg[s.itemId]; a.itemId = s.itemId; a.qty += s.qty; a.unitSell = s.unitSell; a.priced = a.priced || s.priced; if (s.total >= 0) a.total += s.total; }
        for (const auto& st : p.steps) steps.push_back(st);
    }

    // -- financial breakdown --
    Gw2Ui::BeginCard("craft-fin");
    Gw2Ui::SectionHeader("Financial breakdown");
    finRow("Craft cost", Trading::Coins(craftCost), kGoldV, false);
    finRow("Buy outright", anyTpCost ? Trading::Coins(tpCost) : std::string("N/A"), IM_COL32(220, 200, 160, 255), false);
    if (anyTpCost) { const long long save = tpCost - craftCost; finRow("Savings vs buying", signedCoins(save), save >= 0 ? kGreen : kRed, false); }
    finRow("Sell (listed, net)", allTradeable ? Trading::Coins(revSell) : std::string("N/A (account bound)"), IM_COL32(180, 220, 180, 255), false);
    if (allTradeable)
    {
        Gw2Ui::Divider(0.f);
        const long long profit = revSell - craftCost;
        finRow("Profit (craft, then list)", signedCoins(profit), profit >= 0 ? kGreen : kRed, true);
        if (craftCost > 0) { char rb[24]; std::snprintf(rb, sizeof(rb), "%.0f%%", 100.0 * (double)profit / (double)craftCost); finRow("ROI", rb, profit >= 0 ? kGreen : kRed, true); }
    }
    if (!pricesDone) Gw2Ui::Label("Some prices still loading...", Gw2Ui::kTextDim, false, nullptr, 14.f);
    Gw2Ui::EndCard();

    // -- shopping list (Flat aggregate OR grouped By item) + per-material check-off (cart only) --
    float sw           = 0.f;                              // set after BeginCard so ContentWidth resolves the card
    const float ssc    = Gw2Ui::TextScale();
    const float srowH  = 28.f * ssc;
    const float sFs    = 16.f;
    const float cbW    = allowCheckoff ? srowH : 0.f;            // checkbox gutter (cart only)
    const float colTot = 86.f * ssc, colUnit = 116.f * ssc, colQty = 40.f * ssc;
    const ImU32 kGoldCol = IM_COL32(255, 230, 170, 255), kUnitCol = IM_COL32(210, 196, 160, 255);

    long long stillNeed = 0;
    for (const auto& kv : shopAgg) { const CraftingEngine::ShopRow& s = kv.second; if (s.qty > 0 && s.priced && !(allowCheckoff && CraftCart::IsGot(gotKey, s.itemId))) stillNeed += s.total; }

    int shopRowKey = 0;
    auto drawShopRow = [&](const CraftingEngine::ShopRow& s) {
        const ItemCatalog::Item& it = ItemCatalog::ById(s.itemId);
        const bool got = allowCheckoff && CraftCart::IsGot(gotKey, s.itemId);
        const float totX = sw - colTot, unitX = totX - colUnit, qtyX = unitX - colQty;
        const float nameOff = cbW + srowH + 6.f;
        const float nameW = std::max(50.f, qtyX - nameOff);
        ImGui::PushID(shopRowKey);
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##shoprow", shopRowKey, srowH, sw);
        const bool clicked = row.clicked;
        const bool hov = row.hovered;
        const ImVec2 p = row.min;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ++shopRowKey;

        if (allowCheckoff)
        {
            const float cbS = srowH - 11.f;
            const ImVec2 cbMin(p.x + 4.f, p.y + (srowH - cbS) * 0.5f), cbMax(cbMin.x + cbS, cbMin.y + cbS);
            dl->AddRect(cbMin, cbMax, got ? Gw2Ui::kGold : IM_COL32(150, 140, 110, 200), 2.f, 0, 1.4f);
            if (got) Render::DrawGlyph(dl, ImVec2((cbMin.x + cbMax.x) * 0.5f, (cbMin.y + cbMax.y) * 0.5f), cbS * 0.95f, Render::Glyph::Check, Gw2Ui::kGold, Render::GlyphStyle{});
        }

        const float ix = p.x + cbW;
        if (!it.icon.empty())
        {
            char texId[40]; std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", s.itemId);
            if (void* tex = Tex::GetTextureFromURL(texId, it.icon.c_str()))
                dl->AddImage((ImTextureID)tex, ImVec2(ix + 2.f, p.y + 3.f), ImVec2(ix + srowH - 4.f, p.y + srowH - 3.f));
        }
        dl->AddRect(ImVec2(ix + 2.f, p.y + 3.f), ImVec2(ix + srowH - 4.f, p.y + srowH - 3.f), ItemUI::RarityColor(it.rarity), 0.f, 0, 1.2f);

        auto col = [&](float x, float cw, const std::string& str, ImU32 c, Gw2Ui::HAlign ha) {
            Gw2Ui::RowLabel(dl, row, x, std::max(0.f, sw - x - cw + 6.f), str.c_str(), ha, Gw2Ui::VAlign::Middle, c, false, nullptr, sFs);
        };
        col(nameOff, nameW, it.name, got ? Gw2Ui::kTextDim : ItemUI::RarityColor(it.rarity), Gw2Ui::HAlign::Left);
        if (got)
            col(unitX, colUnit + colTot, "got it", Gw2Ui::kTextDim, Gw2Ui::HAlign::Right);
        else if (s.qty <= 0)
            col(unitX, colUnit + colTot, "have enough", Gw2Ui::kTextDim, Gw2Ui::HAlign::Right);
        else
        {
            col(qtyX, colQty, "x" + std::to_string(s.qty), Gw2Ui::kTextDim, Gw2Ui::HAlign::Right);
            if (s.priced)
            {
                col(unitX, colUnit, Trading::Coins(s.unitSell) + " ea", kUnitCol, Gw2Ui::HAlign::Right);
                col(totX, colTot, Trading::Coins(s.total), kGoldCol, Gw2Ui::HAlign::Right);
            }
            else
                col(unitX, colUnit + colTot, "no TP price", Gw2Ui::kTextDim, Gw2Ui::HAlign::Right);
        }

        if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); CraftRowTooltip(it); }
        if (clicked)
        {
            if (allowCheckoff && ImGui::GetIO().MousePos.x < p.x + cbW) CraftCart::SetGot(gotKey, s.itemId, !got);
            else ToggleCraftTpCard(s.itemId, it.name);
        }
        ItemUI::DrawCopyMenu("##craftshopcopy", hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right), it.name, it.chatLink);
        ImGui::PopID();
    };

    Gw2Ui::BeginCard("craft-shop");
    sw = Gw2Ui::ContentWidth();
    { char sh[64]; std::snprintf(sh, sizeof(sh), "Shopping list (to buy %s)", Trading::Coins(stillNeed).c_str()); Gw2Ui::SectionHeader(sh); }
    if (allowByItem && CraftToggle("##shopbyitem", "Group by item", &app.config.craftShopByItem, "Show materials grouped per cart item instead of one flat list", 150.f)) app.settingsDirty = true;
    if (allowByItem) ImGui::Dummy(ImVec2(0.f, 4.f));

    const bool byItem = allowByItem && app.config.craftShopByItem;
    if (shopAgg.empty())
        Gw2Ui::Label("Nothing to buy.", Gw2Ui::kTextDim, false, nullptr, 14.f);
    else if (!byItem)
    {
        ImGui::PushID("shopflat");
        std::vector<CraftingEngine::ShopRow> shop; shop.reserve(shopAgg.size());
        for (auto& kv : shopAgg) shop.push_back(kv.second);
        std::sort(shop.begin(), shop.end(), [](const CraftingEngine::ShopRow& a, const CraftingEngine::ShopRow& b) { return a.total > b.total; });
        for (const CraftingEngine::ShopRow& s : shop) drawShopRow(s);
        ImGui::PopID();
    }
    else
    {
        ImGui::PushID("shopbyitemlist");
        for (int i = 0; i < (int)plans.size() && i < (int)items.size(); ++i)
        {
            const CraftingEngine::Plan& pl = plans[i];
            if (!pl.ok || pl.shopping.empty()) continue;
            const int outId = items[i].first;
            const ItemCatalog::Item& out = ItemCatalog::ById(outId);
            long long sub = 0;
            for (const CraftingEngine::ShopRow& s : pl.shopping) if (s.qty > 0 && s.priced && !(allowCheckoff && CraftCart::IsGot(gotKey, s.itemId))) sub += s.total;

            ImGui::PushID(outId);
            const Gw2Ui::RowHotspot hdr = Gw2Ui::Row("##shophdr", -1, srowH, sw);
            const bool hc = hdr.clicked;
            const bool hh = hdr.hovered;
            ImGui::PopID();
            if (hc) g_craftShopCollapsed[outId] = !g_craftShopCollapsed[outId];
            if (hh) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const bool collapsed = g_craftShopCollapsed[outId];
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 hp = hdr.min;
            Render::DrawGlyph(dl, ImVec2(hp.x + srowH * 0.5f, hp.y + srowH * 0.5f), srowH * 0.46f,
                              collapsed ? Render::Glyph::CaretRight : Render::Glyph::CaretDown, Gw2Ui::kTextSub, Render::GlyphStyle{});
            const float oicX = hp.x + srowH;
            if (!out.icon.empty())
            {
                char texId[40]; std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", outId);
                if (void* tex = Tex::GetTextureFromURL(texId, out.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(oicX + 2.f, hp.y + 3.f), ImVec2(oicX + srowH - 4.f, hp.y + srowH - 3.f));
            }
            { std::string ht = out.name + "  x" + std::to_string(items[i].second);
              Gw2Ui::RowLabel(dl, hdr, oicX - hp.x + srowH + 4.f, colTot + 6.f, ht.c_str(),
                              Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(228, 222, 204, 255), false, nullptr, 15.5f); }
            Gw2Ui::RowLabel(dl, hdr, sw - colTot, 6.f, Trading::Coins(sub).c_str(),
                            Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, kGoldCol, false, nullptr, 16.f);

            if (!collapsed) for (const CraftingEngine::ShopRow& s : pl.shopping) drawShopRow(s);
        }
        ImGui::PopID();
    }
    Gw2Ui::EndCard();

    // -- crafting steps (build order) --
    Gw2Ui::BeginCard("craft-steps");
    Gw2Ui::SectionHeader("Crafting steps");
    if (allowByItem && CraftToggle("##stepsbyitem", "Group by item", &app.config.craftStepsByItem, "Group the build order under each cart item instead of one flat list", 150.f)) app.settingsDirty = true;
    if (allowByItem) ImGui::Dummy(ImVec2(0.f, 4.f));
    const bool stepsByItem = allowByItem && app.config.craftStepsByItem;

    const float w    = Gw2Ui::ContentWidth();
    const float sc   = Gw2Ui::TextScale();
    const float hdrH = 26.f * sc;
    const float oico = 22.f * sc;
    const float iico = 18.f * sc;
    const float numW = 26.f * sc;
    int stepKey = 0;
    auto drawStep = [&](const CraftingEngine::StepRow& st, int num) {
        ImGui::PushID(++stepKey);
        const ItemCatalog::Item& out = ItemCatalog::ById(st.outId);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec2 hp = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, hdrH));
        const Gw2Ui::RowHotspot headRow{ hp, w, hdrH, false, false };
        char nb[8]; std::snprintf(nb, sizeof(nb), "%d.", num);
        Gw2Ui::RowLabel(dl, headRow, 0.f, w - numW + 4.f, nb,
                        Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 16.f);
        const float oX = hp.x + numW, oY = hp.y + (hdrH - oico) * 0.5f;
        if (!out.icon.empty())
        {
            char texId[40]; std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", st.outId);
            if (void* tex = Tex::GetTextureFromURL(texId, out.icon.c_str()))
                dl->AddImage((ImTextureID)tex, ImVec2(oX, oY), ImVec2(oX + oico, oY + oico));
        }
        dl->AddRect(ImVec2(oX, oY), ImVec2(oX + oico, oY + oico), ItemUI::RarityColor(out.rarity), 0.f, 0, 1.2f);

        float nameRightOff = w;
        if (!st.disc.empty())
        {
            const float pf = 13.f;
            const float pw = Gw2Ui::PillWidth(st.disc.c_str(), pf);
            const float ph = Gw2Ui::PillHeight(pf);
            Gw2Ui::PillAt(dl, Gw2Ui::RowRight(headRow, pw, ph), st.disc.c_str(), pf);
            nameRightOff = w - pw - 8.f;
        }
        char hb[128]; std::snprintf(hb, sizeof(hb), "Craft %lldx %s", st.crafts * st.outCnt, out.name.c_str());
        const float nameXOff = numW + oico + 6.f;
        Gw2Ui::RowLabel(dl, headRow, nameXOff, std::max(0.f, w - nameRightOff), hb,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        IM_COL32(232, 226, 208, 255), false, nullptr, 16.f);

        const float chipH = iico + 4.f, leftX = oX;
        float cx = leftX, cy = ImGui::GetCursorScreenPos().y, maxY = cy;
        int ingNo = 0;
        for (const auto& ic : st.ing)
        {
            const ItemCatalog::Item& im = ItemCatalog::ById(ic.first);
            char cb[24]; std::snprintf(cb, sizeof(cb), "x%lld", (long long)ic.second * st.crafts);
            const float chipW = iico + 4.f + Gw2Ui::MeasureWidth(cb, 14.f) + 12.f;
            if (cx + chipW > hp.x + w && cx > leftX) { cx = leftX; cy += chipH + 3.f; }
            ImGui::PushID(1000 + ingNo++);
            ImGui::SetCursorScreenPos(ImVec2(cx, cy));
            const bool icl = ImGui::InvisibleButton("##ing", ImVec2(chipW, chipH));
            const bool ihv = ImGui::IsItemHovered();
            if (ihv) dl->AddRectFilled(ImVec2(cx - 2.f, cy - 1.f), ImVec2(cx + chipW - 6.f, cy + chipH + 1.f), IM_COL32(255, 255, 255, 18), 3.f);
            const float iy = cy + (chipH - iico) * 0.5f;
            if (!im.icon.empty())
            {
                char texId2[40]; std::snprintf(texId2, sizeof(texId2), "TC_ITEM_%d", ic.first);
                if (void* tex = Tex::GetTextureFromURL(texId2, im.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(cx, iy), ImVec2(cx + iico, iy + iico));
            }
            dl->AddRect(ImVec2(cx, iy), ImVec2(cx + iico, iy + iico), ItemUI::RarityColor(im.rarity), 0.f, 0, 1.f);
            Gw2Ui::LabelDL(dl, ImVec2(cx + iico + 4.f, cy), ImVec2(cx + chipW, cy + chipH), cb,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSub, false, nullptr, 14.f);
            if (ihv) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); CraftRowTooltip(im); }
            ItemUI::DrawCopyMenu("##ingcopy", ihv && ImGui::IsMouseClicked(ImGuiMouseButton_Right), im.name, im.chatLink);
            if (icl) ToggleCraftTpCard(ic.first, im.name);
            ImGui::PopID();
            cx += chipW + 4.f;
            maxY = std::max(maxY, cy + chipH);
        }
        ImGui::SetCursorScreenPos(ImVec2(hp.x, maxY + 7.f));
        ImGui::Dummy(ImVec2(w, 1.f));
        ImGui::PopID();
    };

    if (steps.empty())
        Gw2Ui::Label("No crafting steps -- buy directly.", Gw2Ui::kTextDim, false, nullptr, 14.f);
    else if (!stepsByItem)
    {
        ImGui::PushID("stepsflat");
        int n = 0; for (const CraftingEngine::StepRow& st : steps) drawStep(st, ++n);
        ImGui::PopID();
    }
    else
    {
        ImGui::PushID("stepsbyitemlist");
        for (int i = 0; i < (int)plans.size() && i < (int)items.size(); ++i)
        {
            const CraftingEngine::Plan& pl = plans[i];
            if (!pl.ok || pl.steps.empty()) continue;
            const ItemCatalog::Item& outItem = ItemCatalog::ById(items[i].first);
            char gh[96]; std::snprintf(gh, sizeof(gh), "%s  x%lld", outItem.name.c_str(), items[i].second);
            Gw2Ui::Label(gh, Gw2Ui::kGold, true, nullptr, 16.f);
            int n = 0; for (const CraftingEngine::StepRow& st : pl.steps) drawStep(st, ++n);
        }
        ImGui::PopID();
    }
    Gw2Ui::EndCard();
}

// A "back to cart" icon button: the Cart glyph in a small rounded button with a count badge (the number of items
// in the active cart), pinned at the top-right of the toggle row. While previewing a recipe it glows gold and
// returns to the cart on click; in the cart view it is a passive item-count indicator. Returns true on click.
static bool DrawCartBackButton(float w, float h, int count, bool active)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##craftcartbtn", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    const bool clk = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = hov ? IM_COL32(64, 58, 40, 240) : IM_COL32(44, 41, 30, 222);
    const ImU32 br = (active || hov) ? Gw2Ui::kGold : IM_COL32(150, 140, 110, 170);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 4.f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), br, 4.f, 0, active ? 1.5f : 1.2f);
    Render::DrawGlyph(dl, ImVec2(p.x + w * 0.5f, p.y + h * 0.5f + 1.f), h - 10.f,
                      Render::Glyph::Cart, (active || hov) ? Gw2Ui::kTextSelected : Gw2Ui::kTextSub, Render::GlyphStyle{});
    if (count > 0)
    {
        char nb[8]; std::snprintf(nb, sizeof(nb), "%d", count > 99 ? 99 : count);
        const float r = 9.f;
        const ImVec2 bc(p.x + w - r * 0.75f, p.y + r * 0.75f);   // top-right corner, only a slight overhang
        dl->AddCircleFilled(bc, r, Gw2Ui::kGold, 16);
        dl->AddCircle(bc, r, IM_COL32(36, 30, 16, 255), 16, 1.3f);
        Gw2Ui::LabelDL(dl, ImVec2(bc.x - r, bc.y - r), ImVec2(bc.x + r, bc.y + r), nb,
                       Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(28, 24, 12, 255), true, nullptr, 14.f);
    }
    if (hov) Gw2Ui::Tooltip(active ? "Back to cart" : "Items in cart");
    return clk;
}

static void DrawCraftBody(App& app)
{
    // Drawn inside the single ##craftbodyhost scroll child (set up by DrawCraftingContent) -- the standard
    // single-scroll-child pattern (like ##itembody), so cards are scrollbar-aware with no per-element insets.

    // -- project picker (the saved cart selector) --
    {
        ImGui::PushID("craftproj");
        const std::vector<std::string> names = CraftCart::Names();
        const std::string active = CraftCart::Active();
        std::vector<const char*> cnames; cnames.reserve(names.size());
        int sel = 0;
        for (int i = 0; i < (int)names.size(); ++i) { cnames.push_back(names[i].c_str()); if (names[i] == active) sel = i; }
        // Picker dropdown + New/Rename/Delete. The "Project" label is dropped (the dropdown is self-evident) so
        // the row fits a narrow rail. The dropdown takes a capped share; the 3 buttons share the rest via
        // FillWidth, wrapping to a 2nd row when even their minimum widths won't fit alongside the dropdown.
        const float projW   = Gw2Ui::ContentWidth();
        const float projGap = 6.f;
        const float btnMin  = 56.f;
        const bool  projWrap = projW < 190.f + projGap + (btnMin * 3.f + projGap * 2.f);
        const float ddW     = projWrap ? projW : std::min(190.f, projW - projGap - (btnMin * 3.f + projGap * 2.f));
        if (Gw2Ui::Dropdown("##projsel", cnames.data(), (int)cnames.size(), &sel, ddW) && sel >= 0 && sel < (int)names.size())
        { CraftCart::SetActive(names[sel]); g_craftDirty = true; g_craftProjMode = 0; }
        if (projWrap) ImGui::Dummy(ImVec2(0.f, 4.f)); else ImGui::SameLine(0.f, projGap);
        const float projBtnW = projWrap ? Gw2Ui::FillWidth(projW, 3, projGap, btnMin)
                                        : Gw2Ui::FillWidth(projW - ddW - projGap, 3, projGap, btnMin);
        if (Gw2Ui::ActionButton("New", projBtnW, 26.f)) { g_craftProjMode = 1; g_craftProjEdit[0] = '\0'; }
        ImGui::SameLine(0.f, projGap);
        if (Gw2Ui::ActionButton("Rename", projBtnW, 26.f)) { g_craftProjMode = 2; std::snprintf(g_craftProjEdit, sizeof(g_craftProjEdit), "%s", active.c_str()); }
        ImGui::SameLine(0.f, projGap);
        if (Gw2Ui::ActionButton("Delete", projBtnW, 26.f)) { CraftCart::Delete(active); g_craftDirty = true; g_craftProjMode = 0; }
        if (g_craftProjMode != 0)
        {
            ImGui::Dummy(ImVec2(0.f, 4.f));
            const char* projEditLbl = g_craftProjMode == 1 ? "New:" : "Rename:";
            Gw2Ui::Label(projEditLbl, Gw2Ui::kTextDim, false, nullptr, 14.f);
            ImGui::SameLine(0.f, 6.f);
            // Fit the edit box: take the content width minus the label, the OK(52)+Cancel(70) tail and gaps.
            const float projEditW = Gw2Ui::ContentWidth()
                                  - Gw2Ui::MeasureWidth(projEditLbl, 14.f) - 6.f   // label + its gap
                                  - 52.f - 70.f - 6.f - 6.f;                       // OK + Cancel + their gaps
            Gw2Ui::TextBox("##projedit", g_craftProjEdit, sizeof(g_craftProjEdit), std::max(120.f, projEditW));
            ImGui::SameLine(0.f, 6.f);
            if (Gw2Ui::ActionButton("OK", 52.f, 26.f))
            {
                if (g_craftProjMode == 1) CraftCart::New(g_craftProjEdit);
                else CraftCart::Rename(active, g_craftProjEdit);
                g_craftProjMode = 0; g_craftDirty = true;
            }
            ImGui::SameLine(0.f, 6.f);
            if (Gw2Ui::ActionButton("Cancel", 70.f, 26.f)) g_craftProjMode = 0;
        }
        ImGui::PopID();
    }
    Gw2Ui::Divider(0.f);

    // -- add-to-cart search + light-up toggle buttons (the Items-tab "Tradeable" style) --
    const float topW = Gw2Ui::ContentWidth();
    // Search box, with the "back to cart" badge pinned on its right (sits above the toggle row, near "Known only").
    const int   cartCount = (int)CraftCart::Items(CraftCart::Active()).size();
    const bool  showCart  = cartCount > 0;
    const float cartW = 46.f, cartH = 30.f, cartGap = 8.f;
    const float searchW = showCart ? std::min(360.f, topW - cartW - cartGap - 4.f) : std::min(360.f, topW - 8.f);
    Gw2Ui::SearchBox("##craftsearch", g_craftSearch, sizeof(g_craftSearch), searchW, "Add an item to craft...");
    if (showCart)
    {
        Gw2Ui::SameLineRightCluster(cartW + 6.f);   // pin right with a small inset so the count badge does not clip
        if (DrawCartBackButton(cartW, cartH, cartCount, g_craftPreview > 0)) { g_craftPreview = 0; g_craftPreviewQty = 1; }
    }
    ImGui::Dummy(ImVec2(0.f, 5.f));   // breathing room between the search bar and the toggle buttons
    // The 3 compute toggles share the content width via FillWidth (was 158/184/120 = 478px fixed, which
    // overflowed a narrow rail). When even their minimum widths won't fit one row, wrap to two rows (2 + 1).
    const float togGap  = 8.f;
    const float togMin  = 110.f;
    const bool  togWrap = topW < togMin * 3.f + togGap * 2.f;
    const float togW    = togWrap ? Gw2Ui::FillWidth(topW, 2, togGap, togMin)
                                  : Gw2Ui::FillWidth(topW, 3, togGap, togMin);
    if (CraftToggle("##craftown", "Use my materials", &app.config.craftUseOwnMaterials, "Subtract materials you already own from the shopping list", togW)) { g_craftDirty = true; app.settingsDirty = true; }
    ImGui::SameLine(0.f, togGap);
    if (CraftToggle("##craftsub", "Craft sub-components", &app.config.craftSubComponents, "Recurse into craftable intermediates instead of buying them when cheaper", togW)) { g_craftDirty = true; app.settingsDirty = true; }
    if (togWrap) ImGui::Dummy(ImVec2(0.f, 4.f)); else ImGui::SameLine(0.f, togGap);
    CraftToggle("##craftknown", "Known only", &g_craftKnownOnly, "Show only recipes the selected character has learned", togW);

    if (g_craftSearch[0])
    {
        ImGui::PushID("craftres");
        const std::vector<int>& hits = ItemCatalog::Query(g_craftSearch, "", "", 0xFFu, 0, 120, ItemCatalog::Sort::Name, true);
        const bool knownFilter = g_craftKnownOnly && CraftKnown::HasFetched() && !g_craftChar.empty();
        int shown = 0;
        for (int idx : hits)
        {
            const ItemCatalog::Item& it = ItemCatalog::At(idx);
            if (!CraftingEngine::RecipesFor(it.id)) continue;
            if (knownFilter && !SelKnowsCraft(it.id, g_craftChar)) continue;
            // result row (click = open the recipe preview on the right) + an explicit "Add" button (quick add)
            const ImVec2 srp = ImGui::GetCursorScreenPos();
            const float sfw = Gw2Ui::ContentWidth();
            const float srH = 24.f * Gw2Ui::TextScale();
            const float saddW = 60.f;
            if (CraftItemRow(shown, sfw - saddW - 8.f, it).clicked) { g_craftPreview = it.id; g_craftPreviewQty = 1; }
            ImGui::SetCursorScreenPos(Gw2Ui::RowRight(Gw2Ui::RowHotspot{ srp, sfw, srH, false, false }, saddW, 24.f));
            ImGui::PushID(it.id);
            if (Gw2Ui::ActionButton("Add", saddW, 24.f, Gw2Ui::ActionButtonVariant::Primary, "Add to cart")) { CraftQueueAdd(it.id); g_craftSearch[0] = '\0'; }
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(srp.x, srp.y + srH));
            if (++shown >= 8) break;
        }
        if (shown == 0) Gw2Ui::Label(knownFilter ? "No matching recipe is known by this character." : "No craftable item matches.",
                                     Gw2Ui::kTextDim, false, nullptr, 14.f);
        ImGui::PopID();
    }
    Gw2Ui::Divider(0.f);

    // -- recipe preview: clicking a search/rail row opens that recipe's breakdown HERE (with an Add-to-cart
    // button + a Back-to-cart). The cart is restored when the preview is dismissed or the recipe is added. --
    if (g_craftPreview > 0)
    {
        const ItemCatalog::Item& pit = ItemCatalog::ById(g_craftPreview);
        g_craftPreviewQty = std::max(1LL, g_craftPreviewQty);
        const unsigned optBits = (app.config.craftUseOwnMaterials ? 1u : 0u) | (app.config.craftSubComponents ? 2u : 0u);
        const double pnow = ImGui::GetTime();
        if (g_craftPreviewBuiltId != g_craftPreview || g_craftPreviewBuiltQty != g_craftPreviewQty
            || g_craftPreviewVer != CraftingEngine::Version() || g_craftPreviewOpts != optBits
            || (!g_craftPreviewPlan.pricesComplete && pnow >= g_craftPreviewNext))
        {
            CraftingEngine::Opts o{ app.config.craftUseOwnMaterials, app.config.craftSubComponents };
            std::unordered_map<int, long long> ow;
            if (app.config.craftUseOwnMaterials) { const AccountData::Model& m = AccountData::Get(); if (m.haveMats) for (const auto& ms : m.materials) if (ms.id > 0 && ms.count > 0) ow[ms.id] += ms.count; }
            g_craftPreviewPlan = CraftingEngine::ComputePlan(g_craftPreview, g_craftPreviewQty, o, ow);
            g_craftPreviewBuiltId = g_craftPreview; g_craftPreviewBuiltQty = g_craftPreviewQty;
            g_craftPreviewVer = CraftingEngine::Version(); g_craftPreviewOpts = optBits; g_craftPreviewNext = pnow + 0.3;
        }
        const CraftingEngine::Plan& pp = g_craftPreviewPlan;

        Gw2Ui::BeginCard("pv-head");
        Gw2Ui::SectionHeader("Recipe");
        if (CraftItemRow(0, 0.f, pit).clicked) ToggleCraftTpCard(g_craftPreview, pit.name);
        ImGui::PushID("pvq");
        char qb[24]; std::snprintf(qb, sizeof(qb), "x%lld", g_craftPreviewQty);
        // The steppers stay fixed; the two action buttons share the remaining content width via FillWidth so
        // they never run off a narrow rail (was 124 + 110 hardcoded).
        const float pvStepper = 30.f + 6.f + Gw2Ui::MeasureWidth(qb, 16.f) + 6.f + 30.f + 12.f;
        const float pvActW    = Gw2Ui::FillWidth(Gw2Ui::ContentWidth() - pvStepper, 2, 6.f, 90.f);
        if (Gw2Ui::ActionButton("-", 30.f, 26.f) && g_craftPreviewQty > 1) --g_craftPreviewQty;
        ImGui::SameLine(0.f, 6.f);
        Gw2Ui::Label(qb, IM_COL32(236, 230, 212, 255), false, nullptr, 16.f);
        ImGui::SameLine(0.f, 6.f);
        if (Gw2Ui::ActionButton("+", 30.f, 26.f)) ++g_craftPreviewQty;
        ImGui::SameLine(0.f, 12.f);
        if (Gw2Ui::ActionButton("Add to cart", pvActW, 26.f, Gw2Ui::ActionButtonVariant::Primary))
        { CraftCart::Add(CraftCart::Active(), g_craftPreview, g_craftPreviewQty); g_craftDirty = true; g_craftPreview = 0; g_craftPreviewQty = 1; }
        ImGui::SameLine(0.f, 6.f);
        if (Gw2Ui::ActionButton("Back to cart", pvActW, 26.f)) { g_craftPreview = 0; g_craftPreviewQty = 1; }
        ImGui::PopID();
        Gw2Ui::EndCard();

        // the breakdown / shopping / steps use the SAME shared renderer as the cart (single-item, no check-off)
        DrawCraftResults(app, std::vector<CraftingEngine::Plan>{ pp },
                         std::vector<std::pair<int, long long>>{ { g_craftPreview, g_craftPreviewQty } },
                         std::string(), /*allowCheckoff*/ false, /*allowByItem*/ false);
        return;   // the slide-in TP card (if a material was clicked) is drawn by DrawCraftingContent over the body
    }

    const std::string activeProj = CraftCart::Active();
    std::vector<std::pair<int, long long>> items = CraftCart::Items(activeProj);

    if (items.empty())
    {
        Gw2Ui::EmptyState("Cart is empty.", "Search a recipe (or pick one in the rail), preview it, then Add to cart.");
        return;
    }

    // -- recompute the per-entry plans behind a change token (cart items/toggles/recipe-version, or while prices load) --
    const double now = ImGui::GetTime();
    bool incomplete = false; for (const auto& p : g_craftPlans) if (!p.pricesComplete) incomplete = true;
    if (g_craftDirty || CraftingEngine::Version() != g_craftRecipeVer
        || items != g_craftItemsBuilt || (incomplete && now >= g_craftNextRecalc))
    {
        CraftingEngine::Opts opts{ app.config.craftUseOwnMaterials, app.config.craftSubComponents };
        std::unordered_map<int, long long> owned;
        if (app.config.craftUseOwnMaterials) { const AccountData::Model& m = AccountData::Get(); if (m.haveMats) for (const auto& ms : m.materials) if (ms.id > 0 && ms.count > 0) owned[ms.id] += ms.count; }
        g_craftPlans.clear();
        for (const auto& [id, qty] : items) g_craftPlans.push_back(CraftingEngine::ComputePlan(id, qty, opts, owned));
        g_craftDirty = false; g_craftRecipeVer = CraftingEngine::Version(); g_craftItemsBuilt = items; g_craftNextRecalc = now + 0.3;
    }

    // -- cart (qty steppers + remove + clear); the active project's items --
    Gw2Ui::BeginCard("craft-cart");
    { char hdr[48]; std::snprintf(hdr, sizeof(hdr), "Cart (%d)", (int)items.size()); Gw2Ui::SectionHeader(hdr); }
    int removeId = -1;
    ImGui::PushID("craftcart");
    for (int i = 0; i < (int)items.size(); ++i)
    {
        const int id = items[i].first; const long long qty = items[i].second;
        const ItemCatalog::Item& it = ItemCatalog::ById(id);
        if (CraftItemRow(i, 0.f, it).clicked) ToggleCraftTpCard(id, it.name);   // click -> TP price card
        ImGui::PushID(id);
        if (Gw2Ui::ActionButton("-", 30.f, 24.f) && qty > 1) { CraftCart::SetQty(activeProj, id, qty - 1); g_craftDirty = true; }
        ImGui::SameLine(0.f, 6.f);
        char qb[24]; std::snprintf(qb, sizeof(qb), "x%lld", qty);
        Gw2Ui::Label(qb, IM_COL32(236, 230, 212, 255), false, nullptr, 16.f); ImGui::SameLine(0.f, 6.f);
        if (Gw2Ui::ActionButton("+", 30.f, 24.f)) { CraftCart::SetQty(activeProj, id, qty + 1); g_craftDirty = true; }
        ImGui::SameLine(0.f, 12.f);
        if (Gw2Ui::ActionButton("Remove", 84.f, 24.f)) removeId = id;
        ImGui::PopID();
    }
    ImGui::PopID();
    if (removeId >= 0) { CraftCart::Remove(activeProj, removeId); g_craftDirty = true; }
    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (Gw2Ui::ActionButton("Clear cart", 110.f, 26.f)) { CraftCart::Clear(activeProj); g_craftDirty = true; }
    Gw2Ui::EndCard();

    DrawCraftResults(app, g_craftPlans, items, activeProj, /*allowCheckoff*/ true, /*allowByItem*/ true);
}

// The Crafting view = a left rail (a character's known recipes, by discipline) + the right body (search /
// queue / breakdown / shopping list / steps), with a draggable, persisted splitter between them.
static void DrawCraftingContent(App& app)
{
    g_craftWantMats = app.config.craftUseOwnMaterials;   // published to entry.cpp's AccountData::Tick so materials poll while needed

    if (!CraftingEngine::Ready())
    {
        Gw2Ui::EmptyState("Recipes loading...", "The crafting database loads in the background on login.");
        return;
    }

    Gw2Ui::PushTextScale(1.3f);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    const float  maxRail = std::clamp(avail.x - 360.f, 220.f, 520.f);
    float& railW = app.config.PaneW("craft.rail", 300.f);
    railW = std::clamp(railW, 220.f, maxRail);

    DrawCraftRail(app, railW, avail.y);   // LEFT rail (known recipes)

    if (Gw2Ui::VSplitter("##craft_rail_split", origin.x + railW + 3.f, origin.y, avail.y, &railW, 220.f, maxRail))
        app.settingsDirty = true;

    // RIGHT body: ONE scroll child (##craftbodyhost) -- the standard single-scroll-child pattern, exactly like
    // the Items tab's ##itembody. It establishes the body's left margin and owns the scrollbar, so the cards'
    // content region is scrollbar-aware (no per-element insets). The top controls scroll with the cards.
    const ImVec2 bodyPos(origin.x + railW + 6.f, origin.y);
    const ImVec2 bodySize(avail.x - railW - 6.f, avail.y);
    ImGui::SetCursorScreenPos(bodyPos);
    ImGui::BeginChild("##craftbodyhost", bodySize, false);
    DrawCraftBody(app);
    ImGui::EndChild();

    Gw2Ui::PopTextScale();   // the slide-in card draws at its own (1.0) scale, like the Flip Finder

    ItemTp::DrawSlideCard(app, g_craftTpSel, g_craftTpShown, g_craftTpName, g_craftTpAnim, g_craftTpScrollItem,
                          bodyPos.x, bodyPos.y, bodySize.x, bodySize.y);
}

// ---- Charts view (g_tpView == 3) -- a dedicated full-size price-history surface --------------------------------
static void EnsureChartResults()
{
    const uint64_t cv = ItemCatalog::Version();
    const bool tpReady = TpEligibility::IsReady();
    if (g_chartCatalogVer == cv && g_chartTpReadyBuilt == tpReady && g_chartSearchBuilt == g_chartSearch)
        return;   // change-token: rebuild only when the search / catalog / tradeable-readiness changes
    g_chartCatalogVer = cv;
    g_chartTpReadyBuilt = tpReady;
    g_chartSearchBuilt = g_chartSearch;
    g_chartResults.clear();
    if (ItemCatalog::Count() == 0) return;
    const std::vector<int>& q = ItemCatalog::Query(g_chartSearch, std::string(), std::string(), 0u, 0, 80,
                                                   ItemCatalog::Sort::Relevance, true);   // typeahead: best matches first
    for (int idx : q)
    {
        if (g_chartResults.size() >= 400) break;   // cap; the user types to narrow
        if (ItemTp::Tradeable(ItemCatalog::At(idx).id)) g_chartResults.push_back(idx);
    }
}

static void DrawMarketChart(App& app)
{
    (void)app;
    PriceHistory::WantMarketIndex();
    PriceHistory::MarketSeries ms;
    if (!PriceHistory::GetMarketIndex(ms) || ms.size() < 2)
    {
        Gw2Ui::Label(PriceHistory::IsFetchingMarket() ? "Loading market index..." : "No market data available.",
                     Gw2Ui::kTextDim, false, nullptr, 16.f);
        return;
    }
    const ImU32 cBuy  = IM_COL32(140, 205, 140, 255);   // buy_value  = gold in buy orders (demand)
    const ImU32 cSell = IM_COL32(235, 178, 110, 255);   // sell_value = gold in sell listings (supply)

    const int cutoff = PriceHistory::TodayDay() - g_chartRangeDays;
    static std::vector<ImVec2> buyV, sellV;
    for (int pass = 0; pass < 2; ++pass)
    {
        const int cut = pass == 0 ? cutoff : 0;
        buyV.clear(); sellV.clear();
        for (const auto& p : ms)
        {
            if (p.day < cut) continue;
            buyV.push_back(ImVec2((float)p.day,  (float)((double)p.buyValue  / 10000.0)));   // copper -> gold
            sellV.push_back(ImVec2((float)p.day, (float)((double)p.sellValue / 10000.0)));
        }
        if (buyV.size() >= 2 || sellV.size() >= 2) break;
    }

    Gw2Ui::Label("Whole Trading Post: total gold in buy orders vs sell listings.", Gw2Ui::kTextSub, false, nullptr, 16.f);
    Gw2Ui::Label("Buy value", cBuy, false, nullptr, 16.f);
    ImGui::SameLine();
    Gw2Ui::Label("Sell value", cSell, false, nullptr, 16.f);

    Gw2Ui::ChartSeries series[2];
    series[0].points = buyV.data();  series[0].count = (int)buyV.size();  series[0].color = cBuy;  series[0].name = "Buy value";
    series[1].points = sellV.data(); series[1].count = (int)sellV.size(); series[1].color = cSell; series[1].name = "Sell value";

    auto fmtY = [](float gold) -> std::string {
        char b[24];
        const double g = gold;
        if (g >= 1e6)      std::snprintf(b, sizeof(b), "%.1fM", g / 1e6);
        else if (g >= 1e3) std::snprintf(b, sizeof(b), "%.0fk", g / 1e3);
        else               std::snprintf(b, sizeof(b), "%.0f", g);
        return b;
    };
    auto onHover = [&ms, cBuy, cSell](float nearestX, ImVec2) {
        const int day = (int)(nearestX + 0.5f);
        long long bv = 0, sv = 0;
        for (const auto& p : ms) if (p.day == day) { bv = p.buyValue; sv = p.sellValue; break; }
        auto goldStr = [](long long copper) -> std::string {
            const double g = (double)copper / 10000.0;
            char b[32];
            if (g >= 1e6)      std::snprintf(b, sizeof(b), "%.2fM gold", g / 1e6);
            else if (g >= 1e3) std::snprintf(b, sizeof(b), "%.1fk gold", g / 1e3);
            else               std::snprintf(b, sizeof(b), "%.0f gold", g);
            return b;
        };
        ImGui::BeginTooltip();
        Gw2Ui::Label(PriceHistory::DayLabel(day).c_str(), Gw2Ui::kGold, false, nullptr, 16.f);
        Gw2Ui::Label(("Buy value   " + goldStr(bv)).c_str(), cBuy,  false, nullptr, 16.f);
        Gw2Ui::Label(("Sell value  " + goldStr(sv)).c_str(), cSell, false, nullptr, 16.f);
        ImGui::EndTooltip();
    };

    const ImVec2 ca = ImGui::GetContentRegionAvail();
    Gw2Ui::LineChart("##mktidxchart", ImVec2(ca.x, std::max(160.f, ca.y - 8.f)), series, 2, fmtY, onHover);
}

static const char* ChartRangeLabel(int days)
{
    return days == 30 ? "30d" : days == 90 ? "90d" : days == 180 ? "180d" : "1y";
}

// At-a-glance stat tiles for the charted item: live TP price (TpPrices) + range-derived history stats. Reuses
// the Sessions-style StatGrid so it matches the rest of the app.
static void DrawChartStatTiles(App& app)
{
    (void)app;
    TpPrices::Want(g_chartItemId);
    TpPrices::Price tp;
    const bool haveTp = TpPrices::Get(g_chartItemId, tp);
    PriceHistory::Series hist;
    const bool haveHist = PriceHistory::Get(g_chartItemId, hist);

    const ImU32 cVal = IM_COL32(255, 230, 170, 255);
    const ImU32 cSub = IM_COL32(220, 210, 185, 255);

    std::vector<Gw2Ui::Stat> tiles;
    if (haveTp && tp.tradeable)
    {
        tiles.push_back({ "Sell (now)", Trading::Coins(tp.sellUnit), cVal });
        tiles.push_back({ "Buy (now)",  Trading::Coins(tp.buyUnit),  cVal });
        const int profit = Trading::FlipProfit(tp.buyUnit, tp.sellUnit);
        tiles.push_back({ "Flip margin", (profit < 0 ? "-" : "") + Trading::Coins(profit < 0 ? -profit : profit),
                          profit > 0 ? Gw2Ui::kSuccessGreen : Gw2Ui::kLossRed });
    }
    if (haveHist && hist.size() >= 2)
    {
        const int cutoff = PriceHistory::TodayDay() - g_chartRangeDays;
        int hi = 0, lo = 0; bool any = false; long long volSum = 0; int n = 0;
        for (const auto& p : hist)
        {
            if (p.day < cutoff) continue;
            const int s = p.sellAvg > 0 ? p.sellAvg : p.buyAvg;
            if (s > 0) { if (!any) { hi = lo = s; any = true; } else { hi = std::max(hi, s); lo = std::min(lo, s); } }
            volSum += (long long)p.buyQty + p.sellQty;
            ++n;
        }
        const std::string rl = ChartRangeLabel(g_chartRangeDays);
        if (any)
        {
            tiles.push_back({ rl + " high", Trading::Coins(hi), cVal });
            tiles.push_back({ rl + " low",  Trading::Coins(lo), cSub });
        }
        if (n > 0)
        {
            const long long avg = volSum / n;
            char vb[24];
            if (avg >= 1000) std::snprintf(vb, sizeof(vb), "%.1fk", avg / 1000.0);
            else             std::snprintf(vb, sizeof(vb), "%lld", avg);
            tiles.push_back({ "Avg volume/day", vb, cSub });
        }
    }
    if (!tiles.empty())
        Gw2Ui::StatGrid(ImGui::GetContentRegionAvail().x, tiles, 70.f);
}

static void DrawChartsContent(App& app)
{
    // One controls row: range chips (left) | search box (centre) | mode-switch button (right). The chips/button are
    // shorter than the search field, so vertically-centre them on it (draw the search first to learn its height).
    const float chipH = 30.f, chipFs = 16.f, cgap = 6.f;
    const float rowStartX = ImGui::GetCursorPosX();
    const float rowStartY = ImGui::GetCursorPosY();
    const float rowAvail  = ImGui::GetContentRegionAvail().x;
    const float leftW  = 58.f * 4 + cgap * 3;        // the 4 range chips
    const float rightW = 134.f;                      // the single mode-switch button
    const float rightMargin = 8.f;                   // keep the button off the window's right edge / scrollbar
    const float pad = 16.f;
    const float searchW = std::max(160.f, rowAvail - leftW - rightW - rightMargin - pad * 2.f);
    const float searchX = rowStartX + leftW + pad;

    // Search box (centre) -- drawn first so the shorter chips/button can be centred on its measured height.
    ImGui::SetCursorPos(ImVec2(searchX, rowStartY));
    Gw2Ui::SearchBox("##chartsearch", g_chartSearch, sizeof(g_chartSearch), searchW, "Search for an item to chart...");
    const float searchH = ImGui::GetItemRectSize().y;
    const float chipY = rowStartY + std::max(0.f, (searchH - chipH) * 0.5f);

    // Range chips (left), vertically centred. The range applies to BOTH views, so it stays lit in either. The day
    // VALUES are non-contiguous, so map g_chartRangeDays <-> the chip index.
    static const int  rangeDays[]   = { 30, 90, 180, 365 };
    static const char* rangeLabels[] = { "30d", "90d", "180d", "1y" };
    Gw2Ui::ChipItem rangeChips[4];
    for (int i = 0; i < 4; ++i) { rangeChips[i].label = rangeLabels[i]; rangeChips[i].width = 58.f; }
    int rangeSel = 0;
    for (int i = 0; i < 4; ++i) if (g_chartRangeDays == rangeDays[i]) { rangeSel = i; break; }
    ImGui::SetCursorPos(ImVec2(rowStartX, chipY));
    if (Gw2Ui::ChipFlow("chartrange", rangeChips, 4, &rangeSel, chipH, chipFs, cgap))
        g_chartRangeDays = rangeDays[rangeSel];

    // Mode-switch button (right), vertically centred. Volume is always shown on the item chart, so there is no
    // Volume toggle; the only mode is the item price chart vs the whole-TP market index, and the button label is
    // the view it switches TO ("Market index" on a price chart; "Price chart" on the market index).
    ImGui::SetCursorPos(ImVec2(rowStartX + rowAvail - rightW - rightMargin, chipY));
    if (Gw2Ui::ChipToggle("##cmode", g_chartMarket ? "Price chart" : "Market index", false, ImVec2(rightW, chipH), chipFs))
        g_chartMarket = !g_chartMarket;

    // Continue below the row.
    ImGui::SetCursorPos(ImVec2(rowStartX, rowStartY + searchH));
    Gw2Ui::Divider(0.f);

    // The search box is a TYPEAHEAD -- matches drop down only while typing; picking one collapses it.
    const bool searching = g_chartSearch[0] != '\0';
    if (searching)
    {
        g_chartMarket = false;   // typing always means searching for an item, not the market view
        EnsureChartResults();
        if (ItemCatalog::Count() == 0)
            Gw2Ui::Label("Loading item catalog...", Gw2Ui::kTextDim, false, nullptr, 16.f);
        else if (g_chartResults.empty())
            Gw2Ui::Label("No tradeable items match.", Gw2Ui::kTextDim, false, nullptr, 16.f);
        else
        {
            const int shown = std::min((int)g_chartResults.size(), 9);   // typeahead: just the best few matches
            Gw2Ui::PushTextScale(1.25f);                                  // bigger item names in the dropdown
            const float rowH = 24.f * Gw2Ui::TextScale();
            ImGui::SetCursorPosX(searchX);                                // drop the list directly under the search box
            ImGui::BeginChild("##charttypeahead", ImVec2(searchW, shown * rowH + 8.f), true);
            const float rowW = ImGui::GetContentRegionAvail().x;
            for (int i = 0; i < shown; ++i)
            {
                const ItemCatalog::Item& it = ItemCatalog::At(g_chartResults[i]);
                ImGui::PushID(it.id);
                const ItemUI::ResultRow r = ItemUI::DrawResultRow(i, rowW, it.id, it.icon, it.name, it.rarity);
                if (r.clicked) { g_chartItemId = it.id; g_chartItemName = it.name; g_chartSearch[0] = '\0'; }
                ImGui::PopID();
            }
            ImGui::EndChild();
            Gw2Ui::PopTextScale();
        }
        return;   // while typing, the view is just the picker
    }

    if (g_chartMarket) { DrawMarketChart(app); return; }

    if (g_chartItemId <= 0)
    {
        Gw2Ui::Label("Search for an item to chart its price history.", Gw2Ui::kTextDim, false, nullptr, 16.f);
        return;
    }

    // Selected item: title + Sessions-style stat tiles + the full-width price-history chart.
    Gw2Ui::Label(g_chartItemName.c_str(), Gw2Ui::kGold, true, nullptr, 22.f);
    ImGui::Spacing();
    DrawChartStatTiles(app);
    ImGui::Spacing();
    const ImVec2 ca = ImGui::GetContentRegionAvail();
    ItemTp::DrawPriceHistory(app, g_chartItemId, ca.x, std::max(180.f, ca.y - 6.f), g_chartRangeDays, true);
}

unsigned TradingPostNeededDomains()
{
    return g_craftWantMats ? (unsigned)AccountData::DomMaterials : 0u;
}

void DrawTradingPostContent(App& app)
{
    static const Gw2Ui::ChipItem kTpViews[4] = { { "My TP" }, { "Flip Finder" }, { "Crafting" }, { "Charts" } };
    Gw2Ui::ChipRow("##tpview", kTpViews, 4, &g_tpView, 28.f, 16.f, 8.f, 80.f);
    Gw2Ui::Divider(0.f);

    g_craftWantMats = false;   // cleared each frame; the Crafting view re-sets it while open + useOwnMaterials
    if (g_tpView == 0) DrawMyTpContent(app);
    else if (g_tpView == 1) DrawFlipFinderContent(app);
    else if (g_tpView == 2) DrawCraftingContent(app);
    else DrawChartsContent(app);
}

void OpenTradingPostTab(App& app, int view)
{
    if (view >= 0) g_tpView = view;   // 0 = My TP, 1 = Flip Finder, 2 = Crafting, 3 = Charts (-1 = leave current)
    OpenSettingsTab(app, SettingsTabTradingPost);
}
void OpenCraftingCart(App& app) { g_tpView = 2; OpenSettingsTab(app, SettingsTabTradingPost); }
