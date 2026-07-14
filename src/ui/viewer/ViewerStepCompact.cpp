#include "ui/viewer/ViewerStepCompact.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "ui/viewer/ViewerModeBodies.h"    // DrawDungeonDetailBody (shared dungeon content)
#include "ui/viewer/ViewerEventPanels.h"   // ViewerEvents::DrawTimers/DrawAreas (the Events tab)
#include "ui/viewer/ViewerFarming.h"       // DrawFarmingTab (the Farming tab body)
#include "ui/Gw2Ui.h"
#include "ui/UiStrings.h"
#include "ui/GuideViewer.h"
#include "ui/tabs/MapThumbnail.h"   // ObjectiveTooltip (the shared objective hover)
#include "app/StaticData.h"
#include "guide/Completion.h"
#include "guide/StepStatus.h"
#include "util/Draw.h"
#include "util/Textures.h"
#include "model/ObjectiveTypes.h"
#include "Shared.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr float kCompactDetailInsetL = 22.f;
constexpr float kCompactDetailInsetR = 10.f;
constexpr float kCompactDetailInsetT = 8.f;
constexpr float kCompactDetailInsetB = 8.f;
constexpr float kCompactTitleIconSize = 40.f;
constexpr float kCompactTitleGap = 10.f;
constexpr float kCompactActionGap = 7.f;

struct CompactObjectiveList
{
    std::vector<int> Visible;
    int Remaining = 0;
    int VisibleRemaining = 0;
};

bool CompactUsesLargeTaskTiles(App& app, const Step& step)
{
    return step.Type == "heart" && !step.TaskChips.empty() &&
           TaskChipStyleIndex(app) != TaskChipStyleText &&
           ResolvedTaskIconSize(app, /*compact*/ true) == TaskIconSizeLarge;
}

float EstimateCompactCurrentObjectivePanelHeight(App& app, const Step& step, float fs, float innerWidth)
{
    const float vs = ViewerScale(app);
    const ImGuiStyle& style = ImGui::GetStyle();
    const Objective::Info* info = Objective::Get(step.Type);
    const std::string title = step.Name.empty() ? step.Type : step.Name;
    const float contentW = std::max(80.f, innerWidth);
    const float titleW = std::max(40.f, contentW - kCompactTitleIconSize * vs - kCompactTitleGap * vs);
    const float titleTextH = Gw2Ui::MeasureWrappedHeight(title.c_str(), fs, titleW);
    const float titleH = std::max(kCompactTitleIconSize * vs, titleTextH);
    float h = kCompactDetailInsetT * vs + kCompactDetailInsetB * vs;

    h += titleH + style.ItemSpacing.y;

    const float chipH = std::max(19.f * vs, (fs - 6.f) + 8.f * vs);
    h += chipH + style.ItemSpacing.y;
    h += style.ItemSpacing.y; // ImGui::Spacing() after the metadata row.

    h += EstimateTaskChipsHeight(app, step, fs, contentW, /*compact*/ true);

    if (info && info->GroupName)
        h += Gw2Ui::MeasureWrappedHeight(info->GroupName, fs - 4.f, contentW) + style.ItemSpacing.y;
    if (!IsAutoType(step.Type) && app.state.curArrived)
        h += Gw2Ui::MeasureWrappedHeight("Do it, then tick its box below.", fs - 3.f, contentW) + style.ItemSpacing.y;

    const bool heartDetail = step.Type == "heart" && !step.TaskChips.empty() && !step.WikiText.empty();
    const std::string detail = heartDetail ? step.WikiText : DetailText(step);
    if (!detail.empty())
        h += Gw2Ui::MeasureWrappedHeight(detail.c_str(), fs - 1.f, contentW,
                                         Gw2Ui::Gw2Italic() ? Gw2Ui::Gw2Italic() : nullptr) + style.ItemSpacing.y;

    h += kCompactActionGap * vs + EstimateObjectiveActionButtonsHeight(contentW, true, vs);
    if (titleTextH > kCompactTitleIconSize * vs + 0.5f)
        h += 8.f * vs; // Faux-bold wrapped titles render a touch taller than CalcTextSizeA reports.
    h += CompactUsesLargeTaskTiles(app, step) ? 10.f * vs : 4.f * vs;
    return std::ceil(h);
}

