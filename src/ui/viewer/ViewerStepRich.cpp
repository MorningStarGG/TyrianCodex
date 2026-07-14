#include "ui/viewer/ViewerStepRich.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "ui/viewer/ViewerFarming.h"   // DrawFarmingTab (the Farming secondary panel)
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"
#include "ui/tabs/MapThumbnail.h"   // ObjectiveTooltip (the shared objective hover)
#include "guide/StepStatus.h"
#include "util/Coords.h"
#include "util/Draw.h"
#include "model/EventAreas.h"
#include "model/EventTimers.h"
#include "ui/viewer/ViewerEventPanels.h"   // public ViewerEvents::DrawTimers/DrawAreas (defined below, shared with Compact)
#include "Shared.h"          // MumbleLink (live player position for player-relative event-area ranking)
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <set>
#include <string>
#include <vector>

struct RelatedObjective
{
    int   Index = -1;
    float DistSq = 0.f;
};

static void DrawRelatedObjectiveRow(App& app, const RelatedObjective& rel, float fs, int row)
{
    const float vs = ViewerScale(app);
    const Step& s = app.state.zone.Steps[rel.Index];
    const float rowH = 40.f * vs;
    const float iconSize = 26.f * vs;
    const float availW = Gw2Ui::ContentWidth();
    const bool selected = rel.Index == app.state.curStep || s.StepId == app.state.manualTarget;
    ImGui::PushID(rel.Index);
    const Gw2Ui::RowHotspot hit = Gw2Ui::Row("##related", row, rowH, availW, false, selected);
    const ImVec2 p = hit.min;
    const bool clicked = hit.clicked;
    const bool hovered = hit.hovered;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    PaintTypeIconAt(dl, s.Type, ImVec2(p.x + 6.f * vs, p.y + (rowH - iconSize) * 0.5f), iconSize, true);

    char lv[16] = "";
    if (s.RecommendedLevel > 0) std::snprintf(lv, sizeof(lv), "Lv %d", s.RecommendedLevel);
    const float lvFs = std::max(11.f, fs - 7.f);
    const float lvW = Gw2Ui::PillWidth(lv, lvFs);
    const float textXOff = 6.f * vs + iconSize + 9.f * vs;
    const float textW = std::max(44.f * vs, availW - textXOff - lvW - 10.f * vs);
    const std::string name = s.Name.empty() ? s.Type : s.Name;
    Gw2Ui::RowLabel(dl, hit, textXOff, lv[0] ? lvW + 14.f * vs : 8.f * vs, name.c_str(),
                    Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                    Objective::ColorOf(s.Type, 255), false, nullptr, fs - 2.f, textW);

    if (lv[0])
        Gw2Ui::PillAt(dl, Gw2Ui::RowRight(hit, lvW, Gw2Ui::PillHeight(lvFs), 4.f * vs), lv, lvFs);

    if (hovered)
        ObjectiveTooltip(s.Name.c_str(), s.Type.c_str(), &s, &app.state.zone, s.CX, s.CY, app.config.stepHoverStyle == 1);

    if (clicked) OnObjectiveTargetClicked(app, s);
    ImGui::PopID();
}

static void DrawRelatedObjectivesPanel(App& app, int currentIndex, float fs)
{
    if (!ViewerRich(app) || currentIndex < 0 || currentIndex >= (int)app.state.zone.Steps.size()) return;

    // Cached behind a change token (no per-frame rebuild/sort: this draws every frame the secondary panel
    // shows). The order is distance-from-the-current-objective + the done-set, so rebuild only when the zone,
    // current step, character, or progress version changes.
    static std::vector<RelatedObjective> related;
    static uint32_t    s_mapId = 0xFFFFFFFFu;
    static int         s_cur   = -1;
    static uint64_t    s_ver   = (uint64_t)-1;
    static std::string s_char;
    const uint32_t mapId = app.state.zone.MapId;
    const uint64_t ver   = app.progress.Version();
    if (mapId != s_mapId || currentIndex != s_cur || ver != s_ver || app.state.currentChar != s_char)
    {
        const Step& cur = app.state.zone.Steps[currentIndex];
        related.clear();
        related.reserve(app.state.zone.Steps.size());
        for (int i = 0; i < (int)app.state.zone.Steps.size(); ++i)
        {
            if (i == currentIndex) continue;
            const Step& s = app.state.zone.Steps[i];
            if (StepIsDone(app.progress, app.state.currentChar, s)) continue;
            const float dx = s.CX - cur.CX;
            const float dy = s.CY - cur.CY;
            related.push_back({ i, dx * dx + dy * dy });
        }
        std::sort(related.begin(), related.end(), [](const RelatedObjective& a, const RelatedObjective& b) {
            return a.DistSq < b.DistSq;
        });
        s_mapId = mapId; s_cur = currentIndex; s_ver = ver; s_char = app.state.currentChar;
    }

    ImGui::Spacing();
    Gw2Ui::BeginAccentCard("related-objectives", 0.f, IM_COL32(214, 170, 86, 210),
                           IM_COL32(3, 5, 5, 126), IM_COL32(150, 118, 64, 128));
    Gw2Ui::SectionHeader("Nearby", "target another", fs - 3.f);

    const int limit = std::min((int)related.size(), 6);
    if (limit == 0)
    {
        Gw2Ui::Label("No nearby incomplete objectives.", Gw2Ui::kTextSub, false, nullptr, fs - 3.f);
    }
    else
    {
        for (int i = 0; i < limit; ++i)
        {
            ImGui::PushID(related[i].Index);
            DrawRelatedObjectiveRow(app, related[i], fs, i);
            ImGui::PopID();
        }
    }
    Gw2Ui::EndCard();
}

