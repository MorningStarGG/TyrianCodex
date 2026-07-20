#include "world/MapRenderer.h"
#include "util/Draw.h"   // TrailColorFor, DrawPersonalPin
#include "util/Coords.h" // ClampToRect (travel entrance marker)
#include "util/Textures.h"
#include "world/CategoryMarkers.h"
#include "world/FishMarkers.h"
#include "guide/StepStatus.h"
#include "model/ObjectiveTypes.h"
#include "render/glyphs/Glyphs.h" // Render::DrawGlyph (map above/below floor arrows)
#include "Shared.h"
#include <imgui.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

ImVec2 MapRenderer::ContinentToMapScreen(float ccx, float ccy) const
{
    const MapProj &m = mapProj_;
    float rx = (ccx - m.cenX) / m.scale, ry = (ccy - m.cenY) / m.scale;
    if (m.rotate)
    {
        const float tx = rx * m.cr - ry * m.sr;
        ry = rx * m.sr + ry * m.cr;
        rx = tx;
    } // CreateRotationZ
    return ImVec2(m.bMin.x + m.bSize.x * 0.5f + rx, m.bMin.y + m.bSize.y * 0.5f + ry);
}

// The exact inverse of ContinentToMapScreen (screen -> continent), for placing a target where a click landed.
// Un-rotates by the compass angle (transpose of the forward rotation), then un-scales about MapCenter.
void MapRenderer::MapScreenToContinent(ImVec2 s, float &ccx, float &ccy) const
{
    const MapProj &m = mapProj_;
    float rx = s.x - (m.bMin.x + m.bSize.x * 0.5f), ry = s.y - (m.bMin.y + m.bSize.y * 0.5f);
    if (m.rotate)
    {
        const float tx = rx * m.cr + ry * m.sr;
        ry = -rx * m.sr + ry * m.cr;
        rx = tx;
    } // R^-1 = R^T
    ccx = m.cenX + rx * m.scale;
    ccy = m.cenY + ry * m.scale;
}

namespace
{
    constexpr float kAssistVisibleMargin = 36.f;
    constexpr float kAssistCenterTolerance = 32.f;
    constexpr float kAssistCloseEnoughTolerance = 52.f;
    constexpr float kAssistPanSafety = 0.70f; // aim ~70% of remaining error per drag -> undershoot, monotonic, no oscillation
    constexpr float kAssistGainSmoothing = 0.35f;
    constexpr float kAssistMinGain = 0.04f; // low floor so gain can compensate GW2's high map-drag sensitivity (~8x observed)
    constexpr float kAssistMaxGain = 1.20f;
    constexpr float kAssistFineGainScale = 0.82f;
    constexpr float kAssistFineMinGain = 0.04f;
    constexpr float kAssistProbeDrag = 44.f; // small first-attempt probe (px) to measure sensitivity before scaling (keep small so it can't fling)
    constexpr int kAssistMaxFineAttempts = 2;
    constexpr int kAssistMaxPanAttempts = 12;
    constexpr double kAssistPanReadbackSeconds = 0.22;
    constexpr double kAssistCenterConfirmSeconds = 0.18;
    constexpr double kAssistFocusReadbackSeconds = 0.18;
    constexpr double kAssistWaypointClickDelaySeconds = 0.16;
    constexpr float kAssistZeroMoveReadbackPx = 1.0f;

    // Synthetic-drag safety net: the left mouse button must NEVER be left logically down across frames.
    // The pan drag is issued as ONE atomic SendInput batch (move+down+moves+up together) so it cannot leak;
    // these statics + the release at the top of UpdateAssist are belt-and-suspenders for any partial send.
    static bool s_dragButtonDown = false;
    static POINT s_dragRestoreCursor{};
    static bool s_dragHaveRestore = false;

    struct DragSendResult
    {
        bool sent = false;
        MapAssistTransport transport = MapAssistTransport::None;
        intptr_t wndProcResult = 0;
        const char *status = "not sent";
    };

    static ImVec2 ClampPoint(ImVec2 p, ImVec2 min, ImVec2 max)
    {
        return ImVec2(std::clamp(p.x, min.x, max.x), std::clamp(p.y, min.y, max.y));
    }

