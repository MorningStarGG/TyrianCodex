#include "ui/NotifyActions.h"
#include "app/App.h"
#include "model/EventTimers.h"
#include "ui/ApiReminder.h"
#include "ui/GuideViewer.h"    // TravelToLocation (sets the personal target AND pops the travel card)
#include "ui/tabs/TimersTab.h" // OpenTimersTab

void NotifyActions::Dispatch(App& app, Notify::Action action, const std::string& payload)
{
    switch (action)
    {
    case Notify::Action::OpenApiSettings:
        ApiReminder::OpenApiSettings(app);
        return;

    case Notify::Action::OpenTimers:
        OpenTimersTab(app);
        return;

    case Notify::Action::ShowEvent:
    {
        // Take the player to the event: TravelToLocation both sets the personal target (so the arrow retargets)
        // and raises the Travel / Show target / Not now card. Falls back to the board when the event has no
        // resolved spot to head for.
        for (const EventTimer& ev : app.eventTimers.Events())
        {
            if (ev.Key != payload) continue;
            if (!ev.HasCoord) break;
            TravelToLocation(app, "waypoint", ev.MapIds.empty() ? 0u : ev.MapIds.front(),
                             ev.ContinentX, ev.ContinentY, ev.Name, ev.Waypoint);
            return;
        }
        OpenTimersTab(app);
        return;
    }

    case Notify::Action::None:
    default:
        return;
    }
}
