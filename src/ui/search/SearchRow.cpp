#include "ui/search/SearchRow.h"
#include "app/App.h"
#include "render/glyphs/Glyphs.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"                    // TravelToLocation, OpenZoneTravelChoice, SetClipboard, ViewerAlert
#include "ui/WaypointFavorites.h"
#include "ui/wiki/WikiReader.h"                 // Wiki::OpenWiki ("Open in Wiki" right-click action)
#include "ui/tabs/MapThumbnail.h"              // DrawMapThumbnail (hover preview)
#include "ui/dashboard/widgets/WidgetUtil.h"   // DashUtil::PlayerContinent / ContinentDist / ContToMeters
#include "util/Draw.h"                          // FormatDistance
#include "util/Textures.h"                      // Tex::GetTextureFromAssetId (zone icon)
#include "model/ObjectiveTypes.h"               // Objective::ColorOf
#include "guide/FarmCategories.h"               // Farm::kCategoryNames (Farming kind's row-2 material chips)
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

void PaintTypeIconAt(ImDrawList* dl, const std::string& type, ImVec2 p, float size, bool medallion);  // ViewerComponents

namespace
{
    bool PlayerPoint(App& app, float& px, float& py, int& cont)
    {
        if (app.state.zone.Loaded && DashUtil::PlayerContinent(app.state.zone, px, py)) { cont = app.state.zone.ContinentId; return true; }
        return false;
    }

    void AppendFooterPart(std::string& footer, const std::string& part)
    {
        if (part.empty()) return;
        if (!footer.empty()) footer += "   ";
        footer += part;
    }

    // Which row-2 objective chips apply to the selected map-KIND (the contextual filter). All + Zones always (the
    // place itself); objectives only where the kind has them. sel: 0=All 1=wp 2=poi 3=vista 4=hero 5=heart
    // 6=zone 7=marker(gathering).
    bool ChipValidForKind(const std::string& kind, int sel)
    {
        if (sel == 0) return true;                             // All
        if (kind == "OpenWorld") return sel >= 1 && sel <= 5;  // waypoints / POIs / vistas / hero / hearts
        if (kind == "City" || kind == "Festival" || kind == "WvW")
            return sel == 1 || sel == 2 || sel == 3;           // waypoints / POIs / vistas
        return false;   // lounges (place-only -- objectives are tag-filtered) / dungeons/fractals/raids/strikes/story/...
    }

    // A short tag for a place's map kind, shown as a pill on every zone/place row so kinds are always identifiable
    // (and same-name maps -- e.g. two "A Fork in the Road" story steps -- read clearly instead of as duplicates).
    std::string KindPillLabel(const std::string& kind)
    {
        if (kind == "OpenWorld") return "Open World";
        if (kind == "GuildHall") return "Guild Hall";
        return kind;   // City / Lounge / WvW / Dungeon / Fractal / Raid / Strike / Story / Festival / Home / Tutorial / PvP
    }

