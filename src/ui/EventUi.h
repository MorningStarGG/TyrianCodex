#pragma once
#include "ui/Gw2Ui.h"
#include "model/EventAreas.h"
class App;

// Event-specific presentation shared by the Viewer and Checklist. Framework
// controls remain in Gw2Ui; catalogs and persisted marks remain in the model.
namespace EventUi
{
    std::string CategoryLabel(const std::string& category);
    ImU32 CategoryColor(const std::string& category);
    std::string LocationText(const EventArea& event);
    void Tooltip(const EventArea& event);
    void Menu(App& app, const EventArea& event, bool hovered);
    void Travel(App& app, const EventArea& event);
}