static void DrawRichSecondaryPanel(App& app, int currentIndex, float fs)
{
    if (RichSecondaryIndex(app) == RichSecondaryEvents)
    {
        const int eventRows = ViewerEvents::DrawTimers(app, fs);
        const int areaLimit = eventRows > 0 ? std::max(2, 6 - eventRows) : 6;
        const int areaRows = ViewerEvents::DrawAreas(app, currentIndex, fs, areaLimit);
        if (eventRows + areaRows > 0)
        {
            if (ImGui::GetContentRegionAvail().y > 170.f)
                DrawRelatedObjectivesPanel(app, currentIndex, fs);
            return;
        }
    }
    DrawRelatedObjectivesPanel(app, currentIndex, fs);
}

// The shared event cards (Timed Events + Nearby Event Areas) for the Rich detail column OUTSIDE step mode
// (travel / zone-complete), where there is no current-objective "Nearby objectives" secondary -- so the same
// events you'd see in step mode still show. Areas rank by the player position; the index only gates the panel
// and is the fallback ranking origin.
static void DrawRichEventCards(App& app, float fs)
{
    const int idx = std::max(0, app.state.curStep);
    const int rows = ViewerEvents::DrawTimers(app, fs);
    const int areaLimit = rows > 0 ? std::max(2, 6 - rows) : 6;
    ViewerEvents::DrawAreas(app, idx, fs, areaLimit);
}

static void DrawRichDetailColumn(App& app, int dispStep, float fs, ImU32 subCol)
{
    if (app.travel.Active())
    {
        DrawTravelTargetPanel(app, fs, subCol, false);
        DrawRichEventCards(app, fs);
        return;
    }
    if (app.state.viewerMode == ViewerMode::Dungeon)
    {
        DrawDungeonDetail(app, fs, false);
        return;
    }
    if (app.state.viewerMode == ViewerMode::Complete || app.state.viewerMode == ViewerMode::Recommend)
    {
        DrawSummaryDetailRich(app, fs);
        DrawRichEventCards(app, fs);
        return;
    }
    if (dispStep < 0) return;   // safety
    const Step& c = app.state.zone.Steps[dispStep];
    DrawCurrentObjectivePanel(app, c, fs, subCol, false);
    DrawRichSecondaryPanel(app, dispStep, fs);
}



void DrawRichStepContent(App& app, int dispStep, float fs, ImU32 subCol)
{
    if (FarmingRunActive(app)) { DrawFarmingTab(app); return; }   // a gathering run takes over the rich panel too
    const float vs = ViewerScale(app);
    const float availW = ImGui::GetContentRegionAvail().x;
    const float availY = ImGui::GetContentRegionAvail().y;
    const bool wide = availW >= 700.f;

    if (wide)
    {
        const float gap = 12.f * vs;
        float leftW = std::clamp(availW * 0.47f, 320.f * vs, 520.f * vs);
        if (availW - leftW - gap < 300.f * vs) leftW = std::max(280.f * vs, availW - gap - 300.f * vs);
        ImGui::BeginChild("##rich_objectives_col", ImVec2(leftW, 0.f), false);
        DrawZoneObjectiveList(app);
        ImGui::EndChild();
        ImGui::SameLine(0.f, gap);
        ImGui::BeginChild("##rich_detail_col", ImVec2(0.f, 0.f), false);
        DrawRichDetailColumn(app, dispStep, fs, subCol);
        ImGui::EndChild();
        return;
    }

    const bool travel  = app.travel.Active();
    const bool dungeon = app.state.viewerMode == ViewerMode::Dungeon;
    const bool summary = !travel && !dungeon &&
                         (app.state.viewerMode == ViewerMode::Complete || app.state.viewerMode == ViewerMode::Recommend);
    const bool haveStep = dispStep >= 0 && dispStep < (int)app.state.zone.Steps.size();   // dispStep is -1 when every step is done; the wide path guards this too
    float detailH;
    if (travel) detailH = EstimateTravelTargetPanelHeight(app, fs, availW, false);
    else if (dungeon)
    {
        detailH = fs + (fs - 5.f) + 18.f * vs;   // "Do this" + chips + paddings (fs is text-scaled; only the pad layout px scale)
        const int t = app.state.inst ? (int)app.state.instSteps.size() : 0;
        if (t > 0)
            detailH += Gw2Ui::MeasureWrappedHeight(app.state.instSteps[std::min(app.state.instStep, t - 1)]->Text.c_str(),
                                                   fs - 1.f, std::max(40.f * vs, availW - 26.f * vs));
        detailH += 22.f * vs;   // accent-card padding
        detailH += 42.f * vs;   // divider + Open guide button
    }
    else if (summary) detailH = SummaryDetailContentHeight(app, fs, availW - 24.f * vs, 20.f * vs) + 16.f * vs;   // wrap-aware card content + BeginAccentCard pad (8+8)
    else if (haveStep) detailH = EstimateCurrentObjectivePanelHeight(app, app.state.zone.Steps[dispStep], fs, availW, false);
    else detailH = 0.f;

    const float dividerH = 12.f * vs;
    const float detailSafety = 18.f * vs;
    const float listBlockMin = 120.f * vs;
    const float listBlockH = std::max(listBlockMin, availY - detailH - detailSafety - dividerH);
    DrawZoneObjectiveList(app, false, 0.f, listBlockH);
    Gw2Ui::Divider();
    if (travel)       DrawTravelTargetPanel(app, fs, subCol, false);
    else if (dungeon) DrawDungeonDetail(app, fs, false);
    else if (summary) DrawSummaryDetailRich(app, fs);
    else if (haveStep) DrawCurrentObjectivePanel(app, app.state.zone.Steps[dispStep], fs, subCol, false);
}
