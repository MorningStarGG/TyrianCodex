#include "ui/tabs/WardrobeSection.h"

#include "app/App.h"
#include "app/AccountData.h"
#include "app/SkinCatalog.h"
#include "app/ItemCatalog.h"       // dye-unlock item chat link
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiGallery.h"  // the shared GalleryBrowser shell
#include "render/glyphs/Glyphs.h"  // Render::DrawGlyph (armor rail caret)
#include "ui/ApiReminder.h"
#include "ui/GuideViewer.h"        // SetClipboard + ViewerAlert (right-click copy)
#include "ui/items/ItemRender.h"   // ItemUI::RarityColor
#include "ui/wiki/WikiReader.h"    // Wiki::RequestOpen (Open in Wiki)
#include "util/Textures.h"         // Tex::GetTextureFromURL (skin icon)
#include "util/ImageCache.h"       // GetSkinImage (bundled appearance render)
#include "util/ChatLink.h"         // ChatLink::Skin (wardrobe chat link)
#include "ui/tabs/CosmeticsSection.h"   // DrawCosmeticScope (the Outfits Wardrobe sub-chip)
#include "ui/tabs/LegendaryArmorySection.h"  // DrawLegendaryArmoryContent (the Legendary Wardrobe sub-chip)
#include "app/CosmeticCatalog.h"    // CosmeticCatalog::Kind::Outfits

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace
{
    int  g_view = 0;            // 0 = Skins, 1 = Dyes, 2 = Outfits, 3 = Legendary
    char g_search[96] = "";
    int  g_skinType = 0;        // 0 All, 1 Armor, 2 Weapon, 3 Back, 4 Gathering
    int  g_lockFilter = 0;      // 0 = All, 1 = Locked, 2 = Unlocked

    const char* kTypeApi[]  = { "",    "Armor", "Weapon", "Back", "Gathering" };
    const char* kLock[]     = { "All", "Locked", "Unlocked" };

    // true if a row with this unlocked-state passes the current All/Locked/Unlocked filter.
    bool LockPass(bool unlocked) { return g_lockFilter == 0 || (g_lockFilter == 2) == unlocked; }

    // Right-click copy menu for a dye: hex / name / RGB, plus the dye-unlock item's chat link when known.
    void DyeCopyMenu(const Gw2Ui::Dye& d, bool openNow)
    {
        char hex[8];  std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", d.r, d.g, d.b);
        char rgb[24]; std::snprintf(rgb, sizeof(rgb), "%d, %d, %d", d.r, d.g, d.b);
        const std::string link = (d.item > 0) ? ItemCatalog::ById(d.item).chatLink : std::string();
        std::vector<Gw2Ui::MenuNode> nodes;
        nodes.push_back({ "Copy hex", 1 });
        if (d.name && d.name[0]) nodes.push_back({ "Copy name", 2 });
        nodes.push_back({ "Copy RGB", 3 });
        if (!link.empty()) nodes.push_back({ "Copy chat link", 4 });
        switch (Gw2Ui::ContextMenuTree("##dyecopy", nodes, openNow))
        {
            case 1: SetClipboard(hex);                 ViewerAlert("Copied dye hex."); break;
            case 2: SetClipboard(d.name ? d.name : ""); ViewerAlert("Copied dye name."); break;
            case 3: SetClipboard(rgb);                 ViewerAlert("Copied dye RGB."); break;
            case 4: SetClipboard(link);                ViewerAlert("Copied dye chat link."); break;
            default: break;
        }
    }

    // Right-click copy menu for a skin: just the name (a skin is not an item -> no chat link).
    void SkinCopyMenu(const SkinCatalog::Skin& s, bool openNow)
    {
        std::vector<Gw2Ui::MenuNode> nodes;
        if (!s.name.empty()) nodes.push_back({ "Open in Wiki", 1 });
        if (!s.name.empty()) nodes.push_back({ "Copy name", 2 });
        if (s.id > 0)        nodes.push_back({ "Copy chat link", 3 });
        switch (Gw2Ui::ContextMenuTree("##skincopy", nodes, openNow))
        {
            case 1: Wiki::RequestOpen(s.name); break;
            case 2: SetClipboard(s.name); ViewerAlert("Copied skin name."); break;
            case 3: SetClipboard(ChatLink::Skin(s.id)); ViewerAlert("Copied chat link - paste in game."); break;
            default: break;
        }
    }

    // Left-rail selection, captured as a (type, sub, slot) triple so one selection model serves every chip:
    //   g_railType : "" / "Armor" / "Weapon" / "Back" / "Gathering"
    //   g_railSub  : armor weight class, OR weapon type, OR gathering tool ("" = whole type)
    //   g_railSlot : armor slot within the weight ("" = whole weight; only meaningful while g_railType==Armor)
    // When the active chip is Armor the rail implies type=Armor; when Weapon, type=Weapon; when All the selected
    // node sets all three. Switching/leaving a chip clears the triple.
    const char* kWeights[] = { "Light", "Medium", "Heavy", "Clothing" };
    std::string g_railType;    // "" = all
    std::string g_railSub;     // weight / weapon-type / tool ("" = whole type)
    std::string g_railSlot;    // armor slot ("" = whole weight)
    // Armor "browse by set": when non-empty the grid filters type=="Armor" && set==g_railSet across all weights,
    // and the weight/slot selection is suppressed (the two are mutually exclusive -- see ClearRailSel callers).
    std::string g_railSet;     // chosen armor set name ("" = none)
    bool g_setsMode = false;   // Armor "Sets" container selected: browse all set-bearing armor (no specific set)

    void ClearRailSel() { g_railType.clear(); g_railSub.clear(); g_railSlot.clear(); g_railSet.clear(); g_setsMode = false; }

    // cached skin display order (indices into SkinCatalog::Items()), rebuilt only on a change token (F5)
    std::vector<int> g_skinOrder;
    uint64_t g_soVer = (uint64_t)-1; double g_soAt = -1.0; int g_soType = -1; int g_soLock = -1;
    std::string g_soSearch = "\x01"; size_t g_soUnlN = (size_t)-1;
    std::string g_soRType = "\x01", g_soRSub = "\x01", g_soRSlot = "\x01", g_soRSet = "\x01";   // rail selection (part of the order token)

    // ---- cached rail model covering all four skin types -> their sub-tree, with unl/tot per node:
    //   Armor      -> weight class -> slot   (3 levels deep)
    //   Weapon     -> weapon type           (2 levels)
    //   Gathering  -> tool type             (2 levels)
    //   Back       -> no children           (1 level)
    // Rebuilt only behind a change token (catalog version / account change) so the rail never scans the catalog
    // per frame. The Armor weights keep a fixed Light/Medium/Heavy/Clothing order + head->toe slot order; the
    // weapon/gathering subs sort alphabetically.
    struct RailLeaf  { std::string name; int unl = 0, tot = 0; };                      // armor slot / weapon type / tool
    struct RailSub   { std::string name; int unl = 0, tot = 0; std::vector<RailLeaf> leaves; }; // armor weight (leaves=slots)
    // `sets` is only populated for the Armor type (g_rail[0]): the distinct armor SET names with unl/tot, sorted
    // alphabetically (armor skins with an empty set are omitted). setsUnl/setsTot = the aggregate over set-bearing
    // pieces (so the "Sets" header counts only pieces that have a set, not all armor).
    struct RailType  { int unl = 0, tot = 0, setsUnl = 0, setsTot = 0;
                       std::vector<RailSub> subs; std::vector<RailLeaf> sets; }; // one per skin type
    // g_rail[0]=Armor (subs=weights, each with slot leaves), [1]=Weapon (subs=types, no leaves),
    // [2]=Back (no subs), [3]=Gathering (subs=tools, no leaves).
    RailType g_rail[4];
    const char* kRailType[4] = { "Armor", "Weapon", "Back", "Gathering" };
    uint64_t g_arVer = (uint64_t)-1; double g_arAt = -1.0; size_t g_arUnlN = (size_t)-1;

    int WeightIndex(const std::string& w)
    { for (int i = 0; i < 4; ++i) if (w == kWeights[i]) return i; return -1; }

    int SlotRank(const std::string& s)   // head->toe ordering for the slot rows
    {
        static const char* k[] = { "Helm", "HelmAquatic", "Shoulders", "Coat", "Gloves", "Leggings", "Boots" };
        for (int i = 0; i < (int)(sizeof(k) / sizeof(k[0])); ++i) if (s == k[i]) return i;
        return 99;
    }

    int RarityRank(const std::string& r)   // tier order for the Back rail's rarity rows
    {
        static const char* k[] = { "Junk", "Basic", "Fine", "Masterwork", "Rare", "Exotic", "Ascended", "Legendary" };
        for (int i = 0; i < (int)(sizeof(k) / sizeof(k[0])); ++i) if (r == k[i]) return i;
        return 99;
    }

    int RailTypeIndex(const std::string& t)
    { for (int i = 0; i < 4; ++i) if (t == kRailType[i]) return i; return -1; }

    // find-or-append a RailSub by name in a flat (no-leaf) type bucket; tallies unl/tot.
    RailSub& FindSub(RailType& rt, const std::string& name)
    {
        for (RailSub& s : rt.subs) if (s.name == name) return s;
        rt.subs.push_back({ name, 0, 0, {} });
        return rt.subs.back();
    }

    void RebuildRail(const std::vector<SkinCatalog::Skin>& all,
                     const std::function<bool(int)>& unlocked, const AccountData::Model& m)
    {
        if (g_arVer == SkinCatalog::Version() && g_arAt == m.skinsAt && g_arUnlN == m.skinsUnlocked.size()) return;
        for (int i = 0; i < 4; ++i) g_rail[i] = RailType{};
        // Armor is pre-seeded with its four weight buckets in fixed order so empty weights are simply skipped.
        for (int i = 0; i < 4; ++i) g_rail[0].subs.push_back({ kWeights[i], 0, 0, {} });

        for (const SkinCatalog::Skin& s : all)
        {
            if (!s.wardrobe) continue;
            const int ti = RailTypeIndex(s.type);
            if (ti < 0) continue;
            const bool unl = unlocked(s.id);
            RailType& rt = g_rail[ti];
            ++rt.tot; if (unl) ++rt.unl;

            if (ti == 0)   // Armor: weight -> slot (+ a flat set tally for "browse by set")
            {
                if (!s.set.empty())   // set names tally across all weights; empty-set pieces are simply omitted
                {
                    ++g_rail[0].setsTot; if (unl) ++g_rail[0].setsUnl;
                    RailLeaf* set = nullptr;
                    for (RailLeaf& l : g_rail[0].sets) if (l.name == s.set) { set = &l; break; }
                    if (!set) { g_rail[0].sets.push_back({ s.set, 0, 0 }); set = &g_rail[0].sets.back(); }
                    ++set->tot; if (unl) ++set->unl;
                }
                const int wi = WeightIndex(s.weight);
                if (wi < 0) continue;
                RailSub& wgt = g_rail[0].subs[wi];
                ++wgt.tot; if (unl) ++wgt.unl;
                RailLeaf* leaf = nullptr;
                for (RailLeaf& l : wgt.leaves) if (l.name == s.subtype) { leaf = &l; break; }
                if (!leaf) { wgt.leaves.push_back({ s.subtype, 0, 0 }); leaf = &wgt.leaves.back(); }
                ++leaf->tot; if (unl) ++leaf->unl;
            }
            else if (ti == 1 || ti == 3)   // Weapon / Gathering: one flat sub level by subtype
            {
                if (s.subtype.empty()) continue;
                RailSub& sub = FindSub(rt, s.subtype);
                ++sub.tot; if (unl) ++sub.unl;
            }
            else if (ti == 2)   // Back: one flat sub level by RARITY (back items have no subtype)
            {
                if (s.rarity.empty()) continue;
                RailSub& sub = FindSub(rt, s.rarity);
                ++sub.tot; if (unl) ++sub.unl;
            }
        }

        // Order: armor slots head->toe; weapon/gathering subs alphabetically.
        for (RailSub& wgt : g_rail[0].subs)
            std::sort(wgt.leaves.begin(), wgt.leaves.end(),
                      [](const RailLeaf& a, const RailLeaf& b) {
                          const int ra = SlotRank(a.name), rb = SlotRank(b.name);
                          if (ra != rb) return ra < rb;
                          return a.name < b.name;
                      });
        for (int i = 1; i <= 3; ++i)
            std::sort(g_rail[i].subs.begin(), g_rail[i].subs.end(),
                      [i](const RailSub& a, const RailSub& b) {   // weapon/gathering: alphabetical; back: rarity tier
                          if (i == 2) { const int ra = RarityRank(a.name), rb = RarityRank(b.name); if (ra != rb) return ra < rb; }
                          return a.name < b.name;
                      });
        std::sort(g_rail[0].sets.begin(), g_rail[0].sets.end(),
                  [](const RailLeaf& a, const RailLeaf& b) { return a.name < b.name; });   // armor sets alphabetically

        g_arVer = SkinCatalog::Version(); g_arAt = m.skinsAt; g_arUnlN = m.skinsUnlocked.size();
    }

    // pretty armor-slot label (the catalog subtype is the raw API slot, e.g. "HelmAquatic").
    const char* SlotLabel(const std::string& s)
    {
        if (s == "HelmAquatic") return "Aquatic Helm";
        return s.c_str();
    }

    // cached dye display order (indices into app.dyes)
    std::vector<int> g_dyeOrder;
    double g_doAt = -1.0; int g_doLock = -1; std::string g_doSearch = "\x01"; size_t g_doUnlN = (size_t)-1; size_t g_doN = (size_t)-1;

    std::string Lower(const char* s) { std::string o; if (s) for (const char* p = s; *p; ++p) o += (char)std::tolower((unsigned char)*p); return o; }

    // a rarity worth showing on its own line: not blank and not the unremarkable "Basic" tier (omitting Basic
    // fixes the old dangling "Basic" line).
    bool NotableRarity(const std::string& r) { return !r.empty() && r != "Basic"; }

    void SkinTooltip(const SkinCatalog::Skin& s, bool unlocked)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        if (const SkinCatalog::Anim* an = SkinCatalog::AnimFor(s.id))            // an animated APNG render -> cycle frames
            Gw2Ui::TooltipHeroImageFrame(ImageCache::GetSkinImage(s.id), 256.f, 320.f, an->frames, an->FrameAt(ImGui::GetTime()));
        else
            Gw2Ui::TooltipHeroImage(ImageCache::GetSkinImage(s.id), 256.f, 320.f);   // bundled appearance render (else just text)
        Gw2Ui::TooltipTitle(s.name.c_str());

        // subtitle: the skin's broad category (armor folds the weight in -> "Heavy Armor").
        std::string subtitle;
        if      (s.type == "Armor")     subtitle = (s.weight.empty() ? std::string("Armor") : (s.weight + " Armor"));
        else if (s.type == "Weapon")    subtitle = "Weapon";
        else if (s.type == "Back")      subtitle = "Back Item";
        else if (s.type == "Gathering") subtitle = "Gathering Tool";
        else                            subtitle = s.type;
        if (!subtitle.empty()) Gw2Ui::TooltipText(subtitle.c_str());

        // DATA lines ("Label: value"), like the item / Fishing tooltips.
        Gw2Ui::TooltipSeparator();
        if (s.type == "Armor")
        {
            if (!s.subtype.empty()) { std::string l = "Slot: " + s.subtype; Gw2Ui::TooltipText(l.c_str()); }
            if (!s.weight.empty())  { std::string l = "Weight: " + s.weight; Gw2Ui::TooltipText(l.c_str()); }
            if (!s.set.empty())     { std::string l = "Set: " + s.set; Gw2Ui::TooltipText(l.c_str()); }
        }
        else if (s.type == "Weapon")
        {
            if (!s.subtype.empty()) { std::string l = "Type: " + s.subtype; Gw2Ui::TooltipText(l.c_str()); }
        }
        if (NotableRarity(s.rarity)) { std::string l = "Rarity: " + s.rarity; Gw2Ui::TooltipText(l.c_str()); }

        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted(unlocked ? "Unlocked" : "Not yet unlocked");
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Right-click for menu.");
        Gw2Ui::TooltipEnd();
    }

    // ---- skin rail selection-key codec: encode the (type, sub, slot, set) selection as one string for GalleryRail.
    // Fields are '\x1f'-delimited "type\x1fsub\x1fslot\x1fset"; "" = All Skins. The Armor "Sets" container uses the
    // sentinel set "\x01" (browse all set-bearing armor); a specific set carries its name in the set field.
    const char kSep = '\x1f';
    const char kSetsMark = '\x01';

    std::string EncodeSkinKey(const std::string& type, const std::string& sub, const std::string& slot, const std::string& set)
    {
        if (type.empty() && sub.empty() && slot.empty() && set.empty()) return std::string();
        std::string k; k.reserve(type.size() + sub.size() + slot.size() + set.size() + 3);
        k = type; k += kSep; k += sub; k += kSep; k += slot; k += kSep; k += set;
        return k;
    }
    void DecodeSkinKey(const std::string& key, std::string& type, std::string& sub, std::string& slot, std::string& set)
    {
        type.clear(); sub.clear(); slot.clear(); set.clear();
        std::string* f[4] = { &type, &sub, &slot, &set };
        int fi = 0;
        for (size_t i = 0; i <= key.size() && fi < 4; )
        {
            const size_t j = key.find(kSep, i);
            const size_t end = (j == std::string::npos) ? key.size() : j;
            *f[fi++] = key.substr(i, end - i);
            if (j == std::string::npos) break;
            i = j + 1;
        }
    }

    // Build the GalleryRail tree for the active type-chip from the cached g_rail; node keys encode (type,sub,slot,set).
    void BuildSkinRail(int chip, Gw2Ui::GalleryRailNode& root)
    {
        root.children.clear();
        auto armorSubtree = [&](Gw2Ui::GalleryRailNode& parent) {
            const RailType& rt = g_rail[0];
            for (int i = 0; i < 4; ++i)
            {
                const RailSub& wgt = rt.subs[i];
                if (wgt.tot == 0) continue;
                Gw2Ui::GalleryRailNode wn{ EncodeSkinKey("Armor", kWeights[i], "", ""), kWeights[i], wgt.unl, wgt.tot, {} };
                for (const RailLeaf& sl : wgt.leaves)
                    wn.children.push_back({ EncodeSkinKey("Armor", kWeights[i], sl.name, ""), SlotLabel(sl.name), sl.unl, sl.tot, {} });
                parent.children.push_back(std::move(wn));
            }
            if (!rt.sets.empty())   // the "browse by set" container -> set leaves
            {
                Gw2Ui::GalleryRailNode sn{ EncodeSkinKey("Armor", "", "", std::string(1, kSetsMark)), "Sets", rt.setsUnl, rt.setsTot, {} };
                for (const RailLeaf& set : rt.sets)
                    sn.children.push_back({ EncodeSkinKey("Armor", "", "", set.name), set.name, set.unl, set.tot, {} });
                parent.children.push_back(std::move(sn));
            }
        };
        auto flatSubs = [&](int ti, Gw2Ui::GalleryRailNode& parent) {
            const RailType& rt = g_rail[ti];
            const char* type = kRailType[ti];
            for (const RailSub& sub : rt.subs)
                if (sub.tot > 0)
                    parent.children.push_back({ EncodeSkinKey(type, sub.name, "", ""), sub.name, sub.unl, sub.tot, {} });
        };

        if (chip == 0)   // All: All Skins + each type (with its subtree)
        {
            int totAll = 0, unlAll = 0; for (int i = 0; i < 4; ++i) { totAll += g_rail[i].tot; unlAll += g_rail[i].unl; }
            root.children.push_back({ std::string(), "All Skins", unlAll, totAll, {} });
            for (int ti = 0; ti < 4; ++ti)
            {
                const RailType& rt = g_rail[ti];
                if (rt.tot == 0) continue;
                Gw2Ui::GalleryRailNode tn{ EncodeSkinKey(kRailType[ti], "", "", ""), kRailType[ti], rt.unl, rt.tot, {} };
                if (ti == 0) armorSubtree(tn); else flatSubs(ti, tn);
                root.children.push_back(std::move(tn));
            }
        }
        else if (chip == 1)   // Armor: All Armor + weights->slots + Sets
        {
            root.children.push_back({ EncodeSkinKey("Armor", "", "", ""), "All Armor", g_rail[0].unl, g_rail[0].tot, {} });
            armorSubtree(root);
        }
        else   // Weapon(2) / Back(3) / Gathering(4): All X + flat subs
        {
            const int ti = chip - 1;
            const char* allLbl = (chip == 2) ? "All Weapons" : (chip == 3) ? "All Back Items" : "All Gathering";
            root.children.push_back({ EncodeSkinKey(kRailType[ti], "", "", ""), allLbl, g_rail[ti].unl, g_rail[ti].tot, {} });
            flatSubs(ti, root);
        }
    }

    // ---- Skins gallery (on the shared shell; the type-chips sit above it, the rail via GalleryRail) ----
    void DrawSkins(App& app, const AccountData::Model& m, float /*width*/)
    {
        const std::vector<SkinCatalog::Skin>& all = SkinCatalog::Items();
        auto unlocked = [&](int id) { return m.skinsUnlocked.find(id) != m.skinsUnlocked.end(); };
        const std::string q = Lower(g_search);
        const float scale = Gw2Ui::TextScale();

        // type chips (5) -- a row above the shell; the lock filter + search + Grid/List live in the shell toolbar.
        static const Gw2Ui::ChipItem kTypeChips[] = {
            { "All" }, { "Armor" }, { "Weapon" }, { "Back" }, { "Gathering" }
        };
        if (Gw2Ui::ChipRow("##wst", kTypeChips, 5, &g_skinType, 28.f, 16.f))
            ClearRailSel();   // switching chips drops the rail selection

        int unl = 0, tot = 0, fU = 0, fT = 0, pU = 0, pT = 0;
        for (const SkinCatalog::Skin& s : all)
            if (s.wardrobe)
            { ++tot; const bool u = unlocked(s.id); if (u) ++unl; if (s.premium) { ++pT; if (u) ++pU; } else { ++fT; if (u) ++fU; } }
        RebuildRail(all, unlocked, m);

        // cached rail tree (rebuilt only when the catalog / account / chip changes).
        static Gw2Ui::GalleryRailNode s_tree; static uint64_t s_treeTok = ~0ull;
        const uint64_t treeTok = SkinCatalog::Version()
            ^ ((uint64_t)(m.skinsAt * 1000.0) << 1) ^ ((uint64_t)m.skinsUnlocked.size() << 20) ^ ((uint64_t)(unsigned)g_skinType << 40);
        if (s_treeTok != treeTok) { BuildSkinRail(g_skinType, s_tree); s_treeTok = treeTok; }

        // Effective (type, sub, slot, set) feeding the grid filter (chip type + rail selection), plus sets-mode.
        std::string selType, selSub, selSlot;
        if (g_skinType == 0) { selType = g_railType; selSub = g_railSub; selSlot = g_railSlot; }
        else                 { selType = kTypeApi[g_skinType]; selSub = g_railSub; selSlot = g_railSlot; }
        const bool setsMode = (selType == "Armor") && g_setsMode;
        const std::string selSet     = (selType == "Armor" && !setsMode) ? g_railSet : std::string();
        const std::string selWeight  = (selType == "Armor" && selSet.empty() && !setsMode) ? selSub : std::string();
        const std::string selSubtype = (selType == "Weapon" || selType == "Gathering") ? selSub : std::string();
        const std::string selRarity  = (selType == "Back") ? selSub : std::string();
        const std::string selArmSlot = (selType == "Armor" && selSet.empty() && !setsMode) ? selSlot : std::string();

        // the rail's current selection key (matches a tree node); GalleryRail (in the shell) reads + updates it.
        const std::string keyType = (g_skinType == 0) ? g_railType : std::string(kTypeApi[g_skinType]);
        std::string railKey = setsMode ? EncodeSkinKey("Armor", "", "", std::string(1, kSetsMark))
                                       : EncodeSkinKey(keyType, g_railSub, g_railSlot, g_railSet);

        uint64_t tok = 1469598103934665603ull;
        auto mix = [&tok](uint64_t v) { tok ^= v; tok *= 1099511628211ull; };
        mix(SkinCatalog::Version()); mix((uint64_t)(m.skinsAt * 1000.0)); mix((uint64_t)(unsigned)g_skinType);
        mix((uint64_t)(unsigned)g_lockFilter); mix((uint64_t)m.skinsUnlocked.size());
        for (const char* p = g_search; *p; ++p) mix((unsigned char)*p);
        for (char c : railKey) mix((unsigned char)c);

        Gw2Ui::GalleryBrowser::FilterDropdown lockF{ "Lock", kLock, 3, &g_lockFilter, 132.f };

        Gw2Ui::GalleryBrowser gb;
        gb.id = "ward.skins";
        gb.title = "Skins";
        gb.have = unl; gb.total = tot; gb.tallyVerb = "unlocked";
        gb.rightStatus = SkinCatalog::Refreshing() ? "updating..." : nullptr;
        gb.showProgress = (pT == 0);   // single bar if no premium split; else the Free/Premium 3-bar below
        if (pT > 0)
            gb.drawProgress = [unl, fU, fT, pU, pT, tot](float w) {
                Gw2Ui::ProgressSeg segs[2] = {
                    { "Free",    fU, fT, IM_COL32(126, 196, 140, 255) },   // an in-game route exists
                    { "Premium", pU, pT, IM_COL32(214, 150, 224, 255) },   // gem-store only
                };
                Gw2Ui::ProgressBreakdown(segs, 2, "All", unl, tot, w);
            };
        gb.searchBuf = g_search; gb.searchBufSize = (int)sizeof(g_search); gb.searchHint = "Search skins...";
        gb.filters = &lockF; gb.filterCount = 1;
        gb.viewMode = &app.config.itemsView; gb.settingsDirty = &app.settingsDirty;
        gb.railRoot = &s_tree; gb.railSelectedKey = &railKey;
        gb.railWidthPx = &app.config.PaneW("wardrobe.rail", 200.f); gb.railDefaultW = 200.f;
        gb.railStyle.collapseTopLeaf = true;
        gb.orderToken = tok;
        gb.gridCell = 58.f; gb.gridGap = 6.f; gb.listRowH = 38.f;
        gb.emptyText = all.empty() ? (SkinCatalog::IsReady() ? "No skins." : "Loading skins...") : "No matches.";

        gb.rebuildOrder = [&](std::vector<int>& order) {
            for (int i = 0; i < (int)all.size(); ++i)
            {
                const SkinCatalog::Skin& s = all[i];
                if (!s.wardrobe) continue;
                if (!selType.empty()    && s.type != selType)       continue;   // type (chip or All-rail node)
                if (setsMode            && s.set.empty())           continue;   // Sets overview: only set-bearing armor
                if (!selSet.empty()     && s.set != selSet)         continue;   // armor: browse by set
                if (!selWeight.empty()  && s.weight != selWeight)   continue;   // armor: weight class
                if (!selArmSlot.empty() && s.subtype != selArmSlot) continue;   // armor: slot within the weight
                if (!selSubtype.empty() && s.subtype != selSubtype) continue;   // weapon type / gathering tool
                if (!selRarity.empty()  && s.rarity != selRarity)   continue;   // back: rarity tier
                if (!LockPass(unlocked(s.id))) continue;
                if (!q.empty() && s.keyLower.find(q) == std::string::npos) continue;
                order.push_back(i);
            }
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                const bool ua = unlocked(all[a].id), ub = unlocked(all[b].id);
                if (ua != ub) return ua;             // unlocked first
                return all[a].name < all[b].name;
            });
        };
        gb.drawGridCell = [&](int idx, ImVec2 cmin, float cs) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const SkinCatalog::Skin& s = all[idx];
            const bool unl = unlocked(s.id);
            ImGui::InvisibleButton("##sc", ImVec2(cs, cs));
            const bool hov = ImGui::IsItemHovered();
            if (!s.icon.empty())
            {
                char t[40]; std::snprintf(t, sizeof(t), "TC_SKIN_%d", s.id);
                if (void* tex = Tex::GetTextureFromURL(t, s.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cs - 1.f, cmin.y + cs - 1.f));
            }
            dl->AddRect(cmin, ImVec2(cmin.x + cs, cmin.y + cs), ItemUI::RarityColor(s.rarity), 2.f, 0, 1.4f);
            if (!unl) dl->AddRectFilled(ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cs - 1.f, cmin.y + cs - 1.f), IM_COL32(0, 0, 0, 150), 2.f);
            if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); SkinTooltip(s, unl); }
            SkinCopyMenu(s, hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        };
        gb.drawListRow = [&](int idx, const Gw2Ui::RowHotspot& row) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const SkinCatalog::Skin& s = all[idx];
            const bool unl = unlocked(s.id);
            const float ic = row.height - 8.f, statusW = 120.f * scale;
            const ImVec2 p = row.min;
            if (!s.icon.empty())
            {
                char t[40]; std::snprintf(t, sizeof(t), "TC_SKIN_%d", s.id);
                if (void* tex = Tex::GetTextureFromURL(t, s.icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic));
            }
            dl->AddRect(ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic), ItemUI::RarityColor(s.rarity), 0.f, 0, 1.2f);
            Gw2Ui::RowLabel(dl, row, row.width - statusW, 8.f, unl ? "Unlocked" : "Locked", Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                            unl ? Gw2Ui::kGold : Gw2Ui::kTextDim, false, nullptr, 16.f);
            std::string nm = s.name; if (!s.subtype.empty()) { nm += "   "; nm += s.subtype; }
            Gw2Ui::RowLabel(dl, row, ic + 12.f, statusW + 12.f, nm.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                            unl ? ItemUI::RarityColor(s.rarity) : Gw2Ui::kTextDim, false, nullptr, 18.f);
            if (row.hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); SkinTooltip(s, unl); }
            SkinCopyMenu(s, row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        };

        Gw2Ui::DrawGalleryBrowser(gb);

        // decode the (possibly clicked) rail selection back into the statics for next frame's filter.
        std::string dt, ds, dsl, dse; DecodeSkinKey(railKey, dt, ds, dsl, dse);
        g_setsMode = (dse == std::string(1, kSetsMark));
        if (g_skinType == 0) g_railType = dt;
        g_railSub = ds; g_railSlot = dsl; g_railSet = g_setsMode ? std::string() : dse;
    }

    // ---- Dyes gallery ----
    // Dyes -- a flat swatch gallery on the shared Gw2Ui::GalleryBrowser shell (no rail).
    void DrawDyes(App& app, const AccountData::Model& m)
    {
        const std::vector<Gw2Ui::Dye>& dyes = app.dyes;
        auto unlocked = [&](int id) { return m.dyesUnlocked.find(id) != m.dyesUnlocked.end(); };
        const std::string q = Lower(g_search);
        const float scale = Gw2Ui::TextScale();

        int unl = 0;
        for (const Gw2Ui::Dye& d : dyes) if (unlocked(d.id)) ++unl;

        uint64_t tok = 1469598103934665603ull;
        auto mix = [&tok](uint64_t v) { tok ^= v; tok *= 1099511628211ull; };
        mix((uint64_t)(m.dyesAt * 1000.0)); mix((uint64_t)(unsigned)g_lockFilter);
        for (const char* p = g_search; *p; ++p) mix((unsigned char)*p);
        mix((uint64_t)m.dyesUnlocked.size()); mix((uint64_t)dyes.size());

        auto dyeTip = [&](const Gw2Ui::Dye& d, bool ul) {
            if (!Gw2Ui::TooltipBegin()) return;
            Gw2Ui::TooltipTitle(d.name ? d.name : "Dye");
            { ImDrawList* tdl = ImGui::GetWindowDrawList(); const ImVec2 p = ImGui::GetCursorScreenPos();
              const float w = 200.f, h = 48.f;
              tdl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(d.r, d.g, d.b, 255), 4.f);
              tdl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 70), 4.f);
              ImGui::Dummy(ImVec2(w, h)); }
            Gw2Ui::TooltipSeparator();
            char hex[40]; std::snprintf(hex, sizeof(hex), "#%02X%02X%02X   (RGB %d, %d, %d)", d.r, d.g, d.b, d.r, d.g, d.b);
            Gw2Ui::TooltipMuted(hex);   // the dye's cloth-material color (what the swatch shows)
            Gw2Ui::TooltipMuted(ul ? "Unlocked" : "Not yet unlocked");
            Gw2Ui::TooltipSeparator();
            Gw2Ui::TooltipMuted("Right-click to copy");
            Gw2Ui::TooltipEnd();
        };

        Gw2Ui::GalleryBrowser::FilterDropdown lockF{ "Lock", kLock, 3, &g_lockFilter, 132.f };

        Gw2Ui::GalleryBrowser gb;
        gb.id = "ward.dyes";
        gb.title = "Dyes";
        gb.have = unl; gb.total = (int)dyes.size(); gb.tallyVerb = "unlocked";
        gb.rightStatus = m.haveDyes ? nullptr : "loading...";
        gb.showProgress = true;   // dyes have no gem-store-exclusive split -> a single unlocked bar
        gb.searchBuf = g_search; gb.searchBufSize = (int)sizeof(g_search); gb.searchHint = "Search dyes...";
        gb.filters = &lockF; gb.filterCount = 1;
        gb.viewMode = &app.config.itemsView; gb.settingsDirty = &app.settingsDirty;
        gb.orderToken = tok;
        gb.gridCell = 40.f; gb.gridGap = 6.f; gb.listRowH = 30.f;
        gb.emptyText = dyes.empty() ? "Dye palette not loaded." : "No matches.";

        gb.rebuildOrder = [&](std::vector<int>& order) {
            for (int i = 0; i < (int)dyes.size(); ++i)
            {
                if (!LockPass(unlocked(dyes[i].id))) continue;
                if (!q.empty() && Lower(dyes[i].name).find(q) == std::string::npos) continue;
                order.push_back(i);
            }
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                return std::string(dyes[a].name ? dyes[a].name : "") < std::string(dyes[b].name ? dyes[b].name : "");
            });
        };
        gb.drawGridCell = [&](int idx, ImVec2 cmin, float cs) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const Gw2Ui::Dye& d = dyes[idx];
            const bool ul = unlocked(d.id);
            ImGui::InvisibleButton("##dc", ImVec2(cs, cs));
            const bool hov = ImGui::IsItemHovered();
            dl->AddRectFilled(cmin, ImVec2(cmin.x + cs, cmin.y + cs), IM_COL32(d.r, d.g, d.b, ul ? 255 : 90), 4.f);
            dl->AddRect(cmin, ImVec2(cmin.x + cs, cmin.y + cs), ul ? Gw2Ui::kGold : IM_COL32(90, 90, 90, 200), 4.f, 0, ul ? 1.6f : 1.f);
            if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); dyeTip(d, ul); }
            DyeCopyMenu(d, hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        };
        gb.drawListRow = [&](int idx, const Gw2Ui::RowHotspot& row) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const Gw2Ui::Dye& d = dyes[idx];
            const bool ul = unlocked(d.id);
            const float sw = row.height - 8.f, statusW = 110.f * scale;
            const ImVec2 p = row.min;
            dl->AddRectFilled(ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + sw, p.y + 4.f + sw), IM_COL32(d.r, d.g, d.b, ul ? 255 : 90), 3.f);
            dl->AddRect(ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + sw, p.y + 4.f + sw), ul ? Gw2Ui::kGold : IM_COL32(90, 90, 90, 200), 3.f, 0, 1.2f);
            Gw2Ui::RowLabel(dl, row, row.width - statusW, 8.f, ul ? "Unlocked" : "Locked", Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                            ul ? Gw2Ui::kGold : Gw2Ui::kTextDim, false, nullptr, 16.f);
            Gw2Ui::RowLabel(dl, row, sw + 12.f, statusW + 12.f, d.name ? d.name : "Dye", Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                            ul ? IM_COL32(228, 222, 204, 255) : Gw2Ui::kTextDim, false, nullptr, 18.f);
            if (row.hovered) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); dyeTip(d, ul); }
            DyeCopyMenu(d, row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        };

        Gw2Ui::DrawGalleryBrowser(gb);
    }
}

