#include "ui/HeartTaskDetails.h"
#include "ui/Gw2Ui.h"
#include "app/State.h"
#include "Shared.h"
#include "ui/InputProbe.h"
#include "ui/Effect.h"
#include "util/Textures.h"
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Only the Nexus/GPU services are absent. These tests run the production answer
// component, Gw2Ui buttons/cards/search/text and ImGui input engine unchanged.
NexusLinkData_t* NexusLink = nullptr;
namespace Tex
{
    void* GetTextureFromAssetId(uint32_t) { return nullptr; }
    const Texture_t* GetAssetTex(uint32_t) { return nullptr; }
    const Texture_t* GetFileTex(const char*) { return nullptr; }
}
namespace InputProbe
{
    bool Enabled() { return false; }
    void NoteTextBox(const char*, unsigned, bool, int, bool) {}
}
namespace Fx
{
    void ScrollingHighlight(ImDrawList*, ImVec2, ImVec2, float) {}
}

namespace
{
    std::string FontPath;

    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    struct Harness
    {
        HeartTaskData Tasks;
        HeartDetailView View;
        ImVec2 Position{250.f, 100.f}; // Center / top of the button beneath a world instruction.
        std::string Owner = "26:heart:5:headstone";
        float Opacity = 1.f;
        Harness()
        {
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.DisplaySize = ImVec2(800.f, 600.f);
            io.DeltaTime = 1.f / 60.f;
            for (int i = 0; i < ImGuiKey_COUNT; ++i) io.KeyMap[i] = i;
            if (!FontPath.empty())
            {
                std::vector<Gw2Ui::FontBake> ladder;
                for (float size : {12.f, 14.f, 16.f, 18.f, 20.f, 22.f, 24.f, 26.f, 28.f, 30.f, 32.f})
                {
                    ImFont* font = io.Fonts->AddFontFromFileTTF(FontPath.c_str(), size);
                    Check(font != nullptr, "The bundled Menomonia font must load for typography checks");
                    ladder.push_back({size, font, nullptr});
                }
                Gw2Ui::SetGw2FontLadder(ladder.data(), (int)ladder.size());
            }
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            Gw2Ui::SetGlobalScale(1.f);
            Tasks.Details.push_back({"notes", "Notes", {"Name", "Correct response"},
                                    {{"Merin Trollsbane", "Second line."}}});
        }
        ~Harness()
        {
            Gw2Ui::SetGw2Font(nullptr, nullptr);
            ImGui::DestroyContext();
        }

        ImGuiWindow* ButtonWindow() const
        {
            ImGuiWindow* window = ImGui::FindWindowByName("##tc_heart_world");
            Check(window != nullptr, "The world Answers control must exist");
            return window;
        }
        ImVec2 Button() const
        {
            const ImGuiWindow* window = ButtonWindow();
            return ImVec2(window->Pos.x + window->Size.x * 0.5f, window->Pos.y + window->Size.y * 0.5f);
        }
        ImGuiWindow* PanelWindow() const
        {
            ImGuiWindow* window = ImGui::FindWindowByName("##tc_heart_world_answers");
            Check(window != nullptr && window->Active, "Open answers must have a separate visible card");
            return window;
        }
        ImVec2 Search() const
        {
            const ImGuiWindow* window = PanelWindow();
            return ImVec2(window->Pos.x + Gw2Ui::CardPadding() + 10.f * Gw2Ui::GlobalScale(),
                          window->Pos.y + Gw2Ui::CardPadding() + Gw2Ui::InputBoxHeight(HeartGuidanceUi::kAnswerFontSize) * 0.5f);
        }
        ImVec2 List() const
        {
            for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
                if (window->Active && std::strstr(window->Name, "##answers_"))
                    return ImVec2(window->Pos.x + 10.f, window->Pos.y + window->Size.y * 0.5f);
            throw std::runtime_error("The open answer card must contain its scrollable list");
        }
        bool Frame(ImVec2 mouse, bool down = false, bool visible = true, bool cover = false)
        {
            ImGuiIO& io = ImGui::GetIO();
            io.MousePos = mouse;
            io.MouseDown[0] = down;
            ImGui::NewFrame();
            const bool captured = io.WantCaptureMouse;
            if (visible)
                DrawNearbyHeartTaskDetails(Tasks, View, Owner, {"notes"}, Position, io.DisplaySize, Opacity);
            if (cover)
            {
                ImGui::SetNextWindowPos(ImVec2(Position.x - 110.f, Position.y - 20.f));
                ImGui::SetNextWindowSize(ImVec2(220.f, 160.f));
                if (ImGui::Begin("Other addon", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings))
                    ImGui::TextUnformatted("An overlapping addon window owns this input.");
                ImGui::End();
            }
            ImGui::Render();
            const ImGuiWindow* fallback = ImGui::FindWindowByName("Debug##Default");
            Check(!fallback || !fallback->Active, "Measuring and opening answers must never display ImGui's fallback Debug window");
            return captured;
        }
        void Settle(ImVec2 mouse, bool visible = true, bool cover = false)
        {
            for (int i = 0; i < 3; ++i) Frame(mouse, false, visible, cover);
        }
    };

