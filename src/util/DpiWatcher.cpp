#include "util/DpiWatcher.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>

// OS display scale for the GW2 window's monitor (GetDpiForMonitor / 96) - the real scale (e.g. 1.25 at 125%),
// independent of the process's DPI awareness (unlike GetDpiForWindow, which returns 96 for a DPI-unaware app).
// Dynamically loaded (shcore.dll, Win8.1+); falls back to GetDpiForWindow, then 1.0.
static float OsDpiScaleForGameWindow()
{
    static HWND s_hwnd = nullptr;
    if (!s_hwnd || !IsWindow(s_hwnd)) s_hwnd = FindWindowW(L"ArenaNet_Gr_Window_Class", nullptr);   // GW2's real class (Gr, not Dx)
    if (!s_hwnd) return 1.f;
    typedef HRESULT(WINAPI* GetDpiForMonitor_t)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitor_t fn = []() -> GetDpiForMonitor_t {
        HMODULE h = LoadLibraryW(L"shcore.dll");
        return h ? (GetDpiForMonitor_t)GetProcAddress(h, "GetDpiForMonitor") : nullptr;
    }();
    if (fn)
    {
        HMONITOR mon = MonitorFromWindow(s_hwnd, MONITOR_DEFAULTTONEAREST);
        UINT dx = 96, dy = 96;
        if (mon && fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy) == 0 /*S_OK*/ && dx > 0) return (float)dx / 96.f;
    }
    typedef UINT(WINAPI* GetDpiForWindow_t)(HWND);   // fallback
    static GetDpiForWindow_t fw = (GetDpiForWindow_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fw) { const UINT d = fw(s_hwnd); if (d > 0) return (float)d / 96.f; }
    return 1.f;
}

void DpiWatcher::ReadGameDpiFlag()   // GW2: <OPTION Name="dpiScaling" ... Value="true|false"/> in GFXSettings.Gw2-64.exe.xml
{
    if (gw2GfxPath_.empty()) return;
    std::ifstream f(gw2GfxPath_);
    std::string line;
    while (std::getline(f, line))
        if (line.find("\"dpiScaling\"") != std::string::npos)
        { gameDpiOn_ = line.find("Value=\"true\"") != std::string::npos; break; }
}

void DpiWatcher::ReadNexusDpiFlag()  // Nexus: "DPIScaling": true|false in <GW2>\addons\Nexus\Settings.json
{
    if (nexusSettingsPath_.empty()) return;
    std::ifstream f(nexusSettingsPath_);
    try { nlohmann::json j; f >> j; if (j.contains("DPIScaling") && j["DPIScaling"].is_boolean()) nexusDpiOn_ = j["DPIScaling"].get<bool>(); }
    catch (...) { /* missing/locked -> keep last */ }
}

// Keep the GW2 + Nexus DPI flags current. EVENT-DRIVEN: FindFirstChangeNotification watches the two folders;
// each frame we only do a non-blocking WaitForSingleObject(0) on the handles and RE-READ a file only when its
// folder actually changed. Lazy one-time setup (the API needs the DIRECTORY). osDpi is sampled at setup +
// re-sampled on a change (cheap; the OS scale rarely moves).
void DpiWatcher::Refresh(const std::string& settingsPath)
{
    if (!watchInit_)
    {
        watchInit_ = true;
        osDpiScale_ = OsDpiScaleForGameWindow();
        if (const char* appdata = std::getenv("APPDATA"))
        {
            const std::string dir = std::string(appdata) + "\\Guild Wars 2";
            gw2GfxPath_ = dir + "\\GFXSettings.Gw2-64.exe.xml";
            HANDLE h = FindFirstChangeNotificationA(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
            gw2Watch_ = (h == INVALID_HANDLE_VALUE) ? nullptr : h;
        }
        if (!settingsPath.empty())
        {
            const size_t s1 = settingsPath.find_last_of("\\/");                          // strip "settings.json"
            const std::string addonDir = (s1 == std::string::npos) ? settingsPath : settingsPath.substr(0, s1);
            const size_t s2 = addonDir.find_last_of("\\/");                              // strip the addon-dir leaf
            const std::string nexusDir = ((s2 == std::string::npos) ? addonDir : addonDir.substr(0, s2)) + "\\Nexus";
            nexusSettingsPath_ = nexusDir + "\\Settings.json";
            HANDLE h = FindFirstChangeNotificationA(nexusDir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
            nexusWatch_ = (h == INVALID_HANDLE_VALUE) ? nullptr : h;
        }
        ReadGameDpiFlag();    // initial read (no change event fires for the startup state)
        ReadNexusDpiFlag();
        return;
    }

    if (gw2Watch_ && WaitForSingleObject((HANDLE)gw2Watch_, 0) == WAIT_OBJECT_0)
    { osDpiScale_ = OsDpiScaleForGameWindow(); ReadGameDpiFlag(); FindNextChangeNotification((HANDLE)gw2Watch_); }
    if (nexusWatch_ && WaitForSingleObject((HANDLE)nexusWatch_, 0) == WAIT_OBJECT_0)
    { ReadNexusDpiFlag(); FindNextChangeNotification((HANDLE)nexusWatch_); }
}

void DpiWatcher::Close()
{
    if (gw2Watch_)   { FindCloseChangeNotification((HANDLE)gw2Watch_);   gw2Watch_ = nullptr; }
    if (nexusWatch_) { FindCloseChangeNotification((HANDLE)nexusWatch_); nexusWatch_ = nullptr; }
    watchInit_ = false;   // reset so a disable->re-enable re-arms the watches
}