    static bool PointInRect(ImVec2 p, ImVec2 min, ImVec2 max)
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }

    static float Length(ImVec2 v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    static bool MapCenterNearZone(const Zone &zone, float cx, float cy)
    {
        if (!zone.Loaded || !zone.HasRects)
            return true;
        const float minX = std::min(zone.ContRect[0], zone.ContRect[2]);
        const float maxX = std::max(zone.ContRect[0], zone.ContRect[2]);
        const float minY = std::min(zone.ContRect[1], zone.ContRect[3]);
        const float maxY = std::max(zone.ContRect[1], zone.ContRect[3]);
        const float padX = std::max(1200.f, (maxX - minX) * 0.45f);
        const float padY = std::max(1200.f, (maxY - minY) * 0.45f);
        return cx >= minX - padX && cx <= maxX + padX && cy >= minY - padY && cy <= maxY + padY;
    }

    static void UpdateMeasuredPanGain(float requested, float actual, float &gain, bool &calibrated)
    {
        if (std::fabs(requested) < 24.f || std::fabs(actual) < 2.f || requested * actual <= 0.f)
            return;
        const float effectiveness = std::fabs(actual / requested);
        if (effectiveness < 0.04f || effectiveness > 30.f)
            return; // accept GW2's high drag sensitivity so the gain can damp it

        const float measuredGain = std::clamp(kAssistPanSafety / effectiveness, kAssistMinGain, kAssistMaxGain);
        // SNAP to the first valid measurement (full weight) so the very next drag is correctly sized -- a slow
        // EMA from the default left the early drags 4-6x too large, which is what flung the map up and down.
        // EMA only smooths refinements after that first calibration.
        const float w = calibrated ? kAssistGainSmoothing : 1.0f;
        gain = std::clamp(gain * (1.f - w) + measuredGain * w, kAssistMinGain, kAssistMaxGain);
        calibrated = true;
    }

    static LONG NormalizeAbsolute(int value, int origin, int size)
    {
        if (size <= 1)
            return 0;
        return (LONG)std::lround((double)(value - origin) * 65535.0 / (double)(size - 1));
    }

    static INPUT MouseMoveAbsolute(POINT p)
    {
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = NormalizeAbsolute(p.x, vx, vw);
        input.mi.dy = NormalizeAbsolute(p.y, vy, vh);
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        return input;
    }

    static INPUT MouseButton(DWORD flags)
    {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;
        return input;
    }

    static bool ClientPointToForegroundScreen(ImVec2 client, POINT &screen)
    {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd)
            return false;
        POINT p{(LONG)std::lround(client.x), (LONG)std::lround(client.y)};
        if (!ClientToScreen(hwnd, &p))
            return false;
        screen = p;
        return true;
    }

    // Force-release a leaked synthetic left-button-down (and restore the saved cursor). No-op normally,
    // since the drag batch always pairs its own up; called at UpdateAssist entry as a safety net.
    static void ForceReleaseDragButton()
    {
        if (!s_dragButtonDown)
            return;
        INPUT up = MouseButton(MOUSEEVENTF_LEFTUP);
        SendInput(1, &up, sizeof(INPUT));
        s_dragButtonDown = false;
        if (s_dragHaveRestore)
        {
            SetCursorPos(s_dragRestoreCursor.x, s_dragRestoreCursor.y);
            s_dragHaveRestore = false;
        }
    }

    static const char *TransportLabel(MapAssistTransport transport)
    {
        switch (transport)
        {
        case MapAssistTransport::WndProc:
            return "wndproc";
        case MapAssistTransport::SendInput:
            return "sendinput";
        case MapAssistTransport::Blocked:
            return "blocked";
        default:
            return "none";
        }
    }

    static void CaptureWndProcDiagnostics(MapAssistTarget &target)
    {
        HWND hwnd = GetForegroundWindow();
        target.lastWndProcHwnd = (uintptr_t)hwnd;
        target.lastGameFocus = GameHasFocus();
        target.lastTextboxFocus = TextboxHasFocus();
        target.lastWantCaptureMouse = ImGui::GetIO().WantCaptureMouse;
        target.lastWndProcClass.clear();
        target.lastWndProcTitle.clear();
        if (!hwnd)
            return;

        char cls[80]{};
        if (GetClassNameA(hwnd, cls, (int)sizeof(cls)))
            target.lastWndProcClass = cls;

        char title[96]{};
        if (GetWindowTextA(hwnd, title, (int)sizeof(title)))
            target.lastWndProcTitle = title;
    }

    // GW2's world map pans only from REAL cursor motion (raw input), not from injected WndProc mouse
    // messages -- those return "handled" but never move the canvas (the root cause of the regression after
    // the SendInput drag fallback was removed). So the drag is a guarded SendInput: save the cursor, issue
    // move->down->stepped-moves->up as ONE atomic batch over the map canvas, then restore the cursor.
    // Atomic = the button is never left down across frames. Gated on game focus, no textbox, and the user
    // not physically holding a mouse button (we yield to real input).
    static constexpr int kDragSteps = 6;
    static bool SendMapDragViaSendInput(ImVec2 fromClient, ImVec2 toClient)
    {
        if (!GameHasFocus() || TextboxHasFocus())
            return false;
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
            return false; // user holding LMB -> yield
        if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
            return false; // user holding RMB -> yield

        POINT from{}, to{}, original{};
        if (!ClientPointToForegroundScreen(fromClient, from))
            return false;
        if (!ClientPointToForegroundScreen(toClient, to))
            return false;
        if (!GetCursorPos(&original))
            return false;
        s_dragRestoreCursor = original;
        s_dragHaveRestore = true;

        INPUT seq[2 + kDragSteps + 1]{};
        int n = 0;
        seq[n++] = MouseMoveAbsolute(from);
        seq[n++] = MouseButton(MOUSEEVENTF_LEFTDOWN);
        s_dragButtonDown = true;
        for (int i = 1; i <= kDragSteps; ++i)
        {
            const float t = (float)i / (float)kDragSteps;
            POINT p{(LONG)std::lround(from.x + (to.x - from.x) * t),
                    (LONG)std::lround(from.y + (to.y - from.y) * t)};
            seq[n++] = MouseMoveAbsolute(p);
        }
        seq[n++] = MouseButton(MOUSEEVENTF_LEFTUP);

        const UINT sent = SendInput((UINT)n, seq, sizeof(INPUT));
        SetCursorPos(original.x, original.y);
        s_dragHaveRestore = false;
        if (sent == (UINT)n)
        {
            s_dragButtonDown = false;
            return true;
        }
        return false; // partial inject: keep s_dragButtonDown set so UpdateAssist's entry net releases it
    }

    static DragSendResult SendMapDrag(ImVec2 fromClient, ImVec2 toClient)
    {
        if (!GameHasFocus() || TextboxHasFocus())
            return {false, MapAssistTransport::Blocked, 0, "input blocked focus/text"};
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
            return {false, MapAssistTransport::Blocked, 0, "input blocked mouse"};

        if (SendMapDragViaSendInput(fromClient, toClient))
            return {true, MapAssistTransport::SendInput, 0, "drag sent sendinput"};
        return {false, MapAssistTransport::Blocked, 0, "input blocked sendinput"};
    }

    static bool SendMapClickViaSendInput(ImVec2 client)
    {
        if (!GameHasFocus() || TextboxHasFocus())
            return false;
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
            return false;
        if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
            return false;

        POINT target{}, original{};
        if (!ClientPointToForegroundScreen(client, target))
            return false;
        if (!GetCursorPos(&original))
            return false;

        INPUT input[3]{};
        int n = 0;
        input[n++] = MouseMoveAbsolute(target);
        input[n++] = MouseButton(MOUSEEVENTF_LEFTDOWN);
        input[n++] = MouseButton(MOUSEEVENTF_LEFTUP);

        const UINT sent = SendInput((UINT)n, input, sizeof(INPUT));
        SetCursorPos(original.x, original.y);
        return sent == (UINT)n;
    }

    // The waypoint click that opens GW2's teleport confirmation. Real-cursor SendInput only -- injected
    // WndProc clicks are ignored by the map just like the drag. The caller (TryClickWaypointAfterPan) gates
    // this to fire exactly once, for a confirmed waypoint, then stops all further synthetic input.
    static bool SendMapClick(ImVec2 client)
    {
        return SendMapClickViaSendInput(client);
    }

    static bool InvokeMapFocusPlayer()
    {
        if (!APIDefs || !APIDefs->GameBinds_InvokeAsync)
            return false;
        if (APIDefs->GameBinds_IsBound && !APIDefs->GameBinds_IsBound(GB_MapFocusPlayer))
            return false;
        APIDefs->GameBinds_InvokeAsync(GB_MapFocusPlayer, 90);
        return true;
    }

    static void DrawAssistMarker(ImDrawList *dl, const MapProj &m, ImVec2 p, const MapAssistTarget &target)
    {
        const ImVec2 min(m.bMin.x + kAssistVisibleMargin, m.bMin.y + kAssistVisibleMargin);
        const ImVec2 max(m.bMin.x + m.bSize.x - kAssistVisibleMargin, m.bMin.y + m.bSize.y - kAssistVisibleMargin);
        const bool visible = PointInRect(p, min, max);
        const ImVec2 c = visible ? p : ClampPoint(p, min, max);

        const float now = (float)ImGui::GetTime();
        const float ph = std::fmod(now, 1.25f) / 1.25f;
        const float pulse = 1.f - ph;
        const ImU32 gold = IM_COL32(255, 198, 72, 255);
        const ImU32 soft = IM_COL32(255, 198, 72, (int)(175.f * pulse));

        if (visible)
        {
            const float r = 13.f;
            if (!target.waypointLink.empty())
            {
                if (void *wpTex = Tex::GetTextureFromAssetId(157353u))
                    dl->AddImage((ImTextureID)wpTex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
            }
            dl->AddCircle(c, r + 4.f, gold, 28, 2.8f);
            dl->AddCircle(c, r + 8.f + ph * 18.f, soft, 32, 2.2f);
            dl->AddLine(ImVec2(c.x - 18.f, c.y), ImVec2(c.x + 18.f, c.y), gold, 1.5f);
            dl->AddLine(ImVec2(c.x, c.y - 18.f), ImVec2(c.x, c.y + 18.f), gold, 1.5f);
        }
        else
        {
            const ImVec2 center(m.bMin.x + m.bSize.x * 0.5f, m.bMin.y + m.bSize.y * 0.5f);
            ImVec2 dir(p.x - center.x, p.y - center.y);
            const float len = std::max(Length(dir), 1.f);
            dir.x /= len;
            dir.y /= len;
            const ImVec2 perp(-dir.y, dir.x);
            const ImVec2 tip(c.x + dir.x * 9.f, c.y + dir.y * 9.f);
            const ImVec2 base(c.x - dir.x * 10.f, c.y - dir.y * 10.f);
            dl->AddTriangleFilled(tip, ImVec2(base.x + perp.x * 8.f, base.y + perp.y * 8.f),
                                  ImVec2(base.x - perp.x * 8.f, base.y - perp.y * 8.f), gold);
            dl->AddCircle(c, 19.f + ph * 9.f, soft, 28, 2.f);
        }

        if (!target.label.empty())
        {
            const char *text = target.label.c_str();
            const ImVec2 textSz = ImGui::CalcTextSize(text);
            ImVec2 labelPos(c.x + 16.f, c.y - textSz.y * 0.5f);
            labelPos = ClampPoint(labelPos, ImVec2(m.bMin.x + 12.f, m.bMin.y + 12.f),
                                  ImVec2(m.bMin.x + m.bSize.x - textSz.x - 18.f, m.bMin.y + m.bSize.y - textSz.y - 12.f));
            const ImVec2 pad(7.f, 4.f);
            dl->AddRectFilled(ImVec2(labelPos.x - pad.x, labelPos.y - pad.y),
                              ImVec2(labelPos.x + textSz.x + pad.x, labelPos.y + textSz.y + pad.y),
                              IM_COL32(6, 7, 6, 205), 3.f);
            dl->AddRect(ImVec2(labelPos.x - pad.x, labelPos.y - pad.y),
                        ImVec2(labelPos.x + textSz.x + pad.x, labelPos.y + textSz.y + pad.y),
                        IM_COL32(190, 145, 68, 180), 3.f);
            dl->AddText(labelPos, IM_COL32(255, 226, 160, 255), text);
        }
    }

    static const char *RouteModeLabel(int mode)
    {
        switch (std::clamp(mode, 0, 4))
        {
        case 0:
            return "Trail";
        case 1:
            return "HybridTrail";
        case 2:
            return "HybridNearest";
        case 3:
            return "DirectTrail";
        case 4:
            return "DirectNearest";
        default:
            return "Unknown";
        }
    }

    static const char *FocusProbeState(const MapAssistTarget &target)
    {
        if (target.focusProbeWaiting)
            return "waiting";
        if (target.focusProbeDone)
            return target.focusProbeInvoked ? "done" : "unavailable";
        if (target.focusProbeRequested)
            return "pending";
        return "off";
    }

    static void DrawMapDiagnostics(ImDrawList *dl, const MapProj &m, const Config &cfg,
                                   const GuideState &st, const Mumble::Context &ctx)
    {
        const ImVec2 p(m.bMin.x + 18.f, m.bMin.y + 56.f);
        const ImVec2 pad(8.f, 6.f);
        char lines[16][192]{};
        int n = 0;

        if (cfg.debugRoute)
        {
            const int pref = std::clamp(cfg.pathType, 0, 3);
            const char *requested = kPathTypeNames[pref];
            const std::string activeRoute = st.zone.ActiveRouteLabel.empty()
                                                ? (st.zone.ActiveKind.empty() ? std::string(requested) : st.zone.ActiveKind)
                                                : st.zone.ActiveRouteLabel;
            const std::string routeText = st.zone.ActiveRouteFallback
                                              ? (activeRoute + " (fallback from " + requested + ")")
                                              : activeRoute;
            std::snprintf(lines[n++], sizeof(lines[0]),
                          "routing: %s  route:%s%s  step %d  off-route:%s",
                          RouteModeLabel(cfg.routeMode), routeText.c_str(), st.zone.ActiveComplete ? "" : " (partial)",
                          st.curStep, st.offRoute ? "yes" : "no");
        }

        if (cfg.mapAssistDebug)
        {
            std::snprintf(lines[n++], sizeof(lines[0]),
                          "map assist: center %.1f, %.1f  scale %.3f",
                          ctx.MapCenterX, ctx.MapCenterY, ctx.MapScale);
            if (st.mapAssist.active)
            {
                const MapAssistTarget &target = st.mapAssist;
                const float errX = target.cx - ctx.MapCenterX;
                const float errY = target.cy - ctx.MapCenterY;
                const float pxX = errX / m.scale;
                const float pxY = errY / m.scale;
                const float pxLen = Length(ImVec2(pxX, pxY));
                const ImVec2 center(m.bMin.x + m.bSize.x * 0.5f, m.bMin.y + m.bSize.y * 0.5f);
                const char *status = target.panStatus.empty() ? "-" : target.panStatus.c_str();
                if (target.panDone && pxLen > kAssistCloseEnoughTolerance)
                    status = "drift after centered";
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "target %.1f, %.1f  attempts %d  status:%s",
                              target.cx, target.cy, target.panAttempts, status);
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "error c %.1f, %.1f  px %.1f, %.1f  screen %.0f, %.0f",
                              errX, errY, pxX, pxY, center.x + pxX, center.y + pxY);
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "drag req %.1f, %.1f  moved %.1f, %.1f  gain %.2f, %.2f  fine %d/%d",
                              target.lastRequestedX, target.lastRequestedY,
                              target.lastMovedX, target.lastMovedY,
                              target.panGainX, target.panGainY,
                              target.fineAttempts, kAssistMaxFineAttempts);
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "transport %s  wnd:%d send:%d zero:%d/%d wndRet:%lld",
                              TransportLabel(target.lastTransport),
                              target.wndProcDragAttempts, target.sendInputDragAttempts,
                              target.consecutiveZeroMoveReadbacks, target.zeroMoveReadbacks,
                              (long long)target.lastWndProcResult);
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "focus game:%s text:%s imgui:%s hwnd:%p",
                              target.lastGameFocus ? "yes" : "no",
                              target.lastTextboxFocus ? "yes" : "no",
                              target.lastWantCaptureMouse ? "yes" : "no",
                              (void *)target.lastWndProcHwnd);
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "window %s | %s",
                              target.lastWndProcClass.empty() ? "-" : target.lastWndProcClass.c_str(),
                              target.lastWndProcTitle.empty() ? "-" : target.lastWndProcTitle.c_str());
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "flags requested:%s done:%s waiting:%s  probe:%s",
                              target.panRequested ? "yes" : "no",
                              target.panDone ? "yes" : "no",
                              (target.waitingForReadback || target.centerConfirmWaiting) ? "yes" : "no",
                              FocusProbeState(target));
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "click waypoint:%s known:%s requested:%s done:%s status:%s",
                              target.waypointTarget ? "yes" : "no",
                              target.waypointKnown ? "yes" : "no",
                              target.waypointClickRequested ? "yes" : "no",
                              target.waypointClickDone ? "yes" : "no",
                              target.waypointClickStatus.empty() ? "-" : target.waypointClickStatus.c_str());
                std::snprintf(lines[n++], sizeof(lines[0]),
                              "probe before %.1f, %.1f  after %.1f, %.1f  player %.1f, %.1f",
                              target.focusProbeBeforeX, target.focusProbeBeforeY,
                              target.focusProbeAfterX, target.focusProbeAfterY,
                              target.focusProbePlayerX, target.focusProbePlayerY);
            }
            else
            {
                std::snprintf(lines[n++], sizeof(lines[0]), "target: none - click Show on Map to start a probe");
            }
        }

        if (n <= 0)
            return;

        float w = 0.f;
        float h = 0.f;
        for (int i = 0; i < n; ++i)
        {
            const ImVec2 sz = ImGui::CalcTextSize(lines[i]);
            w = std::max(w, sz.x);
            h += sz.y + (i + 1 < n ? 4.f : 0.f);
        }

        dl->AddRectFilled(ImVec2(p.x - pad.x, p.y - pad.y),
                          ImVec2(p.x + w + pad.x, p.y + h + pad.y),
                          IM_COL32(6, 7, 6, 215), 3.f);
        dl->AddRect(ImVec2(p.x - pad.x, p.y - pad.y),
                    ImVec2(p.x + w + pad.x, p.y + h + pad.y),
                    IM_COL32(190, 145, 68, 180), 3.f);

        float y = p.y;
        for (int i = 0; i < n; ++i)
        {
            const ImU32 col = (i == 0) ? IM_COL32(255, 226, 160, 255) : IM_COL32(210, 230, 255, 255);
            dl->AddText(ImVec2(p.x, y), col, lines[i]);
            y += ImGui::CalcTextSize(lines[i]).y + 4.f;
        }
    }

    static bool TryClickWaypointAfterPan(const Config &cfg, MapAssistTarget &target,
                                         const MapProj &m, ImVec2 screenTarget, double now)
    {
        if (target.mode != MapAssistMode::Travel)
        {
            target.waypointClickRequested = false;
            if (target.waypointClickStatus.empty() || target.waypointClickStatus == "armed")
                target.waypointClickStatus = "show only";
            return false;
        }

        if (!target.waypointTarget)
        {
            if (target.waypointClickStatus.empty())
                target.waypointClickStatus = "not waypoint";
            return false;
        }

        if (!target.waypointKnown)
        {
            target.waypointClickRequested = false;
            if (!target.waypointClickDone)
                target.waypointClickStatus = "locked";
            return false;
        }

        if (!cfg.mapAssistClickWaypoint)
        {
            target.waypointClickRequested = false;
            if (!target.waypointClickDone)
                target.waypointClickStatus = "disabled";
            return false;
        }

        if (!target.panDone || target.waypointClickDone || !target.waypointClickRequested)
            return false;

        const ImVec2 min(m.bMin.x + kAssistVisibleMargin, m.bMin.y + kAssistVisibleMargin);
        const ImVec2 max(m.bMin.x + m.bSize.x - kAssistVisibleMargin, m.bMin.y + m.bSize.y - kAssistVisibleMargin);
        if (!PointInRect(screenTarget, min, max))
        {
            target.waypointClickRequested = false;
            target.waypointClickStatus = "offscreen";
            return false;
        }

        if (target.waypointClickTime <= 0.0)
        {
            target.waypointClickTime = now + kAssistWaypointClickDelaySeconds;
            target.waypointClickStatus = "delay";
            return false;
        }

        if (now < target.waypointClickTime)
        {
            target.waypointClickStatus = "delay";
            return false;
        }

        if (SendMapClick(screenTarget))
        {
            target.waypointClickRequested = false;
            target.waypointClickDone = true;
            target.waypointClickStatus = "sent";
            target.panStatus = "waypoint clicked";
            return true;
        }

        target.waypointClickStatus = "input blocked";
        return false;
    }
}

