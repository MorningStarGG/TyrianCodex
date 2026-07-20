#include "ui/viewer/ViewerObjectiveList.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"
#include "ui/ZoneRow.h"
#include "ui/wiki/WikiReader.h"   // Wiki::OpenWiki (objective right-click "Open in Wiki")
#include "ui/tabs/MapThumbnail.h" // ObjectiveTooltip (the shared objective hover)
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/Regions.h"
#include "guide/StepStatus.h"
#include "guide/RouteMode.h" // ModeIsNearest (objective-list ordering)
#include "util/Draw.h"
#include "util/Coords.h" // ContinentToWorldXZ (nearest-distance list sort)
#include "util/Textures.h"
#include "model/ObjectiveTypes.h"
#include "render/glyphs/Glyphs.h" // Render::DrawGlyph (the compact Show-completed check-icon chip)
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

// ---- Objective search/filter state (the single owner; the predicate + the search box live here) ----
static char s_objSearch[96] = "";
static int s_objFilter = 0; // 0=all, then Objective::Info order used by Rich viewer filters

static bool ContainsInsensitive(const std::string &haystack, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    const unsigned char first = (unsigned char)needle[0];
    for (size_t i = 0; i < haystack.size(); ++i)
    {
        if (std::tolower((unsigned char)haystack[i]) != std::tolower(first))
            continue;
        size_t h = i, n = 0;
        while (h < haystack.size() && needle[n] &&
               std::tolower((unsigned char)haystack[h]) == std::tolower((unsigned char)needle[n]))
        {
            ++h;
            ++n;
        }
        if (!needle[n])
            return true;
    }
    return false;
}

static const char *ObjectiveFilterType(int filter)
{
    switch (filter)
    {
    case 1:
        return "heart";
    case 2:
        return "waypoint";
    case 3:
        return "vista";
    case 4:
        return "poi";
    case 5:
        return "hero";
    default:
        return "";
    }
}

bool ObjectiveMatchesRichFilter(const Step &s)
{
    const char *type = ObjectiveFilterType(s_objFilter);
    if (type[0] && s.Type != type)
        return false;
    if (!s_objSearch[0])
        return true;
    std::string hay = s.Name + " " + s.Type + " " + s.WikiText + " " + s.WhatToDo;
    for (const TaskChip &chip : s.TaskChips)
    {
        hay += " " + chip.Label + " " + chip.Category;
        for (const std::string &ex : chip.Examples)
            hay += " " + ex;
    }
    return ContainsInsensitive(hay, s_objSearch);
}

// Lets the Recommend / Dungeon list bodies filter against the same search box without touching the state.
bool MatchesObjectiveSearch(const std::string &text) { return ContainsInsensitive(text, s_objSearch); }

