#include "ui/viewer/ViewerTaskChips.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "render/tasks/TaskIcons.h"
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

static void FormatTaskChipLabel(const TaskChip& chip, char* label, size_t labelSize)
{
    if (chip.Count > 1) std::snprintf(label, labelSize, "%s x%d", chip.Label.c_str(), chip.Count);
    else                std::snprintf(label, labelSize, "%s", chip.Label.c_str());
}

static std::string UpperTaskLabel(const std::string& label)
{
    std::string out = label;
    for (char& ch : out)
        ch = (char)std::toupper((unsigned char)ch);
    return out;
}

static bool UseLargeTaskTiles(const App& app, int style, bool compact)
{
    return style != TaskChipStyleText && ResolvedTaskIconSize(app, compact) == TaskIconSizeLarge;
}

static bool UseMediumTaskChips(const App& app, bool compact)
{
    return ResolvedTaskIconSize(app, compact) >= TaskIconSizeMedium;
}

static float TaskChipHeight(float chipFs, bool mediumIcons, float vs)
{
    // chipFs is a FONT size (already scaled by PushTextScale); the literals are LAYOUT px.
    return mediumIcons ? std::max(34.f * vs, chipFs + 17.f * vs) : std::max(24.f * vs, chipFs + 11.f * vs);
}

static float TaskChipWidth(const TaskChip& chip, const char* label, float chipFs, int style, bool mediumIcons, float vs)
{
    const float h = TaskChipHeight(chipFs, mediumIcons, vs);
    if (style == TaskChipStyleIconsOnly) return h;
    const float textW = Gw2Ui::MeasureWidth(label, chipFs);
    if (style == TaskChipStyleIconText) return textW + h + (mediumIcons ? 16.f * vs : 13.f * vs);
    return textW + 17.f * vs;
}

static float TaskTileIconSize(const App& app, bool compact)
{
    const float vs = ViewerScale(app);
    return (compact ? 44.f : 52.f) * vs;
}

static float TaskTileLabelFs(float chipFs)
{
    return std::max(10.f, chipFs - 1.f);
}

static float TaskTileWidth(const App& app, const TaskChip& chip, float chipFs, int style, bool compact)
{
    const float vs = ViewerScale(app);
    const float iconSize = TaskTileIconSize(app, compact); // already scaled
    if (style == TaskChipStyleIconsOnly) return iconSize + (compact ? 20.f * vs : 24.f * vs);

    const float labelFs = TaskTileLabelFs(chipFs);
    const std::string display = UpperTaskLabel(chip.Label);
    const float measured = Gw2Ui::MeasureWidth(display.c_str(), labelFs) + 18.f * vs;
    const float minW = compact ? iconSize + 34.f * vs : iconSize + 30.f * vs;
    const float maxW = compact ? 118.f * vs : 126.f * vs;
    return std::clamp(measured, minW, maxW);
}

static float TaskTileHeight(const App& app, const TaskChip& chip, float chipFs, int style, bool compact)
{
    const float vs = ViewerScale(app);
    const float iconSize = TaskTileIconSize(app, compact); // already scaled
    const float padY = (compact ? 7.f : 9.f) * vs;
    if (style == TaskChipStyleIconsOnly) return iconSize + padY * 2.f;

    const float w = TaskTileWidth(app, chip, chipFs, style, compact); // already scaled
    const float labelFs = TaskTileLabelFs(chipFs);
    const float labelW = std::max(20.f * vs, w - 10.f * vs);
    const std::string display = UpperTaskLabel(chip.Label);
    const float labelH = std::min(Gw2Ui::MeasureWrappedHeight(display.c_str(), labelFs, labelW),
                                  (labelFs + 4.f * vs) * 2.05f);
    return padY + iconSize + 4.f * vs + labelH + padY;
}