// Draw the active route as a flat polyline on the in-game FULLSCREEN MAP (when open) or the MINIMAP /
// COMPASS corner (otherwise). MapTrail / Pathing FlatMap: continent points project as
// screen = (cp - MapCenter)/scale + center, where scale = MapScale / (DPI-corrected UI scaling) (see the
// scaling/base/scale math in Draw() below), with the compass rotation applied on the minimap;
// the compass region geometry mirrors Pathing's UpdateBounds. Drawn on the background list (over the
// game's map, under our windows). Gated by the master toggle + the per-surface "show on map/minimap".

// Objective type -> the per-type circle/marker index (parallel to Config::objCircleKind / kindEnabled).
static int CircleKindIndex(const std::string &t)
{
    if (t == "waypoint")
        return 0;
    if (t == "poi")
        return 1;
    if (t == "vista")
        return 2;
    if (t == "hero")
        return 3;
    if (t == "heart")
        return 4;
    return -1;
}

void MapRenderer::Draw(const Config &cfg, const GuideState &st, Travel::Controller &travel,
                       Follow::TrailFollower &follower, ProgressStore &progress, const DpiState &dpi)
{
    const bool live = GameIsLive(); // update the freshness tracker every frame (before any early-out below)
    if (!cfg.showGuide || !MumbleLink)
        return;
    // Suppress ALL map overlays (trail / pins / circles) while the world isn't ticking -- a loading screen
    // after a teleport leaves a STALE map id + open-map flag, so without this the map trail bleeds through it.
    if (!live)
        return;
    const auto &ctx = MumbleLink->Context;
    const bool mapOpen = IsMapOpen();
    // The compass only exists during active gameplay; the open map can show while IsGameplay is false.
    if (!mapOpen && (!NexusLink || !NexusLink->IsGameplay))
        return;

    const float W = ImGui::GetIO().DisplaySize.x, H = ImGui::GetIO().DisplaySize.y;
    // CONTINENT -> ACTUAL-PIXEL conversion = MapScale / (the GAME's UI scale). NexusLink->Scaling is the logical->
    // actual ratio but it tracks NEXUS's DPI setting, while the map renders at the GAME's DPI. So we correct it:
    // divide out Nexus's DPI factor and multiply in the game's (both read from the settings files, * osDPI). When the
    // two DPI settings match (or are both off) this is just NexusLink->Scaling - so it stays correct for everyone.
    const float nexusScale = (NexusLink && NexusLink->Scaling > 0.01f) ? NexusLink->Scaling : UiScaleNow();
    const float scaling = nexusScale * (dpi.gameDpiOn ? dpi.osDpiScale : 1.f) / (dpi.nexusDpiOn ? dpi.osDpiScale : 1.f);
    const float base = ctx.MapScale / scaling;                        // the auto-adjusting base scale
    const float openM = cfg.mapScaleCustom ? cfg.mapOpenScale : 1.0f; // optional fine-tune multiplier (default 1 = pure auto)
    const float miniM = cfg.mapScaleCustom ? cfg.miniMapScale : 1.0f;
    const float scale = mapOpen ? (base * openM) : (base * miniM);
    if (scale <= 0.f)
        return;
    ImVec2 bMin, bSize;
    if (mapOpen)
    {
        bMin = ImVec2(0.f, 0.f);
        bSize = ImVec2(W, H);
    } // map fills the screen, center == MapCenter
    else
    {
        const float cw = (float)ctx.CompassWidth, ch = (float)ctx.CompassHeight;
        if (cw < 1.f || ch < 1.f)
            return;
        const float vw = cw * scaling - 2.f, vh = ch * scaling - 1.f; // compass ACTUAL px = logical * Scaling (was uiScale)
        // Anchor to the screen corner the compass occupies: bottom-right (default) above the ~40px (logical)
        // bottom UI strip, or top-right. Width hugs the right edge; the player (MapCenter) lands at centre.
        if (IsCompassTopRight())
            bMin = ImVec2(W - vw, 1.f); // top inset (was W+1.f -- screen WIDTH, which pushed the minimap off the bottom edge)
        else
            bMin = ImVec2(W - vw, H - 37.f * scaling - vh);
        bSize = ImVec2(vw, vh);
    }

    // Publish the projection so HandleClick can reuse it (project the pin to screen for the minimap clear),
    // then project() is just the shared ContinentToMapScreen reading it -> one implementation, no drift.
    mapProj_ = MapProj{true, mapOpen, !mapOpen && IsCompassRotationEnabled(), bMin, bSize, scale,
                       std::cos(ctx.CompassRotation), std::sin(ctx.CompassRotation),
                       ctx.MapCenterX, ctx.MapCenterY};
    auto project = [this](const ImVec2 &cp) -> ImVec2
    { return ContinentToMapScreen(cp.x, cp.y); };

    // Now decide whether to DRAW anything -- the projection above is published UNCONDITIONALLY so HandleClick
    // can place/clear a personal target on the minimap even when nothing is being drawn. Route ribbon (per-surface
    // toggle + a loaded trail); travel pins (independent of the trail toggle); objective pins.
    // Like the in-world ribbon: hide the route trail when the zone is complete / nothing to guide to, unless
    // travelling, in a dungeon, or this surface's "show ... trail when complete" toggle is on. (curStep/the
    // guidance snapshot freeze while the map is open, so test completion directly from progress.)
    const bool showWhenComplete = mapOpen ? cfg.showMapTrailWhenComplete : cfg.showMiniMapTrailWhenComplete;
    const bool trailHasRoute = travel.Active() || st.inDungeon || showWhenComplete ||
                               !ZoneComplete(progress, st.currentChar, st.zone);
    const bool wantTrail = st.trailCont.size() >= 2 && (mapOpen ? cfg.showMapTrail : cfg.showMiniMapTrail) && trailHasRoute;
    const bool wantPins = travel.Active();
    const bool wantGuidePins = cfg.showMapPins && st.zone.Loaded && !travel.Active() && !st.inDungeon; // objective pins
    // Objective area circle: surface 0 both / 1 map only / 2 minimap only / 3 off. Only for the ACTIVE
    // objective (not travel: the travel pins own the destination then).
    const bool wantCircles = st.zone.Loaded && !st.inDungeon && !travel.Active() && cfg.objCircleSurface != 3 &&
                             (cfg.objCircleSurface == 0 || (cfg.objCircleSurface == 1 && mapOpen) ||
                              (cfg.objCircleSurface == 2 && !mapOpen));
    const bool wantGather = (mapOpen ? cfg.gatherOnMap : cfg.gatherOnMinimap) && !st.activeGather.empty(); // per-surface, independent toggles
    const bool wantFish = (mapOpen ? cfg.fishOnMap : cfg.fishOnMinimap) && !st.activeFish.empty();
    if (!wantTrail && !wantPins && !wantGuidePins && !wantCircles && !wantGather && !wantFish)
        return;

    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    if (!mapOpen)
        dl->PushClipRect(bMin, ImVec2(bMin.x + bSize.x, bMin.y + bSize.y), true); // clip to the minimap

    // Objective area circles (tinted ObjectiveCircle ring + optional faint fill), drawn UNDER the trail/pins.
    // Hearts use their real area (the bounds polygon -> centroid + max vertex radius); other types a default
    // world radius. Per-type toggles (objCircleKind) + per-surface selection (objCircleSurface) gate it.
    if (wantCircles)
    {
        // ACTIVE objective only, computed exactly like the guide pin so it stays live while the map is open and
        // st.curStep is frozen: the valid+incomplete curStep, else the first incomplete in authored order.
        const int total = (int)st.zone.Steps.size();
        int dispStep = (st.curStep >= 0 && st.curStep < total &&
                        !StepIsDone(progress, st.currentChar, st.zone.Steps[st.curStep]))
                           ? st.curStep
                           : -1;
        if (dispStep < 0)
            for (int i = 0; i < total; ++i)
                if (!StepIsDone(progress, st.currentChar, st.zone.Steps[i]))
                {
                    dispStep = i;
                    break;
                }

        const int kind = dispStep >= 0 ? CircleKindIndex(st.zone.Steps[dispStep].Type) : -1;
        if (kind >= 0 && cfg.objCircleKind[kind])
        {
            const Step &s = st.zone.Steps[dispStep];
            // Two baked textures sharing the SAME ring geometry (painted ring outer edge ~0.77 of the half-extent,
            // transparent beyond): the SHADED ring carries a faint inner fill baked in (white @ alpha 64); the
            // UNSHADED ring is the bare ring. So area-shading is just a texture swap -- no separate fill primitive.
            // We tint the whole image with the type colour at FULL alpha; the texture's baked alpha defines the
            // fill-vs-ring contrast, and stretching the image stretches its baked fill too.
            const char *texPath = cfg.objCircleBg ? "data\\textures\\ui\\ObjectiveCircleShaded.png"
                                                  : "data\\textures\\ui\\ObjectiveCircleUnShaded.png";
            const Texture_t *circTex = Tex::GetFileTex(texPath);
            const ImTextureID circId = (circTex && circTex->Resource) ? (ImTextureID)circTex->Resource : nullptr;
            const ImU32 ring = Objective::ColorOf(s.Type, 255);
            const float kRingOuter = 0.77f; // painted ring outer edge as a fraction of the texture half-extent

            if (!s.Bounds.empty())
            {
                // Heart: the painted ring STRETCHED to the real area's bounding box (hearts are elongated, not
                // round - so a circle always over/undershoots one axis). Drawn as an image QUAD from 4 projected
                // continent corners so it stays correct under the minimap's compass rotation.
                float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
                for (const Pt2 &p : s.Bounds)
                {
                    minX = std::min(minX, p.x);
                    maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y);
                    maxY = std::max(maxY, p.y);
                }
                const float ccx = (minX + maxX) * 0.5f, ccy = (minY + maxY) * 0.5f;
                const float hW = (maxX - minX) * 0.5f, hH = (maxY - minY) * 0.5f;
                const ImVec2 c0 = project(ImVec2(ccx, ccy));
                const ImVec2 ex = project(ImVec2(ccx + hW, ccy));
                if (hW > 1.f && hH > 1.f && std::hypot(ex.x - c0.x, ex.y - c0.y) >= 3.f)
                {
                    const float rw = hW / kRingOuter, rh = hH / kRingOuter; // expand so the ring's outer edge lands on the bbox
                    if (circId)
                        dl->AddImageQuad(circId,
                                         project(ImVec2(ccx - rw, ccy - rh)), project(ImVec2(ccx + rw, ccy - rh)),
                                         project(ImVec2(ccx + rw, ccy + rh)), project(ImVec2(ccx - rw, ccy + rh)),
                                         ImVec2(0.f, 0.f), ImVec2(1.f, 0.f), ImVec2(1.f, 1.f), ImVec2(0.f, 1.f), ring);
                    else
                    {
                        ImVec2 poly[48];
                        for (int i = 0; i < 48; ++i)
                        {
                            const float a = i * (6.2831853f / 48.f);
                            poly[i] = project(ImVec2(ccx + hW * std::cos(a), ccy + hH * std::sin(a)));
                        }
                        dl->AddPolyline(poly, 48, ring, true, 2.f);
                    }
                }
            }
            else
            {
                // Point objective: a small ROUND ring just past the icon (waypoints + POIs a touch larger for
                // their more generous range; you have to stand right on vistas/hero points). World-anchored so it
                // scales with zoom, but floored to a minimum screen size so it stays visible on the zoomed-out minimap.
                const float Rcont = Objective::ReachContinent(s.Type); // SAME radius the guide auto-completes at
                const ImVec2 sc = project(ImVec2(s.CX, s.CY));
                const ImVec2 e = project(ImVec2(s.CX + Rcont, s.CY));
                const float Rseen = std::max(std::hypot(e.x - sc.x, e.y - sc.y), 7.f);
                if (circId)
                {
                    const float texHalf = Rseen / kRingOuter;
                    dl->AddImage(circId, ImVec2(sc.x - texHalf, sc.y - texHalf), ImVec2(sc.x + texHalf, sc.y + texHalf), ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), ring);
                }
                else
                    dl->AddCircle(sc, Rseen, ring, 48, 2.f);
            }
        }
    }

    if (wantTrail)
    {
        // Opacity is the master trail opacity times a per-surface multiplier (open map vs minimap), so the two
        // surfaces can be dimmed independently. Width is its own setting.
        const float baseOpa = std::clamp(cfg.trailOpacity * (mapOpen ? cfg.mapTrailOpacity : cfg.miniTrailOpacity), 0.f, 1.f);
        const ImU32 colBase = TrailColorFor(cfg.trailColor, (int)std::round(255.f * baseOpa));
        const float thick = (float)(std::clamp(mapOpen ? cfg.mapTrailWidth : cfg.miniTrailWidth, 0, 5) + 1); // px dropdown index -> 1..6 px
        // High/low fade: dim segments whose world height is far above/below yours, so a flat map of a vertical
        // zone isn't a tangle. Needs the 1:1 world-height trail; the player's height comes from MumbleLink.
        constexpr float kFloorBand = 8.f;  // within this height (m) of you = full opacity (same floor)
        constexpr float kFloorFade = 28.f; // fully faded (to a faint floor) ~this much above/below
        const bool fadeHiLo = cfg.mapFadeHiLo && st.zone.Trail.size() == st.trailCont.size();
        const float avY = MumbleLink->AvatarPosition.Y;
        auto drawRange = [&](int start, int end)
        {
            if (start < 0) start = 0;
            if (end >= (int)st.trailCont.size()) end = (int)st.trailCont.size() - 1;
            if (end <= start)
                return;
            ImVec2 prev = project(st.trailCont[start]);
            for (int i = start + 1; i <= end; ++i)
            {
                const ImVec2 cur = project(st.trailCont[i]);
                const float lpx = prev.x - bMin.x, lcx = cur.x - bMin.x, lpy = prev.y - bMin.y, lcy = cur.y - bMin.y;
                const bool off = (lpx < 0 && lcx < 0) || (lpx > bSize.x && lcx > bSize.x) ||
                                 (lpy < 0 && lcy < 0) || (lpy > bSize.y && lcy > bSize.y); // segment fully off one side
                if (!off)
                {
                    ImU32 col = colBase;
                    if (fadeHiLo)
                    {
                        const float dy = std::fabs((st.zone.Trail[i - 1].y + st.zone.Trail[i].y) * 0.5f - avY);
                        const float f = 1.f - std::clamp((dy - kFloorBand) / kFloorFade, 0.f, 0.88f); // -> [0.12, 1]
                        col = TrailColorFor(cfg.trailColor, (int)std::round(255.f * baseOpa * f));
                    }
                    dl->AddLine(prev, cur, col, thick);
                }
                prev = cur;
            }
        };
        if (st.zone.TrailSections.empty())
            drawRange(0, (int)st.trailCont.size() - 1);
        else
        {
            for (const TrailSectionRange &s : st.zone.TrailSections)
                drawRange(s.Start, s.End);
        }
    }

    // Travel markers (continent positions, same projection), styled like the guide STEP pins: a BLUE "leave here"
    // pulse at the border crossing out of YOUR current zone, a GREEN radar-pulsing DESTINATION pin at the resolved
    // confirmed waypoint (the teleport hop -- destination-zone preferred), plus the orange DIAMOND for an Alt+click
    // personal spot. (Replaces the old gold/cyan hop pins + the mid-zone star.)
    if (travel.Active())
    {
        const float t = (float)ImGui::GetTime();
        const float ph = std::fmod(t, 1.5f) / 1.5f;                 // 0..1 ramp (1.5s period)
        const float ease = 0.5f - 0.5f * std::cos(ph * 6.2831853f); // smooth 0..1..0 throb

        float gcx = 0.f, gcy = 0.f;
        uint32_t gmid = 0;
        const bool haveGoal = travel.GetAnchor(gcx, gcy, gmid);

        // Entrance pulse: where you head OUT of the current zone toward the goal (clamp the goal to this zone).
        if (haveGoal && st.zone.HasRects)
        {
            float ecx = gcx, ecy = gcy;
            if (gmid != st.zone.MapId)
                Coords::ClampToRect(gcx, gcy, st.zone.ContRect, ecx, ecy);
            const ImVec2 ep = project(ImVec2(ecx, ecy));
            const unsigned ea = (unsigned)(180.f + 75.f * ease);
            dl->AddCircleFilled(ep, 4.f, IM_COL32(120, 200, 255, ea), 16); // blue "leave here" dot
            dl->AddCircle(ep, 8.f + 1.5f * ease, IM_COL32(120, 200, 255, ea), 18, 2.f);
        }

        // Destination radar pin = the resolved confirmed hop (else the nearest waypoint to the goal).
        const Travel::WpRef &known = travel.Known();
        const Travel::WpRef &fin = travel.Final();
        const Travel::WpRef &dest = known.valid ? known : fin;
        if (dest.valid)
        {
            const ImVec2 dp = project(ImVec2(dest.cx, dest.cy));
            const float dr = 13.f;
            if (void *tex = Tex::GetTextureFromAssetId(157353u)) // waypoint icon
                dl->AddImage((ImTextureID)tex, ImVec2(dp.x - dr, dp.y - dr), ImVec2(dp.x + dr, dp.y + dr));
            dl->AddCircle(dp, dr + 2.f, IM_COL32(150, 255, 175, 255), 24, 2.6f);
            dl->AddCircle(dp, dr + 2.f + ph * 11.f, IM_COL32(150, 255, 175, (unsigned)(170.f * (1.f - ph))), 28, 2.f);
        }

        // Alt+click personal spot: the orange diamond (matches the in-world marker), pulsing.
        if (travel.IsPersonal() && haveGoal)
            DrawPersonalPin(dl, project(ImVec2(gcx, gcy)), 9.f, /*pulse*/ true);
    }

    // Guide pins, matching what the arrow AIMS at per RouteMode: the current OBJECTIVE (destination) in EVERY
    // mode, PLUS - for HYBRID modes - a second pin at the trail ENTRANCE where the guide routes you onto the
    // route. Direct/Trail show only the destination. Green (distinct from the gold/cyan/orange travel pins).
    // Open-world only (not travel, not dungeons).
    if (cfg.showMapPins && st.zone.Loaded && !travel.Active() && !st.inDungeon)
    {
        // The current objective, computed exactly like the steps panel so it stays LIVE while the fullscreen
        // map is OPEN (then UpdateGuidanceAndArrow is paused and st.curStep is stale): the valid+incomplete
        // st.curStep, else the first incomplete in order. So checking a step off with the map open advances
        // the pin immediately instead of waiting until you close the map.
        const int total = (int)st.zone.Steps.size();
        int dispStep = (st.curStep >= 0 && st.curStep < total &&
                        !StepIsDone(progress, st.currentChar, st.zone.Steps[st.curStep]))
                           ? st.curStep
                           : -1;
        if (dispStep < 0)
            for (int i = 0; i < total; ++i)
                if (!StepIsDone(progress, st.currentChar, st.zone.Steps[i]))
                {
                    dispStep = i;
                    break;
                }

        if (dispStep >= 0)
        {
            const Step &c = st.zone.Steps[dispStep];
            const float t = (float)ImGui::GetTime();
            const float ph = std::fmod(t, 1.5f) / 1.5f;                 // 0..1 ramp (1.5s period)
            const float ease = 0.5f - 0.5f * std::cos(ph * 6.2831853f); // smooth 0..1..0 throb

            // Destination = the current objective: type icon + bright-green ring + an expanding "radar" pulse.
            const ImVec2 dp = project(ImVec2(c.CX, c.CY));
            const float dr = 13.f;
            if (uint32_t aid = 0; Objective::TryIconAssetId(c.Type, aid))
                if (void *tex = Tex::GetTextureFromAssetId(aid))
                    dl->AddImage((ImTextureID)tex, ImVec2(dp.x - dr, dp.y - dr), ImVec2(dp.x + dr, dp.y + dr));
            dl->AddCircle(dp, dr + 2.f, IM_COL32(150, 255, 175, 255), 24, 2.6f);
            dl->AddCircle(dp, dr + 2.f + ph * 11.f, IM_COL32(150, 255, 175, (unsigned)(170.f * (1.f - ph))), 28, 2.f);

            // Above/below indicator: an up/down arrow by the pin when the objective sits on a different floor
            // (its world height differs from yours by > kFloorThresh). Mirrors the in-world floor arrows.
            if (cfg.mapAboveBelow && c.TrailIndex >= 0 && c.TrailIndex < (int)st.zone.Trail.size())
            {
                constexpr float kFloorThresh = 10.f;
                const float dyF = st.zone.Trail[c.TrailIndex].y - MumbleLink->AvatarPosition.Y;
                if (std::fabs(dyF) > kFloorThresh)
                    Render::DrawGlyph(dl, ImVec2(dp.x + dr + 8.f, dp.y), 13.f,
                                      dyF > 0.f ? Render::Glyph::ArrowUp : Render::Glyph::ArrowDown,
                                      IM_COL32(150, 255, 175, 255), {});
            }

            // Hybrid only: a second "entrance" pin where the arrow has you join the trail (approachMeters back
            // along the route from the objective) - the exact entrance the guidance computes for the arrow.
            if ((cfg.routeMode == 1 || cfg.routeMode == 2) && c.TrailIndex >= 0 && c.TrailIndex < (int)st.trailCont.size()) // Hybrid Trail/Nearest only
            {
                int eSeg;
                float eT;
                follower.PointBackAlongTrail(c.TrailIndex, (float)cfg.approachMeters, eSeg, eT); // interpolated (smooth) entrance
                if (eSeg >= 0 && eSeg + 1 < (int)st.trailCont.size())
                {
                    const ImVec2 a = st.trailCont[eSeg], b = st.trailCont[eSeg + 1]; // interpolate the continent trail (1:1 with the follower's)
                    const ImVec2 ep = project(ImVec2(a.x + (b.x - a.x) * eT, a.y + (b.y - a.y) * eT));
                    const unsigned ea = (unsigned)(180.f + 75.f * ease);           // 180..255 alpha throb
                    dl->AddCircleFilled(ep, 4.f, IM_COL32(130, 230, 205, ea), 16); // teal-green "enter here" dot
                    dl->AddCircle(ep, 8.f + 1.5f * ease, IM_COL32(130, 230, 205, ea), 18, 2.f);
                }
            }
        }
    }

    if (wantGather) // farming gathering-node pins (single-sourced projection; same enabled-type filter as in-world)
        CategoryMarkers::DrawMap(cfg, st, project, bMin, bSize);
    if (wantFish) // fishing-hole pins (same projection; watchlist dims off-kind holes via st.fishWatchTypes)
        FishMarkers::DrawMap(cfg, st, project, bMin, bSize);

    if (!mapOpen)
        dl->PopClipRect();
}

