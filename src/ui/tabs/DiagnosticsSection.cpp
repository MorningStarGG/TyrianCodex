#include "ui/tabs/DiagnosticsSection.h"
#include "ui/InputProbe.h"     // keyboard probe: collection API (Gw2Ui reports into it); readout lives here
#include "ui/tabs/SettingsCommon.h"
#include "ui/SettingsWindow.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "app/Glue.h"
#include "app/AccountData.h"   // live roster (in-account status) for the character-data cleanup
#include "app/CharData.h"      // AllCharacterNames / PurgeCharacter
#include "Shared.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "ui/dashboard/Notify.h"   // test-fire the notification toasts/log
#include "util/Textures.h"
#include "util/ImageCache.h"
#include "util/OwnedTex.h"   // live addon-owned image VRAM (diagnostics readout)
#include "app/DataBootstrap.h"   // self-download bootstrap status (source / core / assets progress)
#include "api/core/Http.h"       // shared-transport diagnostics (session / abort state)
#include "util/Dyes.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
#include <imgui.h>
#include <imgui_internal.h>   // input probe: ActiveId / HoveredWindow / FindWindowByName (diagnostic readout only)
#include <nlohmann/json.hpp>
#include <atomic>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ui/tabs/JournalTab.h"

// Diagnostics section body - rendered in Options > Diagnostics (under that panel's "Diagnostics" header) and
// surfaced in settings search. Live MumbleLink/guide state + reset progress, then the image-cache stats +
// force-clear buttons. (This is the consolidated Diagnostics & Debug home; there is no separate tab.)
static const char* MapAssistTransportLabel(MapAssistTransport transport)
{
    switch (transport)
    {
        case MapAssistTransport::WndProc:   return "wndproc";
        case MapAssistTransport::SendInput: return "sendinput";
        case MapAssistTransport::Blocked:   return "blocked";
        default:                            return "none";
    }
}

// ---- Input probe (config.inputProbe) --------------------------------------------------------------------
// Counters only; the readout lives in DrawDiagnosticsSection below. wmKeyDown/wmChar are written from the
// WndProc thread and read on the render thread, hence atomics; the ImGui-side fields are render-thread only.
namespace
{
    std::atomic<unsigned> g_probeKeyDown{0};   // WM_KEYDOWN / WM_SYSKEYDOWN our hook was handed
    std::atomic<unsigned> g_probeChar{0};      // WM_CHAR our hook was handed
    unsigned              g_probeImgui = 0;    // characters that actually landed in ImGui's input queue
    unsigned              g_probeLast = 0;     // last codepoint ImGui received
    double                g_probeLastAt = -1.0;
    bool                  g_probeOn = false;

    // Per-box samples. Text boxes report as they are SUBMITTED, so the list is in submission (top-to-bottom,
    // window-by-window) order -- which is the whole point: InputText drains the queue, so the box that runs
    // first is the one that gets the characters. We show the last COMPLETE frame, because this readout is
    // itself drawn mid-frame (the settings window draws before the dashboard) and would otherwise show a
    // half-filled list that stops at itself.
    struct BoxSample
    {
        std::string id;
        unsigned    imguiId = 0;
        bool        active = false;    // held ActiveId when it ran
        int         queue = 0;         // characters waiting when it ran (before its InputText drained them)
        bool        changed = false;   // its InputText reported an edit
    };
    std::vector<BoxSample> g_boxesCur, g_boxesLast;
    int                    g_boxFrame = -1;

    void ResetInputProbe()
    {
        g_probeKeyDown = 0; g_probeChar = 0;
        g_probeImgui = 0; g_probeLast = 0; g_probeLastAt = -1.0;
        g_boxesCur.clear(); g_boxesLast.clear(); g_boxFrame = -1;
    }
}

void InputProbe::SetEnabled(bool on)
{
    if (g_probeOn && !on) { g_boxesCur.clear(); g_boxesLast.clear(); g_boxFrame = -1; }
    g_probeOn = on;
}
bool InputProbe::Enabled() { return g_probeOn; }

void InputProbe::NoteMessage(unsigned msg)
{
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) ++g_probeKeyDown;
    else if (msg == WM_CHAR)                       ++g_probeChar;
}

void InputProbe::SampleFrame()
{
    if (!ImGui::GetCurrentContext()) return;
    const ImGuiIO& io = ImGui::GetIO();
    const int n = io.InputQueueCharacters.Size;
    if (n <= 0) return;
    g_probeImgui += (unsigned)n;
    g_probeLast   = (unsigned)io.InputQueueCharacters[n - 1];
    g_probeLastAt = ImGui::GetTime();
}

void InputProbe::NoteTextBox(const char* id, unsigned imguiId, bool active, int queueOnEntry, bool changed)
{
    if (!g_probeOn || !ImGui::GetCurrentContext()) return;
    const int f = ImGui::GetFrameCount();
    if (f != g_boxFrame) { g_boxesLast.swap(g_boxesCur); g_boxesCur.clear(); g_boxFrame = f; }
    if (g_boxesCur.size() >= 24) return;   // bounded: a runaway surface can't grow this without limit
    BoxSample s;
    s.id = id ? id : "?";
    s.imguiId = imguiId; s.active = active; s.queue = queueOnEntry; s.changed = changed;
    g_boxesCur.push_back(std::move(s));
}