    // Row-1 chips (single-select). Each sets BOTH the map-kind filter and the entry-type selector:
    //   Open World = open-world zones + objectives (the classic "All"); Zones = the open-world zone summary list
    //   (typeSel = zone); Farming = gathering nodes (typeSel = marker) with row-2 material categories; All = every
    //   place; the rest = one map kind. Only the full filter bar draws these (the compact widget returns earlier).
    struct KindChip { const char* label; const char* kind; int typeSel; };   // typeSel: 0 objectives, 6 zone-only, 7 marker
    const KindChip kKindChips[] = {
        { "All", "", 0 }, { "Open World", "OpenWorld", 0 }, { "Zones", "OpenWorld", 6 },   // All = everything (places + objectives); Zones = place rows only
        { "Cities", "City", 0 }, { "Lounges", "Lounge", 0 }, { "WvW", "WvW", 0 },
        { "Dungeons", "Dungeon", 0 }, { "Fractals", "Fractal", 0 }, { "Raids", "Raid", 0 }, { "Strikes", "Strike", 0 },
        { "Story", "Story", 0 }, { "Festival", "Festival", 0 }, { "Home", "Home", 0 },
        { "Guild Halls", "GuildHall", 0 }, { "Tutorials", "Tutorial", 0 }, { "PvP", "PvP", 0 },
        { "Farming", "", 7 },
    };
    void DrawKindChips(App& app, Search::Filters& f)
    {
        static std::set<std::string> present; static size_t builtN = (size_t)-1;
        if (builtN != app.mapIndex.size())
        {
            present.clear();
            for (const MapInfo& mi : app.mapIndex) present.insert(mi.Kind);
            present.insert("OpenWorld");
            builtN = app.mapIndex.size();
        }
        // Build the SHOWN chip set (present-filtered, content-width) + a parallel KindChip* list, then drive the
        // shared flow-wrap chip row. The selection is COMPOUND (kind + typeSel), so map it to/from a local index.
        std::vector<Gw2Ui::ChipItem> items;
        std::vector<const KindChip*> shown;
        int sel = -1;
        for (const KindChip& kc : kKindChips)
        {
            const bool farming = (kc.typeSel == 7);
            const bool alwaysShow = (kc.kind[0] == '\0' && !farming) || std::strcmp(kc.kind, "OpenWorld") == 0;  // All / Open World / Zones
            if (farming) { if (app.farming.empty()) continue; }           // Farming -> only if gathering is bundled
            else if (!alwaysShow && !present.count(kc.kind)) continue;     // a real kind -> only if it has places
            const bool isSel = (f.kind == kc.kind) &&
                               (kc.typeSel == 0 ? (f.typeSel != 6 && f.typeSel != 7) : f.typeSel == kc.typeSel);
            if (isSel) sel = (int)items.size();
            Gw2Ui::ChipItem ci;
            ci.label = kc.label;
            ci.width = Gw2Ui::MeasureWidth(kc.label, 18.f) + 16.f;
            items.push_back(ci);
            shown.push_back(&kc);
        }
        if (Gw2Ui::ChipFlow("kindchips", items.data(), (int)items.size(), &sel, 30.f, 18.f, 4.f) && sel >= 0)
        { f.kind = shown[sel]->kind; f.typeSel = shown[sel]->typeSel; f.gatherCat = 0; }
        ImGui::Spacing();
    }
}

