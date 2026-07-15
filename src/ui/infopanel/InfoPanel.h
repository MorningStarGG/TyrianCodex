#pragma once
class App;

// The Info Panel: a Titan/ElvUI-style data-text bar docked near the top or bottom of the screen, with three
// zones (left / center / right) inside a configurable screen-width strip. Like the HUD bar, EVERY setting lives
// in Config (config.info* scalars +
// config.infoTexts placement + config.infoTextOpts per-text options) and is per-character via the Info
// ConfigProfileFamily (see ui/profiles/ConfigProfiles.h) -- there is no bespoke payload/store here anymore.
// The data-text catalog itself lives in ui/infopanel/InfoData.h.
namespace InfoPanel
{
    void     RegisterConfig();                                       // register infoTexts/infoTextOpts with ConfigProfiles (call ONCE, before ConfigProfiles::Init)
    void     Render(App& app);
    void     DrawSettings(App& app, const char* page);               // SEC_INFO; page="" = Layout & texts, else a data-text key
    bool     IsEnabled(App& app);                                    // is the Info Panel bar on in the active profile?
    void     SetEnabled(App& app, bool on);                          // toggle the Info Panel (active profile) + persist
    unsigned NeededDomains(App& app);                                // AccountData domains the shown texts need
    bool     HasWvwText(App& app);                                   // any live-WvW-match text enabled -> gates AccountData::TickWvw
}
