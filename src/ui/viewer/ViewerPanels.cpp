#include "ui/viewer/ViewerPanels.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"
#include "ui/ZoneRow.h"
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

static ImU32 TravelAccent(const App& app)
{
    return app.travel.IsPersonal() ? IM_COL32(255, 150, 40, 255) : IM_COL32(120, 200, 255, 255);
}

static std::string TravelTargetTitle(App& app)
{
    return app.travel.IsPersonal() ? app.travel.PersonalTitle() : ("Traveling to " + app.travel.GoalZoneName());
}

static std::string TravelTargetHint(App& app)
{
    const Travel::WpRef& known = app.travel.Known();
    const Travel::WpRef& fin = app.travel.Final();
    if (known.valid)
        return "Teleport: " + known.name + " is your nearest unlocked waypoint. Use the map action to choose travel or target display.";
    if (fin.valid)
        return "Closest waypoint: " + fin.name + (app.travel.FinalKnown() ? "" : " (may be locked)") + ". Follow the arrow or use the map action.";
    return app.travel.IsPersonal()
        ? app.travel.PersonalHint()
        : std::string("Follow the arrow toward the zone border.");
}

float EstimateTravelTargetPanelHeight(App& app, float fs, float cardW, bool compact)
{
    const float vs = ViewerScale(app);
    const float pad = 16.f * vs;
    const float contentW = std::max(80.f, cardW - 18.f * vs);
    const float iconSize = (!compact ? 50.f : 46.f) * vs;
    const std::string title = TravelTargetTitle(app);
    const float titleW = std::max(40.f, contentW - iconSize - 10.f * vs);
    float h = pad;
    h += std::max(iconSize, Gw2Ui::MeasureWrappedHeight(title.c_str(), fs, titleW));
    h += std::max(19.f * vs, (fs - 6.f) + 8.f * vs) + 10.f * vs;
    h += Gw2Ui::MeasureWrappedHeight(TravelTargetHint(app).c_str(), fs - 1.f, contentW) + 12.f * vs;
    h += EstimateObjectiveActionButtonsHeight(contentW, compact, vs);
    return h;
}

float EstimateCurrentObjectivePanelHeight(App& app, const Step& c, float fs, float cardW, bool compact)
{
    const Objective::Info* info = Objective::Get(c.Type);
    const std::string title = c.Name.empty() ? c.Type : c.Name;
    const bool rich = !compact;
    const float vs = ViewerScale(app);
    const float pad = 16.f * vs; // BeginAccentCard top+bottom padding.
    const float contentW = std::max(80.f, cardW - 18.f * vs); // BeginAccentCard accent inset plus wrapped-text guard.
    const float iconSize = (rich ? 50.f : 46.f) * vs;
    const float titleW = std::max(40.f, contentW - iconSize - 10.f * vs);
    float h = pad;

    h += std::max(iconSize, Gw2Ui::MeasureWrappedHeight(title.c_str(), fs, titleW));
    h += std::max(19.f * vs, (fs - 6.f) + 8.f * vs) + 4.f * vs; // metadata chips row.

    const bool richHeartChips = c.Type == "heart" && !c.TaskChips.empty() && !c.WikiText.empty();
    const std::string detail = richHeartChips ? c.WikiText : DetailText(c);
    const bool arrivalHint = !IsAutoType(c.Type) && app.state.curArrived;

    if (rich)
    {
        h += 12.f * vs;
        if (info && info->GroupName)
            h += Gw2Ui::MeasureWrappedHeight(info->GroupName, fs - 4.f, contentW) + 3.f * vs;
        if (arrivalHint)
            h += Gw2Ui::MeasureWrappedHeight("Do it, then tick its box below.", fs - 3.f, contentW) + 2.f * vs;
        if (!detail.empty())
            h += Gw2Ui::MeasureWrappedHeight(detail.c_str(), fs - 1.f, contentW) + 4.f * vs;
        if (c.Type == "heart" && !c.TaskChips.empty())
            h += 12.f * vs + EstimateTaskChipsHeight(app, c, fs, contentW, compact);
        h += 12.f * vs + EstimateObjectiveActionButtonsHeight(contentW, false, vs);
    }
    else
    {
        h += 6.f * vs;
        h += EstimateTaskChipsHeight(app, c, fs, contentW, compact);
        if (info && info->GroupName)
            h += Gw2Ui::MeasureWrappedHeight(info->GroupName, fs - 4.f, contentW) + 3.f * vs;
        if (arrivalHint)
            h += Gw2Ui::MeasureWrappedHeight("Do it, then tick its box below.", fs - 3.f, contentW) + 2.f * vs;
        if (!detail.empty())
            h += Gw2Ui::MeasureWrappedHeight(detail.c_str(), fs - 1.f, contentW) + 6.f * vs;
        h += EstimateObjectiveActionButtonsHeight(contentW, true, vs);
    }

    return h;
}

