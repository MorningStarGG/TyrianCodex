#include "ui/viewer/ViewerModeBodies.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "ui/Gw2Ui.h"
#include "ui/UiStrings.h"
#include "ui/GuideViewer.h"
#include "ui/ZoneRow.h"
#include "ui/StoryRecommend.h"   // max-level story-progression view (Next / Groups / DrawNextStoryCard)
#include "guide/Story.h"         // StoryData::ReleaseName (release group headers)
#include "app/StaticData.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/Regions.h"
#include "guide/StepStatus.h"
#include "util/Draw.h"
#include "util/Textures.h"
#include "model/ObjectiveTypes.h"
#include "Shared.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// A small medallion icon for the summary card: a green check (zone complete) or a compass (no guide route).
static void DrawSummaryIcon(ImDrawList* dl, ImVec2 c, float r, bool complete)
{
    const ImU32 col = complete ? IM_COL32(120, 235, 130, 255) : IM_COL32(120, 200, 255, 255);
    dl->AddCircleFilled(c, r, IM_COL32(2, 6, 5, 190), 24);
    dl->AddCircle(c, r, col, 24, 1.7f);
    if (complete)
    {
        dl->AddLine(ImVec2(c.x - r * 0.42f, c.y + r * 0.02f), ImVec2(c.x - r * 0.06f, c.y + r * 0.40f), col, 2.4f);
        dl->AddLine(ImVec2(c.x - r * 0.06f, c.y + r * 0.40f), ImVec2(c.x + r * 0.46f, c.y - r * 0.36f), col, 2.4f);
    }
    else   // a small compass cross + hub
    {
        dl->AddLine(ImVec2(c.x, c.y - r * 0.58f), ImVec2(c.x, c.y + r * 0.58f), col, 1.5f);
        dl->AddLine(ImVec2(c.x - r * 0.58f, c.y), ImVec2(c.x + r * 0.58f, c.y), col, 1.5f);
        dl->AddCircleFilled(c, r * 0.18f, col, 12);
    }
}

// The EXACT inner content height of the summary card below -- mirrors DrawSummaryDetailRich / the compact
// DrawCompactSummaryDetail item-for-item: icon row, name/map line (WRAP-AWARE), the "Complete" progress bar in
// Complete mode ONLY (its caption + gap + bar + Dummy pad -- ~45px, NOT the bar height alone), the hint line,
// the divider, and the Open-guide button. Both viewers reserve their recommended-list height from this so the
// list shrinks from the bottom and the button is never clipped (compact frame) or pushed off (rich auto-card).
float SummaryDetailContentHeight(App& app, float fs, float contentW, float barHeight)
{
    const float vs       = ViewerScale(app);   // layout-pixel multiplier (text already scaled by PushTextScale)
    const float sp       = ImGui::GetStyle().ItemSpacing.y;
    const bool  complete = app.state.viewerMode == ViewerMode::Complete;
    const float w        = std::max(40.f, contentW);

    char line[128];
    if (complete) std::snprintf(line, sizeof(line), "%s   %d/%d", app.state.zone.Name.c_str(),
                                (int)app.state.zone.Steps.size(), (int)app.state.zone.Steps.size());
    else          std::snprintf(line, sizeof(line), "%s isn't a guided zone yet.",
                                StaticData::MapNameOr(CurrentMapId(), "This map"));
    const char* hint = app.state.charLevel > 0 ? "Head here next - pick a zone above."
                                               : UiStr::NoKey("level-based picks");

    float h = (fs + 8.f * vs) + sp;                              // icon + "Zone complete"/"No guide route" row (fs is text-scaled)
    h += Gw2Ui::MeasureWrappedHeight(line, fs - 3.f, w) + sp;    // name / map line (wraps if the name is long)
    if (complete)                                                // the "Complete" bar: caption(18) + gap(3) + bar + Dummy pad(6)
        h += (18.f * vs + 3.f * vs + std::max(12.f, barHeight) * vs + 6.f * vs) + sp;
    h += Gw2Ui::MeasureWrappedHeight(hint, fs - 3.f, w) + sp;    // hint line
    h += 12.f * vs + sp;                                         // divider
    h += 30.f * vs;                                              // Open-guide button (DrawGuideOnlyButton, fixed 30)
    return std::ceil(h);
}