void Search::DrawFilterBar(App& app, Filters& f, bool compact)
{
    Search::EnsureIndex(app);
    const float availW = ImGui::GetContentRegionAvail().x;
    Gw2Ui::SearchBox("##atlassearch", f.text, sizeof(f.text), availW,
                     compact ? "Search..." : "Search waypoints, POIs, hearts, zones...");
    ImGui::Spacing();
    if (compact) return;   // the dashboard widget keeps just the search box (space)
    DrawKindChips(app, f);   // row 1: kind chips (Open World / Zones / Cities / ... / Farming) -> f.kind + f.typeSel

    // Row 2, CONTEXTUAL to the row-1 selection:
    //   Farming kind (typeSel==7) -> material categories (Ore/Wood/...); Zones (typeSel==6) / All / instance kinds
    //   -> nothing (the place rows ARE the content); zone kinds with objectives -> the objective-type chips.
    const float gap = 4.f;
    const float h = 30.f;
    const float iconW = std::clamp(availW * 0.07f, 30.f, 48.f);
    if (f.typeSel == 7)   // Farming -> material category chips (mirror the Farming viewer's row-1)
    {
        Gw2Ui::ChipItem fcats[Farm::kCategoryCount];
        for (int i = 0; i < Farm::kCategoryCount; ++i)
        {
            fcats[i].label = Farm::kCategoryNames[i];
            fcats[i].tooltip = Farm::CategoryDesc(i);
            fcats[i].width = Gw2Ui::MeasureWidth(Farm::kCategoryNames[i], 18.f) + 16.f;
        }
        Gw2Ui::ChipFlow("fcats", fcats, Farm::kCategoryCount, &f.gatherCat, 30.f, 18.f, 4.f);
        ImGui::Spacing();
    }
    else if (f.typeSel != 6 && !f.kind.empty() && ChipValidForKind(f.kind, 1))   // a zone kind that has objectives
    {
        struct Chip { const char* label; const char* type; const char* tip; int sel; };
        static const Chip kChips[] = {
            { "All", "", "All types", 0 },
            { "", "waypoint", "Waypoints", 1 }, { "", "poi", "Points of Interest", 2 }, { "", "vista", "Vistas", 3 },
            { "", "hero", "Hero Points", 4 }, { "", "heart", "Hearts", 5 },
        };
        if (!ChipValidForKind(f.kind, f.typeSel)) f.typeSel = 0;   // selected type no longer applies -> All
        // Build the present-filtered chip set; the selection value (c.sel) is non-contiguous after filtering, so
        // track a local index + a parallel value list. Icon chips draw PaintTypeIconAt; text chips show the label.
        std::vector<Gw2Ui::ChipItem> items;
        std::vector<int> values;
        int sel = -1;
        for (const Chip& c : kChips)
        {
            if (!ChipValidForKind(f.kind, c.sel)) continue;
            const bool icon = c.type && c.type[0] && std::strcmp(c.type, "zone") != 0 && std::strcmp(c.type, "marker") != 0;
            Gw2Ui::ChipItem ci;
            ci.label = icon ? nullptr : (c.label[0] ? c.label : "All");
            ci.tooltip = c.tip;
            ci.width = c.label[0] ? (Gw2Ui::MeasureWidth(c.label, 16.f) + 16.f) : iconW;
            if (icon)
            {
                const char* type = c.type;
                ci.drawIcon = [type](ImDrawList* dl, ImVec2 ctr, float is) {
                    PaintTypeIconAt(dl, type, ImVec2(ctr.x - is * 0.5f, ctr.y - is * 0.5f), is, false);
                };
            }
            if (f.typeSel == c.sel) sel = (int)items.size();
            items.push_back(std::move(ci));
            values.push_back(c.sel);
        }
        if (Gw2Ui::ChipFlow("typechips", items.data(), (int)items.size(), &sel, h, 18.f, gap) && sel >= 0)
            f.typeSel = values[sel];
        ImGui::Spacing();
    }

    std::vector<const char*> regions;
    regions.reserve(Search::RegionOptions().size() + 1);
    regions.push_back("All regions");
    // RegionOptions() is sorted core (Tyrian) regions first, then "Other" (Mists/WvW/PvP/Fractals/festivals). Insert
    // a labeled divider before the first non-core region so the two groups read distinctly.
    int otherAt = -1;
    for (const RegionOpt& r : Search::RegionOptions())
    {
        if (otherAt < 0 && !r.core) otherAt = (int)regions.size();
        regions.push_back(r.label.c_str());
    }
    if (f.regionSel < 0 || f.regionSel >= (int)regions.size()) f.regionSel = 0;
    const int   sectionAt[1]    = { otherAt };
    const char* sectionLabel[1] = { "Other" };
    const int   sectionCount    = (otherAt > 0) ? 1 : 0;
    // Region + status on one row. Each box auto-fits to its longest item (capped inside Gw2Ui::Dropdown); status
    // takes its content width and region gets the rest as its max, so the pair always fits without stacking.
    // Status: All / Recommended (in-band + incomplete) / Incomplete -- Recommended needs an API key.
    static const char* kStatus[] = { "All zones", "Recommended", "Incomplete" };
    const float ui = Gw2Ui::GlobalScale();
    float statusW = 0.f;
    for (const char* s : kStatus) statusW = std::max(statusW, Gw2Ui::MeasureWidth(s, 18.f));
    statusW += 36.f * ui;
    const float rowGap = 8.f * ui;
    Gw2Ui::DropdownPx("##atlasregion", regions.data(), (int)regions.size(), &f.regionSel, std::max(120.f * ui, availW - statusW - rowGap),
                      sectionAt, sectionLabel, sectionCount);
    ImGui::SameLine(0.f, rowGap);
    Gw2Ui::DropdownPx("##atlasstatus", kStatus, 3, &f.statusSel, statusW);
    ImGui::Spacing();
    // Level-range slider: keep entries whose owning-zone band overlaps it (1-80 = off). A manual range browse,
    // independent of the status dropdown.
    Gw2Ui::RangeSliderInt("##atlaslevel", &f.levelMin, &f.levelMax, 1, 80);
    ImGui::Spacing();
}