void DrawRichObjectiveTools(App &app, bool showChips)
{
    const float vs = ViewerScale(app);
    const float availW = ImGui::GetContentRegionAvail().x;
    Gw2Ui::SearchBox("##objsearch", s_objSearch, sizeof(s_objSearch), availW, "Search...");
    ImGui::Spacing();
    if (!showChips)
        return; // type chips only make sense for the objective checklist

    const float gap = 4.f * vs;
    const float allW = std::min(58.f * vs, std::max(48.f * vs, availW * 0.19f));
    const float iconW = std::clamp((availW - allW - gap * 5.f) / 5.f, 34.f * vs, 46.f * vs);
    const float h = 30.f * vs;

    // RADIO row bound to s_objFilter (the 'filter' value): All + per-type icon chips. The filter values are
    // contiguous (0..5), but we keep a parallel value array + a local index so the mapping is explicit (the
    // ChipFlow change maps back through values[]).
    static const char *const kTypes[] = {"", "heart", "waypoint", "vista", "poi", "hero"};
    static const char *const kLabels[] = {"All", nullptr, nullptr, nullptr, nullptr, nullptr};
    static const char *const kTips[] = {"All objectives", "Hearts", "Waypoints", "Vistas",
                                        "Points of Interest", "Hero Points"};
    static const int values[] = {0, 1, 2, 3, 4, 5};
    const int n = 6;

    Gw2Ui::ChipItem chips[6];
    for (int i = 0; i < n; ++i)
    {
        const char *type = kTypes[i];
        chips[i].label = kLabels[i];
        chips[i].tooltip = kTips[i];
        chips[i].width = (i == 0) ? allW : iconW;
        if (type[0])
            chips[i].drawIcon = [type](ImDrawList *dl, ImVec2 c, float is)
            {
                PaintTypeIconAt(dl, type, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f), is, false);
            };
    }

    int sel = 0;
    for (int i = 0; i < n; ++i)
        if (values[i] == s_objFilter)
        {
            sel = i;
            break;
        }
    if (Gw2Ui::ChipFlow("of", chips, n, &sel, h, 16.f, gap))
        s_objFilter = values[sel];
    // "Show completed" is a compact TOGGLE icon chip (a check glyph) right after the type chips -- reveal completed
    // objectives so they can be unticked / right-clicked -> Mark incomplete. Stays on the chip line when there's
    // room, else wraps to its own line below (narrow widget). A chip, NOT a checkbox above the row.
    {
        const bool sc = app.config.viewerShowCompleted;
        const float chipsW = allW + iconW * 5.f + gap * 5.f; // the type-chip row's single-line width
        if (availW - chipsW >= iconW + gap + 4.f * vs)
            ImGui::SameLine(0.f, gap + 4.f * vs);
        auto drawDone = [sc](ImDrawList *dl, ImVec2 c, float is)
        {
            Render::DrawGlyph(dl, c, is * 0.9f, Render::Glyph::Check,
                              sc ? IM_COL32(150, 220, 140, 255) : IM_COL32(152, 146, 128, 255));
        };
        if (Gw2Ui::SelectableChip("##showdone", sc, ImVec2(iconW, h), nullptr, 16.f,
                                  "Show completed objectives -- untick or right-click to mark incomplete", drawDone))
        {
            app.config.viewerShowCompleted = !sc;
            app.settingsDirty = true;
        }
    }
    ImGui::Spacing();
}

// The active zone's remaining-objective list (tick = complete, click = target). Shared by the normal Step
// view AND the Travel view keeps the zone checklist visible while a personal target is set.
// Single source of truth for the objective-list ORDER, shared by both viewer styles (Compact + Rich) so they
// never drift. NEAREST modes sort the whole list by distance from the player (nearest-first); TRAIL modes
// rotate authored order so the current objective leads. Returns ALL indices; each caller
// applies its own visibility filter and per-style row rendering.
const std::vector<int> &OrderedObjectiveSteps(App &app)
{
    // Cached behind a change token (no per-frame rebuild/sort/alloc -- this is called 2-3x/frame by the
    // viewer list + compact list + the dashboard widget). Rebuild only when the zone/mode/current-step
    // changes, or -- in NEAREST mode, where the order tracks live player distance -- when the player has
    // moved > ~2 m (so it still feels live without re-sorting every frame). Render-thread only -> the
    // function-local statics are safe.
    static std::vector<int> s_order;
    static std::vector<float> s_dsq;
    static uint32_t s_mapId = 0xFFFFFFFFu;
    static int s_total = -1;
    static int s_mode = -2;
    static int s_listStart = -1;
    static std::string s_routeId;
    static float s_px = 1e30f, s_pz = 1e30f;

    const int total = (int)app.state.zone.Steps.size();
    const int mode = app.config.routeMode;
    const uint32_t mapId = app.state.zone.MapId;
    const std::string routeId = app.state.zone.ActiveRouteId;
    const bool nearest = ModeIsNearest(mode) && MumbleLink && app.state.zone.HasRects;
    const float px = nearest ? MumbleLink->AvatarPosition.X : 0.f;
    const float pz = nearest ? MumbleLink->AvatarPosition.Z : 0.f;
    const int listStart = (app.state.curStep >= 0 && app.state.curStep < total) ? app.state.curStep : 0;

    bool rebuild = (mapId != s_mapId) || (total != s_total) || (mode != s_mode) || (routeId != s_routeId);
    if (nearest)
    {
        const float dx = px - s_px, dz = pz - s_pz;
        if (dx * dx + dz * dz > 4.f)
            rebuild = true;
    }
    else if (listStart != s_listStart)
        rebuild = true;

    if (rebuild)
    {
        s_order.resize(total);
        for (int i = 0; i < total; ++i)
            s_order[i] = i;
        if (total > 1)
        {
            if (nearest)
            {
                s_dsq.resize(total);
                for (int i = 0; i < total; ++i)
                {
                    float wx, wz;
                    Coords::ContinentToWorldXZ(app.state.zone.Steps[i].CX, app.state.zone.Steps[i].CY,
                                               app.state.zone.ContRect, app.state.zone.MapRect, wx, wz);
                    const float dx = wx - px, dz = wz - pz;
                    s_dsq[i] = dx * dx + dz * dz;
                }
                std::stable_sort(s_order.begin(), s_order.end(), [](int a, int b)
                                 { return s_dsq[a] < s_dsq[b]; });
            }
            else
                for (int k = 0; k < total; ++k)
                    s_order[k] = (listStart + k) % total;
        }
        s_mapId = mapId;
        s_total = total;
        s_mode = mode;
        s_listStart = listStart;
        s_routeId = routeId;
        s_px = px;
        s_pz = pz;
    }
    return s_order;
}

