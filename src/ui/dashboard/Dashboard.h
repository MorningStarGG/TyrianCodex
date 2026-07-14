#pragma once
class App;

// -----------------------------------------------------------------------------------------------------
// Dashboard: a movable panel of widgets. An edge-docked handle (draggable along its edge, with an unread badge)
// that opens a slide-out panel of toggleable, reorderable widgets. Reads the same Notify store as the toasts
// for its log widget. EVERY setting lives in Config (config.dash* scalars + config.dashLayout order/show/span/
// hover-chrome) and is per-character via the Dashboard ConfigProfileFamily (ui/profiles/ConfigProfiles.h) --
// there is no bespoke payload/store here anymore. Widgets come from the registry (widgets/Widget.h). Drawn once
// per frame at the end of Render().
// -----------------------------------------------------------------------------------------------------
namespace Dashboard
{
    void RegisterConfig();                 // register dashLayout with ConfigProfiles (call ONCE, before ConfigProfiles::Init)
    void Render(App& app);                 // handle + slide-out panel (end of Render)
    void Toggle();                         // open/close the panel (tray / keybind entry point)
    bool IsOpen();                         // panel open or animating (AccountData gates its TTL refresh on this)
    bool HasVisibleWvwWidget(App& app);    // a WvW widget on a visible dashboard -> gates AccountData::TickWvw

    void DrawWidgetSettings(App& app);     // the SEC_DASHBOARD per-widget show/half list (Options tab)
    void DrawProfileSettings(App& app);    // the SEC_DASHBOARD profile bar (ConfigProfiles Owner::Dash)
}