// The DETAIL summary card for Complete / Recommend: a green "Zone complete" badge + 100% bar (Complete) or a
// "No guide route" note (Recommend), then the "Head here next - pick a zone above" framing. Auto-height (rich).
void DrawSummaryDetailRich(App& app, float fs)
{
    const bool complete = app.state.viewerMode == ViewerMode::Complete;
    const ImU32 accent = complete ? IM_COL32(120, 235, 130, 220) : IM_COL32(120, 200, 255, 220);
    const float vs = ViewerScale(app);   // layout-pixel multiplier (text already scaled by PushTextScale)
    Gw2Ui::BeginAccentCard("summary-detail", 0.f, accent, kViewerCardBg, kViewerCardBorder);
    const ImVec2 ip = ImGui::GetCursorScreenPos();
    const float isz = fs + 8.f * vs;   // icon size: fs is text-scaled, the +pad is layout
    DrawSummaryIcon(ImGui::GetWindowDrawList(), ImVec2(ip.x + isz * 0.5f, ip.y + isz * 0.5f), isz * 0.5f, complete);
    Gw2Ui::LabelIn(ImVec2(ip.x + isz + 8.f * vs, ip.y), ImVec2(ip.x + isz + 8.f * vs + 280.f * vs, ip.y + isz),
                   complete ? "Zone complete" : "No guide route", Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   complete ? IM_COL32(150, 235, 165, 255) : IM_COL32(150, 205, 255, 255), true, nullptr, fs);
    ImGui::Dummy(ImVec2(isz + 8.f * vs, isz));
    if (complete)
    {
        const int total = (int)app.state.zone.Steps.size();
        char hdr[96]; std::snprintf(hdr, sizeof(hdr), "%s   %d/%d", app.state.zone.Name.c_str(), total, total);
        Gw2Ui::Label(hdr, IM_COL32(220, 245, 210, 255), false, nullptr, fs - 3.f);
        Gw2Ui::ProgressBar(1.f, "Complete", 0.f, 20.f, IM_COL32(120, 235, 130, 255));   // height arg is already *TextScale() inside ProgressBar -- do NOT pre-scale by vs
    }
    else
    {
        char body[128]; std::snprintf(body, sizeof(body), "%s isn't a guided zone yet.",
                                      StaticData::MapNameOr(CurrentMapId(), "This map"));
        Gw2Ui::Label(body, IM_COL32(220, 216, 202, 255), false, nullptr, fs - 3.f);
    }
    Gw2Ui::Label(app.state.charLevel > 0 ? "Head here next - pick a zone above." : UiStr::NoKey("level-based picks"),
                 Gw2Ui::kTextSub, false, nullptr, fs - 3.f);
    Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 52));   // same separator the Step card uses before its buttons
    DrawGuideOnlyButton(app, false);
    Gw2Ui::EndCard();
}

// The dungeon DETAIL content (no outer frame): "Do this" + dist/ETA chips + the current instruction +
// Open-guide button. ONE body shared by the rich viewer, the compact viewer's fixed slot, and the dashboard;
// `compact` reproduces each look exactly (chip size/alpha + button + the rich-only spacing).
void DrawDungeonDetailBody(App& app, float fs, bool compact)
{
    const float vs = ViewerScale(app);   // layout-pixel multiplier (text already scaled by PushTextScale)
    const ImU32 accent = compact ? IM_COL32(255, 198, 82, 255) : IM_COL32(255, 198, 82, 220);
    const float chipFs = compact ? fs - 6.f : fs - 5.f;
    const int total = app.state.inst ? (int)app.state.instSteps.size() : 0;
    const InstanceMarker* cur = (total > 0) ? app.state.instSteps[std::min(app.state.instStep, total - 1)] : nullptr;
    Gw2Ui::Label("Do this", Gw2Ui::kGold, true, nullptr, fs);
    if (cur)
    {
        char dist[32]; FormatDistance(dist, sizeof(dist), app.state.curTravel);
        char eta[32];  std::snprintf(eta,  sizeof(eta), "ETA %s", FormatEta(app.state.curTravel, app.guidance.SpeedEma()).c_str());
        Gw2Ui::Chip(dist, Gw2Ui::kChipText, accent, chipFs);
        ImGui::SameLine(0.f, 6.f * vs);   // chip gap is layout px
        Gw2Ui::Chip(eta, Gw2Ui::kChipText, accent, chipFs);
        if (!compact) ImGui::Spacing();
        DrawWrappedText(cur->Text.c_str(), fs - 1.f, IM_COL32(230, 225, 210, 255));
    }
    else Gw2Ui::Label("Follow the route to the end.", Gw2Ui::kTextSub, false, nullptr, fs - 1.f);
    Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 52));
    DrawGuideOnlyButton(app, compact);
}