const std::vector<int>& Search::RunQuery(App& app, int slot, const Filters& f, bool browse)
{
    const bool searching = f.text[0] != '\0';
    uint32_t typeMask = (f.typeSel <= 0) ? 0u : (1u << (f.typeSel - 1));
    std::string kindF = f.kind;
    int gcat = 0;
    if (searching) { kindF.clear(); typeMask = 0u; }   // a text search is GLOBAL across every kind + type
    else if (f.typeSel == 7) gcat = f.gatherCat;       // Farming: the row-2 material category narrows the markers
    const char* regionLabel = "";   // region is selected/filtered by NAME (regionId isn't unique)
    if (f.regionSel >= 1 && f.regionSel - 1 < (int)Search::RegionOptions().size())
        regionLabel = Search::RegionOptions()[f.regionSel - 1].label.c_str();

    float px = 0.f, py = 0.f; int cont = 1;
    const bool haveP = PlayerPoint(app, px, py, cont);

    char lower[64]; size_t n = 0;
    for (; f.text[n] && n < sizeof(lower) - 1; ++n) lower[n] = (char)std::tolower((unsigned char)f.text[n]);
    lower[n] = '\0';

    return Search::Query(app, slot, lower, typeMask, regionLabel, 0u, haveP, cont, px, py, browse,
                         f.levelMin, f.levelMax, f.statusSel, kindF.c_str(), gcat);
}

