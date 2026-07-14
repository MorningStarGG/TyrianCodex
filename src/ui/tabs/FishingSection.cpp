#include "ui/tabs/FishingSection.h"

#include "app/App.h"
#include "app/AccountData.h"
#include "app/FishData.h"
#include "app/ItemCatalog.h"     // fish are items -> their chat link (copy in the right-click menu)
#include "ui/Gw2Ui.h"
#include "ui/ApiReminder.h"
#include "ui/GuideViewer.h"        // SetClipboard + ViewerAlert (copy) + OpenZoneTravelChoice (fish here)
#include "ui/items/ItemRender.h"   // ItemUI::RarityColor
#include "ui/wiki/WikiReader.h"    // Wiki::OpenWiki ("Open in Wiki" from the fish right-click menu)
#include "util/Textures.h"         // Tex::GetTextureFromURL (fish icon)
#include "render/glyphs/Glyphs.h"  // Render::DrawGlyph + Glyph::Check (caught marker)

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The Fishing tab (redesigned on the Items-tab model). Two scopes: FISH (a left region RAIL + a flat,
// de-duplicated, sortable/filterable fish grid/list -- each fish appears ONCE, under its base non-Avid regional
// collection, killing the Avid duplication) and COLLECTIONS (achievement progress as stat tiles). Reuses the
// shared Gw2Ui widgets (VSplitter / Row / Dropdown / SearchBox / StatGrid / clipper). The in-world hole overlay +
// the Track watchlist + FishingGuidance run are untouched.
namespace
{
    const ImU32 kGreen = IM_COL32(150, 230, 150, 255);

    int  g_scope = 0;     // 0 Fish, 1 Collections
    char g_search[96] = "";
    int  g_caught = 0;    // 0 All, 1 Caught, 2 Uncaught
    int  g_time   = 0;    // 0 Any, 1 Day, 2 Night, 3 Dusk, 4 Dawn
    int  g_bait   = 0;    // index into g_baits (0 = Any)
    int  g_rarity = 0;    // index into g_rarities (0 = Any)
    int  g_hole   = 0;    // index into g_holes (0 = Any)
    int  g_sort   = 0;    // 0 Name, 1 Rarity, 2 Region, 3 Hole, 4 Bait, 5 Catchable
    bool g_asc    = true;
    bool g_now    = false;
    int  g_region = 0;    // rail: 0 = All Fish, else (g_rail index + 1)

    const char* kCaught[] = { "All", "Caught", "Uncaught" };
    // Dusk + Dawn are merged: the game never tags a fish "Dusk" or "Dawn" alone -- the 12 twilight fish all use
    // the combined "Dusk/Dawn" window (catchable in either short phase). So one filter option covers both.
    const char* kTime[]   = { "Any time", "Day", "Night", "Dusk/Dawn" };
    const char* kSort[]   = { "Sort: Name", "Sort: Rarity", "Sort: Region", "Sort: Hole", "Sort: Bait", "Sort: Catchable" };

    std::string Lower(const char* s) { std::string o; if (s) for (const char* p = s; *p; ++p) o += (char)std::tolower((unsigned char)*p); return o; }

    int RarityRank(const std::string& r)
    {
        static const char* o[] = { "Junk", "Basic", "Fine", "Masterwork", "Rare", "Exotic", "Ascended", "Legendary" };
        for (int i = 0; i < 8; ++i) if (r == o[i]) return i;
        return 3;
    }

    // canonical primary hole kind from the messy wiki string ("Lake Fish" -> "Lake", "Any, Open Water" -> "Any",
    // "Freshwater / Brackish Water" -> "Freshwater", "" -> "Any") -- the filter/sort key + a compact row tag.
    std::string HoleKind(const std::string& hole)
    {
        if (hole.empty()) return "Any";
        std::string f = hole.substr(0, hole.find_first_of(",/"));
        while (!f.empty() && f.back()  == ' ') f.pop_back();
        while (!f.empty() && f.front() == ' ') f.erase(f.begin());
        std::string fl = Lower(f.c_str());
        if      (fl.size() >= 5 && fl.compare(fl.size() - 5, 5, " fish")  == 0) f.erase(f.size() - 5);
        else if (fl.size() >= 6 && fl.compare(fl.size() - 6, 6, " water") == 0) f.erase(f.size() - 6);
        while (!f.empty() && f.back() == ' ') f.pop_back();
        fl = Lower(f.c_str());
        if (f.empty() || fl == "none" || fl == "open") return "Any";
        return f;
    }

    std::string Strip(const std::string& name)   // "Krytan Fisher" -> "Krytan"; "Aquatic Trash Collector" -> "Aquatic Trash"
    {
        for (const char* sfx : { " Fisher", " Collector" })
        {
            const size_t n = std::strlen(sfx);
            if (name.size() > n && name.compare(name.size() - n, n, sfx) == 0) return name.substr(0, name.size() - n);
        }
        return name;
    }

    bool TimePass(const std::string& fishTime)
    {
        if (g_time == 0) return true;
        const std::string t = Lower(fishTime.c_str());
        if (t.empty() || t.find("any") != std::string::npos) return true;
        return t.find(Lower(kTime[g_time])) != std::string::npos;
    }

    // ---- facets (built once when the catalog loads) ----
    struct Rail { int collIdx; std::string label; int group; };   // group: 0 regional, 1 worldwide, 2 special
    std::vector<Rail> g_rail;
    struct FEntry { int id; int collIdx; int bit; };              // a fish + its home (non-Avid) collection + bit
    std::vector<FEntry> g_flat;
    std::vector<std::string>  g_baits, g_holes, g_rarities;       // [0] = "Any/All"
    std::vector<const char*>  g_baitPtrs, g_holePtrs, g_rarPtrs;
    size_t g_built = (size_t)-1;

    int CollGroup(const FishData::Collection& c)
    { return c.name.find("Aquatic") != std::string::npos ? 2 : (c.worldwide ? 1 : 0); }

    void BuildFacets(App& app)
    {
        const std::vector<FishData::Collection>& colls = app.fish.Collections();
        if (g_built == colls.size()) return;
        g_rail.clear(); g_flat.clear();
        std::vector<std::string> baits, holes, rar;
        for (int ci = 0; ci < (int)colls.size(); ++ci)
        {
            const FishData::Collection& c = colls[ci];
            if (c.avid) continue;                          // de-dup: Avid repeats the base regional collection
            g_rail.push_back({ ci, Strip(c.name), CollGroup(c) });
            for (int b = 0; b < (int)c.fish.size(); ++b)
            {
                const FishData::Fish* f = app.fish.Find(c.fish[b]);
                if (!f) continue;
                g_flat.push_back({ f->id, ci, b });
                if (!f->bait.empty()  && std::find(baits.begin(), baits.end(), f->bait) == baits.end()) baits.push_back(f->bait);
                const std::string hk = HoleKind(f->hole);
                if (std::find(holes.begin(), holes.end(), hk) == holes.end()) holes.push_back(hk);
                if (!f->rarity.empty() && std::find(rar.begin(), rar.end(), f->rarity) == rar.end()) rar.push_back(f->rarity);
            }
        }
        std::stable_sort(g_rail.begin(), g_rail.end(), [](const Rail& a, const Rail& b)
                         { return a.group != b.group ? a.group < b.group : a.label < b.label; });
        std::sort(baits.begin(), baits.end());
        std::sort(holes.begin(), holes.end());
        std::sort(rar.begin(), rar.end(), [](const std::string& a, const std::string& b) { return RarityRank(a) < RarityRank(b); });
        g_baits = { "Any bait" };    g_baits.insert(g_baits.end(), baits.begin(), baits.end());
        g_holes = { "All holes" };   g_holes.insert(g_holes.end(), holes.begin(), holes.end());
        g_rarities = { "Any rarity" }; g_rarities.insert(g_rarities.end(), rar.begin(), rar.end());
        g_baitPtrs.clear(); for (const std::string& s : g_baits) g_baitPtrs.push_back(s.c_str());
        g_holePtrs.clear(); for (const std::string& s : g_holes) g_holePtrs.push_back(s.c_str());
        g_rarPtrs.clear();  for (const std::string& s : g_rarities) g_rarPtrs.push_back(s.c_str());
        g_built = colls.size();
    }

    // ---- the cached filtered + sorted view (indices into g_flat) ----
    std::vector<int> g_view;
    uint64_t    g_ver = (uint64_t)-1;
    std::string g_kSearch = "\x01";
    int g_kRegion = -1, g_kCaught = -1, g_kTime = -1, g_kBait = -1, g_kRarity = -1, g_kHole = -1, g_kSort = -1, g_kNow = -1, g_kAsc = -1, g_kPhase = -1;

    void RebuildView(App& app, FishData::Phase phase)
    {
        const std::string q = Lower(g_search);
        const int phaseTok = g_now ? (int)phase : -1;
        if (g_ver == app.fish.Version() && g_kSearch == g_search && g_kRegion == g_region && g_kCaught == g_caught
            && g_kTime == g_time && g_kBait == g_bait && g_kRarity == g_rarity && g_kHole == g_hole
            && g_kSort == g_sort && g_kNow == (int)g_now && g_kAsc == (int)g_asc && g_kPhase == phaseTok)
            return;

        const std::vector<FishData::Collection>& colls = app.fish.Collections();
        const int regionColl = g_region > 0 && g_region <= (int)g_rail.size() ? g_rail[g_region - 1].collIdx : -1;
        g_view.clear();
        for (int i = 0; i < (int)g_flat.size(); ++i)
        {
            const FEntry& fe = g_flat[i];
            if (regionColl >= 0 && fe.collIdx != regionColl) continue;
            const FishData::Fish* f = app.fish.Find(fe.id);
            if (!f) continue;
            const bool caught = app.fish.IsCaught(colls[fe.collIdx].id, fe.bit);
            if (g_caught == 1 && !caught) continue;
            if (g_caught == 2 && caught)  continue;
            if (g_rarity > 0 && f->rarity != g_rarities[g_rarity]) continue;
            if (g_bait   > 0 && f->bait   != g_baits[g_bait])      continue;
            if (g_hole   > 0 && HoleKind(f->hole) != g_holes[g_hole]) continue;
            if (!TimePass(f->time)) continue;
            if (g_now && !FishData::CatchableNow(f->time, phase)) continue;
            if (!q.empty()
                && Lower(f->name.c_str()).find(q)   == std::string::npos
                && Lower(f->bait.c_str()).find(q)   == std::string::npos
                && Lower(f->region.c_str()).find(q) == std::string::npos
                && Lower(f->hole.c_str()).find(q)   == std::string::npos
                && Lower(f->rarity.c_str()).find(q) == std::string::npos) continue;
            g_view.push_back(i);
        }
        auto key = [&](int idx)
        {
            const FEntry& fe = g_flat[idx]; const FishData::Fish* f = app.fish.Find(fe.id);
            switch (g_sort)
            {
                case 1: { char b[8]; std::snprintf(b, sizeof(b), "%02d", RarityRank(f->rarity)); return std::string(b) + Lower(f->name.c_str()); }
                case 2: return Lower(Strip(colls[fe.collIdx].name).c_str()) + "|" + Lower(f->name.c_str());
                case 3: return Lower(HoleKind(f->hole).c_str()) + "|" + Lower(f->name.c_str());
                case 4: return Lower((f->bait.empty() ? "any" : f->bait).c_str()) + "|" + Lower(f->name.c_str());
                case 5: return std::string(FishData::CatchableNow(f->time, phase) ? "0" : "1") + Lower(f->name.c_str());
                default: return Lower(f->name.c_str());
            }
        };
        std::stable_sort(g_view.begin(), g_view.end(), [&](int a, int b) { return g_asc ? key(a) < key(b) : key(a) > key(b); });

        g_ver = app.fish.Version(); g_kSearch = g_search; g_kRegion = g_region; g_kCaught = g_caught; g_kTime = g_time;
        g_kBait = g_bait; g_kRarity = g_rarity; g_kHole = g_hole; g_kSort = g_sort; g_kNow = (int)g_now; g_kAsc = (int)g_asc; g_kPhase = phaseTok;
    }

    // ---- per-fish chrome (tooltip / track / menu / travel) ----
    void FishTooltip(const FishData::Fish& f, bool caught, FishData::Phase phase, bool tracked)
    {
        if (!Gw2Ui::TooltipBegin()) return;

        // HEADER: the fish icon + name + "<rarity> Fish" + its location (mirrors the item tooltip's icon header).
        if (!f.icon.empty())
        {
            char t[40]; std::snprintf(t, sizeof(t), "TC_FISH_%d", f.id);
            if (void* tex = Tex::GetTextureFromURL(t, f.icon.c_str()))
            {
                const float ic = (std::max)(46.f, 48.f * Gw2Ui::TextScale());
                ImGui::Image((ImTextureID)tex, ImVec2(ic, ic));
                ImGui::SameLine(0.f, 8.f);
            }
        }
        ImGui::BeginGroup();
        Gw2Ui::TooltipTitle(f.name.c_str());
        { std::string sub = f.rarity.empty() ? std::string("Fish") : (f.rarity + " Fish"); Gw2Ui::TooltipText(sub.c_str()); }
        // Where: the clean location name only. The `region` field is verbose wiki prose that just duplicates it
        // ("the Shiverpeak Mountains" vs "Shiverpeak Mountains"), so showing both reads as the place said twice.
        const std::string& where = !f.location.empty() ? f.location : f.region;
        if (!where.empty()) Gw2Ui::TooltipMuted(where.c_str());
        ImGui::EndGroup();

        // HOW TO CATCH: one fact per line, like the game's own tooltips.
        Gw2Ui::TooltipSeparator();
        { std::string s = "Bait: " + (f.bait.empty() ? std::string("Any") : f.bait); Gw2Ui::TooltipText(s.c_str()); }
        { std::string s = "Time: " + (f.time.empty() ? std::string("Any") : f.time); Gw2Ui::TooltipText(s.c_str()); }
        if (f.power > 0) { char pw[24]; std::snprintf(pw, sizeof(pw), "Power: %d", f.power); Gw2Ui::TooltipText(pw); }
        if (!f.hole.empty()) { std::string s = "Hole: " + f.hole; Gw2Ui::TooltipText(s.c_str()); }

        // LIVE STATUS: caught + catchable-now (bright when biting, dim when not).
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted(caught ? "Caught" : "Not yet caught");
        if (FishData::CatchableNow(f.time, phase))
            Gw2Ui::TooltipText("Catchable now");
        else
        {
            int secToNext = 0; FishData::CurrentPhase(&secToNext);
            const int until = FishData::SecondsUntilCatchable(f.time, phase, secToNext);
            const std::string lt = Lower(f.time.c_str());
            const char* win = (lt.find("dusk") != std::string::npos || lt.find("dawn") != std::string::npos) ? "Dusk/Dawn"
                            : (lt.find("night") != std::string::npos) ? "Night"
                            : (lt.find("day") != std::string::npos) ? "Day" : "another time";
            char nb[80];
            if (until > 0) std::snprintf(nb, sizeof(nb), "Not catchable now -- bites at %s (in %d:%02d)", win, until / 60, until % 60);
            else           std::snprintf(nb, sizeof(nb), "Not catchable now -- bites at %s", win);
            Gw2Ui::TooltipMuted(nb);
        }
        if (tracked) Gw2Ui::TooltipMuted("Tracked -- holes of its kind light up; the arrow leads to the nearest");

        // FOOTER: the actions, dimmed like the item tooltip's "Right-click for copy menu."
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Click to travel   -   Right-click: track / copy");
        Gw2Ui::TooltipEnd();
    }

    void ToggleTrack(App& app, const FishData::Fish& f)
    {
        std::set<int>& w = app.state.fishWatch;
        const bool was = w.count(f.id) != 0;
        if (was) w.erase(f.id); else w.insert(f.id);
        app.state.fishRunActive = !w.empty();
        ViewerAlert(was ? "Stopped tracking this fish."
                        : "Tracking -- its hole kind lights up; turn on \"Fishing holes\" to see them.");
    }

    void FishMenu(App& app, const FishData::Fish& f, bool openNow)
    {
        const bool tracked = app.state.fishWatch.count(f.id) != 0;
        const std::string& chat = ItemCatalog::ById(f.id).chatLink;   // a fish IS an item -> paste it into chat to link it
        std::vector<Gw2Ui::MenuNode> nodes;
        nodes.push_back({ tracked ? "Stop tracking" : "Track (light up its holes)", 1 });
        if (!f.name.empty()) nodes.push_back({ "Open in Wiki", 4 });
        if (!chat.empty())   nodes.push_back({ "Copy chat link", 3 });
        if (!f.name.empty()) nodes.push_back({ "Copy name", 2 });
        const int r = Gw2Ui::ContextMenuTree("##fishmenu", nodes, openNow);
        if (r == 1) ToggleTrack(app, f);
        else if (r == 2) { SetClipboard(f.name); ViewerAlert("Copied fish name."); }
        else if (r == 3) { SetClipboard(chat); ViewerAlert("Copied chat link -- paste it in chat to link this fish."); }
        else if (r == 4) Wiki::OpenWiki(app, f.name);
    }

    void FishClickTravel(App& app, const FishData::Fish& f)
    { if (!f.mapIds.empty()) OpenZoneTravelChoice(app, f.mapIds.front()); }

    // The current Tyrian time-of-day, as a styled phase pill: a sun/moon/twilight glyph + the phase name (in the
    // phase colour) + the countdown to the next fishing window. Dusk + Dawn show as the merged "Dusk/Dawn"
    // (matching the filter); the countdown still tracks the real 4-phase cycle (Day -> Dusk -> Night -> Dawn).
    void DrawPhasePill(FishData::Phase phase, int secToNext)
    {
        const bool twilight = (phase == FishData::Phase::Dusk || phase == FishData::Phase::Dawn);
        const bool night    = (phase == FishData::Phase::Night);
        const char* cur = twilight ? "Dusk/Dawn" : (night ? "Night" : "Day");
        const char* nxt = (phase == FishData::Phase::Day)  ? "Dusk/Dawn"
                        : (phase == FishData::Phase::Dusk) ? "Night"
                        : (phase == FishData::Phase::Night) ? "Dusk/Dawn" : "Day";
        const ImU32 accent = twilight ? IM_COL32(243, 158, 96, 255)    // warm twilight amber
                           : night    ? IM_COL32(150, 174, 235, 255)   // cool night indigo
                                      : IM_COL32(255, 208, 120, 255);  // day gold
        const Render::Glyph g = twilight ? Render::Glyph::Horizon : night ? Render::Glyph::Moon : Render::Glyph::Sun;

        const float fs = 18.f, h = 32.f, padL = 13.f, padR = 15.f, icon = 19.f, gap = 8.f, dotW = 12.f;
        char rhs[48]; std::snprintf(rhs, sizeof(rhs), "%d:%02d to %s", secToNext / 60, secToNext % 60, nxt);
        const float nameW = Gw2Ui::MeasureWidth(cur, fs);
        const float rhsW  = Gw2Ui::MeasureWidth(rhs, fs);
        const float w = padL + icon + gap + nameW + gap + dotW + gap + rhsW + padR;

        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##phasepill", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 rgb = accent & 0x00FFFFFFu;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), rgb | (hov ? 0x3C000000u : 0x2C000000u), h * 0.5f);   // colour fill only, no border
        Render::DrawGlyph(dl, ImVec2(p.x + padL + icon * 0.5f, p.y + h * 0.5f), icon, g, accent, Render::GlyphStyle{});
        float x = p.x + padL + icon + gap;
        Gw2Ui::LabelDL(dl, ImVec2(x, p.y), ImVec2(x + nameW, p.y + h), cur, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, accent, true, nullptr, fs);
        x += nameW + gap;
        dl->AddCircleFilled(ImVec2(x + dotW * 0.5f, p.y + h * 0.5f), 1.8f, rgb | 0x99000000u);
        x += dotW + gap;
        Gw2Ui::LabelDL(dl, ImVec2(x, p.y), ImVec2(x + rhsW, p.y + h), rhs, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSub, false, nullptr, fs);
        if (hov)
        {
            char tip[112]; std::snprintf(tip, sizeof(tip), "Tyrian %s.  %d:%02d until %s.\nFish with a matching Time of Day bite now.", cur, secToNext / 60, secToNext % 60, nxt);
            Gw2Ui::Tooltip(tip);
        }
    }

    // ---- the region rail ----
    bool RailRow(const char* label, int caught, int total, bool selected, int rowIndex, float w)
    {
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##frr", rowIndex, 38.f, w, false, selected);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (selected) dl->AddRectFilled(ImVec2(row.min.x + 2.f, row.min.y + 3.f), ImVec2(row.min.x + 5.f, row.min.y + 35.f), Gw2Ui::kGold, 1.f);
        float cw = 0.f;
        if (total >= 0)
        {
            char cb[24]; std::snprintf(cb, sizeof(cb), "%d / %d", caught, total);
            cw = Gw2Ui::MeasureWidth(cb, 16.f) + 10.f;   // 16/18px = clean atlas downscale ratios (15/17 alias)
            Gw2Ui::RowLabel(dl, row, w - cw - 4.f, 6.f, cb, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                            selected ? Gw2Ui::kGold : (caught >= total && total > 0 ? kGreen : Gw2Ui::kTextDim), false, nullptr, 16.f);
        }
        Gw2Ui::RowLabel(dl, row, 14.f, cw + 10.f, label, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        selected ? Gw2Ui::kGold : Gw2Ui::kTextSelected, false, nullptr, 18.f);
        return row.clicked;
    }

    void DrawRail(App& app, float width, float height)
    {
        const std::vector<FishData::Collection>& colls = app.fish.Collections();
        ImGui::BeginChild("##fishrail", ImVec2(width, height), false);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
        const float w = ImGui::GetContentRegionAvail().x;
        int row = 0, lastGroup = -1;
        // overall caught/total across all non-Avid collections
        int totC = 0, totN = 0;
        for (const Rail& r : g_rail) { auto pr = app.fish.Progress(colls[r.collIdx].id); totC += pr.first; totN += pr.second; }
        ImGui::PushID(-1);
        if (RailRow("All Fish", totC, totN, g_region == 0, row++, w)) g_region = 0;
        ImGui::PopID();
        for (int i = 0; i < (int)g_rail.size(); ++i)
        {
            const Rail& r = g_rail[i];
            if (r.group != lastGroup && lastGroup >= 0) { Gw2Ui::Divider(w); ++row; }
            lastGroup = r.group;
            const std::pair<int, int> pr = app.fish.Progress(colls[r.collIdx].id);
            ImGui::PushID(r.collIdx);   // UNIQUE id per rail row (else they collide -> not clickable + shared highlight roll)
            if (RailRow(r.label.c_str(), pr.first, pr.second, g_region == i + 1, row++, w)) g_region = i + 1;
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    // ---- the fish grid + list (a flat clipper over g_view) ----
    void DrawFishGrid(App& app, FishData::Phase phase)
    {
        const std::vector<FishData::Collection>& colls = app.fish.Collections();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float scale = Gw2Ui::TextScale();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float cell = std::max(52.f, 60.f * scale), gap = std::max(4.f, 6.f * scale);
        const int cols = std::max(1, (int)((availW + gap) / (cell + gap)));
        const int rows = ((int)g_view.size() + cols - 1) / cols;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        ImGuiListClipper clip; clip.Begin(rows, cell + gap);
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    const int vi = r * cols + c;
                    if (vi >= (int)g_view.size()) break;
                    const FEntry& fe = g_flat[g_view[vi]];
                    const FishData::Fish* f = app.fish.Find(fe.id);
                    if (!f) continue;
                    const bool caught = app.fish.IsCaught(colls[fe.collIdx].id, fe.bit);
                    if (c) ImGui::SameLine();
                    const ImVec2 cmin = ImGui::GetCursorScreenPos();
                    ImGui::PushID(f->id);
                    ImGui::InvisibleButton("##fc", ImVec2(cell, cell));
                    const bool hov = ImGui::IsItemHovered();
                    if (!f->icon.empty())
                    {
                        char t[40]; std::snprintf(t, sizeof(t), "TC_FISH_%d", f->id);
                        if (void* tex = Tex::GetTextureFromURL(t, f->icon.c_str()))
                            dl->AddImage((ImTextureID)tex, ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cell - 1.f, cmin.y + cell - 1.f));
                    }
                    dl->AddRect(cmin, ImVec2(cmin.x + cell, cmin.y + cell), ItemUI::RarityColor(f->rarity), 2.f, 0, 1.4f);
                    if (!caught) dl->AddRectFilled(ImVec2(cmin.x + 1.f, cmin.y + 1.f), ImVec2(cmin.x + cell - 1.f, cmin.y + cell - 1.f), IM_COL32(0, 0, 0, 150), 2.f);
                    else Render::DrawGlyph(dl, ImVec2(cmin.x + cell - 9.f, cmin.y + cell - 9.f), 12.f, Render::Glyph::Check, kGreen, Render::GlyphStyle{});
                    const bool tracked = app.state.fishWatch.count(f->id) != 0;
                    if (tracked) dl->AddRect(cmin, ImVec2(cmin.x + cell, cmin.y + cell), Gw2Ui::kGold, 2.f, 0, 2.6f);
                    if (hov)
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        FishTooltip(*f, caught, phase, tracked);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) FishClickTravel(app, *f);
                    }
                    FishMenu(app, *f, hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
                    ImGui::PopID();
                }
        ImGui::PopStyleVar();
    }

    void DrawFishList(App& app, FishData::Phase phase)
    {
        const std::vector<FishData::Collection>& colls = app.fish.Collections();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float scale = Gw2Ui::TextScale();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float rowH = 44.f * scale, ic = rowH - 10.f, metaW = 280.f * scale;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
        ImGuiListClipper clip; clip.Begin((int)g_view.size(), rowH);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
            {
                const FEntry& fe = g_flat[g_view[i]];
                const FishData::Fish* f = app.fish.Find(fe.id);
                if (!f) continue;
                const bool caught = app.fish.IsCaught(colls[fe.collIdx].id, fe.bit);
                ImGui::PushID(f->id);
                const Gw2Ui::RowHotspot row = Gw2Ui::Row("##frow", i, rowH, 0.f, /*allowOverlap*/ true);
                const ImVec2 p = row.min;
                if (!f->icon.empty())
                {
                    char t[40]; std::snprintf(t, sizeof(t), "TC_FISH_%d", f->id);
                    if (void* tex = Tex::GetTextureFromURL(t, f->icon.c_str()))
                        dl->AddImage((ImTextureID)tex, ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic));
                }
                dl->AddRect(ImVec2(p.x + 4.f, p.y + 4.f), ImVec2(p.x + 4.f + ic, p.y + 4.f + ic), ItemUI::RarityColor(f->rarity), 0.f, 0, 1.2f);
                // right meta: region + hole kind (+ rarity colour cue on the name)
                std::string meta = Strip(colls[fe.collIdx].name) + "   -   " + HoleKind(f->hole);
                Gw2Ui::RowLabel(dl, row, row.width - metaW, 12.f, meta.c_str(), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 16.f * scale);
                Gw2Ui::RowLabel(dl, row, ic + 14.f, metaW + 12.f, f->name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                                caught ? ItemUI::RarityColor(f->rarity) : Gw2Ui::kTextDim, false, nullptr, 20.f * scale);
                if (caught) Render::DrawGlyph(dl, ImVec2(p.x + 4.f + ic - 6.f, p.y + 4.f + ic - 6.f), 11.f, Render::Glyph::Check, kGreen, Render::GlyphStyle{});
                const bool tracked = app.state.fishWatch.count(f->id) != 0;
                if (tracked) Render::DrawGlyph(dl, ImVec2(p.x + row.width - 14.f, p.y + rowH * 0.5f), 13.f, Render::Glyph::Star, Gw2Ui::kGold, Render::GlyphStyle{});
                if (row.hovered)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    FishTooltip(*f, caught, phase, tracked);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) FishClickTravel(app, *f);
                }
                FishMenu(app, *f, row.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
                ImGui::PopID();
            }
        ImGui::PopStyleVar();
    }

    // ---- Collections scope: achievement progress as stat tiles, grouped ----
    void DrawCollections(App& app)
    {
        const std::vector<FishData::Collection>& colls = app.fish.Collections();
        ImGui::BeginChild("##fishcoll", ImVec2(0.f, 0.f), false);
        struct Grp { const char* title; std::function<bool(const FishData::Collection&)> pred; };
        const Grp groups[] = {
            { "Regional",  [](const FishData::Collection& c) { return !c.avid && !c.worldwide && c.name.find("Aquatic") == std::string::npos; } },
            { "Worldwide", [](const FishData::Collection& c) { return !c.avid && c.worldwide; } },
            { "Avid",      [](const FishData::Collection& c) { return c.avid; } },
            { "Special",   [](const FishData::Collection& c) { return c.name.find("Aquatic") != std::string::npos; } },
        };
        for (const Grp& g : groups)
        {
            std::vector<Gw2Ui::Stat> tiles;
            for (const FishData::Collection& c : colls)
            {
                if (!g.pred(c)) continue;
                const std::pair<int, int> pr = app.fish.Progress(c.id);
                const bool full = pr.first >= pr.second && pr.second > 0;
                char v[24]; std::snprintf(v, sizeof(v), "%d / %d", pr.first, pr.second);
                tiles.push_back({ Strip(c.name), v, full ? kGreen : Gw2Ui::kTextSelected, full ? kGreen : Gw2Ui::kGold, {} });
            }
            if (tiles.empty()) continue;
            Gw2Ui::SectionHeader(g.title, nullptr, 18.f, Gw2Ui::kGold, true);
            Gw2Ui::StatGrid(0.f, tiles);
            ImGui::Spacing();
        }
        ImGui::EndChild();
    }
}

