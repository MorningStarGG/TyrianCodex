#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerEventPanels.h"

// Live event timers + nearby event areas. Reuses the ONE ViewerEvents implementation (no duplicated timer
// math / rows / click-to-guide); the dashboard just decides when + where to draw it.
void DashW::Events(App& app, float /*w*/)
{
    const auto presentation = Gw2Ui::SectionPresentation::Inline; // Dashboard owns the one frame for both sections.
    const int timers = ViewerEvents::DrawTimers(app, 16.f, presentation);
    if (timers > 0) Gw2Ui::Divider();
    const int areas = ViewerEvents::DrawAreas(app, app.state.curStep, 16.f, 5, presentation);
    if (timers == 0 && areas == 0)
        Gw2Ui::LabelWrapped("No events for this map.", Gw2Ui::kTextDim, false, nullptr, 16.f);
}