void DrawTravelTargetPanel(App& app, float fs, ImU32 subCol, bool compact)
{
    (void)subCol;
    if (!app.travel.Active()) return;
    const float vs = ViewerScale(app);
    const ImU32 accent = TravelAccent(app);
    const bool rich = !compact;
    const std::string title = TravelTargetTitle(app);
    const std::string hint = TravelTargetHint(app);
    const ImVec2 cardP = ImGui::GetCursorScreenPos();
    const float cardW = ImGui::GetContentRegionAvail().x;

    Gw2Ui::BeginAccentCard("travel-target-detail", 0.f, accent,
                           rich ? IM_COL32(3, 5, 5, 188) : IM_COL32(0, 0, 0, 86),
                           rich ? IM_COL32(150, 116, 58, 128) : IM_COL32(126, 110, 78, 96));
    DrawTargetClearX(app, CurrentTargetOverride(app), ImVec2(cardP.x + cardW - 8.f * vs, cardP.y + 8.f * vs), 22.f * vs);

    const float iconSize = (rich ? 50.f : 46.f) * vs;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float titleW = std::max(40.f, ImGui::GetContentRegionAvail().x - iconSize - 10.f * vs);
    const float titleH = std::max(iconSize, Gw2Ui::MeasureWrappedHeight(title.c_str(), fs, titleW));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 iconP(p.x, p.y + (titleH - iconSize) * 0.5f);
    if (app.travel.IsPersonal())
        DrawPersonalPin(dl, ImVec2(iconP.x + iconSize * 0.5f, iconP.y + iconSize * 0.5f), iconSize * 0.28f, true);
    else
    {
        const ImVec2 c(iconP.x + iconSize * 0.5f, iconP.y + iconSize * 0.5f);
        DrawWaypointDestIcon(dl, c, iconSize * 0.42f, iconSize * 0.34f, accent);
    }
    Gw2Ui::LabelIn(ImVec2(p.x + iconSize + 10.f * vs, p.y), ImVec2(p.x + iconSize + 10.f * vs + titleW, p.y + titleH),
                   title.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   accent, true, nullptr, fs, titleW, 1.2f);
    ImGui::Dummy(ImVec2(iconSize + 10.f * vs + titleW, titleH));

    char dist[32]; FormatDistance(dist, sizeof(dist), app.state.curTravel);
    char eta[32];  std::snprintf(eta,  sizeof(eta), "ETA %s", FormatEta(app.state.curTravel, app.guidance.SpeedEma()).c_str());
    Gw2Ui::Chip(dist, Gw2Ui::kChipText, accent, fs - 6.f);
    ImGui::SameLine(0.f, 6.f * vs);
    Gw2Ui::Chip(eta, Gw2Ui::kChipText, accent, fs - 6.f);
    Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 62));
    DrawWrappedText(hint.c_str(), fs - 1.f, IM_COL32(218, 212, 196, 255), false,
                    Gw2Ui::Gw2Italic() ? Gw2Ui::Gw2Italic() : nullptr);
    Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 52));
    DrawTravelActionButtons(app, !rich);
    Gw2Ui::EndCard();
}

void DrawObjectiveChipRow(App& app, const Step& c, float fs, ImU32 typeCol)
{
    const float vs        = ViewerScale(app);
    const float chipFs    = fs - 6.f;
    const float rowStartX = ImGui::GetCursorPosX();
    const float availW    = Gw2Ui::ContentWidth();     // card-aware: GetContentRegionAvail over-extends to the
                                                       // window edge inside a card, pushing the pill past the border

    char dist[32]; FormatDistance(dist, sizeof(dist), app.state.curTravel);
    char eta[40];  std::snprintf(eta, sizeof(eta), "ETA %s", FormatEta(app.state.curTravel, app.guidance.SpeedEma()).c_str());
    char lvl[24] = "";
    if (c.RecommendedLevel > 0) std::snprintf(lvl, sizeof(lvl), "Lv %d", c.RecommendedLevel);

    Gw2Ui::Chip(dist, Gw2Ui::kChipText, typeCol, chipFs);
    ImGui::SameLine(0.f, 6.f * vs);
    Gw2Ui::Chip(eta, Gw2Ui::kChipText, typeCol, chipFs);
    if (lvl[0]) { ImGui::SameLine(0.f, 6.f * vs); Gw2Ui::Chip(lvl, IM_COL32(230, 220, 190, 255), typeCol, chipFs); }

    // Right-aligned complete pill on the SAME row: full "Mark Complete" text when it fits, a checkmark-only pill
    // when the row is too narrow (e.g. a 300px dashboard) -- never a 2nd line, so the row height is unchanged.
    // Marks THIS objective done on click (the guide advances to the next).
    ImGui::SameLine();
    const float afterChipsX = ImGui::GetCursorPosX();
    const float rightEdge   = rowStartX + availW - 8.f * vs;     // 8px right inset (accent cards inset left only)
    const float textW       = 19.f * vs + Gw2Ui::MeasureWidth("Mark Complete", chipFs) + 8.f * vs;
    const float checkW      = std::max(std::max(19.f * vs, chipFs + 8.f * vs), 24.f * vs);
    const bool  roomForText = (rightEdge - (afterChipsX + 6.f * vs)) >= textW;
    const float pillW       = roomForText ? textW : checkW;
    ImGui::SetCursorPosX(std::max(afterChipsX + 6.f * vs, rightEdge - pillW));
    if (Gw2Ui::CheckPill("##objdone", roomForText ? "Mark Complete" : nullptr, chipFs, "Mark this objective complete"))
        CompleteStep(app.progress, app.state, c.StepId);    // guide advances next frame
}