void DrawFishingContent(App& app)
{
    if (!AccountData::HasKey())
    { ApiReminder::Card(app, "fishing", "tracking which fish you've caught", /*gated*/ true); return; }
    if (!AccountData::HasScope(Api::TokenPermission::Progression))
    { Gw2Ui::EmptyState("Missing progression scope", "Fishing needs the progression permission to read caught fish."); return; }
    if (app.fish.Empty())
    { Gw2Ui::Label("Loading fishing data...", Gw2Ui::kTextDim, false, nullptr, 16.f); return; }

    app.fish.Refresh();
    BuildFacets(app);

    int secToNext = 0;
    const FishData::Phase phase = FishData::CurrentPhase(&secToNext);

    // --- scope chips: Fish | Collections ---
    static const Gw2Ui::ChipItem kScopeChips[] = { { "Fish", nullptr, {}, 118.f }, { "Collections", nullptr, {}, 118.f } };
    Gw2Ui::ChipRow("##fscope", kScopeChips, 2, &g_scope, 28.f, 16.f);
    ImGui::Spacing();

    if (g_scope == 1) { DrawCollections(app); return; }

    // --- Fish scope: region RAIL + body ---
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    const float maxRail = std::clamp(avail.x - 340.f, 160.f, 360.f);
    float& railW = app.config.PaneW("fishing.rail", 200.f);
    railW = std::clamp(railW, 160.f, std::max(160.f, maxRail));

    DrawRail(app, railW, avail.y);
    if (Gw2Ui::VSplitter("##fish_rail_split", origin.x + railW + 3.f, origin.y, avail.y, &railW, 160.f, std::max(160.f, maxRail)))
        app.settingsDirty = true;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + railW + 6.f, origin.y));
    ImGui::BeginChild("##fishbody", ImVec2(avail.x - railW - 6.f, avail.y), false);

    // The body is rail-narrowed (as little as ~334px), so every toolbar row sizes from GetContentRegionAvail and
    // stacks instead of running off the right edge. Row 1: the Tyrian time-of-day pill (left) + Catchable-now (right).
    DrawPhasePill(phase, secToNext);
    Gw2Ui::SameLineRightCluster(132.f);
    if (Gw2Ui::ChipToggle("##fnow", "Catchable now", g_now, ImVec2(132.f, 32.f), 16.f)) g_now = !g_now;

    Gw2Ui::SearchBox("##fishsearch", g_search, sizeof(g_search), ImGui::GetContentRegionAvail().x, "Search name / region / hole / bait / rarity...");

    // Filter dropdowns: equal share of the available width (each shrinks to fit); wrap to 3 + 2 when even the
    // collapsed widths no longer fit one row.
    {
        const float gap = 8.f, minDw = 64.f;
        const float availW = ImGui::GetContentRegionAvail().x;
        if (5.f * minDw + 4.f * gap <= availW)
        {
            const float dw = Gw2Ui::FillWidth(availW, 5, gap, minDw);
            { int v = g_caught; if (Gw2Ui::Dropdown("##fcaught", kCaught, 3, &v, dw)) g_caught = v; }
            ImGui::SameLine(0.f, gap); { int v = g_rarity; if (Gw2Ui::Dropdown("##frar", g_rarPtrs.data(), (int)g_rarPtrs.size(), &v, dw)) g_rarity = v; }
            ImGui::SameLine(0.f, gap); { int v = g_hole;   if (Gw2Ui::Dropdown("##fhole", g_holePtrs.data(), (int)g_holePtrs.size(), &v, dw)) g_hole = v; }
            ImGui::SameLine(0.f, gap); { int v = g_bait;   if (Gw2Ui::Dropdown("##fbait", g_baitPtrs.data(), (int)g_baitPtrs.size(), &v, dw)) g_bait = v; }
            ImGui::SameLine(0.f, gap); { int v = g_time;   if (Gw2Ui::Dropdown("##ftime", kTime, 4, &v, dw)) g_time = v; }
        }
        else
        {
            const float dw1 = Gw2Ui::FillWidth(availW, 3, gap, minDw);
            { int v = g_caught; if (Gw2Ui::Dropdown("##fcaught", kCaught, 3, &v, dw1)) g_caught = v; }
            ImGui::SameLine(0.f, gap); { int v = g_rarity; if (Gw2Ui::Dropdown("##frar", g_rarPtrs.data(), (int)g_rarPtrs.size(), &v, dw1)) g_rarity = v; }
            ImGui::SameLine(0.f, gap); { int v = g_hole;   if (Gw2Ui::Dropdown("##fhole", g_holePtrs.data(), (int)g_holePtrs.size(), &v, dw1)) g_hole = v; }
            const float dw2 = Gw2Ui::FillWidth(availW, 2, gap, minDw);
            { int v = g_bait; if (Gw2Ui::Dropdown("##fbait", g_baitPtrs.data(), (int)g_baitPtrs.size(), &v, dw2)) g_bait = v; }
            ImGui::SameLine(0.f, gap); { int v = g_time; if (Gw2Ui::Dropdown("##ftime", kTime, 4, &v, dw2)) g_time = v; }
        }
    }

    // Sort (self-describing dropdown -- "Sort: Name", like the other filters) + direction + Grid/List (pinned right).
    { int v = g_sort; if (Gw2Ui::Dropdown("##fsort", kSort, 6, &v, 160.f)) g_sort = v; }
    ImGui::SameLine(0.f, 6.f); if (Gw2Ui::ActionButton(g_asc ? "Asc" : "Desc", 56.f, 28.f)) g_asc = !g_asc;
    {
        const bool grid = (app.config.itemsView == 0);
        Gw2Ui::SameLineRightCluster(70.f);
        if (Gw2Ui::ActionButton(grid ? "Grid" : "List", 70.f, 28.f)) { app.config.itemsView = grid ? 1 : 0; app.settingsDirty = true; }
    }

    // Watchlist status -- its own row so it never crowds the controls above (shown only while tracking a fish).
    if (!app.state.fishWatch.empty())
    {
        char wl[40]; std::snprintf(wl, sizeof(wl), "Tracking %d", (int)app.state.fishWatch.size());
        Gw2Ui::Label(wl, Gw2Ui::kGold, false, nullptr, 16.f);
        ImGui::SameLine(0.f, 8.f);
        if (Gw2Ui::ActionButton("Clear", 60.f, 26.f)) { app.state.fishWatch.clear(); app.state.fishRunActive = false; }
    }

    RebuildView(app, phase);

    { char h[96]; std::snprintf(h, sizeof(h), "%d fish", (int)g_view.size());
      const std::string sub = g_region > 0 ? g_rail[g_region - 1].label : std::string("All regions");
      Gw2Ui::SectionHeader(sub.c_str(), h, 18.f, Gw2Ui::kGold, true); }

    ImGui::BeginChild("##fishresults", ImVec2(0.f, 0.f), false);
    if (g_view.empty()) Gw2Ui::Label("No matching fish.", Gw2Ui::kTextDim, false, nullptr, 16.f);
    else if (app.config.itemsView == 0) DrawFishGrid(app, phase);
    else DrawFishList(app, phase);
    ImGui::EndChild();

    ImGui::EndChild();   // ##fishbody
}