CompactObjectiveList BuildCompactObjectiveList(App& app)
{
    CompactObjectiveList out;
    if (!app.state.zone.Loaded || app.config.showUpcoming > 1) return out;

    const int total = (int)app.state.zone.Steps.size();
    out.Visible.reserve(total);
    for (int i = 0; i < total; ++i)
    {
        const Step& s = app.state.zone.Steps[i];
        if (StepIsDone(app.progress, app.state.currentChar, s)) continue;
        ++out.Remaining;
        if (ObjectiveMatchesRichFilter(s))
            out.Visible.push_back(i);
    }
    out.VisibleRemaining = (int)out.Visible.size();
    return out;
}

float CompactDetailHeight(App& app, const Step& step, float fs, float width, float availableAfterChrome)
{
    const bool largeTiles = CompactUsesLargeTaskTiles(app, step);
    const float desired = EstimateCompactCurrentObjectivePanelHeight(app, step, fs, width);
    const float minH = largeTiles ? std::clamp(availableAfterChrome * 0.24f, 170.f, 235.f)
                                  : std::clamp(availableAfterChrome * 0.18f, 124.f, 158.f);
    const float listReserve = largeTiles ? std::clamp(availableAfterChrome * 0.16f, 96.f, 170.f)
                                         : std::clamp(availableAfterChrome * 0.24f, 132.f, 210.f);
    const float spaceCap = std::max(112.f, availableAfterChrome - 12.f - listReserve);
    const float upper = std::max(112.f, spaceCap);
    const float lower = std::min(minH, upper);
    return std::clamp(desired, lower, upper);
}

float CompactTravelDetailHeight(App& app, float fs, float width, float availableAfterChrome)
{
    const float desired = EstimateTravelTargetPanelHeight(app, fs, width, /*compact*/ true);
    const float minH = std::clamp(availableAfterChrome * 0.18f, 132.f, 170.f);
    const float listReserve = std::clamp(availableAfterChrome * 0.24f, 132.f, 210.f);
    const float upper = std::max(120.f, availableAfterChrome - 12.f - listReserve);
    return std::clamp(desired, std::min(minH, upper), upper);
}

// The Compact "Events" tab is only offered when there is event data for the active map.
static bool EventsTabAvailable(App& app)
{
    return app.state.zone.Loaded && (app.eventTimers.Loaded() || app.eventAreas.Loaded());
}

// The Compact objective-area tab strip: [ Objectives | Events | Farming ], each chip shown only when available.
// The Farming chip toggles the shared State.farmRunActive flag (so it stays in sync with the dashboard toggle
// widget + tray); Objectives/Events set compactTab + clear the run. Shared Gw2Ui::ChipRow (equal-share radio).
static void DrawCompactViewTabs(App& app, float fs, const char* firstLabel, bool eventsAvail, bool farmAvail)
{
    const float vs = ViewerScale(app);
    GuideState& st = app.state;
    const bool farming = farmAvail && st.farmRunActive;
    const float gap = 5.f * vs, h = 26.f * vs;

    // Build the present/available tabs (equal-share radio chips), with a parallel tab-id array. The tab set is
    // dynamic (Events/Farming only when available) and the selection is COMPOUND (compactTab + the shared
    // farmRunActive flag), so map the current tab to a local index, then apply the per-tab side effect on change.
    enum Tab { TObjectives, TEvents, TFarming };
    std::vector<Gw2Ui::ChipItem> items;
    std::vector<Tab> ids;
    items.push_back({ firstLabel }); ids.push_back(TObjectives);
    if (eventsAvail) { items.push_back({ "Events" }); ids.push_back(TEvents); }
    if (farmAvail)   { items.push_back({ "Farming" }); ids.push_back(TFarming); }

    const Tab cur = farming ? TFarming : (st.compactTab == CompactTabEvents ? TEvents : TObjectives);
    int sel = 0;
    for (int i = 0; i < (int)ids.size(); ++i) if (ids[i] == cur) { sel = i; break; }

    if (Gw2Ui::ChipRow("vtabs", items.data(), (int)items.size(), &sel, h, fs - 4.f, gap, /*minShareW*/ 40.f * vs))
    {
        switch (ids[sel])
        {
        case TObjectives: st.farmRunActive = false; st.compactTab = CompactTabBase;   app.settingsDirty = true; break;
        case TEvents:     st.farmRunActive = false; st.compactTab = CompactTabEvents;                            break;
        case TFarming:    st.farmRunActive = true;                                     app.settingsDirty = true; break;
        }
    }
    ImGui::Spacing();
}

