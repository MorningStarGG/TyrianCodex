#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerEventPanels.h"
#include <imgui.h>

// Live event timers + nearby event areas. Reuses the ONE ViewerEvents implementation (no duplicated timer
// math / rows / click-to-guide); the dashboard just decides when + where to draw it.
void DashW::Events(App& app, float /*w*/)
{
    const int timers = ViewerEvents::DrawTimers(app, 16.f);
    if (timers > 0) ImGui::Spacing();
    const int areas = ViewerEvents::DrawAreas(app, app.state.curStep, 16.f, 5);
    if (timers == 0 && areas == 0)
        Gw2Ui::Label("No events for this map.", Gw2Ui::kTextDim, false, nullptr, 16.f);
}
