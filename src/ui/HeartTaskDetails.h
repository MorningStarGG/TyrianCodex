#pragma once
#include "model/Dataset.h"
#include <imgui.h>
struct HeartDetailView;

namespace HeartGuidanceUi
{
    // Design sizes; Gw2Ui resolves the font and applies screen/local text scaling once.
    inline constexpr float kInstructionFontSize = 20.f;
    inline constexpr float kAnswerFontSize = 18.f;
    inline constexpr float kHeadingFontSize = 22.f;
    inline constexpr float kMaxWidth = 420.f;
    inline constexpr float kGap = 6.f;
    inline constexpr float kAnswerListHeight = 220.f;

    struct WorldTextLayout
    {
        const char* Text;
        ImVec2 Position, Size;
    };
    struct WorldHintLayout
    {
        WorldTextLayout Instruction;
        std::vector<WorldTextLayout> Actions;
        float ActionIconSize;
        ImVec2 AnswersAnchor;
    };
    WorldHintLayout LayoutWorldHint(const std::vector<const HeartTaskAction*>& actions,
                                   ImVec2 markerCenter, float markerSize, ImVec2 displaySize);
}

// Shared guide/world answer presentation. Empty ids selects all details for this heart.
void DrawHeartTaskDetails(const HeartTaskData& tasks, HeartDetailView& view,
                          const std::string& owner, const std::vector<std::string>& ids = {});

// buttonAnchor is the horizontal center / top beneath the measured instruction.
// The button keeps its own fixed window; an answer card opens below it, or above at a screen edge.
void DrawNearbyHeartTaskDetails(const HeartTaskData& tasks, HeartDetailView& view,
                                const std::string& owner, const std::vector<std::string>& ids,
                                ImVec2 buttonAnchor, ImVec2 displaySize, float opacity);