void DrawCurrentObjectivePanel(App& app, const Step& c, float fs, ImU32 subCol, bool compact)
{
    const float vs = ViewerScale(app);
    const ImU32 typeCol = Objective::ColorOf(c.Type, 255);
    std::string title = c.Name.empty() ? c.Type : c.Name;
    const Objective::Info* info = Objective::Get(c.Type);

    // iconSize arrives already-scaled from the call site; the 10.f inset is layout px.
    auto drawTitleRow = [&](float iconSize, bool medallion) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float titleW = std::max(40.f, ImGui::GetContentRegionAvail().x - iconSize - 10.f * vs);
        const float titleH = std::max(iconSize, Gw2Ui::MeasureWrappedHeight(title.c_str(), fs, titleW));
        PaintTypeIconAt(ImGui::GetWindowDrawList(), c.Type, ImVec2(p.x, p.y + (titleH - iconSize) * 0.5f), iconSize, medallion);
        Gw2Ui::LabelIn(ImVec2(p.x + iconSize + 10.f * vs, p.y), ImVec2(p.x + iconSize + 10.f * vs + titleW, p.y + titleH),
                       title.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, typeCol,
                       app.config.boldText, nullptr, fs, titleW, app.config.boldText ? 1.2f : -1.f);
        ImGui::Dummy(ImVec2(iconSize + 10.f * vs + titleW, titleH));
    };

    auto drawDetails = [&]() {
        if (!IsAutoType(c.Type) && app.state.curArrived)
            Gw2Ui::Label("Do it, then tick its box below.", subCol, false, nullptr, fs - 3.f);
        const bool richHeartChips = c.Type == "heart" && !c.TaskChips.empty() && !c.WikiText.empty();
        const std::string detail = richHeartChips ? c.WikiText : DetailText(c);
        if (!detail.empty())
            DrawWrappedText(detail.c_str(), fs - 1.f, IM_COL32(218, 212, 196, 255), false,
                            Gw2Ui::Gw2Italic() ? Gw2Ui::Gw2Italic() : nullptr);
    };

    const bool rich = !compact;
    const TargetOverrideKind overrideKind =
        (!app.state.manualTarget.empty() && c.StepId == app.state.manualTarget)
            ? TargetOverrideKind::Manual
            : TargetOverrideKind::None;
    const ImVec2 cardP = ImGui::GetCursorScreenPos();
    const float cardW = ImGui::GetContentRegionAvail().x;
    Gw2Ui::BeginAccentCard("current-objective", 0.f, typeCol,
                           rich ? IM_COL32(3, 5, 5, 188) : IM_COL32(0, 0, 0, 86),
                           rich ? IM_COL32(150, 116, 58, 128) : IM_COL32(126, 110, 78, 96));
    DrawTargetClearX(app, overrideKind, ImVec2(cardP.x + cardW - 8.f * vs, cardP.y + 8.f * vs), 22.f * vs);
    drawTitleRow((rich ? 50.f : 46.f) * vs, true);
    DrawObjectiveChipRow(app, c, fs, typeCol);
    if (rich)
    {
        Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 74));
        if (info && info->GroupName)
            Gw2Ui::Label(info->GroupName, WithAlpha(typeCol, 220), true, nullptr, fs - 4.f);
        drawDetails();
        if (c.Type == "heart" && !c.TaskChips.empty())
        {
            Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 62));
            DrawHeartTaskChips(app, c, fs, typeCol, compact);
        }
    }
    else
    {
        ImGui::Spacing();
        DrawHeartTaskChips(app, c, fs, typeCol, compact);
        if (info && info->GroupName)
            Gw2Ui::Label(info->GroupName, WithAlpha(typeCol, 210), true, nullptr, fs - 4.f);
        drawDetails();
    }
    if (rich)
    {
        Gw2Ui::Divider(0.f, IM_COL32(178, 144, 84, 52));
        DrawObjectiveActionButtons(app, c, false);
    }
    else DrawObjectiveActionButtons(app, c, true);
    Gw2Ui::EndCard();
}