// The dungeon DETAIL card, auto-height (rich viewer + dashboard). The compact viewer draws its own fixed-height
// frame and calls DrawDungeonDetailBody directly (it needs a measured slot, not an auto-sized card).
void DrawDungeonDetail(App& app, float fs, bool compact)
{
    if (!app.state.inst) return;
    Gw2Ui::BeginAccentCard("dungeon-current", 0.f, IM_COL32(255, 198, 82, compact ? 255 : 220),
                           kViewerCardBg, kViewerCardBorder);   // accent (stripe) is brighter in compact; card surfaces are the one shared color
    DrawDungeonDetailBody(app, fs, compact);
    Gw2Ui::EndCard();
}

// A "head here next" list: the in-band recommended zones for your level (RecommendZones is in-band now -- no
// 5-cap), grouped by region (region header "Name (lo-hi)" then each zone as a clickable [ Lv x-y ][ Recommended ]
// / [ Active ] row). Shared by the Complete + Recommend list area. Excludes excludeMapId + zones already complete
// for the character. Returns true if it drew any rows.
bool DrawRecommendList(App& app, uint32_t excludeMapId)
{
    UpdateCurrentChar(app.state);
    std::vector<const Zone*> recs;
    for (const Zone* z : app.zoneManager.RecommendZones(app.state.charLevel))
    {
        if (z->MapId == excludeMapId) continue;
        if (!MatchesObjectiveSearch(z->Name)) continue;   // search filters the recommended list
        if (ZoneComplete(app.progress, app.state.currentChar, *z)) continue;   // don't recommend a finished zone
        recs.push_back(z);                                  // all in-band zones (RecommendZones is in-band now), not just 5
    }
    if (recs.empty()) return false;

    // Group by RegionId, ordered by the region's lowest min-level.
    std::map<int, std::vector<const Zone*>> byRegion;
    for (const Zone* z : recs) byRegion[z->RegionId].push_back(z);
    std::vector<const std::vector<const Zone*>*> groups;
    for (auto& kv : byRegion) groups.push_back(&kv.second);
    std::sort(groups.begin(), groups.end(), [](const std::vector<const Zone*>* a, const std::vector<const Zone*>* b) {
        return Regions::MinLevel(*a) < Regions::MinLevel(*b);
    });

    const float vs  = ViewerScale(app);   // layout-pixel multiplier (text already scaled by PushTextScale)
    const float lfs = kFontSizePx[std::clamp(app.config.listFontSize, 0, kFontSizeCount - 1)];
    for (const std::vector<const Zone*>* g : groups)
    {
        int lo = 999, hi = 0;
        for (const Zone* z : *g) { lo = std::min(lo, z->MinLevel); hi = std::max(hi, z->MaxLevel); }
        char hdr[96]; std::snprintf(hdr, sizeof(hdr), "%s   (%d-%d)", Regions::Label(*g).c_str(), lo, hi);
        // Auto-shrink the header to fit a narrow (half-width widget) row instead of clipping -- same pattern the
        // zone-name rows use below.
        const float availW = ImGui::GetContentRegionAvail().x;
        float hfs = lfs;
        const float hm = Gw2Ui::MeasureWidth(hdr, hfs);
        if (hm > availW && hm > 1.f) hfs = std::max(12.f, hfs * (availW / hm));   // 12 = smallest baked size
        Gw2Ui::Label(hdr, Gw2Ui::kTextSelected, false, nullptr, hfs);
        for (const Zone* z : *g)
        {
            ZoneRowStyle style; style.height = 26.f * vs; style.nameFs = lfs; style.actionFs = lfs - 2.f;   // row height is layout px
            style.travelViaCard = true;   // viewer recommended rows open the Travel / Show target / Not now card
            DrawZoneRow(app, z, style);
        }
    }
    return true;
}

// At MAX LEVEL the level band is meaningless (every map is in-band), so we recommend by STORY instead: a
// "next story step" card (with Open Journal + Travel) then areas grouped by release in Journal/release order.
// Union model: a release shows while incomplete on EITHER axis (story not all done OR a zone not mapped); its
// zones are always listed with a mapped check, never hidden (so Living World story in already-mapped zones is
// still surfaced). Map grouping comes from StoryRecommend; rows reuse DrawZoneRow.
static void DrawStoryRecommendBody(App& app)
{
    const float vs     = ViewerScale(app);   // layout-pixel multiplier (text already scaled by PushTextScale)
    const float availW = ImGui::GetContentRegionAvail().x;
    StoryRecommend::DrawNextStoryCard(app, availW, /*compact*/ false);
    Gw2Ui::Divider();

    const float lfs = kFontSizePx[std::clamp(app.config.listFontSize, 0, kFontSizeCount - 1)];
    const std::vector<StoryRecommend::ReleaseGroup>& groups = StoryRecommend::Groups(app);
    bool any = false;
    for (const StoryRecommend::ReleaseGroup& g : groups)
    {
        // zones to show this group (respect the viewer's objective search)
        std::vector<const Zone*> show;
        for (const Zone* z : g.zones) if (MatchesObjectiveSearch(z->Name)) show.push_back(z);
        if (show.empty()) continue;
        any = true;

        StoryRecommend::DrawReleaseHeader(g, lfs);

        int stripe = 0;
        for (const Zone* z : show)
        {
            ZoneRowStyle style;
            style.height = 26.f * vs; style.nameFs = lfs; style.actionFs = lfs - 2.f;   // row height is layout px
            style.travelViaCard   = true;
            style.showRecommended = false;   // release-grouped, not a level-band recommendation
            style.mapped          = ZoneComplete(app.progress, app.state.currentChar, *z);
            style.stripe          = stripe++;
            DrawZoneRow(app, z, style);
        }
    }
    if (!any) Gw2Ui::Label("All story and map completion done.", Gw2Ui::kTextSub, false, nullptr, lfs - 2.f);
}