std::vector<int> UpcomingObjectiveSteps(App &app, int wantN)
{
    // Walk the guide's ordered list (nearest-first in Nearest modes, authored-rotated in Trail modes), skipping
    // the current objective and anything already done (incl. confirmed waypoints) -> the real next-up sequence.
    // In Travel mode the viewer shows the travel target (not a zone step) as current, so skip nothing as current.
    std::vector<int> upcoming;
    if (!app.state.zone.Loaded || app.state.zone.Steps.empty())
        return upcoming;
    const int total = (int)app.state.zone.Steps.size();
    const int curDisp = app.travel.Active() ? -1 : DisplayStepIndex(app, total);
    for (int i : OrderedObjectiveSteps(app))
    {
        if (i == curDisp)
            continue;
        if (StepIsDone(app.progress, app.state.currentChar, app.state.zone.Steps[i]))
            continue;
        upcoming.push_back(i);
        if (wantN > 0 && (int)upcoming.size() >= wantN)
            break;
    }
    return upcoming;
}

void ObjectiveRowMenu(App &app, const Step &s, bool hovered, bool completed)
{
    const bool rclick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    std::vector<Gw2Ui::MenuNode> nodes;
    if (completed)
        nodes.push_back({"Mark incomplete", 2}); // ONLY on completed rows (active rows are already incomplete)
    if (!s.Name.empty())
        nodes.push_back({"Open in Wiki", 1});
    if (nodes.empty())
        return;
    const int a = Gw2Ui::ContextMenuTree("##objrowmenu", nodes, rclick);
    if (a == 1)
        Wiki::OpenWiki(app, s.Name);
    else if (a == 2)
        UncompleteStep(app.progress, app.state, s.StepId);
}

