#pragma once
class App;

// The Info Panel: up to five Titan/ElvUI-style data-text bars docked near the top or bottom of the screen, with
// three zones (left / center / right) in each configurable strip. Like the HUD bar, every setting lives in Config
// and is per-character via the Info ConfigProfileFamily (see ui/profiles/ConfigProfiles.h).
// The data-text catalog itself lives in ui/infopanel/InfoData.h.
namespace InfoPanel
{
    void     RegisterConfig();                                       // register infoBars with ConfigProfiles (call ONCE, before ConfigProfiles::Init)
    void     Render(App& app);
    void     DrawSettings(App& app, const char* page);               // SEC_INFO; page="" = Layout & texts, else a data-text key
    void     SetSettingsBar(int index);                              // select Bar 1-5 in the Options page (0-based)
    bool     IsEnabled(App& app);                                    // is the Info Panel bar on in the active profile?
    void     SetEnabled(App& app, bool on);                          // toggle the Info Panel (active profile) + persist
    unsigned NeededDomains(App& app);                                // AccountData domains the shown texts need
    bool     HasWvwText(App& app);                                   // any live-WvW-match text enabled -> gates AccountData::TickWvw
}
