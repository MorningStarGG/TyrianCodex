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
        std::cout << "Heart answer and shared checklist UI checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
