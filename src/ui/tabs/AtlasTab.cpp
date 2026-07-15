#include "ui/tabs/AtlasTab.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/ApiReminder.h"
#include "render/glyphs/Glyphs.h"   // Render::DrawGlyph (expand caret)
#include "ui/search/SearchModel.h"
#include "ui/search/SearchRow.h"
#include "ui/SettingsWindow.h"      // OpenSettingsTab (OpenAtlasTab opens the window + selects the tab)
#include "ui/tabs/MapThumbnail.h"   // GroupTooltip + MapMark (region/zone band hover map)
#include "model/ObjectiveTypes.h"   // Objective::TryIconAssetId (per-objective pin icon)
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    // Atlas filter state at FILE scope (not a function-static) so OpenAtlasTab can preselect a browse type filter.
    Search::Filters g_atlasFilters;
    bool g_atlasInit = false;   // first-draw init done? OpenAtlasTab sets it so a preset (e.g. Zones) isn't wiped

    struct Disp { int idx; int kind; int stripe; };   // kind: 0 = result row, 1 = region band, 2 = zone band

    std::string RKey(const std::string& r) { return "r:" + r; }
    std::string ZKey(const std::string& r, const std::string& z) { return "z:" + r + "|" + z; }

    // Expand every region (and zone, when zone headers are shown) in the result set.
    void SeedAllOpen(std::set<std::string>& open, const std::vector<int>& res,
                     const std::vector<Search::Entry>& all, bool zoneHeaders)
    {
        for (int idx : res)
        {
            const Search::Entry& e = all[idx];
            open.insert(RKey(e.regionLabel));
            if (zoneHeaders) open.insert(ZKey(e.regionLabel, e.zoneName));
        }
    }

    // Region band: a bold gradient header with an expand chevron. Zone band: a lighter, indented sub-header.
    // Both are row-height (so ONE clipper handles bands + rows). Return true when clicked (toggle expand).
    void Chevron(ImDrawList* dl, ImVec2 c, bool expanded, ImU32 col)
    {
        Render::DrawGlyph(dl, c, 14.f, expanded ? Render::Glyph::CaretDown : Render::Glyph::CaretRight, col, { false, false, false });
    }
    // Bands return a bitmask: bit0 = expand clicked, bit2 = band hovered (the caller drives the map tooltip off
    // the hover bit so a future overlay element can't steal IsItemHovered).
    int DrawRegionBand(const char* label, float h, bool expanded)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##rb", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 a = hov ? IM_COL32(56, 46, 24, 175) : IM_COL32(40, 33, 18, 150);
        dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h), a, IM_COL32(14, 11, 7, 30), IM_COL32(14, 11, 7, 0), IM_COL32(40, 33, 18, 40));
        Chevron(dl, ImVec2(p.x + 12.f, p.y + h * 0.5f), expanded, Gw2Ui::kGold);
        Gw2Ui::LabelIn(ImVec2(p.x + 26.f, p.y), ImVec2(p.x + w - 8.f, p.y + h), label, Gw2Ui::HAlign::Left,
                       Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, 20.f, 0.f, 1.2f);
        dl->AddLine(ImVec2(p.x, p.y + h - 1.f), ImVec2(p.x + w, p.y + h - 1.f), IM_COL32(196, 176, 128, 60));
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        return (clicked ? 1 : 0) | (hov ? 4 : 0);
    }
    int DrawZoneBand(const char* label, float h, bool expanded)
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##zb", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hov ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 9));
        Chevron(dl, ImVec2(p.x + 26.f, p.y + h * 0.5f), expanded, IM_COL32(216, 198, 152, 235));
        Gw2Ui::LabelIn(ImVec2(p.x + 40.f, p.y), ImVec2(p.x + w - 4.f, p.y + h), label, Gw2Ui::HAlign::Left,
                       Gw2Ui::VAlign::Middle, IM_COL32(220, 202, 156, 255), false, nullptr, 18.f);
        dl->AddLine(ImVec2(p.x + 16.f, p.y + h - 1.f), ImVec2(p.x + w - 4.f, p.y + h - 1.f), IM_COL32(150, 130, 80, 45));
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        return (clicked ? 1 : 0) | (hov ? 4 : 0);
    }

    // Cheap change token for the grouped display list: `disp` is a pure function of (res, the expand set, browse).
    // Hash the inputs (a fold over the result ints + the small open-set strings) so BuildStable/BuildContiguous --
    // which allocate several std::maps -- re-run ONLY when something changed, not every frame the list is open.
    uint64_t DispToken(const std::vector<int>& res, const std::set<std::string>& open, bool browse)
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        for (int v : res) mix((uint32_t)v);
        mix(0x5BD1E9955BD1E995ull);                        // separator between the result + open-set sections
        for (const std::string& s : open) { for (unsigned char c : s) mix(c); mix('\n'); }
        mix(browse ? 1ull : 2ull);
        return h;
    }

    // Build the display list from an ALREADY region/zone-sorted result list (the browse view), honouring collapse.
    void BuildContiguous(const std::vector<int>& res, const std::vector<Search::Entry>& all,
                         const std::set<std::string>& open, bool zoneHeaders, std::vector<Disp>& disp)
    {
        disp.clear();
        // Count entries per zone: a zone with only its own summary entry (no objectives) shows as a flat row, not
        // a redundant band-then-single-row. A zone WITH objectives (>1 entry) gets a collapsible band.
        std::map<std::string, int> zoneCount;
        if (zoneHeaders) for (int idx : res) zoneCount[all[idx].regionLabel + "|" + all[idx].zoneName]++;
        std::string curR, curZ; bool rOpen = false, zOpen = false; int stripe = 0;
        for (int idx : res)
        {
            const Search::Entry& e = all[idx];
            if (e.regionLabel != curR) { curR = e.regionLabel; curZ.clear(); disp.push_back({ idx, 1, 0 }); rOpen = open.count(RKey(curR)) > 0; stripe = 0; }
            if (!rOpen) continue;
            const bool band = zoneHeaders && zoneCount[e.regionLabel + "|" + e.zoneName] > 1;
            if (band)
            {
                if (e.zoneName != curZ) { curZ = e.zoneName; disp.push_back({ idx, 2, 0 }); zOpen = open.count(ZKey(e.regionLabel, curZ)) > 0; stripe = 0; }
                if (!zOpen) continue;
            }
            else curZ.clear();   // flat row (no band): a later same-name banded zone still re-emits its band
            disp.push_back({ idx, 0, stripe++ });
        }
    }

    // Build from a RELEVANCE-ordered result list (grouped search): group by region then zone in first-appearance
    // (best-match) order, entries within a zone keep their relevance order. Honours collapse.
    void BuildStable(const std::vector<int>& res, const std::vector<Search::Entry>& all,
                     const std::set<std::string>& open, bool zoneHeaders, std::vector<Disp>& disp)
    {
        disp.clear();
        std::vector<std::string> regionOrder;
        std::map<std::string, int> regionFirst, zoneFirst;
        std::map<std::string, std::vector<std::string>> zoneOrder;
        std::map<std::string, std::vector<int>> rows;
        for (int idx : res)
        {
            const Search::Entry& e = all[idx];
            if (!regionFirst.count(e.regionLabel)) { regionFirst[e.regionLabel] = idx; regionOrder.push_back(e.regionLabel); }
            const std::string zk = e.regionLabel + "|" + e.zoneName;
            if (!zoneFirst.count(zk)) { zoneFirst[zk] = idx; zoneOrder[e.regionLabel].push_back(e.zoneName); }
            rows[zk].push_back(idx);
        }
        for (const std::string& r : regionOrder)
        {
            disp.push_back({ regionFirst[r], 1, 0 });
            if (!open.count(RKey(r))) continue;
            for (const std::string& z : zoneOrder[r])
            {
                const std::string zk = r + "|" + z;
                int stripe = 0;
                const bool band = zoneHeaders && rows[zk].size() > 1;   // band only for zones that have objectives
                if (band) { disp.push_back({ zoneFirst[zk], 2, 0 }); if (!open.count(ZKey(r, z))) continue; }
                for (int idx : rows[zk]) disp.push_back({ idx, 0, stripe++ });
            }
        }
    }

    // Zone band hover: the zone map with ALL its objectives pinned by type icon (always on, like the rows).
    void ZoneBandTooltip(const Zone* z)
    {
        if (!z || !z->HasRects) return;
        std::vector<MapMark> pts; pts.reserve(z->Steps.size());
        for (const Step& s : z->Steps) { uint32_t iid = 0; Objective::TryIconAssetId(s.Type, iid); pts.push_back(MapMark{ s.CX, s.CY, iid }); }
        float zr[4]; ZoneMapCrop(z, zr);   // frame to the zone's playable area, not its (possibly oversized) continent rect
        GroupTooltip(z->Name.c_str(), z->MinLevel, z->MaxLevel, 0, 0, z->ContinentId, zr, pts.data(), (int)pts.size(), true);
    }

    // Region band hover: the whole region's union map ONLY -- no objective pins (a region has far too many to
    // read). Cached by regionId (rebuilt only when a different region is hovered).
    void RegionBandTooltip(App& app, const char* label, int regionId)
    {
        static int s_rid = -0x7fffffff; static float s_rect[4] = {}; static int s_lo = 0, s_hi = 0, s_cont = 1;
        static bool s_has = false;
        if (regionId != s_rid)
        {
            s_rid = regionId; s_has = false; s_lo = 999; s_hi = 0; s_cont = 1;
            for (const auto& kv : app.zones)
            {
                const Zone& z = kv.second;
                if (z.RegionId != regionId || !z.HasRects || z.ContinentId != 1) continue;
                if (!s_has) { s_has = true; s_cont = z.ContinentId; for (int i = 0; i < 4; ++i) s_rect[i] = z.ContRect[i]; s_lo = z.MinLevel; s_hi = z.MaxLevel; }
                else { s_rect[0] = std::min(s_rect[0], z.ContRect[0]); s_rect[1] = std::min(s_rect[1], z.ContRect[1]); s_rect[2] = std::max(s_rect[2], z.ContRect[2]); s_rect[3] = std::max(s_rect[3], z.ContRect[3]); s_lo = std::min(s_lo, z.MinLevel); s_hi = std::max(s_hi, z.MaxLevel); }
            }
        }
        if (!s_has) return;
        GroupTooltip(label, s_lo, s_hi, 0, 0, s_cont, s_rect, nullptr, 0, true);   // map only, no pins
    }

}