void DrawObjectiveRow(App &app, int stepIndex, float lfs, float iconSize, bool completed)
{
    if (stepIndex < 0 || stepIndex >= (int)app.state.zone.Steps.size())
        return;
    // Layout-px scale tracks the ACTIVE text scale (the caller's PushTextScale), NOT the global ViewerScale: the
    // viewer pushes vs so this is vs there, but the dashboard widget pushes 1.0 to render fixed-size -- keying off
    // the active scale keeps the row geometry matched to the (passed) iconSize + font on BOTH surfaces.
    const float vs = Gw2Ui::TextScale();
    const Step &s = app.state.zone.Steps[stepIndex];
    ImGui::PushID(stepIndex);

    bool ticked = completed;
    if (Gw2Ui::Checkbox("##chk", &ticked))
    {
        if (ticked && !completed)
        {
            CompleteStep(app.progress, app.state, s.StepId);
            if (s.Type == "waypoint")
                app.progress.SetConfirmed(app.state.currentChar, s.StepId, true); // ticking a waypoint unlocks travel
        }
        else if (!ticked && completed)
            UncompleteStep(app.progress, app.state, s.StepId);
    }
    ImGui::SameLine(0.f, 4.f * vs);

    // The ACTUAL drawing width here (not Gw2Ui::ContentWidth(), which is card-aware and returns the OUTER card's
    // width -- wrong inside a widget's nested scroll child, pushing the level pill past the scrollbar/edge).
    const float rw = ImGui::GetContentRegionAvail().x;
    const std::string name = s.Name.empty() ? s.Type : s.Name;
    char lvbuf[16] = "";
    // Drop the level pill on narrow (half-width) lists -- it crowds the name; the row stays clean like the old widget.
    if (s.RecommendedLevel > 0 && rw >= 330.f)
        std::snprintf(lvbuf, sizeof(lvbuf), "Lv %d", s.RecommendedLevel);
    const float iconSlot = iconSize + 9.f * vs;
    const float lvfs = lfs - 2.f, rowRightInset = 8.f * vs, badgeGap = 10.f * vs;
    const float lvW = Gw2Ui::PillWidth(lvbuf, lvfs);
    const float rowRightOff = rw - rowRightInset;
    const float badgeLeftOff = lvbuf[0] ? rowRightOff - lvW : rowRightOff;
    const float textRightOff = lvbuf[0] ? badgeLeftOff - badgeGap : rowRightOff;
    const float textW = std::max(40.f * vs, textRightOff - iconSlot);
    const float wrapH = Gw2Ui::MeasureWrappedHeight(name.c_str(), lfs, textW);
    const float rh = std::max(38.f * vs, wrapH + 8.f * vs);
    const Gw2Ui::RowHotspot row = Gw2Ui::Row("##row", -1, rh, rw, false, !completed && stepIndex == app.state.curStep);
    const ImVec2 rp = row.min;
    const bool hovered = row.hovered;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const int alpha = completed ? 120 : 255; // completed rows render faded
    PaintTypeIconAt(dl, s.Type, ImVec2(rp.x, rp.y + (rh - iconSize) * 0.5f), iconSize, true);
    Gw2Ui::RowLabel(dl, row, iconSlot, std::max(0.f, rw - textRightOff), name.c_str(),
                    Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Objective::ColorOf(s.Type, alpha), false, nullptr, lfs, textW);
    if (lvbuf[0])
        Gw2Ui::PillAt(dl, Gw2Ui::RowRight(row, lvW, Gw2Ui::PillHeight(lvfs), rowRightInset), lvbuf, lvfs);
    if (hovered)
        ObjectiveTooltip(s.Name.c_str(), s.Type.c_str(), &s, &app.state.zone, s.CX, s.CY, app.config.stepHoverStyle == 1);
    if (!completed && row.clicked)
        OnObjectiveTargetClicked(app, s);
    ObjectiveRowMenu(app, s, hovered, completed);
    ImGui::PopID();
}

// Active rows (ordered, filtered) + a faded "Completed" group when the toggle is on. The shared list BODY behind
// the Rich list, the Compact body, and the objective widget panel -> one source of truth, no drift.
static void DrawObjectiveRowsBody(App &app, float lfs, float iconSize, bool applyFilter)
{
    for (int i : OrderedObjectiveSteps(app))
    {
        const Step &s = app.state.zone.Steps[i];
        if (StepIsDone(app.progress, app.state.currentChar, s))
            continue;
        if (applyFilter && !ObjectiveMatchesRichFilter(s))
            continue;
        DrawObjectiveRow(app, i, lfs, iconSize, false);
    }
    if (!app.config.viewerShowCompleted)
        return;
    bool any = false;
    for (int i : OrderedObjectiveSteps(app))
    {
        const Step &s = app.state.zone.Steps[i];
        if (!app.progress.IsComplete(app.state.currentChar, s.StepId))
            continue; // user-completed (uncheckable); confirmed-only waypoints stay hidden
        if (applyFilter && !ObjectiveMatchesRichFilter(s))
            continue;
        if (!any)
        {
            Gw2Ui::SectionHeader("Completed", nullptr, lfs - 2.f);
            any = true;
        }
        DrawObjectiveRow(app, i, lfs, iconSize, true);
    }
}

