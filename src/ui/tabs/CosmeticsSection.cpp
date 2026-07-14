#include "ui/tabs/CosmeticsSection.h"

#include "app/App.h"
#include "app/AccountData.h"
#include "app/CosmeticCatalog.h"
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiGallery.h"  // the shared GalleryBrowser shell + GalleryRail
#include "ui/ApiReminder.h"
#include "ui/GuideViewer.h"        // SetClipboard + ViewerAlert (right-click copy)
#include "ui/wiki/WikiReader.h"    // Wiki::RequestOpen (Open in Wiki)
#include "util/Textures.h"         // Tex::GetTextureFromURL (cosmetic icon)
#include "util/ImageCache.h"        // ImageCache::GetCosmeticRender (bundled appearance-render hero) + Texture_t
#include "app/ItemCatalog.h"       // ItemCatalog::ById(...).chatLink (Copy chat link for item-backed cosmetics)

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace
{
    // per-scope UI state. Each scope keeps its OWN search + lock + (mounts / mist champions) rail selection so
    // switching scopes doesn't carry filters across. Indexed by (int)Kind so the kKindCount scopes never alias.
    constexpr int kKindCount = (int)CosmeticCatalog::Kind::Count;

    char        g_search[kKindCount][96] = {};
    int         g_lock[kKindCount]       = {};   // 0 = All, 1 = Locked, 2 = Unlocked
    std::string g_railSub[kKindCount];           // mounts / mist champions: selected sub ("" = All)

    const char* kLock[] = { "All", "Locked", "Unlocked" };

    // true if a row with this unlocked-state passes the current All/Locked/Unlocked filter.
    bool LockPass(int lock, bool unlocked) { return lock == 0 || (lock == 2) == unlocked; }

    std::string Lower(const char* s) { std::string o; if (s) for (const char* p = s; *p; ++p) o += (char)std::tolower((unsigned char)*p); return o; }

    // Title-case a lowercased catalog `sub` token (mount type) for display, turning the API '_' separator into a
    // space: "raptor" -> "Raptor", "roller_beetle" -> "Roller Beetle".
    std::string TitleCase(const std::string& s)
    {
        std::string o = s; bool start = true;
        for (char& c : o)
        {
            if (c == '_') c = ' ';
            if (start && std::isalpha((unsigned char)c)) c = (char)std::toupper((unsigned char)c);
            start = (c == ' ' || c == '-' || c == '/');
        }
        return o;
    }

    // ---- per-scope display strings (subtitle + the optional "Label: value" line label) ----
    const char* SubtitleFor(CosmeticCatalog::Kind k)
    {
        switch (k)
        {
            case CosmeticCatalog::Kind::Mounts:    return "Mount Skin";
            case CosmeticCatalog::Kind::Outfits:   return "Outfit";
            case CosmeticCatalog::Kind::Gliders:   return "Glider";
            case CosmeticCatalog::Kind::JadeBots:  return "Jade Bot Skin";
            case CosmeticCatalog::Kind::Skiffs:    return "Skiff Skin";
            case CosmeticCatalog::Kind::Novelties: return "Novelty";
            case CosmeticCatalog::Kind::Finishers:     return "Finisher";
            case CosmeticCatalog::Kind::MailCarriers:  return "Mail Carrier";
            case CosmeticCatalog::Kind::Minis:         return "Miniature";
            case CosmeticCatalog::Kind::MistChampions: return "Mist Champion";
            case CosmeticCatalog::Kind::Titles:        return "Title";
            case CosmeticCatalog::Kind::Emotes:        return "Emote";
            default:                               return "";
        }
    }

    // The account's unlocked id set for this kind (the live AccountData set behind the scope's domain).
    const std::set<int>& UnlockedSet(const AccountData::Model& m, CosmeticCatalog::Kind k)
    {
        switch (k)
        {
            case CosmeticCatalog::Kind::Mounts:    return m.mountsUnlocked;
            case CosmeticCatalog::Kind::Outfits:   return m.outfitsUnlocked;
            case CosmeticCatalog::Kind::Gliders:   return m.glidersUnlocked;
            case CosmeticCatalog::Kind::JadeBots:  return m.jadeBotsUnlocked;
            case CosmeticCatalog::Kind::Skiffs:    return m.skiffsUnlocked;
            case CosmeticCatalog::Kind::Novelties: return m.noveltiesUnlocked;
            case CosmeticCatalog::Kind::Finishers:     return m.finishersUnlocked;
            case CosmeticCatalog::Kind::MailCarriers:  return m.mailCarriersUnlocked;
            case CosmeticCatalog::Kind::Minis:         return m.minisUnlocked;
            case CosmeticCatalog::Kind::MistChampions: return m.mistChampionsUnlocked;
            case CosmeticCatalog::Kind::Titles:        return m.titlesUnlocked;
            case CosmeticCatalog::Kind::Emotes:        return m.emotesUnlocked;
            default:                               { static const std::set<int> kNone; return kNone; }
        }
    }

    // The fetch timestamp + size token for this kind (part of the order change-token).
    void UnlockToken(const AccountData::Model& m, CosmeticCatalog::Kind k, double& at, size_t& n)
    {
        switch (k)
        {
            case CosmeticCatalog::Kind::Mounts:    at = m.mountsAt;    n = m.mountsUnlocked.size();    break;
            case CosmeticCatalog::Kind::Outfits:   at = m.outfitsAt;   n = m.outfitsUnlocked.size();   break;
            case CosmeticCatalog::Kind::Gliders:   at = m.glidersAt;   n = m.glidersUnlocked.size();   break;
            case CosmeticCatalog::Kind::JadeBots:  at = m.jadeBotsAt;  n = m.jadeBotsUnlocked.size();  break;
            case CosmeticCatalog::Kind::Skiffs:    at = m.skiffsAt;    n = m.skiffsUnlocked.size();    break;
            case CosmeticCatalog::Kind::Novelties: at = m.noveltiesAt; n = m.noveltiesUnlocked.size(); break;
            case CosmeticCatalog::Kind::Finishers:     at = m.finishersAt;     n = m.finishersUnlocked.size();     break;
            case CosmeticCatalog::Kind::MailCarriers:  at = m.mailCarriersAt;  n = m.mailCarriersUnlocked.size();  break;
            case CosmeticCatalog::Kind::Minis:         at = m.minisAt;         n = m.minisUnlocked.size();         break;
            case CosmeticCatalog::Kind::MistChampions: at = m.mistChampionsAt; n = m.mistChampionsUnlocked.size(); break;
            case CosmeticCatalog::Kind::Titles:        at = m.titlesAt;        n = m.titlesUnlocked.size();        break;
            case CosmeticCatalog::Kind::Emotes:        at = m.emotesAt;        n = m.emotesUnlocked.size();        break;
            default:                               at = 0.0; n = 0; break;
        }
    }

    // ---- parity tooltip (the Fishing/skin tooltip flow): hero image -> title -> subtitle -> separator ->
    // "Label: value" lines -> separator -> unlock status -> separator -> footer. ----
    void CosmeticTooltip(const CosmeticCatalog::Cosmetic& c, CosmeticCatalog::Kind k, bool unlocked)
    {
        if (!Gw2Ui::TooltipBegin()) return;

        // Hero: the bundled wiki APPEARANCE render (full mount/outfit/glider screenshot) where we have one --
        // ImageCache::GetCosmeticRender yields a Texture_t so TooltipHeroImage preserves aspect; else fall back to
        // the small square render-service API icon (drawn by hand; it's square -> no aspect distortion).
        const CosmeticCatalog::Anim* an = CosmeticCatalog::AnimFor(k, c.id);   // multi-outcome tonics cycle their forms
        const Texture_t* render = an ? ImageCache::GetCosmeticRenderFrame((int)k, c.id, an->FrameAt(ImGui::GetTime()))
                                     : ImageCache::GetCosmeticRender((int)k, c.id);
        if (!render && an) render = ImageCache::GetCosmeticRender((int)k, c.id);   // a missing frame -> the static fallback
        if (render)
            Gw2Ui::TooltipHeroImage(render, 288.f, 320.f);
        else if (!c.icon.empty())
        {
            char t[48]; std::snprintf(t, sizeof(t), "TC_COSM_%d_%d", (int)k, c.id);
            if (void* tex = Tex::GetTextureFromURL(t, c.icon.c_str()))
            {
                const float hero = 192.f;
                const float x0 = ImGui::GetCursorPosX();
                ImGui::Image((ImTextureID)tex, ImVec2(hero, hero));
                ImGui::SetCursorPosX(x0);
                ImGui::Dummy(ImVec2(hero, 3.f));
            }
        }

        Gw2Ui::TooltipTitle(c.name.c_str());
        Gw2Ui::TooltipText(SubtitleFor(k));

        // DATA lines ("Label: value"), like the item / Fishing / skin tooltips.
        Gw2Ui::TooltipSeparator();
        if (k == CosmeticCatalog::Kind::Mounts && !c.sub.empty())
        { std::string l = "Mount: " + TitleCase(c.sub); Gw2Ui::TooltipText(l.c_str()); }
        else if (k == CosmeticCatalog::Kind::Novelties && !c.sub.empty())
        { std::string l = "Kind: " + c.sub; Gw2Ui::TooltipText(l.c_str()); }
        else if (k == CosmeticCatalog::Kind::MistChampions && !c.sub.empty())
        { std::string l = "Hero: " + c.sub; Gw2Ui::TooltipText(l.c_str()); }

        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted(unlocked ? "Unlocked" : "Not yet unlocked");
        if (k != CosmeticCatalog::Kind::Emotes && k != CosmeticCatalog::Kind::Titles)   // the kinds we classified
            Gw2Ui::TooltipColored(c.premium ? "Premium  -  gem store / Black Lion" : "Free  -  earned in-game",
                                  c.premium ? IM_COL32(214, 150, 224, 255) : IM_COL32(126, 196, 140, 255));
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Right-click for menu.");
        Gw2Ui::TooltipEnd();
    }

    // Right-click menu: Open in Wiki + Copy name, plus Copy chat link where the cosmetic is backed by an unlock
    // ITEM (outfits/gliders via unlock_items; jade bots via the resolved skin item) -- that item's precomputed link.
    void CosmeticMenu(const CosmeticCatalog::Cosmetic& c, bool openNow)
    {
        const bool hasLink = c.unlockItem > 0 && !ItemCatalog::ById(c.unlockItem).chatLink.empty();
        std::vector<Gw2Ui::MenuNode> nodes;
        if (!c.name.empty()) nodes.push_back({ "Open in Wiki", 1 });
        if (!c.name.empty()) nodes.push_back({ "Copy name", 2 });
        if (hasLink)         nodes.push_back({ "Copy chat link", 3 });
        switch (Gw2Ui::ContextMenuTree("##cosmmenu", nodes, openNow))
        {
            case 1: Wiki::RequestOpen(c.name); break;
            case 2: SetClipboard(c.name); ViewerAlert("Copied name."); break;
            case 3: SetClipboard(ItemCatalog::ById(c.unlockItem).chatLink);
                    ViewerAlert("Copied chat link - paste in game."); break;
            default: break;
        }
    }

    // ---- left rail: the distinct `sub` values with unl/tot, cached behind a change token. Drives Mounts (by
    // mount type) AND Mist Champions (by hero) -- both group their items by `sub`; flat (no rail) for every other
    // scope. The change token includes the KIND so switching scopes rebuilds. ----
    struct RailType { std::string sub, label; int unl = 0, tot = 0; };
    std::vector<RailType> g_subRail;
    int g_srKind = -1; uint64_t g_srVer = (uint64_t)-1; double g_srAt = -1.0; size_t g_srUnlN = (size_t)-1;

    void RebuildSubRail(const std::vector<CosmeticCatalog::Cosmetic>& all, const std::set<int>& unl,
                        CosmeticCatalog::Kind kind, const AccountData::Model& m)
    {
        double at = 0.0; size_t n = 0; UnlockToken(m, kind, at, n);
        if (g_srKind == (int)kind && g_srVer == CosmeticCatalog::Version() && g_srAt == at && g_srUnlN == n)
            return;
        g_subRail.clear();
        for (const CosmeticCatalog::Cosmetic& c : all)
        {
            if (c.sub.empty()) continue;
            RailType* rt = nullptr;
            for (RailType& r : g_subRail) if (r.sub == c.sub) { rt = &r; break; }
            if (!rt) { g_subRail.push_back({ c.sub, TitleCase(c.sub), 0, 0 }); rt = &g_subRail.back(); }
            ++rt->tot; if (unl.count(c.id)) ++rt->unl;
        }
        std::sort(g_subRail.begin(), g_subRail.end(),
                  [](const RailType& a, const RailType& b) { return a.label < b.label; });
        g_srKind = (int)kind; g_srVer = CosmeticCatalog::Version(); g_srAt = at; g_srUnlN = n;
    }

    // cached display order (indices into CosmeticCatalog::Items(kind)), rebuilt only on a change token.
    std::vector<int> g_order[kKindCount];
    uint64_t g_ovVer[kKindCount];
    double   g_ovAt[kKindCount];
    int      g_ovLock[kKindCount];
    size_t   g_ovUnlN[kKindCount];
    std::string g_ovSearch[kKindCount];
    std::string g_ovSub[kKindCount];
    int      g_unl[kKindCount];      // cached whole-catalog unlocked count (header tally)
    uint64_t g_unlKey[kKindCount];   // its token: (catalog version, unlock-set size)
    int      g_freeU[kKindCount], g_freeT[kKindCount];   // cached Free (non-premium) unlocked / total
    int      g_premU[kKindCount], g_premT[kKindCount];   // cached Premium (gem-store/BL) unlocked / total
    bool g_ovInit = false;

    void EnsureOrderTokensInit()
    {
        if (g_ovInit) return;
        for (int i = 0; i < kKindCount; ++i)
        { g_ovVer[i] = (uint64_t)-1; g_ovAt[i] = -1.0; g_ovLock[i] = -1; g_ovUnlN[i] = (size_t)-1; g_ovSearch[i] = "\x01"; g_ovSub[i] = "\x02"; g_unl[i] = 0; g_unlKey[i] = (uint64_t)-1; g_freeU[i] = g_freeT[i] = g_premU[i] = g_premT[i] = 0; }
        g_ovInit = true;
    }
}