static float DrawTaskTileWidget(const App& app, const TaskChip& chip, float chipFs, int style, bool expandedIcons, ImU32 accent, bool compact)
{
    const float vs = ViewerScale(app);
    const float iconSize = TaskTileIconSize(app, compact); // already scaled
    const float w = TaskTileWidth(app, chip, chipFs, style, compact); // already scaled
    const float h = TaskTileHeight(app, chip, chipFs, style, compact); // already scaled
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tasktile", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + w, p.y + h);
    const ImU32 bg = hovered ? IM_COL32(30, 25, 17, 150) : IM_COL32(5, 7, 7, 70);
    const ImU32 br = hovered ? WithAlpha(accent, 220) : IM_COL32(170, 132, 66, 58);
    dl->AddRectFilled(p, b, bg, 3.f);
    dl->AddRect(p, b, br, 3.f, 0, hovered ? 1.2f : 1.f);

    const ImVec2 iconC(p.x + w * 0.5f, p.y + (compact ? 7.f * vs : 9.f * vs) + iconSize * 0.5f);
    DrawTaskIcon(dl, iconC, iconSize, chip, accent, expandedIcons);

    if (chip.Count > 1)
    {
        char count[8];
        std::snprintf(count, sizeof(count), "%d", chip.Count);
        const float badgeFs = 14.f; // FONT size
        const float badgeW = std::max(15.f * vs, Gw2Ui::MeasureWidth(count, badgeFs) + 7.f * vs);
        const ImVec2 bp(iconC.x + iconSize * 0.28f, iconC.y + iconSize * 0.24f);
        const ImVec2 bb(bp.x + badgeW, bp.y + 16.f * vs);
        dl->AddRectFilled(bp, bb, IM_COL32(6, 7, 6, 238), 8.f);
        dl->AddRect(bp, bb, WithAlpha(accent, 240), 8.f, 0, 1.f);
        Gw2Ui::LabelIn(bp, bb, count, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       IM_COL32(255, 232, 170, 255), false, nullptr, badgeFs);
    }

    if (style == TaskChipStyleIconText)
    {
        const std::string display = UpperTaskLabel(chip.Label);
        const float labelFs = TaskTileLabelFs(chipFs);
        const ImVec2 lp(p.x + 5.f * vs, iconC.y + iconSize * 0.5f + 4.f * vs);
        Gw2Ui::LabelIn(lp, ImVec2(b.x - 5.f * vs, b.y - 5.f * vs), display.c_str(),
                       Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Top,
                       IM_COL32(221, 198, 158, 250), false, nullptr, labelFs, w - 10.f * vs);
    }

    return w;
}

static float DrawTaskChipWidget(const TaskChip& chip, const char* label, float chipFs, int style, bool mediumIcons, bool expandedIcons, ImU32 accent, float vs)
{
    if (style == TaskChipStyleText)
    {
        Gw2Ui::Chip(label, IM_COL32(247, 239, 214, 255), accent, chipFs);
        return TaskChipWidth(chip, label, chipFs, style, mediumIcons, vs);
    }

    const float h = TaskChipHeight(chipFs, mediumIcons, vs); // already scaled
    const float iconSize = h - (mediumIcons ? 6.f * vs : 4.f * vs);
    const float w = TaskChipWidth(chip, label, chipFs, style, mediumIcons, vs); // already scaled
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##taskchip", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + w, p.y + h);
    const ImU32 bg = hovered ? IM_COL32(28, 26, 20, 238) : IM_COL32(8, 11, 10, 214);
    dl->AddRectFilled(p, b, bg, h * 0.5f);
    dl->AddRect(p, b, WithAlpha(accent, hovered ? 255 : 200), h * 0.5f, 0, hovered ? 1.45f : 1.05f);

    const ImVec2 iconC(p.x + h * 0.5f, p.y + h * 0.5f);
    DrawTaskIcon(dl, iconC, iconSize, chip, accent, expandedIcons);

    if (style == TaskChipStyleIconText)
    {
        Gw2Ui::LabelIn(ImVec2(p.x + h + (mediumIcons ? 6.f * vs : 4.f * vs), p.y), ImVec2(b.x - 7.f * vs, b.y), label,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                       IM_COL32(247, 239, 214, 255), false, nullptr, chipFs);
    }
    else if (chip.Count > 1)
    {
        char count[8];
        std::snprintf(count, sizeof(count), "%d", chip.Count);
        const float badgeFs = 14.f; // FONT size
        const float badgeW = std::max(11.f * vs, Gw2Ui::MeasureWidth(count, badgeFs) + 5.f * vs);
        const ImVec2 bp(b.x - badgeW, b.y - 12.f * vs), bb(b.x + 1.f * vs, b.y + 1.f * vs);
        dl->AddRectFilled(bp, bb, IM_COL32(6, 7, 6, 235), 6.f);
        dl->AddRect(bp, bb, WithAlpha(accent, 240), 6.f, 0, 1.f);
        Gw2Ui::LabelIn(bp, bb, count, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       IM_COL32(255, 232, 170, 255), false, nullptr, badgeFs);
    }

    return w;
}