void DrawObjectiveListPanel(App &app, float height)
{
    // Neutralize the host's ambient text-scale (the dashboard pushes dashTextScale up to 1.6x around widgets) so
    // this renders at the SAME size as the compact viewer -- otherwise the viewer-sized rows (18px list font +
    // 26px medallion) get blown up huge and overflow inside a widget.
    Gw2Ui::PushTextScale(1.0f);
    const float lfs = kFontSizePx[std::clamp(app.config.listFontSize, 0, kFontSizeCount - 1)];

    // Mirror the viewer's list-area dispatch (CurrentListKind): a finished or off-coverage zone -> recommend where
    // to go next; a dungeon -> up-next; otherwise the objective checklist. Reuses the viewer's own bodies, so the
    // widget recommends the next zone when you finish instead of going blank -- exactly like the viewer.
    const ViewerListKind kind = CurrentListKind(app);
    if (kind != ViewerListKind::Objectives)
    {
        DrawRichObjectiveTools(app, false); // search box only -- no type chips outside the objective checklist
        ImGui::BeginChild("##objlistpanel", ImVec2(0.f, height), false);
        if (kind == ViewerListKind::DungeonUpNext)
            DrawDungeonUpNextBody(app);
        else
            DrawRecommendBody(app);
        ImGui::EndChild();
        Gw2Ui::PopTextScale();
        return;
    }

    DrawRichObjectiveTools(app, true);
    ImGui::BeginChild("##objlistpanel", ImVec2(0.f, height), false);
    DrawObjectiveRowsBody(app, lfs, 26.f, true);
    ImGui::EndChild();
    Gw2Ui::PopTextScale();
}

