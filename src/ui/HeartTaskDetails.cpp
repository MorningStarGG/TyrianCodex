#include "ui/HeartTaskDetails.h"
#include "app/State.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>

using namespace HeartGuidanceUi;

namespace
{
    ImVec2 AnswerButtonSize()
    {
        const ImVec2 closed = Gw2Ui::ActionButtonSize("Answers");
        const ImVec2 opened = Gw2Ui::ActionButtonSize("Hide answers");
        return ImVec2(std::max(closed.x, opened.x), std::max(closed.y, opened.y));
    }

    std::string Fold(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return text;
    }
    void SelectOwner(HeartDetailView& view, const std::string& owner)
    {
        if (view.Owner != owner)
        {
            view = HeartDetailView{};
            view.Owner = owner;
        }
    }

    void DrawAnswerToggle(HeartDetailView& view, ImVec2 size)
    {
        if (Gw2Ui::ActionButtonPx(view.Expanded ? "Hide answers" : "Answers", size.x, size.y))
            view.Expanded = !view.Expanded;
    }

    // One searchable, scrollable answer body for both guide and world cards.
    void DrawAnswerContent(const HeartTaskData& tasks, HeartDetailView& view,
                           const std::vector<std::string>& ids, float listHeight)
    {
        const float contentWidth = Gw2Ui::ContentWidth();
        Gw2Ui::SearchBox("##answersearch", view.Search, sizeof(view.Search), contentWidth,
                        "Find a question or answer", kAnswerFontSize);
        const std::string search = Fold(view.Search);
        ImGui::BeginChild("##answers", ImVec2(contentWidth, std::max(1.f, listHeight)), false, ImGuiWindowFlags_NoBackground);
        for (const auto& detail : tasks.Details)
        {
            if (!ids.empty() && std::find(ids.begin(), ids.end(), detail.Id) == ids.end()) continue;
            Gw2Ui::LabelWrapped(detail.Title.c_str(), Gw2Ui::kGold, true, nullptr, kHeadingFontSize);
            ImGui::PushID(detail.Id.c_str());
            if (!Gw2Ui::BeginTextTable("##answer_table", detail.Headers, kAnswerFontSize))
            { ImGui::PopID(); continue; }
            for (const auto& row : detail.Rows)
            {
                bool match = search.empty();
                for (const auto& cell : row) match = match || Fold(cell).find(search) != std::string::npos;
                if (!match) continue;
                Gw2Ui::TextTableRow(row);
            }
            Gw2Ui::EndTextTable();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

HeartGuidanceUi::WorldHintLayout HeartGuidanceUi::LayoutWorldHint(const std::vector<const HeartTaskAction*>& actions,
                                                                ImVec2 markerCenter, float markerSize, ImVec2 displaySize)
{
    const float scale = Gw2Ui::GlobalScale();
    const float gap = kGap * scale;
    const float maxWidth = std::max(1.f, std::min(kMaxWidth * scale, displaySize.x));
    const auto actionText = [](const HeartTaskAction& action)
    { return (action.ShortText.empty() ? action.Text : action.ShortText).c_str(); };
    const auto measure = [&](const char* text, float fontSize, float limit)
    {
        // The small margin avoids wrapping a line that fits exactly at a glyph boundary.
        const float width = std::min(limit, Gw2Ui::MeasureWidth(text, fontSize) + 4.f * scale);
        return ImVec2(width, Gw2Ui::MeasureWrappedHeight(text, fontSize, width));
    };
    const char* label = actions.size() == 1 ? actionText(*actions.front()) : "Heart tasks";
    WorldHintLayout layout{{label, {}, measure(label, kInstructionFontSize, maxWidth)}, {}, kAnswerFontSize * scale, {}};
    float width = layout.Instruction.Size.x;
    float height = layout.Instruction.Size.y;
    if (actions.size() > 1)
    {
        const float indent = layout.ActionIconSize + gap;
        for (const auto* action : actions)
        {
            const char* text = actionText(*action);
            const ImVec2 size = measure(text, kAnswerFontSize, std::max(1.f, maxWidth - indent));
            layout.Actions.push_back({text, ImVec2(indent, height + gap), size});
            width = std::max(width, indent + size.x);
            height += gap + std::max(layout.ActionIconSize, size.y);
        }
    }
    // Fit the complete text block once; independently clamping rows would stack them at a screen edge.
    const ImVec2 position(
        std::floor(std::clamp(markerCenter.x + markerSize * 0.5f + gap, 0.f, std::max(0.f, displaySize.x - width))),
        std::floor(std::clamp(markerCenter.y - kInstructionFontSize * scale * 0.5f, 0.f,
                              std::max(0.f, displaySize.y - height - gap - AnswerButtonSize().y))));
    layout.Instruction.Position = position;
    for (auto& row : layout.Actions)
    {
        row.Position.x += position.x;
        row.Position.y += position.y;
    }
    layout.AnswersAnchor = ImVec2(position.x + width * 0.5f, position.y + height + gap);
    return layout;
}

void DrawHeartTaskDetails(const HeartTaskData& tasks, HeartDetailView& view,
                          const std::string& owner, const std::vector<std::string>& ids)
{
    if (tasks.Details.empty()) return;
    SelectOwner(view, owner);
    ImGui::PushID(owner.c_str());
    DrawAnswerToggle(view, AnswerButtonSize());
    if (view.Expanded) DrawAnswerContent(tasks, view, ids, kAnswerListHeight * Gw2Ui::TextScale());
    ImGui::PopID();
}

void DrawNearbyHeartTaskDetails(const HeartTaskData& tasks, HeartDetailView& view,
                                const std::string& owner, const std::vector<std::string>& ids,
                                ImVec2 buttonAnchor, ImVec2 displaySize, float opacity)
{
    if (tasks.Details.empty()) return;
    SelectOwner(view, owner);
    Gw2Ui::PushTextScale(1.f); // World hints do not inherit a guide/dashboard panel's local text scale.
    const float scale = Gw2Ui::GlobalScale();
    const ImVec2 buttonSize = AnswerButtonSize();
    const ImVec2 buttonPos(
        std::floor(std::clamp(buttonAnchor.x - buttonSize.x * 0.5f, 0.f, std::max(0.f, displaySize.x - buttonSize.x))),
        std::floor(std::clamp(buttonAnchor.y, 0.f, std::max(0.f, displaySize.y - buttonSize.y))));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    // The control's window never becomes the card. Label changes, card padding and
    // viewport fitting cannot resize or move the button when it opens or closes.
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                                  ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::SetNextWindowPos(buttonPos);
    ImGui::SetNextWindowSize(buttonSize);
    if (ImGui::Begin("##tc_heart_world", nullptr, flags))
    {
        ImGui::PushID(owner.c_str());
        DrawAnswerToggle(view, buttonSize);
        ImGui::PopID();
    }
    ImGui::End();

    if (view.Expanded)
    {
        const float gap = kGap * scale;
        const float chromeHeight = 2.f * Gw2Ui::CardPadding() + Gw2Ui::InputBoxHeight(kAnswerFontSize) + ImGui::GetStyle().ItemSpacing.y;
        const float desiredHeight = chromeHeight + kAnswerListHeight * scale;
        const float below = std::max(0.f, displaySize.y - buttonPos.y - buttonSize.y - gap);
        const float above = std::max(0.f, buttonPos.y - gap);
        const bool placeBelow = desiredHeight <= below || below >= above;
        const ImVec2 panelSize(std::min(kMaxWidth * scale, displaySize.x), std::min(desiredHeight, placeBelow ? below : above));
        const ImVec2 panelPos(
            std::floor(std::clamp(buttonPos.x + buttonSize.x * 0.5f - panelSize.x * 0.5f,
                                  0.f, std::max(0.f, displaySize.x - panelSize.x))),
            placeBelow ? buttonPos.y + buttonSize.y + gap : buttonPos.y - panelSize.y - gap);
        ImGui::SetNextWindowPos(panelPos);
        ImGui::SetNextWindowSize(panelSize);
        if (ImGui::Begin("##tc_heart_world_answers", nullptr, flags))
        {
            ImGui::PushID(owner.c_str());
            Gw2Ui::BeginCard("##heart_answer_card", panelSize.x, IM_COL32(12, 10, 7, 235));
            DrawAnswerContent(tasks, view, ids, panelSize.y - chromeHeight);
            Gw2Ui::EndCard();
            ImGui::PopID();
        }
        ImGui::End();
    }
    ImGui::PopStyleVar(4);
    Gw2Ui::PopTextScale();
}
