#include "ui/HeartTaskDetails.h"
#include "ui/Gw2Ui.h"
#include "app/State.h"
#include "Shared.h"
#include "ui/InputProbe.h"
#include "ui/Effect.h"
#include "guide/EventProgress.h"
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
            const float cardWidth = Gw2Ui::ContentWidth();
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
            Check(table->OuterRect.GetWidth() <= cardWidth + 0.01f,
                  "The framework table must respect its enclosing card's inner width");
            Check(Gw2Ui::ContentWidth() <= table->Columns[table->CurrentColumn].WorkMaxX - ImGui::GetCursorScreenPos().x + 0.01f,
                  "Content width inside a table must describe the current cell, not the enclosing card");
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

    void CheckChecklistControls()
    {
        Harness ui;
        EventProgressStore history;
        Gw2Ui::ChecklistState state;
        std::vector<Gw2Ui::ChecklistNode> roots(1);
        auto& root = roots.front();
        root.Id = "region"; root.Label = "Kryta";
        Gw2Ui::ChecklistNode zone;
        zone.Id = "zone"; zone.Label = "Queensdale";
        for (const char* id : {"event-a", "event-b"})
        {
            Gw2Ui::ChecklistNode leaf;
            leaf.Id = leaf.ItemId = id;
            leaf.Label = "Two separate events sharing this name";
            leaf.Context = "Queensdale Kryta";
            zone.Children.push_back(leaf);
        }
        root.Children.push_back(zone);
        Check(Gw2Ui::ExpandChecklistTo(roots,state,"zone") && state.Expanded.count("region"),
              "Current zone must expand the full ancestor path");
        bool show = false, enabled = true;
        int bodyClicks = 0, visible = 0;
        ImVec2 checkbox, body, chip, groupCheckbox;
        const auto frame = [&](ImVec2 mouse, bool down) {
            ImGuiIO& io = ImGui::GetIO(); io.MousePos = mouse; io.MouseDown[0] = down;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(30.f,30.f)); ImGui::SetNextWindowSize(ImVec2(640.f,540.f));
            ImGui::Begin("Checklist control tests",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            chip = ImGui::GetCursorScreenPos(); chip.x += 19.f; chip.y += 15.f;
            Gw2Ui::ShowCompletedChip("##completed",show,ImVec2(38.f,30.f),"Show completed events");
            groupCheckbox = ImGui::GetCursorScreenPos(); groupCheckbox.x += 43.f; groupCheckbox.y += 19.f;
            Gw2Ui::UpdateChecklist(roots,state,[&](const auto& id){return history.IsComplete("Alice",id);});
            Gw2Ui::ChecklistCallbacks callbacks;
            callbacks.CanMark = enabled;
            callbacks.DrawLeaf = [&](const auto& node,int depth,int row) {
                Gw2Ui::ChecklistItemSpec spec;
                spec.Id = node.Id.c_str(); spec.Label = node.Label.c_str(); spec.Marked = node.Done != 0; spec.CanMark = enabled;
                spec.Detail = "24m"; spec.DetailStyle = Gw2Ui::ChecklistDetailStyle::Pill;
                const auto hit = Gw2Ui::ChecklistItem(spec,depth,row);
                if (node.ItemId == "event-a")
                {
                    checkbox = ImVec2(hit.Row.contentMin.x+15.f,(hit.Row.rowMin.y+hit.Row.rowMax.y)/2.f);
                    body = ImVec2(hit.Row.contentMin.x+100.f,checkbox.y);
                }
                if (hit.CheckboxClicked) history.SetComplete("Alice",node.ItemId,!spec.Marked);
                if (hit.BodyClicked) ++bodyClicks;
            };
            callbacks.MarkGroup = [&](const auto& group,bool marked) {
                std::vector<std::string> ids;
                Gw2Ui::VisitChecklistItems(group,[&](const auto& item){ids.push_back(item.ItemId);});
                history.SetMany("Alice",ids,marked);
            };
            visible = Gw2Ui::DrawChecklist(roots,state,callbacks);
            ImGui::End(); ImGui::Render();
            const auto* debug = ImGui::FindWindowByName("Debug##Default");
            Check(!debug || !debug->Active,"Checklist controls must not open a fallback Debug window");
        };
        const ImVec2 outside(760.f,590.f);
        for (int i=0;i<3;++i) frame(outside,false);
        frame(checkbox,false); frame(checkbox,true);
        Check(!history.IsComplete("Alice","event-a"),"Event checkbox waits for a normal release");
        frame(checkbox,false);
        Check(history.IsComplete("Alice","event-a") && !history.IsComplete("Alice","event-b") && bodyClicks==0,
              "A checkbox changes only its own event and never fires travel");
        frame(outside,false);
        Check(root.Done==1 && root.Total==2,"Shared counts must reflect marked IDs rather than duplicate labels");
        frame(body,false); frame(body,true); frame(body,false);
        Check(bodyClicks==1,"Clicking the row body must remain separate from its checkbox");
        frame(chip,false); frame(chip,true); frame(chip,false);
        Check(show,"Shared Show completed chip must toggle on release");
        frame(checkbox,false); frame(checkbox,true); frame(checkbox,false);
        Check(!history.IsComplete("Alice","event-a"),"A shown completed event must be uncheckable");
        enabled=false;
        frame(checkbox,false); frame(checkbox,true); frame(checkbox,false);
        Check(!history.IsComplete("Alice","event-a"),"Disabled/pre-login event checkboxes cannot mutate history");
        enabled=true;
        history.SetComplete("Alice","event-a",true);
        state.Filter=Gw2Ui::ChecklistFilter::Unmarked;
        frame(outside,false);
        Check(!root.Children.front().Children.front().Visible && root.Children.front().Children.back().Visible && root.Total==2,
              "Filtering marked events must retain full counts and other identical-name events");
        state.Filter=Gw2Ui::ChecklistFilter::Marked;
        std::strcpy(state.Search,"QUEENSDALE");
        frame(outside,false);
        Check(visible==1,"Search must find map context case-insensitively and honor marked/unmarked filters");
        history.SetComplete("Alice","event-a",false);
        frame(outside,false);
        Check(visible==0,"A filtered view must update immediately when another surface clears its mark");
        state.Filter=Gw2Ui::ChecklistFilter::All;
        state.Search[0]='\0';
        history.SetMany("Alice",{"event-a","event-b"},true);
        frame(outside,false);
        const auto before=history.Version();
        frame(groupCheckbox,false); frame(groupCheckbox,true); frame(groupCheckbox,false);
        Check(history.CompletedIds("Alice").empty() && history.Version()==before+1 && bodyClicks==1,
              "Group checkboxes must clear their descendants with one shared persistence mutation");
    }

    void CheckChecklistLayout()
    {
        Harness ui;
        for (float scale : {0.75f,1.f,1.5f,2.f})
        {
            Gw2Ui::SetGlobalScale(scale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.f,20.f)); ImGui::SetNextWindowSize(ImVec2(700.f,550.f));
            ImGui::Begin("Checklist layout tests",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            Gw2Ui::BeginCard("checklist-card");
            const float cardLeft = ImGui::GetCursorScreenPos().x;
            Gw2Ui::ChecklistItemSpec item;
            item.Id="short"; item.Label="Short name"; item.Detail="25m";
            const auto shortRow=Gw2Ui::ChecklistItem(item,0,0);
            item.Id="long";
            item.Label="A much longer event instruction containing many words that must wrap cleanly without covering the next event row or its checkbox and must remain readable at every supported scale.";
            item.Subtitle="Group event  -  Lv 80  -  A location with a longer name";
            const auto longRow=Gw2Ui::ChecklistItem(item,1,1);
            Check(longRow.Row.rowMax.y-longRow.Row.rowMin.y > shortRow.Row.rowMax.y-shortRow.Row.rowMin.y,
                  "Long event text and metadata must grow the shared row");
            Check(longRow.Row.rowMin.y>=shortRow.Row.rowMax.y && longRow.Row.rowMax.x<=cardLeft+Gw2Ui::CardInnerWidth()+0.1f,
                  "Checklist rows must stay inside card padding and cannot overlap");
            Gw2Ui::EndCard(); ImGui::End(); ImGui::Render();
        }
    }

    void CheckRightClusterLayout()
    {
        Harness ui;
        for (float scale : {0.75f,1.f,1.5f,2.f})
        for (float width : {220.f,580.f})
        {
            Gw2Ui::SetGlobalScale(scale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.f,20.f)); ImGui::SetNextWindowSize(ImVec2(width,550.f));
            ImGui::Begin("Checklist toolbar tests",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            Gw2Ui::BeginAccentCard("toolbar",0.f,IM_COL32_WHITE);
            const auto left = ImGui::GetCursorScreenPos();
            const float cardRight = left.x + Gw2Ui::CardInnerWidth();
            const float searchBottom = left.y + Gw2Ui::InputBoxHeight();
            bool shown=false;
            char search[32]="";
            ImVec2 controlsEnd;
            Gw2Ui::SearchToolbar("##search",search,sizeof(search),"Search events...",[&] {
                const auto controlsLeft = ImGui::GetCursorScreenPos();
                Check(controlsLeft.y>searchBottom,"Shared search toolbars must leave a gap before controls");
                Gw2Ui::ShowCompletedChip("##shown",shown,ImVec2(38.f*scale,30.f*scale),"Show completed");
                const auto chipEnd = ImGui::GetItemRectMax();
                const float buttonWidth = std::min(Gw2Ui::ContentWidth(), std::max(132.f*scale,Gw2Ui::ActionButtonSize("Event checklist").x));
                Gw2Ui::SameLineRightCluster(buttonWidth,6.f*scale);
                Gw2Ui::ActionButtonPx("Event checklist",buttonWidth,30.f*scale);
                const auto buttonMin=ImGui::GetItemRectMin(), buttonMax=ImGui::GetItemRectMax();
                controlsEnd = ImVec2(buttonMax.x,std::max(chipEnd.y,buttonMax.y));
                Check(std::fabs(buttonMax.x-cardRight)<0.1f,"Right clusters must respect the card's right padding");
                const bool fits = cardRight-chipEnd.x >= buttonWidth+6.f*scale;
                Check(fits ? std::fabs(buttonMin.y-controlsLeft.y)<0.1f && buttonMin.x>=chipEnd.x+6.f*scale-0.1f
                           : buttonMin.y>=chipEnd.y,
                      "Toolbar actions must share a row when they fit and wrap without overlapping at narrow widths");
            });
            Check(ImGui::GetCursorScreenPos().y>controlsEnd.y,"Shared toolbars must leave a gap before list content");
            Gw2Ui::EndCard(); ImGui::End(); ImGui::Render();
        }
    }

    void CheckChecklistDistancePills()
    {
        Harness ui;
        ImGui::GetIO().DisplaySize = ImVec2(900.f,1400.f);
        constexpr ImU32 border = IM_COL32(91,47,231,255), title = IM_COL32(31,173,121,255);
        for (float scale : {0.75f,1.f,1.5f,2.f})
        for (float localScale : {1.f,1.25f})
        for (float width : {300.f,620.f})
        for (const char* distance : {"near","24m","1.2km"})
        {
            Gw2Ui::SetGlobalScale(scale);
            Gw2Ui::PushTextScale(localScale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.f,20.f)); ImGui::SetNextWindowSize(ImVec2(width,1320.f));
            ImGui::Begin("Distance pill tests",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            Gw2Ui::BeginAccentCard("distances",0.f,IM_COL32_WHITE);
            Gw2Ui::ChecklistItemSpec item;
            item.Id="event"; item.Label="Help scholars defend their camp against invading enemies.";
            item.Subtitle="Event - Lv 65";
            item.Color=title; item.Detail=distance; item.DetailFontSize=16.f;
            item.DetailStyle=Gw2Ui::ChecklistDetailStyle::Pill; item.DetailBorder=border;
            item.DrawIcon=[](auto*,auto,float) {}; // Reserve the production event icon column.
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,0.5f);
            const auto row=Gw2Ui::ChecklistItem(item,0,0);
            ImGui::PopStyleVar();
            const auto* list=ImGui::GetWindowDrawList();
            ImVec2 badgeMin(1.e9f,1.e9f),badgeMax(-1.e9f,-1.e9f),textMax(-1.e9f,-1.e9f);
            int badgeVertices=0, textVertices=0;
            for (const auto& vertex : list->VtxBuffer)
            {
                const auto rgb=vertex.col & 0x00ffffffu;
                if (!(vertex.col >> IM_COL32_A_SHIFT)) continue;
                if (rgb==(border & 0x00ffffffu))
                {
                    ++badgeVertices;
                    badgeMin.x=std::min(badgeMin.x,vertex.pos.x); badgeMin.y=std::min(badgeMin.y,vertex.pos.y);
                    badgeMax.x=std::max(badgeMax.x,vertex.pos.x); badgeMax.y=std::max(badgeMax.y,vertex.pos.y);
                    Check((vertex.col >> IM_COL32_A_SHIFT)<=128,"Shared pill borders must obey scoped opacity");
                }
                else if (rgb==(title & 0x00ffffffu) || rgb==(Gw2Ui::kTextSub & 0x00ffffffu))
                {
                    ++textVertices;
                    textMax.x=std::max(textMax.x,vertex.pos.x); textMax.y=std::max(textMax.y,vertex.pos.y);
                }
            }
            if (!(badgeVertices>0 && textVertices>0))
                std::cerr << "Missing pill/text: scale=" << scale << " local=" << localScale << " width=" << width
                          << " distance=" << distance << " badges=" << badgeVertices << " text=" << textVertices
                          << " row=" << row.Row.rowMin.x << ',' << row.Row.rowMin.y << ".." << row.Row.rowMax.x << ',' << row.Row.rowMax.y << '\n';
            Check(badgeVertices>0 && textVertices>0,"Near/metre/kilometre distance pills and event text must remain visible at narrow widths and every scale");
            Check(badgeMin.x>=row.Row.rowMin.x && badgeMin.y>=row.Row.rowMin.y && badgeMax.y<=row.Row.rowMax.y &&
                  std::fabs(badgeMax.x-(row.Row.rowMax.x-8.f*Gw2Ui::TextScale()))<1.1f,
                  "Distance pills must stay within their row and align with its right padding");
            Check(textMax.x<badgeMin.x || textMax.y<badgeMin.y,"Wrapped event text must never overlap the distance pill");
            Check(ImGui::GetCursorScreenPos().y>=row.Row.rowMax.y,"A stacked distance pill must reserve space before the next event");
            item.Id="next"; item.Label="Next event"; item.Subtitle=""; item.Detail="";
            const auto next=Gw2Ui::ChecklistItem(item,0,1);
            if (next.Row.rowMax.x>next.Row.rowMin.x) // Offscreen rows reserve space without a hit target.
                Check(next.Row.rowMin.y>=row.Row.rowMax.y,"Visible event rows must never overlap");
            Gw2Ui::EndCard(); ImGui::End(); ImGui::Render();
            Gw2Ui::PopTextScale();
        }
    }

    void CheckCardContentBounds()
    {
        Harness ui;
        ImGui::GetIO().DisplaySize = ImVec2(1400.f, 1600.f);
        for (float scale : {0.75f, 1.f, 1.5f, 2.f})
        for (float textScale : {1.f, 1.6f})
        for (float width : {280.f, 600.f})
        for (bool accented : {false, true})
        for (bool explicitWidth : {false, true})
        for (int frame = 0; frame < 2; ++frame) // The second frame includes the list's real scrollbar.
        {
            Gw2Ui::SetGlobalScale(scale);
            Gw2Ui::PushTextScale(textScale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.f, 20.f));
            ImGui::SetNextWindowSize(ImVec2(1200.f, 1500.f));
            ImGui::Begin("Card bounds tests", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            const float windowWidth = Gw2Ui::ContentWidth();
            ImGui::BeginGroup();
            Gw2Ui::PushContentWidth(width);
            const ImVec2 cardStart = ImGui::GetCursorScreenPos();
            const float padding = Gw2Ui::CardPadding();
            Gw2Ui::BeginAccentCard("widget", explicitWidth ? width : 0.f, accented ? IM_COL32_WHITE : 0);
            const ImVec2 content = ImGui::GetCursorScreenPos();
            const float innerWidth = Gw2Ui::CardInnerWidth();
            const float right = content.x + innerWidth;
            Check(std::fabs(right - (cardStart.x + width - padding)) < 0.01f,
                  "Automatic and explicit card widths must fill the same slot with identical right padding");
            Gw2Ui::PushTextScale(1.f); // Objectives neutralizes the dashboard text scale inside its card.
            Check(std::fabs(Gw2Ui::CardInnerWidth() - innerWidth) < 0.01f &&
                  std::fabs(Gw2Ui::ContentWidth() - innerWidth) < 0.01f,
                  "Changing content typography must not change the card's captured insets");
            Gw2Ui::LabelWrapped("Help Arcanist Vance study dwarven relics at Granite Citadel.", Gw2Ui::kGold, false, nullptr, 24.f);
            Check(ImGui::GetItemRectMax().x <= right + 0.01f,
                  "Long objective titles must wrap within the card's content bounds");
            Gw2Ui::BeginScrollArea("objectives", 180.f);
            ImGuiWindow* list = ImGui::GetCurrentWindow();
            Check((list->Flags & ImGuiWindowFlags_NoBackground) && list->Pos.x + list->Size.x <= right + 0.01f,
                  "Scrollable lists must preserve the enclosing background and leave its right border clear");
            Check(std::fabs(Gw2Ui::CardInnerWidth() - Gw2Ui::ContentWidth()) < 0.01f &&
                  ImGui::GetCursorScreenPos().x + Gw2Ui::ContentWidth() <= right + 0.01f,
                  "Scroll areas must resolve their own content width rather than inherit a parent's card width");
            for (int row = 0; row < 30; ++row)
                Gw2Ui::LabelWrapped("An objective with a long description that wraps within this scroll area.", Gw2Ui::kTextSub);
            if (frame == 1)
                Check(list->ScrollbarY && Gw2Ui::ContentWidth() < list->Size.x,
                      "Long bounded lists must remain scrollable and reserve their own scrollbar width");
            Gw2Ui::EndScrollArea();
            Check(std::fabs(ImGui::GetCursorScreenPos().x + Gw2Ui::ContentWidth() - right) < 0.01f,
                  "Closing a scroll area must restore the enclosing card's bounds");
            ImGui::BeginChild("native-child", ImVec2(0.f, 24.f), false, ImGuiWindowFlags_NoBackground);
            Check(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x <= right + 0.01f,
                  "Ordinary auto-width ImGui children must also respect framework card bounds");
            ImGui::EndChild();
            const float contentBottom = ImGui::GetCurrentWindow()->DC.CursorMaxPos.y;
            Gw2Ui::EndCard(); // Intentionally close while the inner text scale is active.
            Check(std::fabs(ImGui::GetItemRectMax().x - (cardStart.x + width)) < 0.01f,
                  "Card frames must reach the assigned slot edge without an implicit exterior margin");
            Check(std::fabs(ImGui::GetItemRectMax().y - (contentBottom + padding)) < 0.01f,
                  "Card backgrounds must use their captured bottom padding even after a text-scale change");
            Gw2Ui::PopTextScale();
            Gw2Ui::PopContentWidth();
            ImGui::EndGroup();
            Check(std::fabs(Gw2Ui::ContentWidth() - windowWidth) < 0.01f,
                  "Ending a widget must restore the available width for following dashboard slots");
            ImGui::End();
            ImGui::Render();
            Gw2Ui::PopTextScale();
        }
    }

    void CheckNarrowChecklistInput()
    {
        Harness ui;
        ImGui::GetIO().DisplaySize = ImVec2(1000.f, 1500.f);
        Gw2Ui::SetGlobalScale(1.5f);
        bool marked = false;
        int checkboxClicks = 0, bodyClicks = 0;
        ImVec2 checkbox, body, pill;
        const auto frame = [&](ImVec2 mouse, bool down) {
            ImGui::GetIO().MousePos = mouse; ImGui::GetIO().MouseDown[0] = down;
            ImGui::NewFrame();
            Gw2Ui::PushTextScale(1.6f);
            ImGui::SetNextWindowPos(ImVec2(20.f, 20.f)); ImGui::SetNextWindowSize(ImVec2(300.f, 1400.f));
            ImGui::Begin("Narrow checklist input", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            Gw2Ui::BeginCard("events");
            ImVec2 iconCenter;
            float iconSize = 0.f;
            Gw2Ui::ChecklistItemSpec item;
            item.Id = "event"; item.Label = "Defend the scholars at Granite Citadel.";
            item.Subtitle = "Defend - Lv 47";
            item.Detail = "24m"; item.DetailStyle = Gw2Ui::ChecklistDetailStyle::Pill;
            item.Marked = marked;
            item.DrawIcon = [&](ImDrawList*, ImVec2 center, float size) { iconCenter = center; iconSize = size; };
            const auto hit = Gw2Ui::ChecklistItem(item, 0, 0);
            if (hit.CheckboxClicked) { marked = !marked; ++checkboxClicks; }
            if (hit.BodyClicked) ++bodyClicks;
            const float scale = Gw2Ui::TextScale();
            checkbox = ImVec2(hit.Row.contentMin.x + 15.f * scale, iconCenter.y);
            body = ImVec2(checkbox.x, iconCenter.y + iconSize * 0.5f + 10.f * scale);
            pill = ImVec2(hit.Row.rowMax.x - 8.f * scale - Gw2Ui::PillWidth(item.Detail, item.DetailFontSize) * 0.5f,
                          hit.Row.rowMax.y - 4.f * scale - Gw2Ui::PillHeight(item.DetailFontSize) * 0.5f);
            Check(body.y < pill.y && iconCenter.x + iconSize * 0.5f < hit.Row.rowMax.x,
                  "Narrow checklist controls, text and distance pill must occupy usable separate regions");
            Gw2Ui::EndCard(); ImGui::End(); Gw2Ui::PopTextScale(); ImGui::Render();
        };
        const ImVec2 outside(900.f, 1450.f);
        for (int i = 0; i < 3; ++i) frame(outside, false);
        const auto click = [&](ImVec2 position) { frame(position, false); frame(position, true); frame(position, false); };
        click(checkbox);
        Check(marked && checkboxClicks == 1 && bodyClicks == 0,
              "The checkbox above a narrow event body must mark only that event without firing travel");
        click(body);
        click(pill);
        Check(marked && checkboxClicks == 1 && bodyClicks == 2,
              "Body and distance clicks below a stacked checkbox must remain body actions, not completion clicks");
    }

    void CheckIndependentWindowsAndHalfSlots()
    {
        Harness ui;
        ImGui::GetIO().DisplaySize = ImVec2(1400.f, 1200.f);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20.f, 20.f)); ImGui::SetNextWindowSize(ImVec2(1100.f, 1100.f));
        ImGui::Begin("Width scope tests", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImRect clip = window->ClipRect;
        for (int slot = 0; slot < 2; ++slot)
        {
            if (slot) ImGui::SameLine(0.f, 12.f);
            ImGui::PushID(slot);
            ImGui::BeginGroup();
            const ImVec2 start = ImGui::GetCursorScreenPos();
            Gw2Ui::PushContentWidth(280.f);
            Gw2Ui::BeginCard("half", 280.f);
            const float inner = Gw2Ui::ContentWidth();
            ImGui::SetNextWindowPos(ImVec2(400.f, 700.f)); ImGui::SetNextWindowSize(ImVec2(700.f, 160.f));
            ImGui::Begin("Independent window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            Check(Gw2Ui::CardInnerWidth() > 600.f,
                  "Popups and unrelated windows must not inherit another window's active card width");
            Gw2Ui::LabelWrapped("A popup owns its own width.");
            ImGui::End();
            Check(std::fabs(Gw2Ui::ContentWidth() - inner) < 0.01f,
                  "Returning from another window must retain the widget's bounds");
            Gw2Ui::BeginCard("nested");
            Gw2Ui::LabelWrapped("Nested card content remains inside the half-width slot.");
            Gw2Ui::EndCard();
            Check(std::fabs(Gw2Ui::ContentWidth() - inner) < 0.01f,
                  "Nested cards must restore their parent's available width");
            Gw2Ui::EndCard();
            Gw2Ui::PopContentWidth();
            ImGui::EndGroup();
            Check(ImGui::GetItemRectMax().x <= start.x + 280.f + 0.01f,
                  "A half-width widget must not occupy its neighbour's slot");
            Check(window->ClipRect.Min.x == clip.Min.x && window->ClipRect.Max.x == clip.Max.x,
                  "Closing all content scopes must restore the host's clipping for later widgets");
            ImGui::PopID();
        }
        ImGui::End(); ImGui::Render();
    }

    void CheckBoundedLabels()
    {
        Harness ui;
        ImGui::GetIO().DisplaySize = ImVec2(1400.f, 1000.f);
        for (float scale : {0.75f, 1.f, 1.5f, 2.f})
        for (float textScale : {1.f, 1.5f, 1.6f})
        {
            Gw2Ui::SetGlobalScale(scale);
            Gw2Ui::PushTextScale(textScale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.f, 20.f)); ImGui::SetNextWindowSize(ImVec2(1300.f, 900.f));
            ImGui::Begin("Bounded label tests", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            for (bool foreground : {false, true})
            for (auto align : {Gw2Ui::HAlign::Left, Gw2Ui::HAlign::Center, Gw2Ui::HAlign::Right})
            for (auto vertical : {Gw2Ui::VAlign::Top, Gw2Ui::VAlign::Middle, Gw2Ui::VAlign::Bottom})
            for (auto shadow : {Gw2Ui::TextShadow::None, Gw2Ui::TextShadow::Outline, Gw2Ui::TextShadow::Drop})
            for (float wrap : {0.f, 60.f})
            for (float width : {-1.f, 0.f, 80.f, 240.f})
            {
                ImDrawList* dl = foreground ? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
                const ImVec4 parentClip = dl->_CmdHeader.ClipRect;
                const int clipDepth = dl->_ClipRectStack.Size, firstVertex = dl->VtxBuffer.Size;
                const ImVec2 a(160.25f, 140.25f), b(a.x + width, a.y + 28.f * scale);
                const char* title = "Current Objective / Recommended Zones\nAdditional guidance";
                if (foreground)
                    Gw2Ui::LabelDL(dl, a, b, title, align, vertical, Gw2Ui::kGold, true, nullptr, 18.f,
                                   wrap, 1.6f, shadow);
                else
                    Gw2Ui::LabelIn(a, b, title, align, vertical, Gw2Ui::kGold, true, nullptr, 18.f,
                                   wrap, 1.6f, shadow);
                if (width <= 0.f)
                    Check(dl->VtxBuffer.Size == firstVertex, "Labels with no usable rectangle must not draw over neighbouring controls");
                for (int vertex = firstVertex; vertex < dl->VtxBuffer.Size; ++vertex)
                {
                    const ImVec2 p = dl->VtxBuffer[vertex].pos;
                    Check(p.x >= a.x - 0.01f && p.x <= b.x + 0.01f &&
                          p.y >= a.y - 0.01f && p.y <= b.y + 0.01f,
                          "Bounded labels must clip every glyph, bold and shadow pass to their own rectangle");
                }
                Check(dl->_ClipRectStack.Size == clipDepth && dl->_CmdHeader.ClipRect.x == parentClip.x &&
                      dl->_CmdHeader.ClipRect.y == parentClip.y && dl->_CmdHeader.ClipRect.z == parentClip.z &&
                      dl->_CmdHeader.ClipRect.w == parentClip.w,
                      "Drawing a bounded label must preserve the enclosing draw-list clip for controls that follow");
            }
            // Flowing labels intentionally keep their natural overhang/shadow. Only the explicit-rectangle
            // APIs constrain ink to a supplied box; fixing titles must not trim ordinary outlined text.
            ImGui::SetCursorScreenPos(ImVec2(100.f, 300.f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int firstVertex = dl->VtxBuffer.Size;
            Gw2Ui::Label("j", Gw2Ui::kGold, true, nullptr, 18.f, 1.6f);
            const ImVec2 naturalMin = ImGui::GetItemRectMin(), naturalMax = ImGui::GetItemRectMax();
            bool overhang = false;
            for (int vertex = firstVertex; vertex < dl->VtxBuffer.Size; ++vertex)
            {
                const ImVec2 p = dl->VtxBuffer[vertex].pos;
                overhang |= p.x < naturalMin.x || p.x > naturalMax.x || p.y < naturalMin.y || p.y > naturalMax.y;
            }
            Check(overhang, "Flowing labels must retain their outlined glyph overhang outside the advance-size box");
            for (const char* title : {"Current Objective", "Objectives / Farming", "Recommended Zones", "Events", u8"Activités / Événements"})
            for (float width : {0.f, 8.f, 42.f, 80.f, 140.f, 240.f})
            {
                const auto fitted = Gw2Ui::Ellipsize(title, 12.f, width);
                Check(Gw2Ui::MeasureWidth(fitted.c_str(), 12.f) <= width + 0.01f,
                      "Ellipsized widget names must fit their resolved font and text scale");
                if (Gw2Ui::MeasureWidth(title, 12.f) <= width && width > 1.f)
                    Check(fitted == title, "Names that fit must remain unchanged");
            }
            ImGui::End(); ImGui::Render();
            Gw2Ui::PopTextScale();
        }
    }

    void CheckColumnLayout()
    {
        Harness ui;
        ImGui::GetIO().DisplaySize = ImVec2(1500.f, 1500.f);
        for (float scale : {0.75f, 1.f, 1.5f, 2.f})
        for (float textScale : {1.f, 1.5f, 1.6f})
        for (float bodyWidth : {280.f, 388.f, 420.f, 560.f})
        for (bool requestedHalf : {false, true})
        {
            Gw2Ui::SetGlobalScale(scale);
            Gw2Ui::PushTextScale(textScale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.f, 20.f)); ImGui::SetNextWindowSize(ImVec2(1300.f, 1450.f));
            ImGui::Begin("Column layout tests", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            Gw2Ui::PushContentWidth(bodyWidth * scale);
            const auto columns = Gw2Ui::MeasureColumns(2, 190.f);
            Check(columns.fits == (bodyWidth >= 388.f) && std::fabs(columns.gap - 8.f * scale) < 0.01f,
                  "Half-width availability and column spacing must depend on layout scale, not dashboard text zoom");
            Check(std::fabs(2.f * columns.width + columns.gap - bodyWidth * scale) < 0.01f,
                  "Equal columns and their gap must occupy the host's assigned width exactly");
            const int count = requestedHalf && columns.fits ? 2 : 1;
            const float width = count == 2 ? columns.width : bodyWidth * scale;
            for (int slot = 0; slot < count; ++slot)
            {
                if (slot) ImGui::SameLine(0.f, columns.gap);
                ImGui::PushID(slot);
                const ImVec2 cardStart = ImGui::GetCursorScreenPos();
                Gw2Ui::BeginCard("widget", width);
                Gw2Ui::LabelWrapped("Objective content wraps inside its assigned column.");
                Gw2Ui::EndCard();
                Check(std::fabs(ImGui::GetItemRectMax().x - (cardStart.x + width)) < 0.01f &&
                      ImGui::GetItemRectMax().x <= start.x + bodyWidth * scale + 1.f,
                      "Rendered half-width frames must respect their columns and leave their neighbour visible");
                if (slot)
                    Check(std::fabs(cardStart.x - (start.x + width + columns.gap)) < 1.f,
                          "The second half-width widget must share its row, separated only by the layout gap");
                ImGui::PopID();
            }
            Gw2Ui::PopContentWidth();
            ImGui::End(); ImGui::Render();
            Gw2Ui::PopTextScale();
        }
    }

    void CheckSectionHosting()
    {
        Harness ui;
        ImGui::GetIO().DisplaySize = ImVec2(1400.f, 1500.f);
        ImGuiID ids[2] = {};
        for (float scale : {0.75f, 1.f, 1.5f, 2.f})
        for (float textScale : {1.f, 1.6f})
        for (float width : {240.f, 600.f})
        for (bool headerVisible : {false, true})
        for (auto presentation : {Gw2Ui::SectionPresentation::Inline, Gw2Ui::SectionPresentation::InlineAccent,
                                  Gw2Ui::SectionPresentation::Card})
        {
            Gw2Ui::SetGlobalScale(scale);
            Gw2Ui::PushTextScale(textScale);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.f, 20.f)); ImGui::SetNextWindowSize(ImVec2(1200.f, 1450.f));
            ImGui::Begin("Section hosting tests", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            const auto before = Gw2Ui::CardsOpened();
            const ImVec2 hostStart = ImGui::GetCursorScreenPos();
            Gw2Ui::BeginCard("host", width);
            if (headerVisible)
            {
                Gw2Ui::LabelWrapped("Widget header", Gw2Ui::kGold, false, nullptr, 18.f);
                Gw2Ui::Divider();
            }
            const float hostRight = ImGui::GetCursorScreenPos().x + Gw2Ui::ContentWidth();
            const ImU32 sectionBg = IM_COL32(14, 79, 157, 185);
            const ImU32 accent = IM_COL32(40, 98, 196, 255);
            for (int section = 0; section < 2; ++section)
            {
                const ImVec2 start = ImGui::GetCursorScreenPos();
                Gw2Ui::BeginSection(section ? "nearby" : "timed", presentation, accent, sectionBg);
                const auto id = ImGui::GetID("event");
                if (presentation == Gw2Ui::SectionPresentation::Inline) ids[section] = id;
                else Check(ids[section] == id, "Shared section controls must keep stable identities in either hosting style");
                const float contentLeft = ImGui::GetCursorScreenPos().x;
                const float contentRight = contentLeft + Gw2Ui::ContentWidth();
                const auto checkRowOrigin = [&] {
                    // ItemSize pixel-snaps new lines. A sub-pixel difference is legitimate; resetting by the
                    // section's whole accent inset would clip the first letters/icons on every later row.
                    Check(std::fabs(ImGui::GetCursorScreenPos().x - contentLeft) < 1.f &&
                          ImGui::GetCursorScreenPos().x >= ImGui::GetCurrentWindow()->ClipRect.Min.x - 1.f,
                          "New rows in an accented section must start inside its content clip, preserving leading letters and icons");
                    Check(std::fabs(ImGui::GetCursorScreenPos().x + Gw2Ui::ContentWidth() - contentRight) < 0.01f,
                          "Row flow must preserve both edges of the section's content bounds");
                };
                if (presentation != Gw2Ui::SectionPresentation::Card)
                {
                    const float inset = presentation == Gw2Ui::SectionPresentation::InlineAccent ? 8.f * Gw2Ui::TextScale() : 0.f;
                    Check(std::fabs(ImGui::GetCursorScreenPos().x - (start.x + inset)) < 0.01f &&
                          std::fabs(ImGui::GetCursorScreenPos().x + Gw2Ui::ContentWidth() - hostRight) < 0.01f,
                          "Inline accents must reserve their own inset without changing the host's right edge");
                }
                Gw2Ui::SectionHeader(section ? "Nearby Events" : "Timed Events");
                checkRowOrigin();
                Gw2Ui::LabelWrapped("Shared event content.");
                checkRowOrigin();
                Gw2Ui::Label("Tasks", Gw2Ui::kGold, false, nullptr, 16.f);
                checkRowOrigin();
                Gw2Ui::Chip("Use", Gw2Ui::kChipText, Gw2Ui::kGold, 12.f);
                ImGui::SameLine(0.f, 4.f * Gw2Ui::TextScale());
                Gw2Ui::Chip("Give", Gw2Ui::kChipText, Gw2Ui::kGold, 12.f);
                ImGui::NewLine();
                checkRowOrigin();
                Gw2Ui::Label("Hearts", Gw2Ui::kGold, false, nullptr, 16.f);
                checkRowOrigin();
                Gw2Ui::LabelWrapped("Decipher dwarven headstones, and protect Priory researchers.");
                checkRowOrigin();
                DrawHeartTaskDetails(ui.Tasks, ui.View, "section-heart");
                checkRowOrigin();
                ImGui::Indent(7.f * Gw2Ui::TextScale());
                Gw2Ui::Label("Indented notes");
                ImGui::Unindent(7.f * Gw2Ui::TextScale());
                checkRowOrigin();
                Gw2Ui::BeginSection("annotation", Gw2Ui::SectionPresentation::Inline);
                Gw2Ui::LabelWrapped("Nested content.");
                Gw2Ui::EndSection();
                checkRowOrigin();
                if (Gw2Ui::BeginScrollArea("section-scroll", 50.f)) Gw2Ui::LabelWrapped("Scrollable notes.");
                Gw2Ui::EndScrollArea();
                checkRowOrigin();
                Gw2Ui::EndSection();
                Check(ImGui::GetItemRectMax().x <= hostRight + 0.01f &&
                      std::fabs(ImGui::GetCursorScreenPos().x + Gw2Ui::ContentWidth() - hostRight) < 0.01f,
                      "Closing shared sections must restore the host's bounds for subsequent content");
            }
            Gw2Ui::EndCard();
            Check(std::fabs(ImGui::GetItemRectMax().x - (hostStart.x + width)) < 0.01f,
                  "Headers and section presentation must not change the widget's outer frame width");
            Check(Gw2Ui::CardsOpened() - before == (presentation == Gw2Ui::SectionPresentation::Card ? 3u : 1u),
                  "Inline sections, including accented sections, must share exactly one host frame");
            bool surfaceDrawn = false, accentDrawn = false;
            for (const ImDrawVert& vertex : ImGui::GetWindowDrawList()->VtxBuffer)
            {
                surfaceDrawn |= vertex.col == sectionBg;
                accentDrawn |= vertex.col == accent;
            }
            Check(surfaceDrawn == (presentation == Gw2Ui::SectionPresentation::Card),
                  "Inline content must inherit the host surface without painting a second background");
            Check(accentDrawn == (presentation != Gw2Ui::SectionPresentation::Inline),
                  "InlineAccent must retain the shared accent decoration without adding a frame");
            ImGui::End(); ImGui::Render();
            Gw2Ui::PopTextScale();
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
        CheckChecklistControls();
        CheckChecklistLayout();
        CheckRightClusterLayout();
        CheckChecklistDistancePills();
        CheckCardContentBounds();
        CheckNarrowChecklistInput();
        CheckIndependentWindowsAndHalfSlots();
        CheckBoundedLabels();
        CheckColumnLayout();
        CheckSectionHosting();
        std::cout << "Heart answer and shared checklist UI checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