static void DrawTaskChipTooltip(const TaskChip& chip)
{
    Gw2Ui::TooltipBegin();
    Gw2Ui::TooltipTitle(chip.Label.c_str());
    if (!chip.Category.empty())
        Gw2Ui::TooltipMuted(chip.Category.c_str());
    const int exampleCount = std::min((int)chip.Examples.size(), 6);
    if (exampleCount > 0)
    {
        Gw2Ui::TooltipSeparator();
        for (int i = 0; i < exampleCount; ++i)
            Gw2Ui::TooltipBullet(chip.Examples[i].c_str());
    }
    const int progressCount = std::min((int)chip.Progress.size(), 4);
    if (progressCount > 0)
    {
        Gw2Ui::TooltipSeparator();
        for (int i = 0; i < progressCount; ++i)
            Gw2Ui::TooltipMuted(chip.Progress[i].c_str());
    }
    Gw2Ui::TooltipEnd();
}

void DrawHeartTaskChips(const App& app, const Step& s, float fs, ImU32 fallbackAccent, bool compact)
{
    if (s.Type != "heart" || s.TaskChips.empty()) return;

    const float vs = ViewerScale(app);
    Gw2Ui::Label("Tasks", IM_COL32(255, 198, 82, 245), true, nullptr, fs - 3.f);
    const float chipFs = std::max(11.f, fs - 6.f); // FONT size
    const float gap = UseLargeTaskTiles(app, TaskChipStyleIndex(app), compact) ? (compact ? 7.f * vs : 9.f * vs) : 6.f * vs;
    const float availW = std::max(80.f, ImGui::GetContentRegionAvail().x - 2.f);
    const int style = TaskChipStyleIndex(app);
    const bool useTiles = UseLargeTaskTiles(app, style, compact);
    const bool mediumIcons = UseMediumTaskChips(app, compact);
    const bool expandedIcons = TaskIconsExpanded(app);
    float lineW = 0.f;

    for (int i = 0; i < (int)s.TaskChips.size(); ++i)
    {
        const TaskChip& chip = s.TaskChips[i];
        char label[80];
        FormatTaskChipLabel(chip, label, sizeof(label));

        const float itemW = useTiles ? TaskTileWidth(app, chip, chipFs, style, compact)
                                     : TaskChipWidth(chip, label, chipFs, style, mediumIcons, vs);
        if (lineW > 0.f && lineW + gap + itemW > availW)
            lineW = 0.f;
        else if (lineW > 0.f)
        {
            ImGui::SameLine(0.f, gap);
            lineW += gap;
        }

        ImGui::PushID(i);
        if (useTiles)
            DrawTaskTileWidget(app, chip, chipFs, style, expandedIcons, TaskChipAccent(chip.Category, fallbackAccent), compact);
        else
            DrawTaskChipWidget(chip, label, chipFs, style, mediumIcons, expandedIcons, TaskChipAccent(chip.Category, fallbackAccent), vs);
        if (ImGui::IsItemHovered()) DrawTaskChipTooltip(chip);
        ImGui::PopID();
        lineW += itemW;
    }
    ImGui::Spacing();
}

float EstimateTaskChipsHeight(const App& app, const Step& s, float fs, float contentW, bool compact)
{
    if (s.Type != "heart" || s.TaskChips.empty()) return 0.f;

    const float vs = ViewerScale(app);
    const float chipFs = std::max(11.f, fs - 6.f); // FONT size
    const float availW = std::max(80.f, contentW - 2.f);
    const int style = TaskChipStyleIndex(app);
    const bool useTiles = UseLargeTaskTiles(app, style, compact);
    const bool mediumIcons = UseMediumTaskChips(app, compact);
    const float gap = useTiles ? (compact ? 7.f * vs : 9.f * vs) : 6.f * vs;
    const float chipRowH = TaskChipHeight(chipFs, mediumIcons, vs);
    float lineW = 0.f;
    float rowsH = 0.f;
    float currentRowH = 0.f;

    for (const TaskChip& chip : s.TaskChips)
    {
        char label[80];
        FormatTaskChipLabel(chip, label, sizeof(label));
        const float itemW = useTiles ? TaskTileWidth(app, chip, chipFs, style, compact)
                                     : TaskChipWidth(chip, label, chipFs, style, mediumIcons, vs);
        const float itemH = useTiles ? TaskTileHeight(app, chip, chipFs, style, compact)
                                     : chipRowH;
        if (lineW > 0.f && lineW + gap + itemW > availW)
        {
            rowsH += currentRowH + gap;
            lineW = itemW;
            currentRowH = itemH;
        }
        else
        {
            lineW += (lineW > 0.f ? gap : 0.f) + itemW;
            currentRowH = std::max(currentRowH, itemH);
        }
    }
    rowsH += currentRowH;

    const float labelH = Gw2Ui::MeasureWrappedHeight("Tasks", fs - 3.f, availW);
    return labelH + 4.f * vs + rowsH + 7.f * vs;
}