void MapRenderer::UpdateAssist(const Config &cfg, GuideState &st, ProgressStore &progress)
{
    ForceReleaseDragButton(); // safety net: never carry a synthetic left-button-down between frames
    if (!cfg.showGuide)
        return;
    const bool mapOpen = MumbleLink && IsMapOpen();
    if (st.mapAssist.active)
    {
        if (!st.mapAssist.stepId.empty() &&
            !st.mapAssist.stepConfirmedAtStart &&
            progress.IsConfirmed(st.currentChar, st.mapAssist.stepId))
        {
            st.mapAssist = MapAssistTarget{};
        }
        else if (st.mapAssist.mapOpenedSeen && !mapOpen)
        {
            st.mapAssist = MapAssistTarget{};
        }
        else if (mapOpen)
        {
            st.mapAssist.mapOpenedSeen = true;
        }
    }

    if (!MumbleLink || !mapOpen || !mapProj_.valid || !mapProj_.mapOpen)
        return;
    const auto &ctx = MumbleLink->Context;
    if (mapProj_.scale <= 0.f)
        return;

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    auto drawDiagnostics = [&]()
    {
        if (cfg.debugRoute || cfg.mapAssistDebug)
            DrawMapDiagnostics(dl, mapProj_, cfg, st, ctx);
    };

    if (!st.mapAssist.active)
    {
        drawDiagnostics();
        return;
    }
    if (st.mapAssist.mapId != 0 && CurrentMapId() != 0 && CurrentMapId() != st.mapAssist.mapId)
    {
        st.mapAssist = MapAssistTarget{};
        drawDiagnostics();
        return;
    }

    const double now = ImGui::GetTime();
    if (st.mapAssist.waitingForReadback && now >= st.mapAssist.nextPanTime)
    {
        const float movedX = (ctx.MapCenterX - st.mapAssist.lastMapCenterX) / mapProj_.scale;
        const float movedY = (ctx.MapCenterY - st.mapAssist.lastMapCenterY) / mapProj_.scale;
        st.mapAssist.lastMovedX = movedX;
        st.mapAssist.lastMovedY = movedY;
        const float requestedLen = Length(ImVec2(st.mapAssist.lastRequestedX, st.mapAssist.lastRequestedY));
        const float movedLen = Length(ImVec2(movedX, movedY));
        const bool zeroMove = requestedLen >= 10.f && movedLen <= kAssistZeroMoveReadbackPx;
        if (zeroMove)
        {
            ++st.mapAssist.zeroMoveReadbacks;
            ++st.mapAssist.consecutiveZeroMoveReadbacks;
            st.mapAssist.panStatus = st.mapAssist.lastTransport == MapAssistTransport::WndProc
                                         ? "zero move wndproc"
                                         : "zero move";
        }
        else
        {
            st.mapAssist.consecutiveZeroMoveReadbacks = 0;
            st.mapAssist.panStatus = movedLen > kAssistZeroMoveReadbackPx ? "readback moved" : "readback";
        }
        UpdateMeasuredPanGain(st.mapAssist.lastRequestedX, movedX, st.mapAssist.panGainX, st.mapAssist.panCalX);
        UpdateMeasuredPanGain(st.mapAssist.lastRequestedY, movedY, st.mapAssist.panGainY, st.mapAssist.panCalY);
        st.mapAssist.waitingForReadback = false;
    }

    if (!st.mapAssist.relaxZoneBounds && !MapCenterNearZone(st.zone, ctx.MapCenterX, ctx.MapCenterY))
    {
        st.mapAssist.panRequested = false;
        st.mapAssist.waitingForReadback = false;
        st.mapAssist.centerConfirmWaiting = false;
        st.mapAssist.panStatus = "outside zone";
    }

    const ImVec2 center(mapProj_.bMin.x + mapProj_.bSize.x * 0.5f, mapProj_.bMin.y + mapProj_.bSize.y * 0.5f);
    st.mapAssist.lastErrorX = st.mapAssist.cx - ctx.MapCenterX;
    st.mapAssist.lastErrorY = st.mapAssist.cy - ctx.MapCenterY;
    ImVec2 delta(st.mapAssist.lastErrorX / mapProj_.scale,
                 st.mapAssist.lastErrorY / mapProj_.scale);
    const ImVec2 target(center.x + delta.x, center.y + delta.y);
    if (!st.mapAssist.waypointClickDone) // once the click fired, hide our marker so it can't cover GW2's teleport confirmation
        DrawAssistMarker(dl, mapProj_, target, st.mapAssist);

    if (!cfg.mapAssistFocusProbe)
        st.mapAssist.focusProbeRequested = false;

    if (st.mapAssist.focusProbeWaiting)
    {
        st.mapAssist.panStatus = "focus wait";
        if (now < st.mapAssist.focusProbeReadbackTime)
        {
            drawDiagnostics();
            return;
        }

        st.mapAssist.focusProbeAfterX = ctx.MapCenterX;
        st.mapAssist.focusProbeAfterY = ctx.MapCenterY;
        st.mapAssist.focusProbeWaiting = false;
        st.mapAssist.focusProbeDone = true;
        st.mapAssist.nextPanTime = now + 0.10;
        st.mapAssist.panStatus = "focus readback";
        drawDiagnostics();
        return;
    }

    if (st.mapAssist.focusProbeRequested)
    {
        st.mapAssist.focusProbeRequested = false;
        st.mapAssist.focusProbeBeforeX = ctx.MapCenterX;
        st.mapAssist.focusProbeBeforeY = ctx.MapCenterY;
        st.mapAssist.focusProbeAfterX = ctx.MapCenterX;
        st.mapAssist.focusProbeAfterY = ctx.MapCenterY;
        st.mapAssist.focusProbePlayerX = ctx.PlayerX;
        st.mapAssist.focusProbePlayerY = ctx.PlayerY;
        st.mapAssist.focusProbeInvoked = false;
        st.mapAssist.focusProbeDone = false;

        if (GameHasFocus() && !TextboxHasFocus() && InvokeMapFocusPlayer())
        {
            st.mapAssist.focusProbeInvoked = true;
            st.mapAssist.focusProbeWaiting = true;
            st.mapAssist.focusProbeReadbackTime = now + kAssistFocusReadbackSeconds;
            st.mapAssist.panStatus = "focus sent";
        }
        else
        {
            st.mapAssist.focusProbeDone = true;
            st.mapAssist.nextPanTime = now + 0.10;
            st.mapAssist.panStatus = "focus unavailable";
        }
        drawDiagnostics();
        return;
    }

    const float len = Length(delta);
    if (st.mapAssist.panDone)
    {
        if (len <= kAssistCenterTolerance)
        {
            st.mapAssist.centerConfirmWaiting = false;
            st.mapAssist.panStatus = "centered";
        }
        else if (len <= kAssistCloseEnoughTolerance)
        {
            st.mapAssist.centerConfirmWaiting = false;
            st.mapAssist.panStatus = "close enough";
        }
        else
        {
            st.mapAssist.panStatus = "drift after centered";
        }
        TryClickWaypointAfterPan(cfg, st.mapAssist, mapProj_, target, now);
        drawDiagnostics();
        return;
    }

    if (st.mapAssist.centerConfirmWaiting)
    {
        if (len > kAssistCenterTolerance)
        {
            st.mapAssist.centerConfirmWaiting = false;
            st.mapAssist.panStatus = "confirm drift";
        }
        else if (now < st.mapAssist.centerConfirmTime)
        {
            st.mapAssist.panStatus = "confirming";
            drawDiagnostics();
            return;
        }
        else
        {
            st.mapAssist.centerConfirmWaiting = false;
            st.mapAssist.panDone = true;
            st.mapAssist.panRequested = false;
            st.mapAssist.panStatus = "centered";
            drawDiagnostics();
            return;
        }
    }

    if (len <= kAssistCenterTolerance)
    {
        st.mapAssist.centerConfirmWaiting = true;
        st.mapAssist.centerConfirmTime = now + kAssistCenterConfirmSeconds;
        st.mapAssist.panStatus = "confirming";
        drawDiagnostics();
        return;
    }

    if (st.mapAssist.fineAttempts >= kAssistMaxFineAttempts && len <= kAssistCloseEnoughTolerance)
    {
        st.mapAssist.centerConfirmWaiting = false;
        st.mapAssist.panDone = true;
        st.mapAssist.panRequested = false;
        st.mapAssist.panStatus = "close enough";
        drawDiagnostics();
        return;
    }

    if (!st.mapAssist.panRequested)
    {
        drawDiagnostics();
        return;
    }
    if (st.mapAssist.panAttempts >= kAssistMaxPanAttempts)
    {
        st.mapAssist.panRequested = false;
        st.mapAssist.panStatus = "attempt cap";
        drawDiagnostics();
        return;
    }

    if (st.mapAssist.waitingForReadback)
    {
        st.mapAssist.panStatus = "waiting";
        drawDiagnostics();
        return;
    }
    if (now < st.mapAssist.nextPanTime)
    {
        st.mapAssist.panStatus = "delay";
        drawDiagnostics();
        return;
    }
    if (ImGui::GetIO().WantCaptureMouse)
    {
        CaptureWndProcDiagnostics(st.mapAssist);
        st.mapAssist.panStatus = "waiting imgui mouse";
        st.mapAssist.nextPanTime = now + 0.05;
        drawDiagnostics();
        return;
    }

    const float dragMargin = std::min(140.f, std::min(mapProj_.bSize.x, mapProj_.bSize.y) * 0.16f);
    const ImVec2 dragMin(mapProj_.bMin.x + dragMargin, mapProj_.bMin.y + dragMargin);
    const ImVec2 dragMax(mapProj_.bMin.x + mapProj_.bSize.x - dragMargin, mapProj_.bMin.y + mapProj_.bSize.y - dragMargin);
    ImVec2 dragRequested{};
    const bool finePhase = PointInRect(target, dragMin, dragMax);

    if (st.mapAssist.panAttempts == 0 && !finePhase)
    {
        // First drag: a small fixed-size probe in the target direction to MEASURE GW2's map-drag
        // sensitivity (observed ~8x) before committing to a gain-scaled drag -- avoids the initial fling
        // that flung the map past the zone and caused the up/down oscillation. (Near targets are already
        // in the fine band and skip the probe -- the conservative default gain keeps those small.)
        const float L = std::max(1.f, Length(delta));
        dragRequested = ImVec2(delta.x / L * kAssistProbeDrag, delta.y / L * kAssistProbeDrag);
        st.mapAssist.panStatus = "probe";
    }
    else if (finePhase)
    {
        // Fine phase: once visible, keep using the measured pan gain and smaller capped center drags.
        // Pulling the marker directly to center can over-correct because GW2's map drag response is not 1:1.
        dragRequested = ImVec2(delta.x * std::clamp(st.mapAssist.panGainX * kAssistFineGainScale, kAssistFineMinGain, kAssistMaxGain),
                               delta.y * std::clamp(st.mapAssist.panGainY * kAssistFineGainScale, kAssistFineMinGain, kAssistMaxGain));
        st.mapAssist.panStatus = "fine damped";
    }
    else
    {
        delta.x *= std::clamp(st.mapAssist.panGainX, kAssistMinGain, kAssistMaxGain);
        delta.y *= std::clamp(st.mapAssist.panGainY, kAssistMinGain, kAssistMaxGain);
        dragRequested = delta;
        st.mapAssist.panStatus = "coarse";
    }

    const float maxDrag = finePhase
                              ? (st.mapAssist.fineAttempts == 0
                                     ? std::min(96.f, std::max(56.f, std::min(mapProj_.bSize.x, mapProj_.bSize.y) * 0.085f))
                                     : std::min(64.f, std::max(42.f, std::min(mapProj_.bSize.x, mapProj_.bSize.y) * 0.060f)))
                              : std::min(560.f, std::max(220.f, std::min(mapProj_.bSize.x, mapProj_.bSize.y) * 0.42f));
    const float dragLen = Length(dragRequested);
    if (dragLen > maxDrag)
    {
        const float scale = maxDrag / dragLen;
        dragRequested.x *= scale;
        dragRequested.y *= scale;
    }

    // Anchor the grab point OFF-CENTRE so the drag's mouse-DOWN never lands on a waypoint sitting at the
    // screen centre, nor on GW2's teleport-confirm dialog (which appears centred) -- that centre press was
    // opening the centred waypoint and then auto-confirming it on the next drag. Offset PERPENDICULAR to the
    // drag so we keep full room along the drag axis; the relative delta (and gain measurement) is unchanged.
    const float anchorLen = Length(dragRequested);
    const ImVec2 perp = (anchorLen > 1.f) ? ImVec2(-dragRequested.y / anchorLen, dragRequested.x / anchorLen)
                                          : ImVec2(0.f, 1.f);
    const float anchorOff = std::min(mapProj_.bSize.x, mapProj_.bSize.y) * 0.22f;
    const ImVec2 from = ClampPoint(ImVec2(center.x + perp.x * anchorOff, center.y + perp.y * anchorOff), dragMin, dragMax);
    const ImVec2 to = ClampPoint(ImVec2(from.x - dragRequested.x, from.y - dragRequested.y), dragMin, dragMax);
    dragRequested = ImVec2(from.x - to.x, from.y - to.y);

    if (Length(dragRequested) < 5.f)
    {
        st.mapAssist.panRequested = false;
        st.mapAssist.panStatus = "drag too small";
        drawDiagnostics();
        return;
    }

    st.mapAssist.lastDragFromX = from.x;
    st.mapAssist.lastDragFromY = from.y;
    st.mapAssist.lastDragToX = to.x;
    st.mapAssist.lastDragToY = to.y;
    CaptureWndProcDiagnostics(st.mapAssist);
    const DragSendResult dragResult = SendMapDrag(from, to);
    st.mapAssist.lastTransport = dragResult.transport;
    st.mapAssist.lastWndProcResult = dragResult.wndProcResult;
    if (dragResult.sent)
    {
        ++st.mapAssist.panAttempts;
        if (dragResult.transport == MapAssistTransport::WndProc)
            ++st.mapAssist.wndProcDragAttempts;
        else if (dragResult.transport == MapAssistTransport::SendInput)
            ++st.mapAssist.sendInputDragAttempts;
        if (finePhase)
            ++st.mapAssist.fineAttempts;
        st.mapAssist.lastMapCenterX = ctx.MapCenterX;
        st.mapAssist.lastMapCenterY = ctx.MapCenterY;
        st.mapAssist.lastRequestedX = dragRequested.x;
        st.mapAssist.lastRequestedY = dragRequested.y;
        st.mapAssist.waitingForReadback = true;
        st.mapAssist.centerConfirmWaiting = false;
        st.mapAssist.nextPanTime = now + kAssistPanReadbackSeconds;
        st.mapAssist.panStatus = dragResult.status;
    }
    else
    {
        st.mapAssist.panStatus = dragResult.status;
    }
    drawDiagnostics();
}