// LIST AREA body for Complete/Recommend: the recommended-zone list with the loading/keyless/empty fallbacks.
void DrawRecommendBody(App& app)
{
    const float fs = kFontSizePx[std::clamp(app.config.viewerFontSize, 0, kFontSizeCount - 1)];
    if (app.characterLevel.LevelLoading(app.api, app.state))
        Gw2Ui::Label("Loading recommendations...", Gw2Ui::kTextSub, false, nullptr, fs - 2.f);
    else if (app.state.charLevel <= 0)
        Gw2Ui::Label(UiStr::NoKeyPrompt("level-based picks", "picks", fs - 2.f, ImGui::GetContentRegionAvail().x),
                     Gw2Ui::kTextSub, false, nullptr, fs - 2.f);
    else if (app.state.charLevel >= 80)                      // max level -> story progression instead of level band
        DrawStoryRecommendBody(app);
    else if (!DrawRecommendList(app, app.state.zone.MapId))   // zone.MapId is 0 off-coverage -> excludes nothing
        Gw2Ui::Label("No more zones to recommend.", Gw2Ui::kTextSub, false, nullptr, fs - 2.f);
}

// LIST AREA body for Dungeon: the path picker (when >1 route) + the upcoming route instructions.
void DrawDungeonUpNextBody(App& app)
{
    if (!app.state.inst)
    {
        Gw2Ui::Label("Follow the route to the end.", Gw2Ui::kTextSub, false, nullptr, 14.f);
        return;
    }
    const float vs  = ViewerScale(app);   // layout-pixel multiplier (text already scaled by PushTextScale)
    const float fs  = kFontSizePx[std::clamp(app.config.viewerFontSize, 0, kFontSizeCount - 1)];
    const float lfs = kFontSizePx[std::clamp(app.config.listFontSize,   0, kFontSizeCount - 1)];

    if (app.state.inst->Routes.size() > 1)   // path picker P1/P2/...
    {
        Gw2Ui::SectionHeader("Path", nullptr, fs - 3.f);
        for (int i = 0; i < (int)app.state.inst->Routes.size(); ++i)
            if (Gw2Ui::MenuItem(app.state.inst->Routes[i].Name.c_str(), i == app.state.instRouteIdx, 26.f * vs, i))   // row height is layout px
                app.zoneManager.ActivateInstanceRoute(i);
        Gw2Ui::Divider();
    }

    const int total = (int)app.state.instSteps.size();
    const bool showUpcoming = (app.config.showUpcoming == 0 || app.config.showUpcoming == 2);   // All or Dungeon-only
    if (total == 0 || !showUpcoming)
    {
        Gw2Ui::Label("Follow the route to the end.", Gw2Ui::kTextSub, false, nullptr, lfs - 2.f);
        return;
    }
    int shown = 0;
    for (int i = app.state.instStep + 1; i < total && shown < 24; ++i, ++shown)
    {
        if (!MatchesObjectiveSearch(app.state.instSteps[i]->Text)) continue;   // search filters the list
        char buf[220]; std::snprintf(buf, sizeof(buf), "%d. %s", i + 1, app.state.instSteps[i]->Text.c_str());
        const float wrapW = std::max(40.f, ImGui::GetContentRegionAvail().x - 4.f * vs);   // wrap inset is layout px
        const float th = Gw2Ui::MeasureWrappedHeight(buf, lfs, wrapW);
        const ImVec2 tp = ImGui::GetCursorScreenPos();
        Gw2Ui::LabelIn(tp, ImVec2(tp.x + wrapW, tp.y + th), buf, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       IM_COL32(214, 214, 210, 255), false, nullptr, lfs, wrapW);
        ImGui::Dummy(ImVec2(wrapW, th));
    }
}