void DrawCompactObjectiveChrome(App& app, const CompactObjectiveList& list, float fs)
{
    const ViewerListKind kind = CurrentListKind(app);
    // Objectives + Recommended kinds: offer a [Objectives/Recommended | Events | Farming] tab strip above the
    // filters. The Events + Farming bodies draw their own headers, so skip search / chips / header for them.
    const bool tabKind = (kind == ViewerListKind::Objectives || kind == ViewerListKind::Recommended);
    const bool eventsAvail = tabKind && EventsTabAvailable(app);
    const bool farmAvail   = tabKind && FarmingTabAvailable(app);
    if (app.state.compactTab == CompactTabEvents && !eventsAvail) app.state.compactTab = CompactTabBase;
    if (eventsAvail || farmAvail)
        DrawCompactViewTabs(app, fs, kind == ViewerListKind::Recommended ? "Recommended" : "Objectives", eventsAvail, farmAvail);
    if (farmAvail && app.state.farmRunActive) return;        // farming body draws its own chips + header
    if (app.state.compactTab == CompactTabEvents) return;    // events body draws its own headers

    DrawRichObjectiveTools(app, kind == ViewerListKind::Objectives);   // search + Show-completed always; type chips only for objectives
    if (kind == ViewerListKind::Recommended) return;   // no header -- region rows + the summary card frame it
    if (kind == ViewerListKind::DungeonUpNext)
    { Gw2Ui::SectionHeader("Up next", nullptr, fs - 2.f); return; }
    if (list.Remaining == 0) return;

    char remBuf[32];
    if (list.VisibleRemaining != list.Remaining)
        std::snprintf(remBuf, sizeof(remBuf), "%d shown", list.VisibleRemaining);
    else
        std::snprintf(remBuf, sizeof(remBuf), "%d left", list.Remaining);
    Gw2Ui::SectionHeader("Objectives", remBuf, fs - 2.f);
}

// Now a thin wrapper over the shared DrawObjectiveRow (checkbox + icon + name + level pill + hover + click +
// right-click menu) so the Compact + Rich viewers + the widget render the identical row and never drift.
void DrawCompactObjectiveRow(App& app, int stepIndex, float lfs, float iconSize)
{
    DrawObjectiveRow(app, stepIndex, lfs, iconSize, false);
}