// Alt+click on the OPEN fullscreen map OR the MINIMAP -> drop a personal travel target where the click landed
// (GW2 still places its own marker on the same spot, since we observe the click without consuming it). Clicking
// (near) our pin clears it. Screen->continent is the inverse of the matching Draw projection -- the open
// map directly; the minimap via the published mapProj_ / MapScreenToContinent (un-rotating the compass).
void MapRenderer::HandleClick(const Config &cfg, GuideState &st, Travel::Controller &travel)
{
    if (!cfg.showGuide || st.inDungeon || !MumbleLink)
        return;
    const bool mapOpen = IsMapOpen();

    // Detect the left-click EDGE + the Alt modifier via Win32, NOT ImGui io: Nexus doesn't reliably feed
    // game-map clicks / modifier state into ImGui's io (io.MouseClicked / io.KeyAlt stay false there), so
    // an Alt+click on the map went undetected. GetAsyncKeyState reads the real key state regardless.
    static bool s_prevLDown = false;
    const bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool clicked = lDown && !s_prevLDown;
    s_prevLDown = lDown;
    if (!clicked || ImGui::GetIO().WantCaptureMouse)
        return; // not a fresh click, or over an addon window
    const ImVec2 m = ImGui::GetMousePos();

    if (mapOpen)
    {
        const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        // Use Draw's PUBLISHED open-map projection (which is DPI-corrected AND carries the optional
        // fine-tune multiplier) so the placed continent point matches EXACTLY where the pin draws. The old
        // inline MapScale/UiScaleNow scale ignored the DPI correction, so under DPI scaling (game DPI != Nexus
        // DPI) the pin landed off to the side of GW2's own marker. Screen<->continent both via mapProj_ now.
        if (!mapProj_.valid || !mapProj_.mapOpen)
            return; // wait for Draw's open-map projection
        // Click (near) the existing pin -> clear it (compare in SCREEN space, ~20px, like the minimap path).
        float px, py;
        if (travel.HasPersonalAt(px, py))
        {
            const ImVec2 pin = ContinentToMapScreen(px, py);
            const float dx = m.x - pin.x, dy = m.y - pin.y;
            if (std::sqrt(dx * dx + dy * dy) <= 20.f)
            {
                travel.Clear();
                return;
            }
        }
        // Placing a new target is Alt-only; a plain click elsewhere is just normal map interaction.
        if (alt)
        {
            float cx, cy;
            MapScreenToContinent(m, cx, cy);
            travel.SetPersonalTarget(cx, cy);
            st.travelOffer = WaypointTravelOffer{};
            st.targetSwitch = TargetSwitchPrompt{};
        }
        return;
    }

    // MINIMAP: same actions as the open map. Use Draw's PUBLISHED projection -- clear by projecting our
    // pin to screen (comparing in SCREEN space avoids inverting the rotated compass for the test), place via
    // MapScreenToContinent. mapProj_ is last frame's (Render runs this before Draw) but the compass is
    // stable, so that's fine; the !mapOpen check below skips the 1-frame transient just after closing the map.
    if (!mapProj_.valid || mapProj_.mapOpen)
        return;
    if (m.x < mapProj_.bMin.x || m.x > mapProj_.bMin.x + mapProj_.bSize.x ||
        m.y < mapProj_.bMin.y || m.y > mapProj_.bMin.y + mapProj_.bSize.y)
        return; // click outside the compass
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    float px, py;
    if (travel.HasPersonalAt(px, py)) // click (near) the pin -> clear it (~22px screen), like GW2's marker
    {
        const ImVec2 pin = ContinentToMapScreen(px, py);
        const float dx = m.x - pin.x, dy = m.y - pin.y;
        if (std::sqrt(dx * dx + dy * dy) <= 21.f)
        {
            travel.Clear();
            return;
        }
    }
    // Placing is Alt-only (a plain compass click is normal minimap interaction).
    if (alt)
    {
        float cx, cy;
        MapScreenToContinent(m, cx, cy);
        travel.SetPersonalTarget(cx, cy);
        st.travelOffer = WaypointTravelOffer{};
        st.targetSwitch = TargetSwitchPrompt{};
    }
}