void DrawWardrobeContent(App& app)
{
    const AccountData::Model& m = AccountData::Get();
    if (!AccountData::HasKey())
    { ApiReminder::Card(app, "wardrobe", "viewing your unlocked skins and dyes", /*gated*/true); return; }
    if (!AccountData::HasScope(Api::TokenPermission::Unlocks))
    { Gw2Ui::EmptyState("Missing unlocks scope", "The Wardrobe needs the unlocks permission."); return; }

    // Skins | Dyes sub-toggle (left) + Grid/List (right, shares the Items-tab view preference).
    // Shrink the two view chips from the available width (reserving the trailing Grid/List button) so the row
    // always fits at the minimum window size; right-align Grid/List only when it fits.
    // Skins | Outfits | Dyes sub-toggle. Outfits live here (it's clothing); they render via DrawCosmeticScope, which
    // brings its OWN toolbar (search + grid/list), so for that sub-view we skip the Wardrobe's grid/list + search.
    // Outfits + Legendary bring their OWN toolbar (search + grid/list), so for those sub-views we skip the
    // Wardrobe's grid/list + search and reserve no trailing-button width.
    const bool outf = (g_view == 2);
    const bool leg  = (g_view == 3);
    // Every sub-view now owns its toolbar: Skins + Dyes on the shared shell, Outfits + Legendary their own. The
    // chip row is just the four sub-view chips (no shared trailing Grid/List button).
    // The four sub-view chips. g_view values are NON-contiguous (Skins=0, Outfits=2, Legendary=3, Dyes=1), so map
    // the display order to/from g_view via a local index.
    static const Gw2Ui::ChipItem kViewChips[4] = { { "Skins" }, { "Outfits" }, { "Legendary" }, { "Dyes" } };
    static const int kViewVals[4] = { 0, 2, 3, 1 };
    int vsel = 0; for (int i = 0; i < 4; ++i) if (kViewVals[i] == g_view) { vsel = i; break; }
    if (Gw2Ui::ChipRow("##wview", kViewChips, 4, &vsel, 30.f, 16.f, 6.f, 50.f)) g_view = kViewVals[vsel];
    if (outf) { DrawCosmeticScope(app, CosmeticCatalog::Kind::Outfits); return; }
    if (leg)  { DrawLegendaryArmoryContent(app); return; }   // its own header + search/sort/grid toolbar
    if (g_view == 0) DrawSkins(app, m, ImGui::GetContentRegionAvail().x);
    else             DrawDyes(app, m);
}