void DrawCompactObjectiveBody(App& app, const CompactObjectiveList& list, float bodyH)
{
    if (bodyH <= 0.f) return;

    const float vs = ViewerScale(app);
    const float lfs = kFontSizePx[std::clamp(app.config.listFontSize, 0, kFontSizeCount - 1)];
    const float iconSize = 26.f * vs;
    const ViewerListKind kind = CurrentListKind(app);
    const bool eventsTab = (kind == ViewerListKind::Objectives || kind == ViewerListKind::Recommended) &&
                           app.state.compactTab == CompactTabEvents && EventsTabAvailable(app);
    ImGui::BeginChild("##compact_obj_body", ImVec2(0.f, bodyH), false);
    if (eventsTab)
    {
        // Same shared cards as the Rich secondary panel -> the two viewers never drift.
        const float efs = kFontSizePx[std::clamp(app.config.viewerFontSize, 0, kFontSizeCount - 1)];
        int rows = ViewerEvents::DrawTimers(app, efs);
        rows += ViewerEvents::DrawAreas(app, std::max(0, app.state.curStep), efs, 8);
        if (rows == 0)
            Gw2Ui::Label("No events for this map.", Gw2Ui::kTextSub, false, nullptr, lfs - 2.f);
    }
    else if (kind == ViewerListKind::Recommended)
    {
        DrawRecommendBody(app);
    }
    else if (kind == ViewerListKind::DungeonUpNext)
    {
        DrawDungeonUpNextBody(app);
    }
    else
    {
        // Objectives: active rows (shared ordering with the Rich list) + a faded "Completed" group when toggled.
        bool anyActive = false;
        for (int i : OrderedObjectiveSteps(app))
        {
            if (std::find(list.Visible.begin(), list.Visible.end(), i) == list.Visible.end()) continue;
            DrawCompactObjectiveRow(app, i, lfs, iconSize); anyActive = true;
        }
        bool anyDone = false;
        if (app.config.viewerShowCompleted)
            for (int i : OrderedObjectiveSteps(app))
            {
                const Step& s = app.state.zone.Steps[i];
                if (!app.progress.IsComplete(app.state.currentChar, s.StepId) || !ObjectiveMatchesRichFilter(s)) continue;
                if (!anyDone) { Gw2Ui::SectionHeader("Completed", nullptr, lfs - 2.f); anyDone = true; }
                DrawObjectiveRow(app, i, lfs, iconSize, true);
            }
        if (!anyActive && !anyDone)
            Gw2Ui::Label("No objectives match the current filter.", Gw2Ui::kTextSub, false, nullptr, lfs - 2.f);
    }
    ImGui::EndChild();
}

// vs is the viewer scale (caller has app); icon size + gap are LAYOUT px and scale with it.
void DrawCompactTitleRow(const Step& c, float fs, ImU32 typeCol, float vs)
{
    const std::string title = c.Name.empty() ? c.Type : c.Name;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float iconSz = kCompactTitleIconSize * vs;
    const float gap = kCompactTitleGap * vs;
    const float titleW = std::max(40.f, ImGui::GetContentRegionAvail().x - iconSz - gap);
    const float titleH = std::max(iconSz, Gw2Ui::MeasureWrappedHeight(title.c_str(), fs, titleW));
    PaintTypeIconAt(ImGui::GetWindowDrawList(), c.Type,
                    ImVec2(p.x, p.y + (titleH - iconSz) * 0.5f), iconSz, true);
    Gw2Ui::LabelIn(ImVec2(p.x + iconSz + gap, p.y),
                   ImVec2(p.x + iconSz + gap + titleW, p.y + titleH),
                   title.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   typeCol, true, nullptr, fs, titleW, 1.2f);
    ImGui::Dummy(ImVec2(iconSz + gap + titleW, titleH));
}

// The shared compact detail-card frame: dark rounded bg + warm border + the left accent stripe (+ an optional
// vertical hairline), then the inset content rect. The four compact detail panels share this instead of each
// re-drawing the identical frame + inset block; the bg/border follow the "Use guide dimming" setting.
struct CompactCardInner { ImVec2 pos, size; };
// vs is the viewer scale (every caller has app); the accent stripe, hairline + content insets are LAYOUT px.
static CompactCardInner DrawCompactCardFrame(ImVec2 p, float w, float height, ImU32 accent, bool hairline, float vs)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + w, p.y + height);
    dl->AddRectFilled(p, b, kViewerCardBg, 3.f);
    dl->AddRect(p, b, kViewerCardBorder, 3.f, 0, 1.f);
    dl->AddRectFilled(ImVec2(p.x + 4.f * vs, p.y + 6.f * vs), ImVec2(p.x + 7.f * vs, b.y - 6.f * vs), accent, 2.f);
    if (hairline)
        dl->AddLine(ImVec2(p.x + 10.f * vs, p.y + 4.f * vs), ImVec2(p.x + 10.f * vs, b.y - 4.f * vs), WithAlpha(accent, 90), 1.f);
    return { ImVec2(p.x + kCompactDetailInsetL * vs, p.y + kCompactDetailInsetT * vs),
             ImVec2(std::max(1.f, w - kCompactDetailInsetL * vs - kCompactDetailInsetR * vs),
                    std::max(1.f, height - kCompactDetailInsetT * vs - kCompactDetailInsetB * vs)) };
}