void DrawCosmeticScope(App& app, CosmeticCatalog::Kind kind)
{
    const int ki = (int)kind;
    if (ki < 0 || ki >= kKindCount) return;
    EnsureOrderTokensInit();

    if (!AccountData::HasKey())
    { ApiReminder::Card(app, "collections", "viewing your unlocked cosmetics", /*gated*/ true); return; }
    if (!AccountData::HasScope(Api::TokenPermission::Unlocks))
    { Gw2Ui::EmptyState("Missing unlocks scope", "These collections need the unlocks permission."); return; }

    const AccountData::Model& m = AccountData::Get();
    const std::vector<CosmeticCatalog::Cosmetic>& all = CosmeticCatalog::Items(kind);
    const std::set<int>& unlocked = UnlockedSet(m, kind);
    auto isUnl = [&](const CosmeticCatalog::Cosmetic& c) { return c.isDefault || unlocked.count(c.id) != 0; };   // emotes: defaults always available

    char*      search   = g_search[ki];
    int&       lock     = g_lock[ki];
    const bool hasRail  = (kind == CosmeticCatalog::Kind::Mounts || kind == CosmeticCatalog::Kind::MistChampions);
    const bool textOnly = (kind == CosmeticCatalog::Kind::Titles || kind == CosmeticCatalog::Kind::Emotes);

    // header count: N / M unlocked across the whole catalog (rail filtering does not move it). Cached behind
    // (catalog version, unlock-set size) so it does not rescan the catalog every frame.
    const uint64_t unlKey = CosmeticCatalog::Version() * 1099511628211ull ^ (uint64_t)unlocked.size();
    if (g_unlKey[ki] != unlKey)
    {
        int u = 0, fU = 0, fT = 0, pU = 0, pT = 0;
        for (const CosmeticCatalog::Cosmetic& c : all)
        {
            const bool unl = isUnl(c);
            if (unl) ++u;
            if (c.premium) { ++pT; if (unl) ++pU; }
            else           { ++fT; if (unl) ++fU; }
        }
        g_unl[ki] = u; g_freeU[ki] = fU; g_freeT[ki] = fT; g_premU[ki] = pU; g_premT[ki] = pT;
        g_unlKey[ki] = unlKey;
    }

    // rail (Mounts by type / Mist Champions by hero): a GalleryRailNode mirror of g_subRail, "All" first.
    double uAt = 0.0; size_t uN = 0; UnlockToken(m, kind, uAt, uN);
    static Gw2Ui::GalleryRailNode s_rail;
    if (hasRail)
    {
        RebuildSubRail(all, unlocked, kind, m);
        s_rail.children.clear();
        int totU = 0, totT = 0; for (const RailType& r : g_subRail) { totU += r.unl; totT += r.tot; }
        s_rail.children.push_back({ std::string(), (kind == CosmeticCatalog::Kind::Mounts) ? "All Mounts" : "All Champions", totU, totT, {} });
        for (const RailType& r : g_subRail) s_rail.children.push_back({ r.sub, r.label, r.unl, r.tot, {} });
    }
    const std::string selSub = hasRail ? g_railSub[ki] : std::string();

    // ONE change token (catalog version / unlock fetch / lock / search / rail sub / kind): drives the shell's order.
    const std::string q = Lower(search);
    uint64_t tok = 1469598103934665603ull;
    auto mix = [&tok](uint64_t v) { tok ^= v; tok *= 1099511628211ull; };
    mix(CosmeticCatalog::Version()); mix((uint64_t)(uAt * 1000.0)); mix((uint64_t)(unsigned)lock);
    mix((uint64_t)uN); mix((uint64_t)(unsigned)ki);
    for (const char* p = search; *p; ++p) mix((unsigned char)*p);
    for (char c : selSub) mix((unsigned char)c);

    char idbuf[16]; std::snprintf(idbuf, sizeof(idbuf), "cosm.%d", ki);
    Gw2Ui::GalleryBrowser::FilterDropdown lockF{ "Lock", kLock, 3, &g_lock[ki], 132.f };

    Gw2Ui::GalleryBrowser gb;
    gb.id = idbuf;
    gb.title = SubtitleFor(kind);
    gb.have = g_unl[ki]; gb.total = (int)all.size(); gb.tallyVerb = "unlocked";
    gb.rightStatus = AccountData::Refreshing() ? "updating..." : nullptr;
    // a 3-bar Free/Premium/All breakdown for the kinds we classified premium; else the single completion bar
    // (Emotes excluded -- its count mixes defaults + untracked "Unlockable", so even one bar would read muddy).
    const bool hasSplit = g_premT[ki] > 0;
    gb.showProgress = !hasSplit && (kind != CosmeticCatalog::Kind::Emotes);
    if (hasSplit)
        gb.drawProgress = [ki, allN = (int)all.size()](float w) {
            Gw2Ui::ProgressSeg segs[2] = {
                { "Free",    g_freeU[ki], g_freeT[ki], IM_COL32(126, 196, 140, 255) },   // earned in-game (non-gem)
                { "Premium", g_premU[ki], g_premT[ki], IM_COL32(214, 150, 224, 255) },   // gem store / Black Lion
            };
            Gw2Ui::ProgressBreakdown(segs, 2, "All", g_unl[ki], allN, w);
        };
    gb.searchBuf = search; gb.searchBufSize = 96; gb.searchHint = "Search...";
    gb.filters = &lockF; gb.filterCount = 1;
    gb.gridListToggle = true; gb.textOnlyList = textOnly;   // Titles/Emotes always list (button still toggles the shared pref)
    gb.viewMode = &app.config.itemsView; gb.settingsDirty = &app.settingsDirty;
    if (hasRail)
    {
        gb.railRoot = &s_rail; gb.railSelectedKey = &g_railSub[ki];
        gb.railWidthPx = &app.config.PaneW("cosmetics.subrail", 200.f); gb.railDefaultW = 200.f;
        gb.railStyle.collapseTopLeaf = true;   // re-click a selected sub -> All (matches the old rail toggle)
    }
    gb.orderToken = tok;
    gb.gridCell = 58.f; gb.gridGap = 6.f; gb.listRowH = 38.f;
    gb.emptyText = all.empty() ? (CosmeticCatalog::IsReady() ? "No entries." : "Loading...") : "No matches.";

    gb.rebuildOrder = [&](std::vector<int>& order) {
        for (int i = 0; i < (int)all.size(); ++i)
        {
            const CosmeticCatalog::Cosmetic& c = all[i];
            if (!selSub.empty() && c.sub != selSub) continue;             // rail filter (mount type / hero)
            if (!LockPass(lock, isUnl(c))) continue;
            if (!q.empty() && c.keyLower.find(q) == std::string::npos) continue;
            order.push_back(i);
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const bool ua = isUnl(all[a]), ub = isUnl(all[b]);
            if (ua != ub) return ua;                 // unlocked first
            return all[a].name < all[b].name;
        });
    };
    gb.drawGridCell = [&](int idx, ImVec2 cmin, float cs) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const CosmeticCatalog::Cosmetic& c = all[idx];
        const bool u = isUnl(c);
        ImGui::InvisibleButton("##cc", ImVec2(cs, cs));
        const bool hov = ImGui::IsItemHovered();
        if (!c.icon.empty())
        {
            char t[48]; std::snprintf(t, sizeof(t), "TC_COSM_%d_%d", ki, c.id);
            if (void* tex = Tex::GetTextureFromURL(t, c.icon.c_str()))
                dl->AddImage((ImTextureID)tex, ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cs - 1.f, cmin.y + cs - 1.f));
        }
        dl->AddRect(cmin, ImVec2(cmin.x + cs, cmin.y + cs), u ? Gw2Ui::kGold : IM_COL32(90, 90, 90, 200), 2.f, 0, u ? 1.6f : 1.f);
        if (!u) dl->AddRectFilled(ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cs - 1.f, cmin.y + cs - 1.f), IM_COL32(0, 0, 0, 150), 2.f);
        if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); CosmeticTooltip(c, kind, u); }
        CosmeticMenu(c, hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    };
    gb.drawListRow = [&](int idx, const Gw2Ui::RowHotspot& row) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const CosmeticCatalog::Cosmetic& c = all[idx];
        const bool u = isUnl(c);
        const float ic = row.height - 8.f, statusW = 120.f;
        const ImVec2 p = row.min;
        if (!c.icon.empty())
        {
            char t[48]; std::snprintf(t, sizeof(t), "TC_COSM_%d_%d", ki, c.id);
            if (void* tex = Tex::GetTextureFromURL(t, c.icon.c_str()))
                dl->AddImage((ImTextureID)tex, ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic));
            dl->AddRect(ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic), u ? Gw2Ui::kGold : IM_COL32(90, 90, 90, 200), 0.f, 0, 1.2f);
        }
        const char* st = u ? "Unlocked" : "Locked";
        if (kind == CosmeticCatalog::Kind::Emotes)   // Default / Unlocked / Locked / (API-untracked) Unlockable
            st = c.isDefault ? "Default" : (c.unlockItem == 0 ? "Unlockable" : (unlocked.count(c.id) ? "Unlocked" : "Locked"));
        Gw2Ui::RowLabel(dl, row, row.width - statusW, 8.f, st, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                        u ? Gw2Ui::kGold : Gw2Ui::kTextDim, false, nullptr, 16.f);
        std::string nm = c.name;
        if (!c.sub.empty() && (hasRail || kind == CosmeticCatalog::Kind::Emotes))
            { nm += "   "; nm += hasRail ? TitleCase(c.sub) : c.sub; }   // mount type / emote command
        Gw2Ui::RowLabel(dl, row, c.icon.empty() ? 12.f : ic + 12.f, statusW + 12.f, nm.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        u ? IM_COL32(228, 222, 204, 255) : Gw2Ui::kTextDim, false, nullptr, 18.f);
        if (row.hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); CosmeticTooltip(c, kind, u); }
        CosmeticMenu(c, row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    };

    Gw2Ui::DrawGalleryBrowser(gb);
}