void DrawZoneObjectiveList(App &app, bool fillStep, float maxHeightOverride, float exactTotalHeight)
{
    const float vs = ViewerScale(app);
    const float fs = kFontSizePx[std::clamp(app.config.viewerFontSize, 0, kFontSizeCount - 1)];
    const float lfs = kFontSizePx[std::clamp(app.config.listFontSize, 0, kFontSizeCount - 1)];
    const ViewerListKind kind = CurrentListKind(app);

    // Non-objective kinds (zone complete / off-coverage / dungeon) share this same list slot + scroll child,
    // so every mode renders inside the per-style viewer.
    if (kind != ViewerListKind::Objectives)
    {
        const float blockTopY = ImGui::GetCursorScreenPos().y;
        DrawRichObjectiveTools(app, false); // search box only -- no type chips outside the objective checklist
        if (kind == ViewerListKind::DungeonUpNext)
            Gw2Ui::SectionHeader("Up next", nullptr, fs - 2.f); // recommended rows are self-evident (region headers + the summary card frame them)
        const float chromeH = ImGui::GetCursorScreenPos().y - blockTopY;
        const float childH = (exactTotalHeight > 0.f) ? std::max(1.f, exactTotalHeight - chromeH)
                                                      : (fillStep ? 0.f : std::min(ImGui::GetIO().DisplaySize.y * 0.45f, 420.f));
        ImGui::BeginChild("##objlist", ImVec2(0.f, childH), false);
        if (kind == ViewerListKind::DungeonUpNext)
            DrawDungeonUpNextBody(app);
        else
            DrawRecommendBody(app);
        ImGui::EndChild();
        return;
    }

    if (!app.state.zone.Loaded || app.config.showUpcoming > 1)
        return; // None(3) / Dungeon-only(2) hide it open-world
    const int total = (int)app.state.zone.Steps.size();
    if (total == 0)
        return;
    // Nothing left to list is handled above (a finished zone -> Recommended kind).
    int remaining = 0;
    for (const Step &s : app.state.zone.Steps)
        if (!StepIsDone(app.progress, app.state.currentChar, s))
            ++remaining;
    if (remaining == 0 && !app.config.viewerShowCompleted)
        return; // all done -> show the completed list if toggled, else nothing
    const bool modernStep = app.state.viewerMode == ViewerMode::Step || app.state.viewerMode == ViewerMode::Travel;
    const float blockTopY = ImGui::GetCursorScreenPos().y;

    if (modernStep)
        DrawRichObjectiveTools(app);

    auto rowVisible = [&](const Step &s)
    {
        if (StepIsDone(app.progress, app.state.currentChar, s))
            return false;
        return !modernStep || ObjectiveMatchesRichFilter(s);
    };

    int visibleRemaining = 0;
    for (const Step &s : app.state.zone.Steps)
        if (rowVisible(s))
            ++visibleRemaining;

    char remBuf[32];
    if (modernStep && visibleRemaining != remaining)
        std::snprintf(remBuf, sizeof(remBuf), "%d shown", visibleRemaining);
    else
        std::snprintf(remBuf, sizeof(remBuf), "%d left", remaining);
    Gw2Ui::SectionHeader("Objectives", remBuf, fs - 2.f);
    if (visibleRemaining == 0 && !app.config.viewerShowCompleted)
    {
        Gw2Ui::Label("No objectives match the current filter.", Gw2Ui::kTextSub, false, nullptr, fs - 3.f);
        return;
    }
    // Pre-measure the visible rows so the scroll child sizes to FIT (capped): the window then auto-resizes
    // to the content (no dead space) and only a long list scrolls within the cap.
    const float availW = ImGui::GetContentRegionAvail().x;
    const float iconSize = 26.f * vs;
    const float iconSlot = iconSize + 9.f * vs;
    const float rowRightInset = 8.f * vs;
    const float badgeGap = 10.f * vs;
    const float checkboxColumnW = 36.f * vs;
    float listH = 0.f;
    for (int i = 0; i < total; ++i)
    {
        const Step &s = app.state.zone.Steps[i];
        if (!rowVisible(s))
            continue;
        const std::string nm = s.Name.empty() ? s.Type : s.Name;
        char lb[16] = "";
        if (s.RecommendedLevel > 0)
            std::snprintf(lb, sizeof(lb), "Lv %d", s.RecommendedLevel);
        const float lvW = Gw2Ui::PillWidth(lb, lfs - 2.f);
        const float rowW = std::max(40.f * vs, availW - checkboxColumnW);
        const float textRight = rowW - rowRightInset - (lb[0] ? lvW + badgeGap : 0.f);
        const float textW = std::max(40.f * vs, textRight - iconSlot);
        listH += std::max(38.f * vs, Gw2Ui::MeasureWrappedHeight(nm.c_str(), lfs, textW) + 8.f * vs);
    }
    // Step mode = a user-resizable window, so the list FILLS the remaining height (and scrolls). Travel mode
    // auto-fits the window, so there the list sizes to its content, capped.
    const float maxListH = maxHeightOverride > 0.f ? maxHeightOverride : ImGui::GetIO().DisplaySize.y * 0.55f;
    const float chromeH = ImGui::GetCursorScreenPos().y - blockTopY;
    float childH = 0.f;
    if (exactTotalHeight > 0.f)
        childH = std::max(1.f, exactTotalHeight - chromeH);
    else
        childH = (app.state.viewerMode == ViewerMode::Step && fillStep) ? 0.f : std::min(listH + 2.f * vs, maxListH);
    ImGui::BeginChild("##objlist", ImVec2(0.f, childH), false);
    // The shared list body: active rows (ordered the same way the Compact list uses) + a faded "Completed" group
    // when the Show-completed toggle is on. Centralized -> the Rich/Compact viewers + the widget never drift.
    DrawObjectiveRowsBody(app, lfs, iconSize, modernStep);
    ImGui::EndChild();
}