void DrawCompactCurrentObjectivePanel(App& app, const Step& c, float fs, ImU32 subCol, float height)
{
    const float vs = ViewerScale(app);
    const ImU32 typeCol = Objective::ColorOf(c.Type, 255);
    const Objective::Info* info = Objective::Get(c.Type);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 b(p.x + w, p.y + height);
    const CompactCardInner card = DrawCompactCardFrame(p, w, height, typeCol, /*hairline*/ true, vs);
    const ImVec2 innerP = card.pos;
    const ImVec2 innerSize = card.size;
    const float actionH = EstimateObjectiveActionButtonsHeight(innerSize.x, true, vs);
    const float bodyH = std::max(1.f, innerSize.y - actionH - kCompactActionGap * vs);
    ImGui::SetCursorScreenPos(innerP);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::BeginChild("##compact_current_body", ImVec2(innerSize.x, bodyH), false);
    DrawCompactTitleRow(c, fs, typeCol, vs);

    DrawObjectiveChipRow(app, c, fs, typeCol);   // shared dist/ETA/Lv chips + the "Mark Complete" pill

    ImGui::Spacing();
    DrawHeartTaskChips(app, c, fs, typeCol, /*compact*/ true);
    if (info && info->GroupName)
        Gw2Ui::Label(info->GroupName, WithAlpha(typeCol, 210), true, nullptr, fs - 4.f);
    if (!IsAutoType(c.Type) && app.state.curArrived)
        Gw2Ui::Label("Do it, then tick its box below.", subCol, false, nullptr, fs - 3.f);

    const bool heartDetail = c.Type == "heart" && !c.TaskChips.empty() && !c.WikiText.empty();
    const std::string detail = heartDetail ? c.WikiText : DetailText(c);
    if (!detail.empty())
        DrawWrappedText(detail.c_str(), fs - 1.f, IM_COL32(218, 212, 196, 255), false,
                        Gw2Ui::Gw2Italic() ? Gw2Ui::Gw2Italic() : nullptr);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(innerP.x, innerP.y + bodyH + kCompactActionGap * vs));
    ImGui::BeginChild("##compact_current_actions", ImVec2(innerSize.x, actionH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawObjectiveActionButtons(app, c, true);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(p.x, b.y));
}

