#include "ui/viewer/ViewerActions.h"
#include "ui/viewer/ViewerComponents.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/viewer/ViewerModel.h"
#include "render/glyphs/Glyphs.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"
#include "ui/SettingsWindow.h"   // OpenSettingsTab (the "Open Hub" detail-card button)
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
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

float EstimateObjectiveActionButtonsHeight(float contentW, bool compact, float vs)
{
    if (compact) return 30.f * vs;
    return contentW >= 390.f * vs ? 30.f * vs : 66.f * vs;
}

namespace
{
enum class ActionIcon { Guide, Map, Copy };

void DrawActionGlyph(ImDrawList* dl, ImVec2 c, ActionIcon icon, ImU32 col, float vs)
{
    Render::Glyph glyph = Render::Glyph::Guide;
    if (icon == ActionIcon::Guide)
        glyph = Render::Glyph::Guide;
    else if (icon == ActionIcon::Map)
        glyph = Render::Glyph::MapPin;
    else
        glyph = Render::Glyph::Copy;
    const float glyphSize = ((glyph == Render::Glyph::MapPin) ? 22.f : 20.f) * vs;   // icon art -> scale
    Render::DrawGlyph(dl, c, glyphSize, glyph, col, Render::GlyphStyle{});
}

bool DrawActionButton(const char* id, float w, float h, ActionIcon icon, ImU32 accent,
                      const char* label, const char* tooltip, bool compact, float vs)
{
    const Gw2Ui::ActionButtonResult r = Gw2Ui::ActionButtonFramePx(id, ImVec2(w, h), Gw2Ui::ActionButtonVariant::Normal,
                                                                    false, tooltip);
    const ImVec2 p = r.min;
    const ImVec2 b = r.max;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool lit = r.hovered || r.held;
    const ImU32 glyphCol = lit ? IM_COL32(255, 220, 150, 255) : IM_COL32(223, 196, 150, 245);
    if (compact || !label || !*label)
    {
        DrawActionGlyph(dl, ImVec2(p.x + w * 0.5f, p.y + h * 0.5f), icon, glyphCol, vs);
    }
    else
    {
        const float iconSlot = 36.f * vs;   // layout icon slot -> scale
        DrawActionGlyph(dl, ImVec2(p.x + 18.f * vs, p.y + h * 0.5f), icon, glyphCol, vs);
        dl->AddLine(ImVec2(p.x + iconSlot, p.y + 5.f * vs), ImVec2(p.x + iconSlot, b.y - 5.f * vs),
                    IM_COL32(160, 126, 70, lit ? 105 : 64), 1.f);
        Gw2Ui::LabelIn(ImVec2(p.x + iconSlot + 8.f * vs, p.y), ImVec2(b.x - 8.f * vs, b.y),
                       label, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       lit ? IM_COL32(255, 232, 184, 255) : IM_COL32(225, 209, 176, 248),
                       false, nullptr, 16.f);
    }

    (void)accent;   // the shared frame owns action-button tint now; keep the local signature stable.
    return r.clicked;
}
}

void DrawObjectiveActionButtons(App& app, const Step& step, bool compact)
{
    const float vs = ViewerScale(app);
    const ImU32 accent = Objective::ColorOf(step.Type, 245);
    const float availW = ImGui::GetContentRegionAvail().x - 8.f * vs;   // reserve right padding (accent cards inset left only)
    if (compact)
    {
        const float gap = 7.f * vs;
        const float h = 30.f * vs;
        const float w = std::max(32.f * vs, (availW - gap * 2.f) / 3.f);
        if (DrawActionButton("##open_guide", w, h, ActionIcon::Guide, accent, nullptr, "Open Hub", true, vs))
            OpenSettingsTab(app, SettingsTabHub);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##show_map", w, h, ActionIcon::Map, accent, nullptr, "Show on Map", true, vs))
            ShowStepOnMap(app, step);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##copy_wp", w, h, ActionIcon::Copy, accent, nullptr, "Copy waypoint", true, vs))
            CopyWaypointForStep(app, step);
        return;
    }

    const float gap = 8.f * vs;
    const float h = 30.f * vs;
    if (availW >= 390.f * vs)
    {
        const float btnW = (availW - gap * 2.f) / 3.f;
        if (DrawActionButton("##open_guide", btnW, h, ActionIcon::Guide, accent, "Open Hub", "Open Hub", false, vs))
            OpenSettingsTab(app, SettingsTabHub);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##show_map", btnW, h, ActionIcon::Map, accent, "Show on Map", "Show on Map", false, vs))
            ShowStepOnMap(app, step);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##copy_wp", btnW, h, ActionIcon::Copy, accent, "Copy waypoint", "Copy waypoint", false, vs))
            CopyWaypointForStep(app, step);
    }
    else
    {
        const float btnW = std::max(100.f * vs, (availW - gap) * 0.5f);
        if (DrawActionButton("##open_guide", btnW, h, ActionIcon::Guide, accent, "Open Hub", "Open Hub", false, vs))
            OpenSettingsTab(app, SettingsTabHub);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##show_map", btnW, h, ActionIcon::Map, accent, "Show on Map", "Show on Map", false, vs))
            ShowStepOnMap(app, step);
        ImGui::Spacing();
        if (DrawActionButton("##copy_wp", availW, h, ActionIcon::Copy, accent, "Copy waypoint", "Copy waypoint", false, vs))
            CopyWaypointForStep(app, step);
    }
}

