#pragma once
class App;

// The GW2-skinned QuickAccess tray menu, in ui/TrayMenu.cpp. Two pieces:
//   RenderQuickAccessMenu - the Nexus QuickAccess context-menu callback (entry.cpp registers it via
//     QuickAccess_AddContextMenu in AddonLoad). Nexus opens its OWN popup we can't size/skin, so this just
//     records the anchor + asks Render to open ours, then dismisses Nexus's.
//   DrawTrayMenu          - called from Render(): draws our auto-sized, GW2-chromed popup (Gw2Ui::ContextMenu)
//     when the callback requested it, and runs the picked item against the injected App&.
// DrawTrayMenu takes App& explicitly; RenderQuickAccessMenu touches only its own file-static anchor.
void RenderQuickAccessMenu();   // Nexus QuickAccess context-menu callback (registered in AddonLoad)
void DrawTrayMenu(App& app);    // Render: our GW2-skinned popup, opened when the callback requested it