void DrawCompactTravelTargetPanel(App& app, float fs, ImU32 subCol, float height)
{
    (void)subCol;
    const float vs = ViewerScale(app);
    const ImU32 accent = app.travel.IsPersonal() ? IM_COL32(255, 150, 40, 255) : IM_COL32(120, 200, 255, 255);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 b(p.x + w, p.y + height);
    ImDrawList* dl = ImGui::GetWindowDrawList();   // also used by the waypoint-icon medallion below
    const CompactCardInner card = DrawCompactCardFrame(p, w, height, accent, /*hairline*/ true, vs);
    const ImVec2 innerP = card.pos;
    const ImVec2 innerSize = card.size;
    const float actionH = EstimateObjectiveActionButtonsHeight(innerSize.x, true, vs);
    const float bodyH = std::max(1.f, innerSize.y - actionH - kCompactActionGap * vs);
    ImGui::SetCursorScreenPos(innerP);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::BeginChild("##compact_travel_body", ImVec2(innerSize.x, bodyH), false);

    // Draw the clear-X INSIDE this child, not the parent: a child window drawn over the parent steals the
    // hover (so a parent-drawn X here was nearly unclickable and half-overdrawn -- compact only). The title
    // row already reserves space for it on the right.
    {
        const ImVec2 bodyP = ImGui::GetCursorScreenPos();
        const float  bodyW = ImGui::GetContentRegionAvail().x;
        DrawTargetClearX(app, CurrentTargetOverride(app), ImVec2(bodyP.x + bodyW - 2.f * vs, bodyP.y), 22.f * vs);
    }

    const std::string title = app.travel.IsPersonal() ? app.travel.PersonalTitle() : ("Traveling to " + app.travel.GoalZoneName());
    const ImVec2 tp = ImGui::GetCursorScreenPos();
    const float iconSz = kCompactTitleIconSize * vs;
    const float gap = kCompactTitleGap * vs;
    const float titleW = std::max(40.f, ImGui::GetContentRegionAvail().x - iconSz - gap - 26.f * vs);
    const float titleH = std::max(iconSz, Gw2Ui::MeasureWrappedHeight(title.c_str(), fs, titleW));
    const ImVec2 iconC(tp.x + iconSz * 0.5f, tp.y + titleH * 0.5f);
    if (app.travel.IsPersonal())
        DrawPersonalPin(dl, iconC, iconSz * 0.28f, true);
    else
    {
        DrawWaypointDestIcon(dl, iconC, iconSz * 0.40f, iconSz * 0.30f, accent);
    }
    Gw2Ui::LabelIn(ImVec2(tp.x + iconSz + gap, tp.y),
                   ImVec2(tp.x + iconSz + gap + titleW, tp.y + titleH),
                   title.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   accent, true, nullptr, fs, titleW, 1.2f);
    ImGui::Dummy(ImVec2(iconSz + gap + titleW, titleH));

    char dist[32]; FormatDistance(dist, sizeof(dist), app.state.curTravel);
    char eta[32];  std::snprintf(eta,  sizeof(eta), "ETA %s", FormatEta(app.state.curTravel, app.guidance.SpeedEma()).c_str());
    Gw2Ui::Chip(dist, Gw2Ui::kChipText, accent, fs - 6.f);
    ImGui::SameLine(0.f, 6.f * vs);
    Gw2Ui::Chip(eta, Gw2Ui::kChipText, accent, fs - 6.f);
    ImGui::Spacing();

    const Travel::WpRef& known = app.travel.Known();
    const Travel::WpRef& fin = app.travel.Final();
    std::string hint;
    if (known.valid)
        hint = "Teleport: " + known.name + " is your nearest unlocked waypoint. Use the map action to choose travel or target display.";
    else if (fin.valid)
        hint = "Closest waypoint: " + fin.name + (app.travel.FinalKnown() ? "" : " (may be locked)") + ". Follow the arrow or use the map action.";
    else
        hint = app.travel.IsPersonal() ? app.travel.PersonalHint()
                                       : std::string("Follow the arrow toward the zone border.");
    DrawWrappedText(hint.c_str(), fs - 1.f, IM_COL32(218, 212, 196, 255), false,
                    Gw2Ui::Gw2Italic() ? Gw2Ui::Gw2Italic() : nullptr);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(innerP.x, innerP.y + bodyH + kCompactActionGap * vs));
    ImGui::BeginChild("##compact_travel_actions", ImVec2(innerSize.x, actionH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawTravelActionButtons(app, true);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(p.x, b.y));
}
}

// Compact DETAIL card for Dungeon: a fixed-height "Do this" card (current instruction + dist/ETA chips).
static float CompactDungeonDetailHeight(App& app, float fs, float width, float avail)
{
    (void)avail;
    // Measure exactly as DrawCompactDungeonDetail draws (real ItemSpacing.y) -> height == content, no gap/scroll.
    const float vs = ViewerScale(app);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float sp = style.ItemSpacing.y;
    const float innerW = std::max(40.f, width - kCompactDetailInsetL * vs - kCompactDetailInsetR * vs);
    const int total = app.state.inst ? (int)app.state.instSteps.size() : 0;
    float h = kCompactDetailInsetT * vs + kCompactDetailInsetB * vs;
    if (total > 0)
    {
        const InstanceMarker* cur = app.state.instSteps[std::min(app.state.instStep, total - 1)];
        h += Gw2Ui::MeasureWrappedHeight("Do this", fs, innerW) + sp;          // "Do this"
        h += std::max(19.f, (fs - 6.f) + 8.f) + sp;                            // dist/ETA chips row
        h += Gw2Ui::MeasureWrappedHeight(cur->Text.c_str(), fs - 1.f, innerW) + sp;   // instruction
    }
    else
        h += Gw2Ui::MeasureWrappedHeight("Follow the route to the end.", fs - 1.f, innerW) + sp;
    h += 12.f + sp;   // divider
    h += 30.f;        // Open guide button
    return std::ceil(h);
}