void DrawTravelActionButtons(App& app, bool compact)
{
    const float vs = ViewerScale(app);
    const ImU32 accent = app.travel.IsPersonal() ? IM_COL32(255, 150, 40, 245) : IM_COL32(120, 200, 255, 245);
    const float availW = ImGui::GetContentRegionAvail().x - 8.f * vs;   // reserve right padding (accent cards inset left only)
    if (compact)
    {
        const float gap = 7.f * vs;
        const float h = 30.f * vs;
        const float w = std::max(32.f * vs, (availW - gap * 2.f) / 3.f);
        if (DrawActionButton("##open_guide", w, h, ActionIcon::Guide, accent, nullptr, "Open Hub", true, vs))
            OpenSettingsTab(app, SettingsTabHub);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##show_map", w, h, ActionIcon::Map, accent, nullptr, "Show on Map", true, vs))
            ShowTravelTargetOnMap(app);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##copy_wp", w, h, ActionIcon::Copy, accent, nullptr, "Copy waypoint", true, vs))
            CopyTravelTargetWaypoint(app);
        return;
    }

    const float gap = 8.f * vs;
    const float h = 30.f * vs;
    if (availW >= 390.f * vs)
    {
        const float btnW = (availW - gap * 2.f) / 3.f;
        if (DrawActionButton("##open_guide", btnW, h, ActionIcon::Guide, accent, "Open Hub", "Open Hub", false, vs))
            OpenSettingsTab(app, SettingsTabHub);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##show_map", btnW, h, ActionIcon::Map, accent, "Show on Map", "Show on Map", false, vs))
            ShowTravelTargetOnMap(app);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##copy_wp", btnW, h, ActionIcon::Copy, accent, "Copy waypoint", "Copy waypoint", false, vs))
            CopyTravelTargetWaypoint(app);
    }
    else
    {
        const float btnW = std::max(100.f * vs, (availW - gap) * 0.5f);
        if (DrawActionButton("##open_guide", btnW, h, ActionIcon::Guide, accent, "Open Hub", "Open Hub", false, vs))
            OpenSettingsTab(app, SettingsTabHub);
        ImGui::SameLine(0.f, gap);
        if (DrawActionButton("##show_map", btnW, h, ActionIcon::Map, accent, "Show on Map", "Show on Map", false, vs))
            ShowTravelTargetOnMap(app);
        ImGui::Spacing();
        if (DrawActionButton("##copy_wp", availW, h, ActionIcon::Copy, accent, "Copy waypoint", "Copy waypoint", false, vs))
            CopyTravelTargetWaypoint(app);
    }
}

void DrawGuideOnlyButton(App& app, bool compact)
{
    const float vs = ViewerScale(app);
    const ImU32 accent = IM_COL32(214, 170, 90, 245);
    const float availW = ImGui::GetContentRegionAvail().x;
    const float h = 30.f * vs;
    const float btnW = std::min(availW, compact ? std::max(140.f * vs, availW * 0.7f) : 220.f * vs);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x + std::max(0.f, (availW - btnW) * 0.5f), p.y));   // centered
    if (DrawActionButton("##open_guide", btnW, h, ActionIcon::Guide, accent,
                         compact ? nullptr : "Open Hub", "Open Hub", compact, vs))
        OpenSettingsTab(app, SettingsTabHub);
}

void DrawTargetClearX(App& app, TargetOverrideKind kind, ImVec2 topRight, float size)
{
    if (kind == TargetOverrideKind::None) return;
    const float vs = ViewerScale(app);   // size arrives already scaled; scale the internal X-stroke insets to match
    const ImVec2 restore = ImGui::GetCursorScreenPos();
    const ImVec2 p(topRight.x - size, topRight.y);
    ImGui::SetCursorScreenPos(p);
    const bool clicked = ImGui::InvisibleButton("##clear_target_x", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + size, p.y + size);
    const ImU32 bg = hovered ? IM_COL32(72, 54, 25, 210) : IM_COL32(4, 6, 5, 150);
    const ImU32 br = hovered ? IM_COL32(226, 166, 70, 230) : IM_COL32(130, 104, 54, 130);
    dl->AddRectFilled(p, b, bg, 2.f);
    dl->AddRect(p, b, br, 2.f, 0, hovered ? 1.35f : 1.f);
    const ImU32 col = hovered ? IM_COL32(255, 228, 174, 255) : IM_COL32(224, 204, 170, 245);
    const float in = 7.f * vs;   // X-stroke inset
    dl->AddLine(ImVec2(p.x + in, p.y + in), ImVec2(b.x - in, b.y - in), IM_COL32(0, 0, 0, 170), 3.f);
    dl->AddLine(ImVec2(b.x - in, p.y + in), ImVec2(p.x + in, b.y - in), IM_COL32(0, 0, 0, 170), 3.f);
    dl->AddLine(ImVec2(p.x + in, p.y + in), ImVec2(b.x - in, b.y - in), col, 1.6f);
    dl->AddLine(ImVec2(b.x - in, p.y + in), ImVec2(p.x + in, b.y - in), col, 1.6f);
    if (hovered)
        Gw2Ui::Tooltip(TargetClearLabel(kind));
    if (clicked)
        ClearTargetOverride(app, kind);
    ImGui::SetCursorScreenPos(restore);
}