bool Search::DrawResultRow(App& app, const Entry& e, const RowStyle& style)
{
    const float rh = style.height;              // already scaled by the caller (matches the clipper height)
    const float sc = Gw2Ui::TextScale();        // fonts auto-scale; scale the icon/pill px to match
    ImGui::PushID((const void*)&e);

    // Recommended (green pill) = in-band for your level AND incomplete -- the shared, cached, completion-aware
    // set (Search::IsRecommendedZone). Hidden for non-zone rows and when the caller suppresses it (the
    // all-recommended Recommended-Zones widget).
    const bool recommended = (e.type == "zone" && !style.hideRecommended) && Search::IsRecommendedZone(app, e.mapId);
    // Shared list-row chrome (hit area + stripe + hover highlight) -- same primitive the item rows use, so the
    // hover fill behaves identically. allowOverlap lets the copy / favorite hotspots sit on top of the row.
    const Gw2Ui::RowHotspot row = Gw2Ui::Row("##srow", style.stripe, rh, /*width*/ 0.f, /*allowOverlap*/ true);
    const ImVec2 p  = row.min;
    const float  rw = row.width;
    const bool rowClicked = row.clicked;
    const bool rowHovered = row.hovered;
    const bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool isWaypoint = e.type == "waypoint";
    const std::string link = !e.waypointLink.empty() ? e.waypointLink : (isWaypoint ? e.chatLink : std::string());
    const bool copyNearestWaypoint = !isWaypoint && !e.waypointLink.empty();
    const char* copyTip = copyNearestWaypoint ? "Copy nearest waypoint" : "Copy waypoint link";
    const char* copyAlert = copyNearestWaypoint ? "Copied nearest waypoint link - paste/click to teleport."
                                                : "Copied waypoint link - paste/click to teleport.";
    const float pillFs = style.compact ? 13.f : 18.f;
    float px = 0.f, py = 0.f; int cont = 1;
    char distBuf[24] = "";
    const bool haveDistance = PlayerPoint(app, px, py, cont) && e.continentId == cont;
    if (haveDistance)
        FormatDistance(distBuf, sizeof(distBuf), DashUtil::ContToMeters(DashUtil::ContinentDist(px, py, e.cx, e.cy)));

    char levelBuf[24] = "";
    if (e.level > 0)
    {
        if (e.type == "zone" && e.levelMax > e.level) std::snprintf(levelBuf, sizeof(levelBuf), "Lv %d-%d", e.level, e.levelMax);
        else                                          std::snprintf(levelBuf, sizeof(levelBuf), "Lv %d", e.level);
    }

    // Pre-measure the normal compact columns before drawing them. If they would leave the name column unusably
    // narrow, dashboard compact rows switch to a one-line name-only presentation; Atlas uses compact=false and
    // stays on the full row path for its fixed-height clipper.
    float measuredRightX = p.x + rw - 8.f;
    const float copySlotW = 24.f * sc;
    const bool canFavoriteControl = style.showFavorite && e.type == "waypoint" && !e.stepId.empty() && !e.chatLink.empty();
    if (canFavoriteControl) measuredRightX -= 18.f * sc + 6.f;
    if (style.showCopy) measuredRightX -= copySlotW + 6.f;   // reserve the copy column even without a link, as before
    // gathering "needs a mount" pill: a short label ("Springer" for one, "Mounts" for several); the full list
    // of mounts shows on hover.
    const std::string mountPill = (e.type == "marker" && !e.mount.empty())
        ? (e.mount.find(',') != std::string::npos ? std::string("Mounts") : e.mount) : std::string();
    const std::string kindPill = (e.type == "zone") ? KindPillLabel(e.kind) : std::string();   // every place row is kind-tagged
    // lounge access tag: "Premium" (gem store) / "Earned" (in-game achievement/story); free lounges get no pill
    const char* accessPill = (e.type == "zone" && e.lounge)
        ? (e.access == "premium" ? "Premium" : (e.access == "earned" ? "Earned" : nullptr)) : nullptr;

    const float isz     = (style.compact ? 18.f : 26.f) * sc;
    const float textX   = p.x + 7.f + isz + 9.f;
    const float nameMin = 110.f * sc;
    // Compact rows give the DISTANCE a fixed-width column so the pill directly to its left (level, or the kind tag
    // on a city with no level) lands at the same x on every row regardless of the distance's digit count. Every
    // other pill packs to its CONTENT -- an absent pill (e.g. a city has no level) holds NO blank column.
    const float distColW = style.compact ? Gw2Ui::PillWidth("999 m", pillFs) : 0.f;
    auto reservePill = [&](float& rx, const char* txt) { if (txt && *txt) rx -= Gw2Ui::PillWidth(txt, pillFs) + 6.f; };
    // ESSENTIAL pills (distance + level) always show; the name-only fallback fires only if even those would crush
    // the name. OPTIONAL pills (kind / mount / access / recommended) are SHED one at a time -- instead of the old
    // all-or-nothing -- when including one would push the name below nameMin, so a tight COMPACT row keeps level +
    // distance rather than going blank.
    float essRightX = measuredRightX;
    if (style.compact) essRightX -= distColW + 6.f;   // fixed distance column
    else reservePill(essRightX, distBuf);
    reservePill(essRightX, levelBuf);                  // level packs to content in both modes (no blank column held)
    const bool narrowCompact = style.compact && (rw < 200.f * sc || (essRightX - 4.f) - textX < 80.f * sc);
    float optRightX = essRightX;
    auto wantOpt = [&](const char* txt) -> bool {
        if (!txt || !*txt || narrowCompact) return false;
        if (!style.compact) return true;                                   // full rows (Atlas) keep every pill
        const float w = Gw2Ui::PillWidth(txt, pillFs) + 6.f;
        if ((optRightX - w - 4.f) - textX < nameMin) return false;         // would crush the name -> drop this pill
        optRightX -= w; return true;
    };
    const bool showKind   = wantOpt(kindPill.empty()  ? nullptr : kindPill.c_str());   // highest-priority optional (every zone is tagged)
    const bool showMount  = wantOpt(mountPill.empty() ? nullptr : mountPill.c_str());
    const bool showAccess = wantOpt(accessPill);
    const bool showRec    = wantOpt(recommended ? "Recommended" : nullptr);

    float rightX = p.x + rw - 8.f;

    // copy-waypoint hotspot FIRST -> the copy icon sits at a CONSISTENT rightmost column on every row (the slot
    // is reserved even without a link, so the pills line up; a POI without a nearby-waypoint link shows blank).
    bool copyClicked = false;
    if (style.showCopy && !narrowCompact)
    {
        const float cw = copySlotW;
        if (!link.empty())
        {
            const float hitH = std::min(rh, 24.f * sc);
            ImGui::SetCursorScreenPos(Gw2Ui::RowRight(row, cw, hitH, (p.x + rw) - rightX));
            copyClicked = ImGui::InvisibleButton("##cp", ImVec2(cw, hitH));
            const bool ch = ImGui::IsItemHovered();
            Render::DrawGlyph(dl, ImVec2(rightX - cw * 0.5f, p.y + rh * 0.5f), 18.f * sc,
                              Render::Glyph::Copy, ch ? Gw2Ui::kGold : Gw2Ui::kTextDim,
                              Render::GlyphStyle{});
            if (ch) Gw2Ui::Tooltip(copyTip);
        }
        rightX -= cw + 6.f;
    }

    // favorite star SECOND -> immediately LEFT of the copy icon (waypoints only), so it never pushes copy around.
    bool favoriteClicked = false;
    bool favoritePressed = false;
    bool favoriteWasSet = false;
    if (canFavoriteControl && !narrowCompact)
    {
        const float fw = 18.f * sc;
        const bool confirmed = app.progress.IsConfirmed(app.state.currentChar, e.stepId);
        favoriteWasSet = WaypointFavorites::Contains(app.state.currentChar, e.stepId);
        ImGui::SetCursorScreenPos(Gw2Ui::RowRight(row, fw, fw, (p.x + rw) - rightX));
        const bool click = ImGui::InvisibleButton("##fav", ImVec2(fw, fw));
        const bool hov = ImGui::IsItemHovered();
        favoritePressed = click;
        favoriteClicked = click && confirmed;
        const ImU32 starCol = !confirmed ? IM_COL32(120, 112, 96, 135)
                            : hov       ? Gw2Ui::kGold
                            : favoriteWasSet ? IM_COL32(224, 184, 86, 245)
                                             : Gw2Ui::kTextDim;
        Render::GlyphStyle starStyle;
        starStyle.filled = favoriteWasSet;
        starStyle.disabled = !confirmed;
        starStyle.shadow = false;
        Render::DrawGlyph(dl, ImVec2(rightX - fw * 0.5f, p.y + rh * 0.5f), fw,
                          Render::Glyph::Star, starCol, starStyle);
        if (hov) Gw2Ui::Tooltip(confirmed ? (favoriteWasSet ? "Remove favorite" : "Add favorite") : "Reach this waypoint before favoriting");
        rightX -= fw + 6.f;
    }

    // A right-aligned pill matching the step list's level pill. The pill box is content-sized + right-aligned to
    // its column edge; `colW` (>0) advances by a FIXED column width instead of the content width, so successive
    // columns line up across rows. A null/empty `txt` with colW>0 reserves an EMPTY column (keeps alignment).
    auto pill = [&](const char* txt, ImU32 border = Gw2Ui::kPillBorder, ImU32 textc = Gw2Ui::kPillText, float colW = 0.f) {
        if (txt && *txt)
        {
            const float pw = Gw2Ui::PillWidth(txt, pillFs), ph = Gw2Ui::PillHeight(pillFs);
            Gw2Ui::PillAt(dl, Gw2Ui::RowRight(row, pw, ph, (p.x + rw) - rightX), txt, pillFs, border, textc);
            rightX -= (colW > pw ? colW : pw) + 6.f;
        }
        else if (colW > 0.f) rightX -= colW + 6.f;
    };

    // distance pill (when on the player's continent) -- fixed distance column on compact rows
    if (!narrowCompact) pill(distBuf[0] ? distBuf : nullptr, Gw2Ui::kPillBorder, Gw2Ui::kPillText, distColW);

    // gathering mount-requirement pill (cyan); the full mount list shows in the hover tooltip
    if (showMount)
        pill(mountPill.c_str(), IM_COL32(96, 170, 200, 200), IM_COL32(196, 230, 245, 255));

    // level pill: "Lv x" / "Lv x-y" -- content-sized, drawn ONLY when present (a city with no level holds no gap)
    if (!narrowCompact && levelBuf[0]) pill(levelBuf);

    // map-kind tag pill (Story / Fractal / City / ...) -- so every place row says what it is (shed first when tight)
    if (showKind) pill(kindPill.c_str(), IM_COL32(116, 130, 158, 205), IM_COL32(198, 208, 226, 255));

    // lounge access pill: gold "Premium" (gem-store / real-money pass) vs teal "Earned" (in-game achievement/story)
    if (showAccess)
    {
        if (e.access == "premium") pill(accessPill, IM_COL32(198, 166, 88, 225), IM_COL32(244, 222, 150, 255));
        else                       pill(accessPill, IM_COL32(98, 168, 156, 210), IM_COL32(196, 236, 224, 255));
    }

    // recommended-for-your-level pill (green), drawn LAST so it sits leftmost of the pill cluster (the standout)
    if (showRec) pill("Recommended", Gw2Ui::kPillGoodBorder, Gw2Ui::kPillGoodText);

    // icon (objectives use their type icon; zones get a procedural "map sheet + pin" glyph, NOT the vista icon)
    const ImVec2 ic(p.x + 7.f, p.y + (rh - isz) * 0.5f);
    if (e.type == "zone")
    {
        if (void* t = Tex::GetTextureFromAssetId(563464u)) dl->AddImage((ImTextureID)t, ic, ImVec2(ic.x + isz, ic.y + isz));
    }
    else if (e.type == "marker")   // gathering node: the bundled material icon, else a soft dot
    {
        void* t = e.iconBundled.empty() ? nullptr
                : Tex::GetBundledTexture(("data\\textures\\markers\\gather\\" + e.iconBundled + ".png").c_str());
        if (t) dl->AddImage((ImTextureID)t, ic, ImVec2(ic.x + isz, ic.y + isz));
        else   dl->AddCircleFilled(ImVec2(ic.x + isz * 0.5f, ic.y + isz * 0.5f), isz * 0.3f, IM_COL32(150, 190, 120, 235), 12);
    }
    else PaintTypeIconAt(dl, e.type, ic, isz, false);

    // name (+ a zone/region sub-line when full; zones show just the region, not their own name twice).
    // Auto-shrink ONLY rows whose name doesn't fit (no wrapping) -- short names keep the full size.
    const ImU32 nameCol = (e.type == "zone") ? Gw2Ui::kGold
                        : (e.type == "marker") ? IM_COL32(190, 222, 150, 255)
                        : Objective::ColorOf(e.type, 255);
    const float baseFs   = style.compact ? 16.f : 20.f;
    const float availName = std::max(20.f, (rightX - 4.f) - textX);
    float nameFs = baseFs;
    if (narrowCompact)
    {
        const float narrowRight = p.x + rw - 8.f;
        const float narrowNameW = std::max(0.f, narrowRight - textX);
        const std::string shown = Gw2Ui::Ellipsize(e.name, baseFs, narrowNameW);
        Gw2Ui::LabelIn(ImVec2(textX, p.y), ImVec2(narrowRight, p.y + rh), shown.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, nameCol, false, nullptr, baseFs);
    }
    else if (style.compact)
    {
        const float measured = Gw2Ui::MeasureWidth(e.name.c_str(), baseFs);
        if (measured > availName && measured > 1.f) nameFs = std::max(11.f, baseFs * (availName / measured));
        Gw2Ui::LabelIn(ImVec2(textX, p.y), ImVec2(rightX - 4.f, p.y + rh), e.name.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, nameCol, false, nullptr, nameFs);
    }
    else
    {
        Gw2Ui::LabelIn(ImVec2(textX, p.y + 3.f), ImVec2(rightX - 4.f, p.y + rh * 0.5f + 3.f), e.name.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Bottom, nameCol, false, nullptr, nameFs);
        const std::string sub = (e.type == "zone") ? e.regionLabel
                                                    : (e.zoneName + (e.regionLabel.empty() ? "" : ("   -   " + e.regionLabel)));
        Gw2Ui::LabelIn(ImVec2(textX, p.y + rh * 0.5f + 1.f), ImVec2(rightX - 4.f, p.y + rh - 2.f), sub.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(180, 172, 148, 255), false, nullptr, 16.f);
    }

    // Right-click context menu: copy the waypoint link + open the wiki article. (Replaces the old
    // narrow-mode instant copy; unifies right-click behaviour with the item surfaces.)
    std::vector<Gw2Ui::MenuNode> menuNodes;
    if (!link.empty())   menuNodes.push_back({ "Copy link", 1 });
    if (!e.name.empty()) menuNodes.push_back({ "Open in Wiki", 2 });
    const int menuAction = menuNodes.empty() ? 0
        : Gw2Ui::ContextMenuTree("##atlasRowMenu", menuNodes, rowRightClicked);

    if (favoriteClicked)
    {
        WaypointFavorites::Favorite fav;
        fav.stepId = e.stepId;
        fav.name = e.name;
        fav.zoneName = e.zoneName;
        fav.chatLink = e.chatLink;
        fav.mapId = e.mapId;
        fav.continentId = e.continentId;
        fav.cx = e.cx;
        fav.cy = e.cy;
        WaypointFavorites::Toggle(app.state.currentChar, fav);
        ViewerAlert(favoriteWasSet ? "Removed favorite waypoint." : "Added favorite waypoint.");
    }
    else if (favoritePressed)
    {
        ViewerAlert("Reach this waypoint before favoriting it.");
    }
    else if (menuAction == 1 || copyClicked) { SetClipboard(link); ViewerAlert(copyAlert); }
    else if (menuAction == 2) { Wiki::OpenWiki(app, e.name); }
    else if (rowClicked)
    {
        if (e.type == "zone" && e.chatLink.empty()) OpenZoneTravelChoice(app, e.mapId);   // a real travelable zone
        else if (e.type == "zone")                  TravelToLocation(app, "waypoint", e.mapId, e.cx, e.cy, e.name, e.chatLink);   // instanced (fractal/raid): travel to its entrance WP
        else                                        TravelToLocation(app, e.type, e.mapId, e.cx, e.cy, e.name, e.chatLink);
    }

    // hover tooltip: details always; the zone map (objective circled, or all the zone's objectives pinned)
    // when style.tipMap (Atlas = always; dashboard widgets = their per-widget config.wMapTip* toggle).
    if (rowHovered)
    {
        std::string footer;
        if (narrowCompact)
        {
            if (distBuf[0]) AppendFooterPart(footer, std::string("Distance: ") + distBuf);
            if (e.type != "zone" && levelBuf[0]) AppendFooterPart(footer, std::string("Level: ") + (levelBuf + 3));
            if (recommended) AppendFooterPart(footer, "Recommended");
            if (!link.empty()) AppendFooterPart(footer, copyNearestWaypoint ? "Right-click to copy nearest waypoint" : "Right-click to copy waypoint");
        }
        if (e.type == "marker" && !e.mount.empty())   // list the specific mounts (always, both layouts)
            AppendFooterPart(footer, std::string(e.markerAllMount ? "Mounts (all nodes): " : "Mounts (some nodes): ") + e.mount);
        const char* footerText = footer.empty() ? nullptr : footer.c_str();
        if (e.type == "zone")
        {
            std::vector<MapMark> pts;
            if (e.zone) { pts.reserve(e.zone->Steps.size()); for (const Step& s : e.zone->Steps) { uint32_t iid = 0; Objective::TryIconAssetId(s.Type, iid); pts.push_back(MapMark{ s.CX, s.CY, iid }); } }
            // Bundled zones hover their own rect (cont-1 tiles); non-zone places (lounges/raids/PvP/...) carry the
            // rect + tile maxZoom OR a bundled static-map slug -- so they get a hover map too, not just zones.
            const float* rectPtr = e.zone ? e.zone->ContRect : e.contRect;
            const bool   hasMap  = e.hasRects || !e.staticMap.empty();
            GroupTooltip(e.name.c_str(), e.level, e.levelMax, 0, 0, e.continentId,
                         rectPtr, pts.data(), (int)pts.size(),
                         style.tipMap && hasMap, footerText,
                         e.tileMaxZoom, e.staticMap.empty() ? nullptr : e.staticMap.c_str());
        }
        else
        {
            const std::string sub = e.zoneName + (e.regionLabel.empty() ? "" : ("   -   " + e.regionLabel));
            ObjectiveTooltip(e.name.c_str(), e.type.c_str(), FindStepById(app, e.stepId), e.zone, e.cx, e.cy,
                             style.tipMap, sub.c_str(), footerText);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rh));
    ImGui::PopID();   // balances the PushID(&e) at the top (was previously leaked once per row)
    return rowHovered;
}