static void DrawCompactDungeonDetail(App& app, float fs, float height)
{
    const float vs = ViewerScale(app);
    const ImU32 accent = IM_COL32(255, 198, 82, 255);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 b(p.x + w, p.y + height);
    const CompactCardInner card = DrawCompactCardFrame(p, w, height, accent, /*hairline*/ false, vs);
    const ImVec2 innerP = card.pos;
    const ImVec2 innerSize = card.size;
    ImGui::SetCursorScreenPos(innerP);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::BeginChild("##compact_dungeon_body", innerSize, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawDungeonDetailBody(app, fs, /*compact*/ true);   // shared content (no frame); same look as before
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(p.x, b.y));
}

// Compact DETAIL summary card for Complete / Recommend: a check/compass medallion + "Zone complete" + 100%
// bar (or "No guide route") + the "Head here next" framing.
static float CompactSummaryDetailHeight(App& app, float fs, float width, float avail)
{
    (void)avail;
    const float vs = ViewerScale(app);
    // Reserve the EXACT card content (wrap-aware; the "Complete" bar counted at its real ~45px and ONLY in
    // Complete mode) + the frame insets. `width` is already the inner content width, so the labels wrap at it
    // exactly as DrawCompactSummaryDetail draws them. One shared measurement (ViewerModeBodies) mirrors the
    // draw, so the recommended-list area shrinks to fit and the Open-guide button is never clipped.
    return SummaryDetailContentHeight(app, fs, width, 18.f * vs) + kCompactDetailInsetT * vs + kCompactDetailInsetB * vs;
}

static void DrawCompactSummaryDetail(App& app, float fs, float height)
{
    const float vs = ViewerScale(app);
    const bool complete = app.state.viewerMode == ViewerMode::Complete;
    const ImU32 accent = complete ? IM_COL32(120, 235, 130, 255) : IM_COL32(120, 200, 255, 255);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 b(p.x + w, p.y + height);
    const CompactCardInner card = DrawCompactCardFrame(p, w, height, accent, /*hairline*/ false, vs);
    const ImVec2 innerP = card.pos;
    const ImVec2 innerSize = card.size;
    ImGui::SetCursorScreenPos(innerP);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::BeginChild("##compact_summary_body", innerSize, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 ip = ImGui::GetCursorScreenPos();
    const float isz = fs * vs + 8.f * vs;   // medallion icon size (fs is a font size; the icon itself scales)
    const ImVec2 c(ip.x + isz * 0.5f, ip.y + isz * 0.5f);
    const float r = isz * 0.5f;
    ImDrawList* bdl = ImGui::GetWindowDrawList();
    bdl->AddCircleFilled(c, r, IM_COL32(2, 6, 5, 190), 24);
    bdl->AddCircle(c, r, accent, 24, 1.7f);
    if (complete)
    {
        bdl->AddLine(ImVec2(c.x - r * 0.42f, c.y + r * 0.02f), ImVec2(c.x - r * 0.06f, c.y + r * 0.40f), accent, 2.4f);
        bdl->AddLine(ImVec2(c.x - r * 0.06f, c.y + r * 0.40f), ImVec2(c.x + r * 0.46f, c.y - r * 0.36f), accent, 2.4f);
    }
    else
    {
        bdl->AddLine(ImVec2(c.x, c.y - r * 0.58f), ImVec2(c.x, c.y + r * 0.58f), accent, 1.5f);
        bdl->AddLine(ImVec2(c.x - r * 0.58f, c.y), ImVec2(c.x + r * 0.58f, c.y), accent, 1.5f);
        bdl->AddCircleFilled(c, r * 0.18f, accent, 12);
    }
    Gw2Ui::LabelIn(ImVec2(ip.x + isz + 8.f * vs, ip.y), ImVec2(ip.x + innerSize.x, ip.y + isz),
                   complete ? "Zone complete" : "No guide route", Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   complete ? IM_COL32(150, 235, 165, 255) : IM_COL32(150, 205, 255, 255), true, nullptr, fs);
    ImGui::Dummy(ImVec2(isz + 8.f * vs, isz));

    if (complete)
    {
        const int total = (int)app.state.zone.Steps.size();
        char hdr[96]; std::snprintf(hdr, sizeof(hdr), "%s   %d/%d", app.state.zone.Name.c_str(), total, total);
        Gw2Ui::Label(hdr, IM_COL32(220, 245, 210, 255), false, nullptr, fs - 3.f);
        Gw2Ui::ProgressBar(1.f, "Complete", 0.f, 18.f * vs, IM_COL32(120, 235, 130, 255));
    }
    else
    {
        char body[128]; std::snprintf(body, sizeof(body), "%s isn't a guided zone yet.",
                                      StaticData::MapNameOr(CurrentMapId(), "This map"));
        Gw2Ui::Label(body, IM_COL32(220, 216, 202, 255), false, nullptr, fs - 3.f);
    }
    Gw2Ui::Label(app.state.charLevel > 0 ? "Head here next - pick a zone above."
                                         : UiStr::NoKeyPrompt("level-based picks", "picks", fs - 3.f, innerSize.x),
                 Gw2Ui::kTextSub, false, nullptr, fs - 3.f);
    Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 52));
    DrawGuideOnlyButton(app, true);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(ImVec2(p.x, b.y));
}