namespace
{
    // One of OUR windows, for the overlap readout. Rect comes from ImGui itself, so nothing has to be plumbed
    // out of the owning module. WasActive (not Active) because windows drawn AFTER the settings window this
    // frame -- the dashboard among them -- have not been submitted yet when this readout draws.
    struct ProbeWin { const char* label; const char* id; bool shown; ImVec2 mn, mx; };

    ProbeWin LookupWin(const char* label, const char* id)
    {
        ProbeWin w{label, id, false, ImVec2(0, 0), ImVec2(0, 0)};
        if (ImGuiWindow* iw = ImGui::FindWindowByName(id))
            if (iw->WasActive)
            {
                w.shown = true;
                w.mn = iw->Pos;
                w.mx = ImVec2(iw->Pos.x + iw->Size.x, iw->Pos.y + iw->Size.y);
            }
        return w;
    }

    bool RectsOverlap(const ProbeWin& a, const ProbeWin& b)
    {
        return a.shown && b.shown &&
               a.mn.x < b.mx.x && b.mn.x < a.mx.x && a.mn.y < b.mx.y && b.mn.y < a.mx.y;
    }
}

void DrawDiagnosticsSection(App& app)
{
    const ImU32 kDim = IM_COL32(176, 166, 140, 255);
    const ImU32 kVal = IM_COL32(226, 222, 212, 255);
    const float fs = SettingsText::Body;
    char b[256];

    // A label/value row that wraps the value and never clips off the panel edge.
    auto kv = [&](const char* label, const char* value, ImU32 vcol, float lwOverride = 0.f) {
        const float w  = ImGui::GetContentRegionAvail().x;
        const float lw = lwOverride > 0.f ? std::min(lwOverride, w - 46.f) : std::min(150.f, w * 0.40f);
        const float vw = std::max(40.f, w - lw - 6.f);
        const float vh = std::max(fs + 4.f, Gw2Ui::MeasureWrappedHeight(value, fs, vw));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        Gw2Ui::LabelIn(p, ImVec2(p.x + lw, p.y + vh), label, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, kDim, false, nullptr, fs);
        Gw2Ui::LabelIn(ImVec2(p.x + lw + 6.f, p.y), ImVec2(p.x + lw + 6.f + vw, p.y + vh), value,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, vcol, false, nullptr, fs, vw);
        ImGui::Dummy(ImVec2(w, vh));
    };

    // ---- Live state ----
    Gw2Ui::SectionHeader("Live state", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-live");
    if (MumbleLink)
    {
        const Mumble::Vector3& a = MumbleLink->AvatarPosition;
        std::snprintf(b, sizeof(b), "%u", MumbleLink->Context.MapId); kv("Map", b, kVal);
        std::snprintf(b, sizeof(b), "%s   (level %d)", app.state.currentChar.c_str(), app.state.charLevel); kv("Character", b, kVal);
        std::snprintf(b, sizeof(b), "%.1f, %.1f, %.1f", a.X, a.Y, a.Z); kv("Avatar", b, kVal);
        std::snprintf(b, sizeof(b), "%s    nearest trail %.1f m    on-screen %d",
                      (NexusLink && NexusLink->IsGameplay) ? "gameplay" : "not gameplay",
                      app.trailRenderer.NearestDist(), app.trailRenderer.OnScreen()); kv("State", b, kVal);
        if (app.state.inDungeon && app.state.inst)
        { std::snprintf(b, sizeof(b), "dungeon '%s'", app.state.inst->Name.c_str()); kv("Active", b, kVal); }
        else if (app.state.zone.Loaded)
        { std::snprintf(b, sizeof(b), "%s  (Lv %d-%d)   step %d / %d", app.state.zone.Name.c_str(),
                        app.state.zone.MinLevel, app.state.zone.MaxLevel, app.state.curStep, (int)app.state.zone.Steps.size()); kv("Active zone", b, kVal); }
        else kv("Active zone", "(none / uncovered map - recommendations shown)", kVal);
        static const char* kMode[] = { "Trail", "HybridTrail", "HybridNearest", "DirectTrail", "DirectNearest" };
        const int pref = std::clamp(app.config.pathType, 0, 3);
        const char *requested = kPathTypeNames[pref];
        const std::string activeRoute = app.state.zone.ActiveRouteLabel.empty()
                                            ? (app.state.zone.ActiveKind.empty() ? std::string(requested) : app.state.zone.ActiveKind)
                                            : app.state.zone.ActiveRouteLabel;
        const std::string routeText = app.state.zone.ActiveRouteFallback
                                          ? (activeRoute + " (fallback from " + requested + ")")
                                          : activeRoute;
        std::snprintf(b, sizeof(b), "%s    %s    off-route: %s", kMode[std::clamp(app.config.routeMode, 0, 4)], routeText.c_str(), app.state.offRoute ? "yes" : "no"); kv("Routing", b, kVal);
        if (app.config.travelOfferDebug && !app.state.travelOffer.debugLine.empty())
            kv("Travel offer", app.state.travelOffer.debugLine.c_str(), IM_COL32(214, 200, 150, 255));
    }
    else
        Gw2Ui::Label("MumbleLink unavailable (not in game yet).", kDim, false, nullptr, fs);
    Gw2Ui::EndCard();

    // ---- Input probe (opt-in): where does a keystroke stop, and what overlaps what? --------------------
    if (app.config.inputProbe)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const ImGuiContext& g = *ImGui::GetCurrentContext();
        const ImU32 kOk  = IM_COL32(150, 210, 150, 255);
        const ImU32 kBad = IM_COL32(226, 140, 120, 255);

        Gw2Ui::SectionHeader("Input probe", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-input");
        SettingsParagraph("Hit Reset, click a search box (this window's, or the dashboard's), type a few letters, then "
                          "read the counters. They follow one keystroke through the chain: 1 and 2 are the raw Windows "
                          "messages our hook is handed (the key press, then the character Windows makes from it), and 3 "
                          "is what actually reached ImGui's text queue. Whichever number stops climbing is where the "
                          "key is being lost.", kDim);
        ImGui::Spacing();

        // Keystroke chain: our hook sees the raw messages; ImGui only ever gets text via its character queue.
        // A wider label column than kv's 150px default -- kv does NOT wrap labels, it overflows them onto the value.
        const float lwP = 200.f;
        const unsigned kd = g_probeKeyDown.load(), wc = g_probeChar.load();
        std::snprintf(b, sizeof(b), "%u", kd);           kv("1. Key press (raw)", b, kd ? kOk : kBad, lwP);
        std::snprintf(b, sizeof(b), "%u", wc);           kv("2. Character (raw)", b, wc ? kOk : kBad, lwP);
        std::snprintf(b, sizeof(b), "%u", g_probeImgui); kv("3. Reached ImGui", b, g_probeImgui ? kOk : kBad, lwP);
        if (g_probeLastAt > 0.0)
        {
            const unsigned c = g_probeLast;
            std::snprintf(b, sizeof(b), "'%c'  (0x%02X)   %.1fs ago",
                          (c >= 32 && c < 127) ? (char)c : '?', c, ImGui::GetTime() - g_probeLastAt);
        }
        else
            std::snprintf(b, sizeof(b), "never");
        kv("Last character", b, g_probeLastAt > 0.0 ? kVal : kBad, lwP);

        ImGui::Spacing();
        // Live modifiers. ImGui's copy/paste needs io.KeyMods to be EXACTLY Ctrl, so a field that will not
        // paste is usually a modifier question -- hold one and read which flag lights up.
        std::snprintf(b, sizeof(b), "%s%s%s%s%s",
                      io.KeyCtrl ? "Ctrl " : "", io.KeyShift ? "Shift " : "", io.KeyAlt ? "Alt " : "",
                      io.KeySuper ? "Super(Cmd/Win) " : "",
                      (!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper) ? "none" : "");
        kv("Modifiers held", b, (io.KeyCtrl || io.KeyShift || io.KeyAlt || io.KeySuper) ? kOk : kDim, lwP);
        std::snprintf(b, sizeof(b), "0x%02X   %s", (unsigned)io.KeyMods,
                      io.KeyMods == ImGuiKeyModFlags_Ctrl ? "== Ctrl (ImGui paste works)"
                                                          : "!= Ctrl (ImGui paste will NOT fire)");
        kv("ImGui KeyMods", b, io.KeyMods == ImGuiKeyModFlags_Ctrl ? kOk : kDim, lwP);

        ImGui::Spacing();
        // Live ImGui state: proves the field really is focused (a blinking caret alone does not).
        kv("Text field focused", io.WantTextInput ? "yes (WantTextInput)" : "no", io.WantTextInput ? kOk : kDim, lwP);
        kv("Keyboard captured", io.WantCaptureKeyboard ? "yes" : "no", io.WantCaptureKeyboard ? kOk : kDim, lwP);
        if (g.ActiveId) std::snprintf(b, sizeof(b), "0x%08X", (unsigned)g.ActiveId);
        else            std::snprintf(b, sizeof(b), "none");
        kv("Active item", b, g.ActiveId ? kVal : kDim, lwP);
        kv("Hovered window", g.HoveredWindow ? g.HoveredWindow->Name : "none", g.HoveredWindow ? kVal : kDim, lwP);

        ImGui::Spacing();
        if (Gw2Ui::Button("Reset counters", 170.f, 28.f))
            ResetInputProbe();
        Gw2Ui::EndCard();

        // ---- Per-box: which text box actually received the characters ----------------------------------
        Gw2Ui::SectionHeader("Text boxes", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-boxes");
        SettingsParagraph("Every Gw2Ui text box on screen, in the order it was submitted this frame. ImGui's "
                          "InputText EMPTIES the character queue when it holds focus, so only the first box to run "
                          "can receive text -- 'queue' is how many characters were still waiting when each box ran. "
                          "The focused box showing queue 0 while an earlier one showed characters is the smoking gun.", kDim);
        ImGui::Spacing();

        if (g_boxesLast.empty())
            Gw2Ui::Label("No text boxes drawn last frame.", kDim, false, nullptr, fs);
        else
        {
            const float lwB = 200.f;
            for (const BoxSample& s : g_boxesLast)
            {
                std::snprintf(b, sizeof(b), "%s   queue %d   id 0x%08X%s",
                              s.active ? "FOCUSED" : "-", s.queue, s.imguiId, s.changed ? "   (edited)" : "");
                kv(s.id.c_str(), b, s.active ? (s.queue > 0 ? kOk : kBad) : kDim, lwB);
            }
            const BoxSample* focused = nullptr;
            for (const BoxSample& s : g_boxesLast)
                if (s.active) { focused = &s; break; }
            ImGui::Spacing();
            if (!focused)
                Gw2Ui::Label("No box holds focus -- click into one first.", kDim, false, nullptr, fs);
            else if (focused->queue > 0 || focused->changed)
            {
                std::snprintf(b, sizeof(b), "OK: '%s' received characters.", focused->id.c_str());
                Gw2Ui::Label(b, kOk, false, nullptr, fs);
            }
            else
            {
                std::snprintf(b, sizeof(b), "'%s' has focus but saw an EMPTY queue.", focused->id.c_str());
                Gw2Ui::Label(b, kBad, false, nullptr, fs);
                SettingsParagraph("If an earlier row above shows queue > 0, that box ate them. If every row shows 0, "
                                  "no characters reached ImGui at all.", kDim);
            }
        }
        Gw2Ui::EndCard();

        // ---- Window overlap: a click lands in the TOPMOST window, so anything covering a panel steals it ----
        Gw2Ui::SectionHeader("Window overlap", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-overlap");
        SettingsParagraph("Our on-screen surfaces and their input rectangles. A rectangle can be larger than what is "
                          "painted, so an overlap steals clicks with nothing visible to explain it. Compared against "
                          "the dashboard panel, since that is the one you interact with by clicking.", kDim);
        ImGui::Spacing();

        const float lwW = 175.f;   // label column: "Dashboard handle" overruns kv's 150px default
        const ProbeWin dash = LookupWin("Dashboard panel", "##tcDashPanel");
        const ProbeWin others[] = {
            LookupWin("HUD bar", "##tcHud"),
            LookupWin("Guide viewer", "##tcguide"),
            LookupWin("Nav arrow", "##tcarrow"),
            LookupWin("Dashboard handle", "##tcDashHandle"),
            LookupWin("Info Panel 1", "##tcInfoPanel0"), LookupWin("Info Panel 2", "##tcInfoPanel1"),
            LookupWin("Info Panel 3", "##tcInfoPanel2"), LookupWin("Info Panel 4", "##tcInfoPanel3"),
            LookupWin("Info Panel 5", "##tcInfoPanel4"),
        };

        if (!dash.shown)
            Gw2Ui::Label("Dashboard panel is closed -- open it to compare.", kDim, false, nullptr, fs);
        else
        {
            std::snprintf(b, sizeof(b), "%.0f, %.0f   %.0f x %.0f",
                          dash.mn.x, dash.mn.y, dash.mx.x - dash.mn.x, dash.mx.y - dash.mn.y);
            kv("Dashboard panel", b, kVal, lwW);
        }
        for (const ProbeWin& w : others)
        {
            if (!w.shown) continue;
            const bool hit = RectsOverlap(dash, w);
            std::snprintf(b, sizeof(b), "%.0f, %.0f   %.0f x %.0f   %s",
                          w.mn.x, w.mn.y, w.mx.x - w.mn.x, w.mx.y - w.mn.y,
                          hit ? "<-- OVERLAPS the dashboard" : "clear");
            kv(w.label, b, hit ? kBad : kDim, lwW);
        }
        Gw2Ui::EndCard();
    }

    // ---- Vista camera probe (does MumbleLink report the cinematic camera pan?) -- only when toggled on ----
    if (MumbleLink && app.config.debugVistaProbe)
    {
        const VistaProbe& vp = app.state.vistaProbe;
        Gw2Ui::SectionHeader("Vista camera probe", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-vista");
        SettingsParagraph("Stand at a vista, click Reset peak, then interact. If the pan is reported, peak jumps far "
                          "past your camera zoom while the avatar stays put (last-2s camera >> avatar).", kDim);
        ImGui::Spacing();
        const double ago = vp.peakAt > 0.0 ? (ImGui::GetTime() - vp.peakAt) : -1.0;
        if (ago >= 0.0) std::snprintf(b, sizeof(b), "cur %.1f m    peak %.1f m  (%.0f s ago)", vp.curDist, vp.peakDist, ago);
        else            std::snprintf(b, sizeof(b), "cur %.1f m    peak %.1f m", vp.curDist, vp.peakDist);
        kv("Cam-avatar", b, kVal);
        std::snprintf(b, sizeof(b), "camera %.1f m   avatar %.1f m", vp.camMoved2s, vp.avaMoved2s); kv("Last 2s moved", b, kVal);
        std::snprintf(b, sizeof(b), "gameplay %s    UITick %u (should be climbing)", vp.gameplayNow ? "yes" : "no", vp.tickNow); kv("Link", b, kVal);
        // The disambiguator: what happened WHILE not-gameplay (the cutscene, if the vista flips it).
        std::snprintf(b, sizeof(b), "not-gameplay seen: %s    peak then %.1f m    link live then: %s",
                      vp.sawNotGameplay ? "yes" : "no", vp.peakDistNoGp, vp.tickAdvNoGp ? "yes" : "no");
        kv("During cutscene", b, vp.sawNotGameplay ? IM_COL32(255, 224, 150, 255) : kVal);
        ImGui::Spacing();
        if (Gw2Ui::Button("Reset peak", 140.f, 28.f))
        {
            VistaProbe& w = app.state.vistaProbe;
            w.peakDist = 0.f; w.peakAt = 0.0;
            w.sawNotGameplay = false; w.peakDistNoGp = 0.f; w.tickAdvNoGp = false;
        }
        Gw2Ui::EndCard();
    }

    // ---- Map assist (only while a pan/assist is active) ----
    if (MumbleLink && app.state.mapAssist.active)
    {
        const MapAssistTarget& ma = app.state.mapAssist;
        const char* probeState = ma.focusProbeWaiting ? "waiting" :
                                 ma.focusProbeDone ? (ma.focusProbeInvoked ? "done" : "unavailable") :
                                 ma.focusProbeRequested ? "pending" : "off";
        Gw2Ui::SectionHeader("Map assist", nullptr, SettingsText::Header);
        Gw2Ui::BeginAccentCard("diag-mapassist", 0.f, IM_COL32(226, 166, 70, 220), IM_COL32(3, 5, 5, 150), IM_COL32(130, 110, 70, 120));
        std::snprintf(b, sizeof(b), "%.1f, %.1f   attempts %d   %s", ma.cx, ma.cy, ma.panAttempts, ma.panStatus.empty() ? "-" : ma.panStatus.c_str()); kv("Target", b, kVal);
        std::snprintf(b, sizeof(b), "%s   wnd %d   send %d   zero %d/%d   wndRet %lld", MapAssistTransportLabel(ma.lastTransport),
                      ma.wndProcDragAttempts, ma.sendInputDragAttempts, ma.consecutiveZeroMoveReadbacks, ma.zeroMoveReadbacks, (long long)ma.lastWndProcResult); kv("Input", b, kVal);
        std::snprintf(b, sizeof(b), "game %s   textbox %s   imgui mouse %s", ma.lastGameFocus ? "yes" : "no", ma.lastTextboxFocus ? "yes" : "no", ma.lastWantCaptureMouse ? "yes" : "no"); kv("Focus", b, kVal);
        std::snprintf(b, sizeof(b), "%s | %s", ma.lastWndProcClass.empty() ? "-" : ma.lastWndProcClass.c_str(), ma.lastWndProcTitle.empty() ? "-" : ma.lastWndProcTitle.c_str()); kv("Window", b, kVal);
        std::snprintf(b, sizeof(b), "%s   before %.0f,%.0f  after %.0f,%.0f  player %.0f,%.0f", probeState,
                      ma.focusProbeBeforeX, ma.focusProbeBeforeY, ma.focusProbeAfterX, ma.focusProbeAfterY, ma.focusProbePlayerX, ma.focusProbePlayerY); kv("Focus probe", b, kVal);
        Gw2Ui::EndCard();
    }

    // ---- Map scale & DPI (base = MapScale / Scaling, auto-adjusts to UI/DPI; custom only fine-tunes it) ----
    if (MumbleLink)
    {
        Gw2Ui::SectionHeader("Map scale & DPI", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-scale");
        kv("Mode", app.config.mapScaleCustom ? "CUSTOM fine-tune" : "AUTO (MapScale / Scaling)", kVal);
        std::snprintf(b, sizeof(b), "Nexus %.4f   uiSize %.3f   MapScale %.4f", NexusLink ? NexusLink->Scaling : 0.f, UiScaleNow(), MumbleLink->Context.MapScale); kv("Scaling", b, kVal);
        std::snprintf(b, sizeof(b), "osScale %.4f   gameDPI %s   nexusDPI %s", app.dpiWatcher.State().osDpiScale, app.dpiWatcher.State().gameDpiOn ? "ON" : "off", app.dpiWatcher.State().nexusDpiOn ? "ON" : "off"); kv("DPI", b, kVal);
        ImGui::Spacing();
        if (Gw2Ui::Button(app.config.mapScaleCustom ? "Use auto" : "Fine-tune (custom)", 180.f))
        {
            app.config.mapScaleCustom = !app.config.mapScaleCustom;
            if (!app.config.mapScaleCustom) { app.config.mapOpenScale = 1.0f; app.config.miniMapScale = 1.0f; }
            app.settingsDirty = true;
        }
        if (app.config.mapScaleCustom)
        {
            std::snprintf(b, sizeof(b), "open x%.4f   mini x%.4f   (1.0000 = pure auto)", app.config.mapOpenScale, app.config.miniMapScale); kv("Fine-tune", b, kVal);
            auto bump = [&](float d) { app.config.mapOpenScale += d; app.settingsDirty = true; };
            ImGui::PushID("opencalib");
            if (Gw2Ui::Button("open -0.01", 86.f))                  bump(-0.01f);
            ImGui::SameLine(); if (Gw2Ui::Button("-0.001", 64.f)) bump(-0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.001", 64.f)) bump(+0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.01", 60.f))  bump(+0.01f);
            ImGui::PopID();
            auto bumpm = [&](float d) { app.config.miniMapScale += d; app.settingsDirty = true; };
            ImGui::PushID("minicalib");
            if (Gw2Ui::Button("mini -0.01", 86.f))                  bumpm(-0.01f);
            ImGui::SameLine(); if (Gw2Ui::Button("-0.001", 64.f)) bumpm(-0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.001", 64.f)) bumpm(+0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.01", 60.f))  bumpm(+0.01f);
            ImGui::PopID();
        }
        Gw2Ui::EndCard();
    }

    // ---- Data bootstrap (self-download) ----
    {
        const ImU32 kAmber = IM_COL32(255, 200, 120, 255);
        const ImU32 kRed   = IM_COL32(255, 140, 120, 255);
        const ImU32 kGreen = IM_COL32(150, 220, 150, 255);
        Gw2Ui::SectionHeader("Data bootstrap", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-bootstrap");
        SettingsParagraph("The DLL self-downloads its data/ (Nexus ships only the DLL). Core (datasets + fonts) is a "
                          "blocking download; the image packs stream in the BACKGROUND and switch in LIVE -- no reload.", kDim);
        ImGui::Spacing();

        std::string src = DataBootstrap::SourceUrl();
        if (src.size() > 58) src = "..." + src.substr(src.size() - 55);
        std::snprintf(b, sizeof(b), "%s%s", src.c_str(), DataBootstrap::SourceOverridden() ? "   (override)" : "");
        kv("Source", b, DataBootstrap::SourceOverridden() ? kAmber : kVal);

        const DataBootstrap::Phase ph = DataBootstrap::CurrentPhase();
        if (ph == DataBootstrap::Phase::Ready)
        { std::snprintf(b, sizeof(b), "ready%s", DataBootstrap::CoreAdopted() ? " (adopted existing data)" : " (downloaded)"); kv("Core", b, kVal); }
        else if (ph == DataBootstrap::Phase::DownloadingCore)
        { std::snprintf(b, sizeof(b), "downloading %d%%", DataBootstrap::CorePct()); kv("Core", b, kAmber); }
        else if (ph == DataBootstrap::Phase::CoreFailed)
        { std::snprintf(b, sizeof(b), "FAILED: %s", DataBootstrap::Error().c_str()); kv("Core", b, kRed); }
        else kv("Core", "checking...", kDim);

        const int need = DataBootstrap::AssetsNeeded(), got = DataBootstrap::AssetsFetched(), fail = DataBootstrap::AssetsFailed();
        if (DataBootstrap::AssetsActive())
        { std::string cur = DataBootstrap::AssetsCurrent(); std::snprintf(b, sizeof(b), "downloading %d/%d   %s", got, need, cur.c_str()); kv("Packs", b, kAmber); }
        else if (!DataBootstrap::AssetsSettled()) kv("Packs", "waiting for core...", kDim);
        else if (need == 0) kv("Packs", "all present (adopted, no download)", kVal);
        else { std::snprintf(b, sizeof(b), "complete: %d fetched live%s", got, fail ? "  (some failed)" : ""); kv("Packs", b, fail ? kAmber : kVal); }

        // Per-pack LIVE status: watch a pack flip missing/downloading -> open (N) mid-session, no reload. Size the
        // label column to the WIDEST pack name (they're long, e.g. cosmetic_renders.pack) so the value never overlaps.
        float packLw = 130.f;
        for (int i = 0; i < ImageCache::kPackCount; ++i)
        {
            char lbl[48]; std::snprintf(lbl, sizeof(lbl), "  %s", ImageCache::PackFileName((ImageCache::Pack)i));
            packLw = std::max(packLw, Gw2Ui::MeasureWidth(lbl, fs) + 16.f);
        }
        for (int i = 0; i < ImageCache::kPackCount; ++i)
        {
            const ImageCache::Pack pp = (ImageCache::Pack)i;
            const char* name = ImageCache::PackFileName(pp);
            char lbl[48]; std::snprintf(lbl, sizeof(lbl), "  %s", name);
            ImU32 col; char st[48];
            if (ImageCache::IsPackOpen(pp))                    { std::snprintf(st, sizeof(st), "open (%d)", ImageCache::PackEntryCount(pp)); col = kGreen; }
            else if (DataBootstrap::AssetsCurrent() == name)   { std::snprintf(st, sizeof(st), "downloading..."); col = kAmber; }
            else if (DataBootstrap::AssetsActive())            { std::snprintf(st, sizeof(st), "queued"); col = kDim; }
            else                                               { std::snprintf(st, sizeof(st), "missing"); col = kAmber; }
            kv(lbl, st, col, packLw);
        }

        // Shared HTTP transport (the reward-fetch "WinHttpOpen failed" symptom lived here): apiAborting should be
        // NO in normal operation; a stuck YES would mean the API scope was left aborted.
        const Api::Http::DiagState hd = Api::Http::Diag();
        std::snprintf(b, sizeof(b), "session %s   api-aborting %s   aborts/resumes %d/%d   in-flight %d",
                      hd.sessionOpen ? "open" : "closed", hd.apiAborting ? "YES" : "no", hd.abortCount, hd.resumeCount, hd.activeReqs);
        kv("Transport", b, hd.apiAborting ? kRed : kVal);
        Gw2Ui::EndCard();
    }

    // ---- Image cache ----
    static bool s_loaded = false;
    static ImageCache::Stats s_tiles, s_icons;
    static int  s_arm = 0;   // two-click confirm: 0 none, 1 clear-tiles armed, 2 free-image-memory armed, 3 clear-image-storage armed
    auto refresh = [&] { s_tiles = ImageCache::GetStats(ImageCache::Ns::Tiles);
                         s_icons = ImageCache::GetStats(ImageCache::Ns::Assets); s_loaded = true; };
    if (!s_loaded) refresh();
    auto mb = [](long long x) { return x / (1024.0 * 1024.0); };

    Gw2Ui::SectionHeader("Image cache", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-cache");
    SettingsParagraph("Maps + icons download once and cache to disk for instant load next session.", kDim);
    ImGui::Spacing();
    std::snprintf(b, sizeof(b), "%d files   (%.1f MB)", s_tiles.files, mb(s_tiles.bytes)); kv("Map tiles", b, kVal);
    std::snprintf(b, sizeof(b), "%d files   (%.1f MB)", s_icons.files, mb(s_icons.bytes)); kv("Icons", b, kVal);
    if (!JournalRewardDiag().empty()) kv("Reward fetch", JournalRewardDiag().c_str(), kVal);

    // Seed-source breakdown -- proves the from-memory pack path is actually in use (per-pack open status is in the
    // Data bootstrap section above). A cached file shadows the pack, so clear the caches to force fresh seeds.
    const ImageCache::PackInfo pk = ImageCache::GetPackInfo();
    std::snprintf(b, sizeof(b), "%d from pack   %d from loose files   %d downloaded", pk.packSeeded, pk.looseSeeded, pk.downloaded);
    kv("Seeded this session", b, kVal);
    // Live addon-owned texture VRAM: total decoded footprint + the evictable (tile/icon/wiki) subset the byte
    // budget bounds. The evictable figure is what auto-eviction keeps under budgetMB.
    const OwnedTex::MemInfo vm = OwnedTex::Mem();
    std::snprintf(b, sizeof(b), "%d textures   %.1f MB   (%.1f / %.0f MB evictable)",
                  vm.textures, mb((long long)vm.bytes), mb((long long)vm.evictableBytes), mb((long long)vm.budgetBytes));
    kv("Image VRAM", b, vm.evictableBytes > vm.budgetBytes ? IM_COL32(255, 200, 120, 255) : kVal);
    ImGui::Spacing();
    if (Gw2Ui::Button(s_arm == 1 ? "Confirm: clear tiles" : "Clear map tiles", 200.f, 30.f))
    { if (s_arm == 1) { ImageCache::Clear(ImageCache::Ns::Tiles); refresh(); s_arm = 0; } else s_arm = 1; }
    ImGui::SameLine();
    if (Gw2Ui::Button("Refresh", 100.f, 30.f)) { refresh(); s_arm = 0; }
    // Downloaded images (icons + wiki art): FREE the decoded GPU memory (the disk cache stays, so they re-load
    // instantly) vs CLEAR the on-disk storage (deletes the downloaded files -> re-downloads on next view).
    if (Gw2Ui::Button(s_arm == 2 ? "Confirm: free memory" : "Free image memory", 200.f, 30.f))
    { if (s_arm == 2) { ImageCache::FreeVram(ImageCache::Ns::Assets); s_arm = 0; } else s_arm = 2; }
    ImGui::SameLine();
    if (Gw2Ui::Button(s_arm == 3 ? "Confirm: clear storage" : "Clear image storage", 210.f, 30.f))
    { if (s_arm == 3) { ImageCache::Clear(ImageCache::Ns::Assets); refresh(); s_arm = 0; } else s_arm = 3; }
    if (s_arm == 2)
        SettingsParagraph("Frees the decoded images from video memory; the disk cache stays, so they re-load instantly. Click again to confirm.",
                          IM_COL32(255, 200, 120, 255));
    else if (s_arm)
        SettingsParagraph("Clearing deletes the on-disk files (re-downloads on next view, non-destructive). Click the same button again to confirm.",
                          IM_COL32(255, 200, 120, 255));
    Gw2Ui::EndCard();

    // ---- Notifications (test) ----
    Gw2Ui::SectionHeader("Notifications (test)", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-notify");
    SettingsParagraph("Fire a sample notification of each type to preview the toast + log styling and placement. "
                      "Disabled types are greyed out -- toggle them in Options > Notifications.", kDim);
    if (!app.config.notifyEnabled)
        SettingsParagraph("Notifications are OFF (master toggle). Enable in Options > Notifications to see them.",
                          IM_COL32(255, 200, 120, 255));
    ImGui::Spacing();

    struct NotifySample { Notify::Kind kind; const char* title; const char* body; };
    static const NotifySample kSamples[] = {
        { Notify::Kind::Zone,   "Now guiding: Queensdale", "Levels 1-15" },
        { Notify::Kind::Travel, "Waypoint copied",         "Paste in chat (or click) to teleport." },
        { Notify::Kind::Action, "Objective complete",      "Heart: Help Farmer Eda with her crops." },
        { Notify::Kind::Event,  "Event nearby",            "Defend the tavern from the Skritt raiders." },
        { Notify::Kind::Info,   "Tyrian Codex",            "This is a test notification." },
    };
    for (int i = 0; i < (int)(sizeof(kSamples) / sizeof(kSamples[0])); ++i)
    {
        const NotifySample& s = kSamples[i];
        const bool kindOn = app.config.notifyEnabled && app.config.notifyKind[(int)s.kind];
        char tip[96];
        std::snprintf(tip, sizeof(tip), kindOn ? "Fire a sample %s notification" : "%s notifications are disabled",
                      Notify::Meta(s.kind).name);
        if (i % 3 != 0) ImGui::SameLine();
        if (Gw2Ui::ActionButton(Notify::Meta(s.kind).name, 110.f, 28.f, Gw2Ui::ActionButtonVariant::Normal, tip, !kindOn))
            Notify::Push(s.kind, s.title, s.body);
    }
    ImGui::Spacing();
    if (Gw2Ui::ActionButton("Test all types", 160.f, 28.f, Gw2Ui::ActionButtonVariant::Primary,
                            "Fire one of every enabled type", !app.config.notifyEnabled))
        for (const NotifySample& s : kSamples) Notify::Push(s.kind, s.title, s.body);
    Gw2Ui::EndCard();

    // ---- Actions ----
    Gw2Ui::SectionHeader("Actions", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-actions");
    if (Gw2Ui::Button("Reset progress (this zone)", 230.f))
    {
        std::vector<std::string> ids; ids.reserve(app.state.zone.Steps.size());
        for (const Step& s : app.state.zone.Steps) ids.push_back(s.StepId);
        app.progress.ResetMany(app.state.currentChar, ids);
        app.state.manualTarget.clear(); app.follower.Reset(); app.gpsArrow.ResetEase();
    }
    Gw2Ui::EndCard();

    // ---- Character data ----
    Gw2Ui::SectionHeader("Character data", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-chardata");
    SettingsParagraph("Per-character data the addon has stored (progress, profiles, sessions, levels, favorites). "
                      "Renamed characters merge automatically (with an API key); use this to clear characters you've "
                      "deleted. Account-level totals are kept.", kDim);
    ImGui::Spacing();
    {
        const std::vector<std::string> names = CharData::AllCharacterNames(app);
        const AccountData::Model& acc = AccountData::Get();
        const bool haveRoster = acc.haveChars && app.api.HasPermission(Api::TokenPermission::Characters);
        auto inAccount = [&](const std::string& n) {
            for (const AccountData::AcctChar& c : acc.characters) if (c.name == n) return true;
            return false;
        };

        static std::string s_armPurge;     // name armed for delete (two-click confirm)
        static int         s_armClear = 0; // "clear all not in account" two-click confirm

        if (names.empty())
            Gw2Ui::Label("No stored character data.", kDim, false, nullptr, fs);

        for (const std::string& n : names)
        {
            const bool active = (n == app.state.currentChar);
            const bool stale  = haveRoster && !inAccount(n);
            const int  objs   = (int)app.progress.CompletedIds(n).size();
            const int  sess   = (int)app.sessionHistory.Sessions(n).size();
            const int  lvl    = app.characterLevel.CachedLevel(n);

            char foot[160];
            if (lvl > 0) std::snprintf(foot, sizeof(foot), "lv %d    %d obj    %d sessions", lvl, objs, sess);
            else         std::snprintf(foot, sizeof(foot), "%d obj    %d sessions", objs, sess);

            ImGui::PushID(n.c_str());
            const bool armed = (s_armPurge == n);
            const char* tip = active ? "The active character can't be purged"
                            : (armed ? "Click again to permanently delete this character's stored data"
                                     : "Delete this character's stored data");
            if (Gw2Ui::ActionButton(armed ? "Confirm delete" : "Delete", 130.f, 24.f,
                                    armed ? Gw2Ui::ActionButtonVariant::Danger : Gw2Ui::ActionButtonVariant::Normal,
                                    tip, active))
            {
                if (armed) { CharData::PurgeCharacter(app, n, false); s_armPurge.clear(); }
                else       { s_armPurge = n; }
            }
            ImGui::SameLine();
            const ImU32 col = stale ? IM_COL32(255, 200, 120, 255) : kVal;
            const char* status = !haveRoster ? "" : (active ? "  (active)" : (stale ? "  (not in account)" : "  (in account)"));
            std::snprintf(b, sizeof(b), "%s%s     %s", n.c_str(), status, foot);
            Gw2Ui::Label(b, col, false, nullptr, fs);
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (haveRoster)
        {
            int staleCount = 0;
            for (const std::string& n : names) if (n != app.state.currentChar && !inAccount(n)) ++staleCount;
            char lbl[96];
            if (s_armClear) std::snprintf(lbl, sizeof(lbl), "Confirm: clear %d character(s)", staleCount);
            else            std::snprintf(lbl, sizeof(lbl), "Clear all not in my account (%d)", staleCount);
            if (Gw2Ui::ActionButton(lbl, 300.f, 28.f,
                                    s_armClear ? Gw2Ui::ActionButtonVariant::Danger : Gw2Ui::ActionButtonVariant::Primary,
                                    "Delete data for every character no longer on your account (keeps account-level totals)",
                                    staleCount == 0))
            {
                if (s_armClear)
                {
                    for (const std::string& n : names)
                        if (n != app.state.currentChar && !inAccount(n)) CharData::PurgeCharacter(app, n, false);
                    s_armClear = 0;
                }
                else { s_armClear = 1; }
            }
        }
        else
            SettingsParagraph("Add an API key (with the 'characters' scope) to flag characters no longer in your account.",
                              kDim);
    }
    Gw2Ui::EndCard();
}