// Open the Atlas tab, optionally pre-selecting a browse type filter + map-kind (see header). Clears the search
// text so the grouped browse list (not a stale search) shows. Sets g_atlasInit so the first-draw reset in
// DrawAtlasContent can't wipe the preset (the bug that made "Atlas > Zones" do nothing on a cold tab).
void OpenAtlasTab(App& app, int typeSel, const char* kind)
{
    OpenSettingsTab(app, SettingsTabAtlas);
    if (typeSel >= 0 || kind)
    {
        if (typeSel >= 0) g_atlasFilters.typeSel = typeSel;
        if (kind)         g_atlasFilters.kind    = kind;
        g_atlasFilters.text[0] = '\0';
        g_atlasInit = true;
    }
}

// Open the Atlas tab at one of its browse SCOPES (the row-1 kind chips). The index parallels the tray menu's
// Atlas scope labels AND the kKindChips table in ui/search/SearchRow.cpp -- keep the three in sync.
void OpenAtlasScope(App& app, int index)
{
    static const struct { const char* kind; int typeSel; } kScopes[] = {
        { "", 0 }, { "OpenWorld", 0 }, { "OpenWorld", 6 }, { "City", 0 }, { "Lounge", 0 }, { "WvW", 0 },
        { "Dungeon", 0 }, { "Fractal", 0 }, { "Raid", 0 }, { "Strike", 0 }, { "Story", 0 }, { "Festival", 0 },
        { "Home", 0 }, { "GuildHall", 0 }, { "Tutorial", 0 }, { "PvP", 0 }, { "", 7 },
    };
    const int n = (int)(sizeof(kScopes) / sizeof(kScopes[0]));
    if (index < 0 || index >= n) index = 0;
    OpenAtlasTab(app, kScopes[index].typeSel, kScopes[index].kind);
}