void DrawCompactStepContent(App& app, int dispStep, float fs, ImU32 subCol)
{
    const float vs = ViewerScale(app);
    const ImVec2 stackStart = ImGui::GetCursorScreenPos();
    const float bottomY = stackStart.y + ImGui::GetContentRegionAvail().y;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float dividerH = 12.f * vs;

    const CompactObjectiveList list = BuildCompactObjectiveList(app);
    DrawCompactObjectiveChrome(app, list, fs);

    const float afterChromeY = ImGui::GetCursorScreenPos().y;

    // Farming run active: the farming body (category chips + material chips + progress list) fills the whole
    // content area below the tab strip -- no separate detail card.
    if (FarmingRunActive(app))
    {
        const float farmH = std::max(1.f, bottomY - afterChromeY);
        ImGui::BeginChild("##compact_farm_tab", ImVec2(0.f, farmH), false);
        DrawFarmingTab(app);
        ImGui::EndChild();
        return;
    }

    const float remainingY = std::max(140.f, bottomY - afterChromeY);
    const float detailInnerW = std::max(80.f, availW - kCompactDetailInsetL * vs - kCompactDetailInsetR * vs);

    const bool travel  = app.travel.Active();
    const bool dungeon = app.state.viewerMode == ViewerMode::Dungeon;
    const bool summary = !travel && !dungeon &&
                         (app.state.viewerMode == ViewerMode::Complete || app.state.viewerMode == ViewerMode::Recommend);
    const bool haveStep = dispStep >= 0 && dispStep < (int)app.state.zone.Steps.size();   // dispStep is -1 when every step is done; the wide path guards this too
    const float detailH = travel  ? CompactTravelDetailHeight(app, fs, detailInnerW, remainingY)
                        : dungeon ? CompactDungeonDetailHeight(app, fs, detailInnerW, remainingY)
                        : summary ? CompactSummaryDetailHeight(app, fs, detailInnerW, remainingY)
                        : haveStep ? CompactDetailHeight(app, app.state.zone.Steps[dispStep], fs, detailInnerW, remainingY)
                                   : 0.f;
    const float bodyH = std::max(1.f, std::floor(bottomY - afterChromeY - detailH - dividerH));

    DrawCompactObjectiveBody(app, list, bodyH);
    ImGui::SetCursorScreenPos(ImVec2(stackStart.x, afterChromeY + bodyH));
    Gw2Ui::Divider();
    ImGui::SetCursorScreenPos(ImVec2(stackStart.x, afterChromeY + bodyH + dividerH));
    if (travel)       DrawCompactTravelTargetPanel(app, fs, subCol, detailH);
    else if (dungeon) DrawCompactDungeonDetail(app, fs, detailH);
    else if (summary) DrawCompactSummaryDetail(app, fs, detailH);
    else if (haveStep) DrawCompactCurrentObjectivePanel(app, app.state.zone.Steps[dispStep], fs, subCol, detailH);
}