    void CheckClickAndCollapse()
    {
        Harness ui;
        const ImVec2 outside(700.f, 500.f);
        ui.Settle(outside);
        const ImVec2 button = ui.Button();
        Check(!ui.Frame(outside), "A closed panel must not capture gameplay input away from its button");
        ui.Settle(button);
        Check(ui.Frame(button), "ImGui must own input over the real Answers button");
        Check(ui.Frame(button, true), "The button press must be captured, not passed to the game");
        Check(!ui.View.Expanded, "Pressing Answers must wait for a normal button release");
        ui.Frame(button);
        Check(ui.View.Expanded, "Answers must open even though ImGui captures the click");
        ui.Settle(button);
        ui.Frame(button, true);
        ui.Frame(button);
        Check(!ui.View.Expanded, "Hide answers must close the panel");
        ui.Settle(outside);
        Check(!ui.Frame(outside), "Collapsing answers must release the former panel area");
        const ImVec2 oldPanelArea(400.f, 220.f);
        ui.Settle(oldPanelArea);
        Check(!ui.Frame(oldPanelArea, true), "The closed panel's former area must pass clicks to gameplay");
        ui.Frame(oldPanelArea);
        Check(!ui.View.Expanded, "An outside click must not reopen the answers");
        Check(!ImGui::GetIO().WantCaptureKeyboard, "A collapsed Answers control must not capture gameplay keys");
        ui.Settle(button, false);
        Check(!ui.Frame(button, false, false), "A hidden hint must leave no active button hit area");
    }

    void CheckOtherWindowsOwnTheirClicks()
    {
        Harness ui;
        ui.Settle(ImVec2(700.f, 500.f));
        const ImVec2 button = ui.Button();
        ui.Settle(button, true, true);
        ui.Frame(button, true, true, true);
        ui.Frame(button, false, true, true);
        Check(!ui.View.Expanded, "Answers must not activate through another addon window");
    }

    void CheckOwnerAndScreenEdges()
    {
        Harness ui;
        ui.Position = ImVec2(799.f, 599.f);
        ui.Settle(ImVec2(10.f, 10.f));
        const ImVec2 edgeButton = ui.Button();
        ui.Settle(edgeButton);
        ui.Frame(edgeButton, true);
        ui.Frame(edgeButton);
        Check(ui.View.Expanded, "An Answers button at the screen edge must remain visible and clickable");
        std::strcpy(ui.View.Search, "Merin");
        ui.Owner = "54:heart:308:configurator";
        ui.Frame(ImVec2(10.f, 10.f));
        Check(ui.View.Owner == ui.Owner && !ui.View.Expanded && ui.View.Search[0] == 0,
              "Switching nearby owners must not carry an expanded panel or search into the next object");
    }

    void CheckFrameworkScalingAndOpacity()
    {
        Harness ui;
        Gw2Ui::SetGlobalScale(1.5f);
        ui.Opacity = 0.5f;
        ui.Settle(ImVec2(700.f, 500.f));
        const ImVec2 button = ui.Button();
        ui.Settle(button);
        Check(ui.Frame(button), "The button hit area must follow Gw2Ui scale");
        ImDrawData* draw = ImGui::GetDrawData();
        Check(draw && draw->TotalVtxCount > 0, "The framework button must draw its own chrome and text");
        for (int list = 0; list < draw->CmdListsCount; ++list)
            for (const auto& vertex : draw->CmdLists[list]->VtxBuffer)
                Check((vertex.col >> IM_COL32_A_SHIFT) <= 128, "Gw2Ui button chrome and text must obey the hint's opacity");
        ui.Frame(button, true);
        ui.Frame(button);
        Check(ui.View.Expanded, "The scaled Gw2Ui action button must open answers");
        ui.Settle(button);
        draw = ImGui::GetDrawData();
        for (int list = 0; list < draw->CmdListsCount; ++list)
            for (const auto& vertex : draw->CmdLists[list]->VtxBuffer)
                Check((vertex.col >> IM_COL32_A_SHIFT) <= 128, "The framework card, search field and answer text must share the hint's opacity");
    }

