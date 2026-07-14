#pragma once
class App;

// -----------------------------------------------------------------------------------------------------
// HUD: a compact centered launcher bar (icon buttons flanking a clock), docked top or bottom. EVERY setting
// lives in Config (config.hud* scalars + config.hudButtons layout) and is per-character via the HUD
// ConfigProfileFamily (see ui/profiles/ConfigProfiles.h) -- there is no bespoke payload/store here anymore.
// Self-rendered from entry.cpp's Render(). Shared clock/time helpers live in ui/InfoHudShared.h.
// -----------------------------------------------------------------------------------------------------
namespace HUD
{
    void RegisterConfig();          // register hudButtons with ConfigProfiles (call ONCE, before ConfigProfiles::Init)
    void Render(App& app);
    void DrawSettings(App& app);    // SEC_HUD: profile bar + scalar model rows + button-layout editor
    bool IsEnabled(App& app);       // is the HUD bar on in the active profile?
    void SetEnabled(App& app, bool on);
}