void DrawAtlasContent(App& app)
{
    Search::Filters& f = g_atlasFilters;   // file-scoped so OpenAtlasTab can preselect a type filter
    if (!g_atlasInit) { f.kind = ""; f.typeSel = 0; g_atlasInit = true; }   // open on "All" -> everything, by region (skipped if OpenAtlasTab preset it)
    // Expand/collapse is purely USER-controlled (the Expand all / Collapse all button + the per-band chevrons)
    // and PERSISTS across filter changes -- switching the type or map-kind filter never wipes what you have open.
    // Browse starts collapsed; a stale key for a region not in the current result set is simply ignored.
    static std::set<std::string> g_browseOpen, g_searchOpen;   // expanded region/zone keys (absent = collapsed)
    static bool g_searchGroup = false;                          // search: flat (default) vs region/zone grouped
    Search::EnsureIndex(app);

    // "Atlas text scale" setting -> scale every font + row height in this tab together (default 1.0).
    Gw2Ui::PushTextScale(std::clamp(app.config.atlasTextScale, 0.8f, 1.6f));

    // No key -> zones still browse/search, but level-aware recommendations are off; nudge (dismissible).
    if (!app.api.HasKey())
        ApiReminder::Card(app, "atlas", "level-based zone recommendations", /*gated*/false);

    {
        const float pw = ImGui::GetContentRegionAvail().x;
        const char* t = "Search every waypoint, point of interest, vista, hero point, heart and zone. Click a "
                        "result to travel there (you still confirm the teleport); the clipboard icon copies its waypoint link. "
                        "Hover for a map preview.";
        const float h = Gw2Ui::MeasureWrappedHeight(t, 18.f, pw);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(pw, h));
        Gw2Ui::LabelIn(p, ImVec2(p.x + pw, p.y + h), t, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       IM_COL32(192, 180, 152, 255), false, nullptr, 18.f, pw);
    }
    ImGui::Spacing();

    // The filter bar draws it all in order: search box -> row-1 map-kind chips -> contextual row-2 objective
    // chips -> region dropdown -> level slider.
    Search::DrawFilterBar(app, f, /*compact*/ false);

    const bool browse  = (f.text[0] == '\0');     // no text typed -> browse (always grouped)
    const bool grouped = browse || g_searchGroup; // searches are flat unless the toggle groups them
    // Allow zone bands; BuildContiguous/BuildStable emit a band ONLY for a zone that actually has objectives
    // (>1 entry) -- a place with no objectives (story instance, city, dungeon) shows as a flat row, never a
    // redundant band-then-single-row. So All / Open World group objectives under zones; instance kinds stay flat.
    const bool zoneHeaders = true;
    std::set<std::string>& open = browse ? g_browseOpen : g_searchOpen;

    const std::vector<int>&            res = Search::RunQuery(app, /*slot*/ 0, f, browse);   // browse=region/zone sort; search=relevance
    const std::vector<Search::Entry>&  all = Search::Entries();

    static const char* kHead[] = { "Results", "Waypoints", "Points of Interest", "Vistas", "Hero Points", "Hearts", "Zones", "Nodes" };
    const char* heading = (f.typeSel >= 0 && f.typeSel <= 7) ? kHead[f.typeSel] : "Results";

    // ---- toolbar: [Expand/Collapse all (grouped)]  .....  [Flat/Grouped toggle (search)]  [count] ----
    {
        const float ui = Gw2Ui::GlobalScale();
        const float th = 26.f * ui;
        if (grouped)
        {
            const bool anyOpen = !open.empty();
            if (Gw2Ui::ActionButtonPx(anyOpen ? "Collapse all" : "Expand all", 124.f * ui, th))
            { if (anyOpen) open.clear(); else SeedAllOpen(open, res, all, zoneHeaders); }
        }
        else ImGui::Dummy(ImVec2(1.f, th));

        char cnt[48]; std::snprintf(cnt, sizeof(cnt), "%d %s", (int)res.size(), heading);
        const float cntW = Gw2Ui::MeasureWidth(cnt, 16.f) + 4.f * ui;
        const float grpW = 104.f * ui;
        const float gap = 8.f * ui;
        const float rightW = cntW + (browse ? 0.f : grpW + gap);
        Gw2Ui::SameLineRightCluster(rightW);   // right-align the cluster only if it fits; else leave it beside the button (no left-overlap, no right-overflow)
        if (!browse)   // search: the flat/grouped toggle, beside the count
        {
            if (Gw2Ui::ActionButtonPx(g_searchGroup ? "Grouped" : "Flat list", grpW, th))
            {
                g_searchGroup = !g_searchGroup;
                if (g_searchGroup && g_searchOpen.empty()) SeedAllOpen(g_searchOpen, res, all, zoneHeaders);   // grouping -> start expanded
            }
            ImGui::SameLine(0.f, gap);
        }
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        Gw2Ui::LabelIn(cp, ImVec2(cp.x + cntW, cp.y + th), cnt, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                       Gw2Ui::kTextDim, false, nullptr, 16.f);
        ImGui::Dummy(ImVec2(cntW, th));
    }
    Gw2Ui::Divider(0.f);

    ImGui::BeginChild("##atlaslist", ImVec2(0.f, 0.f), false);
    Search::RowStyle st; st.height = 44.f * Gw2Ui::TextScale(); st.compact = false; st.showCopy = true; st.showFavorite = true; st.tipMap = true;   // Atlas always shows the hover map
    if (res.empty())
    {
        Gw2Ui::EmptyState("No matches.", "Try a different search or clear the filters.");
    }
    else if (grouped)
    {
        static std::vector<Disp> disp;
        static uint64_t s_dispTok = ~0ull;
        const uint64_t dispTok = DispToken(res, open, browse);
        if (dispTok != s_dispTok)   // rebuild (allocs the grouping std::maps) only when result/expand changed
        {
            if (browse) BuildContiguous(res, all, open, zoneHeaders, disp);   // res already region/zone sorted
            else        BuildStable(res, all, open, zoneHeaders, disp);       // res relevance-ordered -> stable group
            s_dispTok = dispTok;
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)disp.size(), st.height);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const Disp& d = disp[i];
                const Search::Entry& e = all[d.idx];
                ImGui::PushID(i);
                if (d.kind == 1)
                {
                    const std::string k = RKey(e.regionLabel);
                    const int r = DrawRegionBand(e.regionLabel.c_str(), st.height, open.count(k) > 0);
                    if (r & 1) { if (open.count(k)) open.erase(k); else open.insert(k); }
                    if (r & 4) RegionBandTooltip(app, e.regionLabel.c_str(), e.regionId);   // map only
                }
                else if (d.kind == 2)
                {
                    const std::string k = ZKey(e.regionLabel, e.zoneName);
                    const int r = DrawZoneBand(e.zoneName.c_str(), st.height, open.count(k) > 0);
                    if (r & 1) { if (open.count(k)) open.erase(k); else open.insert(k); }
                    if (r & 4) ZoneBandTooltip(e.zone);   // map + objective pins
                }
                else { st.stripe = d.stripe; Search::DrawResultRow(app, e, st); }
                ImGui::PopID();
            }
    }
    else   // search, flat (relevance)
    {
        ImGuiListClipper clipper;
        clipper.Begin((int)res.size(), st.height);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                st.stripe = i;
                Search::DrawResultRow(app, all[res[i]], st);
            }
    }
    ImGui::EndChild();
    Gw2Ui::PopTextScale();
}
