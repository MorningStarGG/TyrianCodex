#pragma once
#include <string>

// DPI flags read from the settings FILES so the map can correct NexusLink->Scaling, which
// tracks NEXUS's DPI while the map renders at the GAME's DPI:
//   corrected = NexusScaling * (gameDPI ? osDPI : 1) / (nexusDPI ? osDPI : 1).
struct DpiState
{
    bool gameDpiOn = false;  // GW2 "dpiScaling"  (%APPDATA%\Guild Wars 2\GFXSettings.Gw2-64.exe.xml)
    bool nexusDpiOn = false; // Nexus "DPIScaling" (<GW2>\addons\Nexus\Settings.json)
    float osDpiScale = 1.0f; // OS display scale for the game's monitor (GetDpiForMonitor / 96), e.g. 1.25
};

// -----------------------------------------------------------------------------------------------------
// DpiWatcher: keeps the two DPI flags + the OS display scale current. EVENT-DRIVEN, not polled -
// FindFirstChangeNotification watches the two settings folders; Refresh() does a non-blocking check each frame
// and re-reads a file only when its folder changed (near-zero cost otherwise). Owns the two watch handles
// (stored as void* so this header needs no <windows.h>); Close() frees them and the dtor is the backstop.
// Refresh() MUST run every frame OUTSIDE the gameplay gate so it stays live while the settings window is
// focused or the map is open. The map renderer reads State() to correct its scale.
// -----------------------------------------------------------------------------------------------------
class DpiWatcher
{
public:
    void Refresh(const std::string &settingsPath); // lazy one-time setup, then event-driven re-read
    void Close();                                  // close the 2 watch handles (idempotent; dtor backstop)
    ~DpiWatcher() { Close(); }

    DpiState State() const { return {gameDpiOn_, nexusDpiOn_, osDpiScale_}; }

private:
    void ReadGameDpiFlag();
    void ReadNexusDpiFlag();

    bool gameDpiOn_ = false, nexusDpiOn_ = false;
    float osDpiScale_ = 1.0f;
    bool watchInit_ = false;
    void *gw2Watch_ = nullptr; // FindFirstChangeNotification HANDLE (nullptr = none)
    void *nexusWatch_ = nullptr;
    std::string gw2GfxPath_, nexusSettingsPath_;
};
