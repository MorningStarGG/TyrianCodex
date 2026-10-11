#include "world/HeartHints.h"
#include "world/MarkerFx.h"
#include "world/Markers.h"
#include "guide/StepStatus.h"
#include "render/tasks/TaskIcons.h"
#include "render/glyphs/Glyphs.h"
#include "ui/HeartTaskDetails.h"
#include "ui/Gw2Ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    struct VisibleHint
    {
        const Step* Heart;
        Math::Vec3 Position;
        std::vector<const HeartTaskAction*> Actions;
        std::vector<std::string> DetailIds;
        ImVec2 Screen;
        float CameraDistance, PlayerDistance, Opacity, Dy;
        std::string Owner;
        ImVec2 AnswersAnchor;
    };
}

bool HeartHints::Enabled(const Config& config)
{
    return config.showGuide && config.showMarkers && config.kindEnabled[4] && config.worldHeartHints;
}

void HeartHints::Draw(App& app, const FrameCtx& fc, uint32_t mapId)
{
    const Config& cfg = app.config;
    if (!Enabled(cfg))
    {
        app.state.heartWorldDetails = HeartDetailView{};
        return;
    }
    const auto it = app.zones.find(mapId);
    if (it == app.zones.end())
    {
        app.state.heartWorldDetails = HeartDetailView{};
        return;
    }
    if (!cfg.worldHeartAnswers) app.state.heartWorldDetails = HeartDetailView{};
    const Zone& zone = it->second;
    std::vector<VisibleHint> visible;
    const float far = std::max(cfg.markerFadeFar, cfg.markerFadeNear + 1.f);
    const auto between = MarkerFx::BeginBetween(cfg, fc);
    for (const Step& heart : zone.Steps)
    {
        if (!heart.HeartTasks) continue;
        const bool done = StepIsDone(app.progress, app.state.currentChar, heart);
        if (done && !cfg.viewerShowCompleted) continue;
        const HeartTaskData& tasks = *heart.HeartTasks;
        for (const auto& anchor : tasks.Anchors)
        {
            if (anchor.HeightEstimated && !zone.CanGuide()) continue;
            const HeartTaskAction* action = nullptr;
            for (const auto& a : tasks.Actions) if (a.Id == anchor.ActionId) { action = &a; break; }
            if (!anchor.Overview && !action) continue;
            Math::Vec3 pos = anchor.Position;
            pos.y += cfg.markerExtraH;
            const float distance = (pos - fc.camPos).Length();
            if (distance >= far) continue;
            float sx, sy, depth;
            if (!Math::WorldToScreen(pos, fc.vp, fc.W, fc.H, sx, sy, depth)) continue;
            const float fade = 1.f - std::clamp((distance - cfg.markerFadeNear) / (far - cfg.markerFadeNear), 0.f, 1.f);
            float opacity = MarkerFx::OpacityCap(fade * MarkerFx::CombatFade(cfg.markerFadeInCombat, fc) *
                                               MarkerFx::BetweenDim(between, ImVec2(sx, sy), distance), cfg);
            if (done) opacity *= 0.30f;
            if (opacity < 0.01f) continue;
            // Several actions, or the area's overview and an NPC, can share a
            // position. Present them together without merging their source data.
            auto grouped = std::find_if(visible.begin(), visible.end(), [&](const VisibleHint& hint)
            { return hint.Heart == &heart && (hint.Position - anchor.Position).Length() < 0.01f; });
            if (grouped == visible.end())
            {
                visible.push_back({&heart, anchor.Position, {}, {}, ImVec2(sx, sy), distance,
                                   (anchor.Position - fc.avatar).Length(), opacity, pos.y - fc.avatar.y,
                                   heart.StepId + ":" + anchor.Id, {}});
                grouped = visible.end() - 1;
            }
            for (const auto& candidate : tasks.Actions)
                if (candidate.Id == anchor.ActionId ||
                    std::find(anchor.ActionIds.begin(), anchor.ActionIds.end(), candidate.Id) != anchor.ActionIds.end())
                    if (std::find(grouped->Actions.begin(), grouped->Actions.end(), &candidate) == grouped->Actions.end())
                        grouped->Actions.push_back(&candidate);
            for (const auto& id : anchor.DetailIds)
                if (std::find(grouped->DetailIds.begin(), grouped->DetailIds.end(), id) == grouped->DetailIds.end())
                    grouped->DetailIds.push_back(id);
        }
    }
    std::sort(visible.begin(), visible.end(), [](const VisibleHint& a, const VisibleHint& b) { return a.CameraDistance > b.CameraDistance; });
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    Gw2Ui::PushTextScale(1.f);
    const float uiScale = Gw2Ui::GlobalScale();
    const float gap = HeartGuidanceUi::kGap * uiScale;
    const ImVec2 displaySize(fc.W, fc.H);
    VisibleHint* nearby = nullptr;
    for (auto& hint : visible)
    {
        const int alpha = (int)(255.f * hint.Opacity);
        const float size = std::clamp(cfg.markerSize * fc.pxScale / std::max(hint.CameraDistance, 0.01f), kMarkerMinPx, 48.f);
        const TaskChip fallback;
        const TaskChip& chip = !hint.Actions.empty() ? hint.Actions.front()->Chip :
                               (!hint.Heart->TaskChips.empty() ? hint.Heart->TaskChips.front() : fallback);
        DrawTaskIcon(dl, hint.Screen, size, chip, IM_COL32(255, 198, 82, alpha), cfg.taskIconArt != 0);
        if (cfg.worldAboveBelow && std::fabs(hint.Dy) > 5.f)
            Render::DrawGlyph(dl, ImVec2(hint.Screen.x, hint.Screen.y + (hint.Dy > 0.f ? -size : size)), 13.f,
                              hint.Dy > 0.f ? Render::Glyph::ArrowUp : Render::Glyph::ArrowDown, IM_COL32(255, 236, 170, alpha), {});
        if (hint.PlayerDistance <= 20.f)
        {
            const auto layout = HeartGuidanceUi::LayoutWorldHint(hint.Actions, hint.Screen, size, displaySize);
            const auto& labelLayout = layout.Instruction;
            const ImVec2 labelEnd(labelLayout.Position.x + labelLayout.Size.x, labelLayout.Position.y + labelLayout.Size.y);
            Gw2Ui::LabelDL(dl, labelLayout.Position, labelEnd, labelLayout.Text,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::Alpha(Gw2Ui::kTextSelected, alpha),
                           true, nullptr, HeartGuidanceUi::kInstructionFontSize, labelLayout.Size.x);
            for (size_t i = 0; i < layout.Actions.size(); ++i)
            {
                const auto& row = layout.Actions[i];
                DrawTaskIcon(dl, ImVec2(row.Position.x - gap - layout.ActionIconSize * 0.5f,
                                       row.Position.y + layout.ActionIconSize * 0.5f), layout.ActionIconSize, hint.Actions[i]->Chip,
                             IM_COL32(255, 198, 82, alpha), cfg.taskIconArt != 0);
                const ImVec2 end(row.Position.x + row.Size.x, row.Position.y + row.Size.y);
                Gw2Ui::LabelDL(dl, row.Position, end, row.Text,
                               Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::Alpha(Gw2Ui::kTextSelected, alpha),
                               true, nullptr, HeartGuidanceUi::kAnswerFontSize, row.Size.x);
            }
            hint.AnswersAnchor = layout.AnswersAnchor;
            if (cfg.worldHeartAnswers && !hint.DetailIds.empty() &&
                (!nearby || hint.PlayerDistance < nearby->PlayerDistance)) nearby = &hint;
        }
    }
    Gw2Ui::PopTextScale();
    if (app.state.heartWorldDetails.Expanded)
    {
        const auto opened = std::find_if(visible.begin(), visible.end(), [&](const VisibleHint& hint)
        { return hint.Owner == app.state.heartWorldDetails.Owner && hint.PlayerDistance <= 20.f; });
        if (opened != visible.end()) nearby = &*opened;
        else app.state.heartWorldDetails = HeartDetailView{};
    }
    if (!nearby)
    {
        app.state.heartWorldDetails = HeartDetailView{};
        return;
    }
    const HeartTaskData& tasks = *nearby->Heart->HeartTasks;
    const bool answers = cfg.worldHeartAnswers && !nearby->DetailIds.empty();
    if (!answers) return;
    DrawNearbyHeartTaskDetails(tasks, app.state.heartWorldDetails, nearby->Owner, nearby->DetailIds,
                              nearby->AnswersAnchor, displaySize, nearby->Opacity);
}
