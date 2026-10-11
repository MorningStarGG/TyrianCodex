#include "ui/EventUi.h"
#include "app/App.h"
#include "app/SectorData.h"
#include "ui/GuideViewer.h"
#include "ui/wiki/WikiReader.h"
#include <algorithm>
#include <cctype>

std::string EventUi::CategoryLabel(const std::string& category)
{
    if (category == "world_boss" || category == "hard_world_boss") return "Boss";
    if (category == "meta" || category == "map_meta" || category == "meta_event") return "Meta";
    if (category == "festival" || category == "seasonal") return "Festival";
    if (category == "dynamic_event" || category == "event_page") return "Event";
    if (category == "group_event") return "Group";
    if (category == "dry_top") return "Dry Top";
    if (category == "timeline") return "Timer";
    if (category == "public_instance") return "Instance";
    if (category.empty()) return "Event";
    auto label = category;
    std::replace(label.begin(), label.end(), '_', ' ');
    label[0] = (char)std::toupper((unsigned char)label[0]);
    return label;
}

ImU32 EventUi::CategoryColor(const std::string& category)
{
    if (category == "world_boss" || category == "hard_world_boss" || category == "group_event") return IM_COL32(229,112,76,235);
    if (category == "festival" || category == "seasonal") return IM_COL32(186,132,238,235);
    if (category == "dynamic_event" || category == "event_page") return IM_COL32(120,210,150,235);
    if (category == "defend" || category == "escort") return IM_COL32(235,207,105,235);
    if (category == "combat") return IM_COL32(226,100,64,235);
    if (category == "collect") return IM_COL32(222,176,82,235);
    if (category == "interact" || category == "repair") return IM_COL32(115,174,224,235);
    if (category == "map_wide" || category == "meta_event") return IM_COL32(255,198,82,235);
    if (category == "dry_top") return IM_COL32(218,174,86,235);
    if (category == "timeline" || category == "timer_page") return IM_COL32(122,180,224,235);
    return IM_COL32(255,205,102,235);
}

std::string EventUi::LocationText(const EventArea& event)
{
    return event.HasWorldCoord ? SectorData::SectorAt(event.MapId, event.WorldX, event.WorldZ) : std::string();
}

void EventUi::Tooltip(const EventArea& event)
{
    Gw2Ui::TooltipBegin();
    Gw2Ui::TooltipTitle(event.Name.c_str());
    const auto kind = CategoryLabel(event.Category) + (event.Level > 0 ? "  -  Lv " + std::to_string(event.Level) : "");
    Gw2Ui::TooltipMuted(kind.c_str());
    const auto location = LocationText(event);
    if (!location.empty()) Gw2Ui::TooltipMuted(location.c_str());
    if (!event.Description.empty()) Gw2Ui::TooltipText(event.Description.c_str());
    Gw2Ui::TooltipMuted("Static event location; the event may not be active.");
    Gw2Ui::TooltipMuted("Tick: mark experienced   -   Right-click: menu");
    Gw2Ui::TooltipEnd();
}

void EventUi::Menu(App& app, const EventArea& event, bool hovered)
{
    const bool done = app.eventProgress.IsComplete(app.state.currentChar, event.EventId);
    std::vector<Gw2Ui::MenuNode> menu;
    if (EventProgressStore::CanMark(app.state.currentChar)) menu.push_back({done ? "Mark incomplete" : "Mark experienced", 1});
    menu.push_back({"Open in Wiki", 2});
    menu.push_back({"Copy name", 3});
    const int action = Gw2Ui::ContextMenuTree("##eventmenu", menu, hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    if (action == 1) app.eventProgress.SetComplete(app.state.currentChar, event.EventId, !done);
    else if (action == 2) Wiki::RequestOpen(event.WikiTitle.empty() ? event.Name : event.WikiTitle);
    else if (action == 3) SetClipboard(event.Name);
}

void EventUi::Travel(App& app, const EventArea& event)
{
    if (event.HasContinentCoord) TravelToLocation(app, "event", event.MapId, event.ContinentX, event.ContinentY, event.Name, "");
}