    void CheckSearchAndScroll()
    {
        Harness ui;
        for (int i = 0; i < 40; ++i)
            ui.Tasks.Details[0].Rows.push_back({"Other headstone " + std::to_string(i), "First line."});
        ui.Settle(ImVec2(700.f, 500.f));
        const ImVec2 button = ui.Button();
        ui.Settle(button);
        ui.Frame(button, true);
        ui.Frame(button);
        const ImVec2 list = ui.List(), search = ui.Search();
        ui.Settle(list);
        ImGui::GetIO().MouseWheel = -3.f;
        ui.Frame(list);
        bool scrolled = false;
        for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(window->Name, "##answers_") && window->Scroll.y > 0.f) scrolled = true;
        Check(scrolled, "The expanded answer list must receive the mouse wheel and scroll");
        ui.Settle(search);
        ui.Frame(search, true);
        ui.Frame(search);
        ImGui::GetIO().AddInputCharactersUTF8("Merin");
        ui.Frame(search);
        Check(std::strcmp(ui.View.Search, "Merin") == 0, "The real Gw2Ui search box must accept answer filtering input");
        ui.Settle(button);
        ui.Frame(button, true);
        ui.Frame(button);
        Check(!ui.View.Expanded, "Hide answers must still close after searching the list");
        ui.Settle(ImVec2(700.f, 500.f));
        Check(!ImGui::GetIO().WantCaptureKeyboard, "Closing an answer search must release gameplay keyboard input");
    }

    void CheckStableButtonAndPanel()
    {
        for (float scale : {0.75f, 1.f, 1.5f, 2.f})
            for (ImVec2 display : {ImVec2(800.f, 600.f), ImVec2(360.f, 240.f)})
                for (ImVec2 anchor : {ImVec2(250.f, 100.f), ImVec2(1.f, 1.f), ImVec2(display.x - 1.f, display.y - 1.f)})
                {
                    Harness ui;
                    Gw2Ui::SetGlobalScale(scale);
                    ImGui::GetIO().DisplaySize = display;
                    ui.Position = anchor;
                    ui.Settle(ImVec2(-100.f, -100.f));
                    const ImVec2 position = ui.ButtonWindow()->Pos, size = ui.ButtonWindow()->Size;
                    const ImVec2 button = ui.Button();
                    for (int cycle = 0; cycle < 3; ++cycle)
                    {
                        ui.Settle(button);
                        ui.Frame(button, true);
                        ui.Frame(button);
                        Check(ui.View.Expanded, "The fixed control must open its card on every click");
                        ui.Settle(button);
                        Check(ui.ButtonWindow()->Pos.x == position.x && ui.ButtonWindow()->Pos.y == position.y &&
                              ui.ButtonWindow()->Size.x == size.x && ui.ButtonWindow()->Size.y == size.y,
                              "Opening answers must not move or resize the button, including at screen edges");
                        const ImGuiWindow* panel = ui.PanelWindow();
                        Check(panel->Pos.x >= 0.f && panel->Pos.y >= 0.f &&
                              panel->Pos.x + panel->Size.x <= display.x + 1.f && panel->Pos.y + panel->Size.y <= display.y + 1.f,
                              "The separate card must fit the viewport at every UI scale");
                        Check(panel->Pos.y >= position.y + size.y || panel->Pos.y + panel->Size.y <= position.y,
                              "Fitting the answer card must not overlap its toggle button");
                        ui.Frame(button, true);
                        ui.Frame(button);
                        Check(!ui.View.Expanded, "The same click position must close answers without chasing the button");
                        ui.Settle(button);
                        Check(ui.ButtonWindow()->Pos.x == position.x && ui.ButtonWindow()->Pos.y == position.y &&
                              ui.ButtonWindow()->Size.x == size.x && ui.ButtonWindow()->Size.y == size.y,
                              "Collapsing answers must keep the button's original bounds");
                    }
                }
    }

    void CheckWrappedWorldInstructions()
    {
        Harness ui;
        HeartTaskAction first, second;
        first.ShortText = "Examine Dwarven Headstones and give a response after comparing the name on the headstone with the documented answers.";
        second.Text = "Study another nearby relic and return to Arcanist Vance."; // Schema-compatible full-text fallback.
        for (float scale : {0.75f, 1.f, 1.5f, 2.f})
        {
            Gw2Ui::SetGlobalScale(scale);
            ui.Settle(ImVec2(-100.f, -100.f));
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const auto single = HeartGuidanceUi::LayoutWorldHint({&first}, ImVec2(790.f, 590.f), 48.f, display);
            Check(single.Instruction.Size.y > HeartGuidanceUi::kInstructionFontSize * scale,
                  "A long instruction must wrap rather than shrink its font or overrun the display");
            Check(std::fabs(single.AnswersAnchor.x - single.Instruction.Position.x - single.Instruction.Size.x * 0.5f) < 0.01f &&
                  single.AnswersAnchor.y > single.Instruction.Position.y + single.Instruction.Size.y,
                  "The button must be centered beneath the complete wrapped instruction");
            const auto group = HeartGuidanceUi::LayoutWorldHint({&first, &second}, ImVec2(790.f, 590.f), 48.f, display);
            float bottom = group.Instruction.Position.y + group.Instruction.Size.y;
            for (const auto& row : group.Actions)
            {
                Check(row.Position.y > bottom && row.Position.x >= 0.f && row.Position.x + row.Size.x <= display.x,
                      "Wrapped actions must keep their spacing and screen bounds as a complete block");
                bottom = row.Position.y + row.Size.y;
            }
            Check(group.AnswersAnchor.y > bottom && group.AnswersAnchor.y + ui.ButtonWindow()->Size.y <= display.y,
                  "The Answers control must follow all measured action rows without overlapping them");
            Check(std::strcmp(group.Actions.back().Text, second.Text.c_str()) == 0,
                  "World instructions must retain the full-text fallback when a short label is unavailable");
        }
    }

    void CheckFrameworkTextTables()
    {
        Harness ui;
        for (float scale : {0.75f, 1.f, 1.5f, 2.f})
        {
            Gw2Ui::SetGlobalScale(scale);
            ImGuiIO& io = ImGui::GetIO();
            io.MousePos = ImVec2(700.f, 500.f);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(10.f, 10.f));
            ImGui::SetNextWindowSize(ImVec2(480.f, 560.f));
            ImGui::Begin("Framework text table", nullptr, ImGuiWindowFlags_NoSavedSettings);
            Gw2Ui::BeginCard("##table_card", 320.f);
            const ImVec2 previousPadding = ImGui::GetStyle().CellPadding;
            const ImVec4 previousHeader = ImGui::GetStyle().Colors[ImGuiCol_TableHeaderBg];
            Check(Gw2Ui::BeginTextTable("##text_table", {"Name / prompt", "Correct response"}, 18.f),
                  "The reusable framework text table must open inside a card");
            ImGuiTable* table = ImGui::GetCurrentContext()->CurrentTable;
            Check(table->Columns[0].WorkMinX - table->OuterRect.Min.x >= Gw2Ui::CardPadding() - 0.01f,
                  "The first column's text must have a scaled gutter from the shaded table edge");
            for (int column = 0; column < table->ColumnsCount; ++column)
                Check(table->Columns[column].WorkMinX - table->Columns[column].MinX >= Gw2Ui::CardPadding() - 0.01f &&
                      table->Columns[column].MaxX - table->Columns[column].WorkMaxX >= Gw2Ui::CardPadding() - 0.01f,
                      "Every header and body cell must reserve framework padding on both sides");
            Check(table->OuterRect.GetWidth() <= Gw2Ui::ContentWidth() + 0.01f,
                  "The framework table must respect its enclosing card's inner width");
            Gw2Ui::TextTableRow({"Merin", "Second line."});
            const float shortStart = table->RowPosY1;
            Gw2Ui::TextTableRow({"A long prompt containing several phrases that must wrap within its column at every scale.",
                                 "An equally long response that needs multiple lines without covering another row or its neighbour."});
            const float longStart = table->RowPosY1;
            Gw2Ui::TextTableRow({"Reyna", "First line."});
            Check(table->RowPosY1 - longStart > longStart - shortStart,
                  "Wrapped table cells must grow their row height rather than collide with the next row");
            Gw2Ui::EndTextTable();
            Check(ImGui::GetStyle().CellPadding.x == previousPadding.x && ImGui::GetStyle().CellPadding.y == previousPadding.y &&
                  std::memcmp(&ImGui::GetStyle().Colors[ImGuiCol_TableHeaderBg], &previousHeader, sizeof(previousHeader)) == 0,
                  "Ending a text table must restore padding and colors for other framework controls");
            Gw2Ui::EndCard();
            ImGui::End();
            ImGui::Render();
        }
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc > 1) FontPath = argv[1];
        CheckClickAndCollapse();
        CheckOtherWindowsOwnTheirClicks();
        CheckOwnerAndScreenEdges();
        CheckFrameworkScalingAndOpacity();
        CheckSearchAndScroll();
        CheckStableButtonAndPanel();
        CheckWrappedWorldInstructions();
        CheckFrameworkTextTables();
        std::cout << "Nearby heart answer input checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
