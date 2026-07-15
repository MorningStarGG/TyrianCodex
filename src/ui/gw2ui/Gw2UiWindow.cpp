// Gw2Ui :: GW2-framed window chrome (frame/title/tab sidebar), resize splitters, content theme.
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiInternal.h"
#include "Shared.h"
#include "render/glyphs/Glyphs.h"
#include "util/Textures.h"
#include "ui/Effect.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Gw2Ui::detail;

namespace
{
    // --- GW2 ImGui palette (so content widgets read GW2-native until they get fully skinned) -------
    int PushGw2Colors()
    {
        struct C
        {
            ImGuiCol id;
            ImVec4 col;
        };
        static const C cs[] = {
            {ImGuiCol_Text, ImVec4(0.92f, 0.88f, 0.78f, 1.00f)},
            {ImGuiCol_TextDisabled, ImVec4(0.64f, 0.60f, 0.52f, 1.00f)},
            {ImGuiCol_ChildBg, ImVec4(0.06f, 0.05f, 0.03f, 0.10f)},
            {ImGuiCol_PopupBg, ImVec4(0.10f, 0.08f, 0.06f, 0.96f)},
            {ImGuiCol_Border, ImVec4(0.50f, 0.42f, 0.24f, 0.32f)},
            {ImGuiCol_FrameBg, ImVec4(0.20f, 0.16f, 0.10f, 0.85f)},
            {ImGuiCol_FrameBgHovered, ImVec4(0.30f, 0.24f, 0.14f, 0.90f)},
            {ImGuiCol_FrameBgActive, ImVec4(0.38f, 0.30f, 0.16f, 0.95f)},
            {ImGuiCol_SliderGrab, ImVec4(0.82f, 0.68f, 0.38f, 1.00f)},
            {ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.86f, 0.50f, 1.00f)},
            {ImGuiCol_CheckMark, ImVec4(1.00f, 0.86f, 0.50f, 1.00f)},
            {ImGuiCol_Header, ImVec4(0.45f, 0.36f, 0.18f, 0.70f)},
            {ImGuiCol_HeaderHovered, ImVec4(0.55f, 0.44f, 0.22f, 0.80f)},
            {ImGuiCol_HeaderActive, ImVec4(0.62f, 0.50f, 0.26f, 0.90f)},
            {ImGuiCol_Button, ImVec4(0.28f, 0.22f, 0.13f, 0.90f)},
            {ImGuiCol_ButtonHovered, ImVec4(0.42f, 0.34f, 0.18f, 1.00f)},
            {ImGuiCol_ButtonActive, ImVec4(0.52f, 0.42f, 0.22f, 1.00f)},
            {ImGuiCol_ScrollbarBg, ImVec4(0.08f, 0.06f, 0.04f, 0.40f)},
            {ImGuiCol_ScrollbarGrab, ImVec4(0.40f, 0.33f, 0.20f, 0.80f)},
            {ImGuiCol_Separator, ImVec4(0.55f, 0.45f, 0.25f, 0.55f)},
        };
        for (const C &c : cs)
            ImGui::PushStyleColor(c.id, c.col);
        return (int)(sizeof(cs) / sizeof(cs[0]));
    }
    // Window geometry EVERY overlay bound is computed from these constants - no eyeballed offsets.
    constexpr float kWinW = 902.f, kWinH = 705.f;
    constexpr float kRegX = 35.f, kRegY = 36.f; // windowRegion top-left
    constexpr float kBgTex = 1024.f;            // 156006 is 1024x1024
    // bg.png is fully transparent for its left ~3.5% (then a hard edge to opaque). Start the background
    // stretch at the opaque body so the fixed-width tab rail on the left always lands on opaque grunge (a
    // solid dark backing at any window size). The selected tab's carve samples BELOW this fraction -- into
    // the transparent margin -- for its left protrusion, giving the see-through "carved" edge. (A full
    // 0..1 stretch instead scales that 3.5% margin out to ~the rail width, leaving the rail see-through.)
    constexpr float kWinTexU0 = 0.038f;
    constexpr float kTitleH = 40.f, kTbVOff = 11.f, kLtbHOff = 2.f, kRtbHOff = 16.f;
    constexpr float kChromeClipTop = -48.f;
    constexpr float kTitleIconSize = 112.f, kTitleOff = 104.f, kSubOff = 20.f, kMargin = 16.f; // title @104 = reserve the larger emblem space
    constexpr float kSideW = 46.f, kSideOff = 3.f;
    constexpr float kTabVOff = 40.f, kTabH = 50.f, kTabW = 84.f;
    // begin/end state
    int s_ncol = 0;
    bool s_font = false;
    // Escape-to-close: s_escClosable is published each frame (true while a Gw2Ui window is focused) for the
    // Nexus WndProc to read; s_escRequest is set by the WndProc after it consumes an Escape, actioned in BeginWindow.
    bool s_escClosable = false;
    bool s_escRequest = false;
}

bool Gw2Ui::BeginWindow(const char *id, bool *open, const char *title, const char *subtitle,
                        const Tab *tabs, int tabCount, int *activeTab, float *ioW, float *ioH, bool restoreSize)
{
    const bool resizable = (ioW && ioH);
    const float ui = GlobalScale();
    auto S = [ui](float v) { return v * ui; };
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

    // We OWN the window size. This is a fully custom-chrome window, so instead of bolting ImGui's native resize
    // (NoResize stays SET) onto our hand-drawn frame -- where the size we draw and the size ImGui resizes to
    // drift out of sync in the shared Nexus context -- we force the size from the persisted value every frame
    // and drive resizing from our OWN grip (further down). The size we draw IS the window size: nothing can
    // desync, and there's no resize callback / constraint whose timing can fail. Aspect-locked (H = W / aspect).
    const float aspect = kWinW / kWinH;
    const float minW = S(kWinW);
    const float minH = S(kWinH);
    float capW = minW;
    if (resizable)
    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize; // game render size (DisplaySize is 1.80-safe; no WorkSize)
        const float maxW = std::min(S(kWinW * 2.f), disp.x > 0.f ? disp.x : S(kWinW * 2.f));
        const float maxH = std::min(S(kWinH * 2.f), disp.y > 0.f ? disp.y : S(kWinH * 2.f));
        capW = std::min(maxW, maxH * aspect); // 200% cap, honouring whichever monitor edge is tighter
        const float loW = std::min(minW, capW);
        const float w = std::clamp(*ioW, loW, capW);
        *ioW = w;
        *ioH = w / aspect; // keep the persisted size clamped + aspect-correct
        ImGui::SetNextWindowSize(ImVec2(w, w / aspect), ImGuiCond_Always);
    }
    else
        ImGui::SetNextWindowSize(ImVec2(minW, minH), ImGuiCond_Always);
    (void)restoreSize; // no longer needed: we force the size every frame, so there's no auto-fit to guard
    if (!ImGui::Begin(id, open, flags))
    {
        ImGui::End();
        return false;
    }

    // Escape closes the window (parity with the game/Nexus windows). The press must also be CONSUMED so it
    // doesn't reach the game (Escape menu) or Nexus -- which a render-thread key read can't do. So the Nexus
    // WndProc (entry.cpp) consumes Escape whenever a Gw2Ui window is focused (WindowEatsEscape) and calls
    // RequestEscapeClose; we action that here. During a keybind capture / textbox the press is still consumed
    // (no menu) but the assigner's own Esc-cancel / the textbox handle it instead of closing the window.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        s_escClosable = true;
        if (s_escRequest)
        {
            s_escRequest = false;
            if (open && !s_kbCapturing && !ImGui::GetIO().WantTextInput)
                *open = false;
        }
    }

    const ImVec2 wp = ImGui::GetWindowPos();
    // We forced the size above, so GetWindowSize() == exactly what we set: the chrome, background, and content
    // child all draw from this one value and can never desync from the actual window.
    const ImVec2 ws = ImGui::GetWindowSize();
    const float W = ws.x, H = ws.y;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    auto P = [&](float x, float y)
    { return ImVec2(wp.x + x, wp.y + y); }; // window-local -> screen
    // PaintTitleBar: the brighter "-active" title-bar textures show only when the mouse is over the
    // title-bar region (TitleBarBounds = 0,0,W,40) - the hover-brighten effect, not window focus.
    const bool active = ImGui::IsMouseHoveringRect(P(0.f, 0.f), P(W, S(kTitleH)));

    // The frame + tabs + corner overhang the window bounds; draw all chrome under one expanded clip.
    dl->PushClipRect(P(S(-40.f), S(kChromeClipTop)), P(W + S(24.f), H + S(60.f)), false);

    // 1) Background: bg.png. The texture is transparent for its left ~3.5% (the carved-tab margin) then opaque.
    //    Stretch its OPAQUE region (u kWinTexU0..1) across the window, so the body AND the fixed-width tab rail
    //    on the left both sit on opaque grunge at any size (no game bleed, and the rail keeps a dark backing).
    //    The selected tab below samples BELOW kWinTexU0 (the transparent margin) for its protrusion -> the
    //    see-through carved edge -- exactly what the original fixed-size draw did, now stretched to fit.
    const T bg = File("data\\textures\\ui\\bg.png");
    if (bg.srv)
        Img(dl, bg, wp, P(W, H), ImVec2(kWinTexU0, 0.f), ImVec2(1.f, 1.f));
    else
    {
        dl->AddRectFilled(wp, P(W, H), IM_COL32(18, 16, 12, 235), S(6.f));
        dl->AddRect(wp, P(W, H), IM_COL32(180, 150, 90, 255), S(6.f), 0, S(2.f));
    }

    // 2) Title-bar draw bounds (WindowBase2.RecalculateLayout).
    const T tlTex = active ? File("data\\textures\\ui\\titlebar-active.png") : File("data\\textures\\ui\\titlebar-inactive.png");
    const T trTex = active ? File("data\\textures\\ui\\window-topright-active.png") : File("data\\textures\\ui\\window-topright.png");
    const float trW = S(trTex.w > 0.f ? trTex.w : 128.f), trH = S(trTex.h > 0.f ? trTex.h : 64.f);
    const float lbH = S(tlTex.h > 0.f ? tlTex.h : 64.f);
    const float rbX = W - trW + S(kRtbHOff);                   // 902-128+16 = 790
    const float rbY = S(-kTbVOff);                             // -11
    const float lbX = S(-kLtbHOff);                            // -2
    const float lbW = rbX - lbX;                              // span fully to the right cap -> no gap at any width
    const float lbU1 = (lbW < S(kBgTex)) ? (lbW / S(kBgTex)) : 1.f; // default: sample the left ~790px 1:1; wider: stretch the full texture
    const float lbBottom = rbY + lbH;                         // -11 + 64 = 53

    // 3) Sidebar bounds + PaintSideBar (solid black over the active region, fade below, split line).
    const float sideBarTop = lbBottom - S(kTbVOff);                 // 53 - 11 = 42
    const float sideBarHeight = S(kTabVOff + kTabH * (float)tabCount); // 40 + 50*n
    const float saX = lbX + S(kSideOff), saY = sideBarTop - S(kSideOff); // (1, 39)
    const float localSideH = H - sideBarTop;                        // 638
    const float inaY = saY + sideBarHeight;
    const float inaBottom = inaY + (localSideH - sideBarHeight); // = saY + localSideH = 677
    dl->AddRectFilled(P(saX, saY), P(saX + S(kSideW), saY + sideBarHeight), IM_COL32(0, 0, 0, 255));
    if (const T fade = File("data\\textures\\ui\\fade-down-46.png"); fade.srv && inaBottom > inaY)
        Img(dl, fade, P(saX, inaY), P(saX + S(kSideW), inaBottom));
    if (const T split = Asset(605026); split.srv)
        Img(dl, split, P(saX + S(kSideW) - S(split.w * 0.5f), saY), P(saX + S(kSideW) + S(split.w * 0.5f), inaBottom));

    // Title-bar textures (drawn over the sidebar top), then icon + title + subtitle.
    if (tlTex.srv)
        Img(dl, tlTex, P(lbX, rbY), P(lbX + lbW, rbY + lbH), ImVec2(0, 0), ImVec2(lbU1, 1.f));
    if (trTex.srv)
        Img(dl, trTex, P(rbX, rbY), P(rbX + trW, rbY + trH));
    if (const T logo = File("data\\textures\\ui\\TCDXLogo.png"); logo.srv && logo.w > 0.f && logo.h > 0.f)
    {
        const float iconMax = S(kTitleIconSize);
        const float scale = std::min(iconMax / logo.w, iconMax / logo.h);
        const float iw = logo.w * scale, ih = logo.h * scale;
        const float ix = lbX + (S(kTitleOff) - iw) * 0.5f;
        const float iy = rbY + (lbH - ih) * 0.5f;
        Img(dl, logo, P(ix, iy), P(ix + iw, iy + ih));
    }
    {
        ImFont *big = (NexusLink && NexusLink->FontBig) ? (ImFont *)NexusLink->FontBig : ImGui::GetFont();
        ImFont *sf = (NexusLink && NexusLink->Font) ? (ImFont *)NexusLink->Font : big;
        const float titleX = lbX + S(kTitleOff); // -2 + 80 = 78 (emblem space)
        const float cenY = rbY + lbH * 0.5f;  // title bar vertical centre = 21
        const float bigFs = big ? big->FontSize * ui : 0.f;
        const float sfFs = sf ? sf->FontSize * ui : 0.f;
        if (big)
            dl->AddText(big, bigFs, P(titleX, cenY - bigFs * 0.5f), IM_COL32(255, 244, 207, 255), title);
        if (subtitle && *subtitle && sf)
        {
            const float tw = big ? big->CalcTextSizeA(bigFs, 100000.f, 0.f, title).x : S(160.f);
            dl->AddText(sf, sfFs, P(titleX + tw + S(kSubOff), cenY - sfFs * 0.5f), IM_COL32(255, 255, 255, 255), subtitle);
        }
    }

    // 4) Exit button: (rightBar.Right - MARGIN*2 - exitW, rightBar.Y + MARGIN) = (852, 5).
    {
        const T ex = File("data\\textures\\ui\\button-exit.png");
        const T exA = File("data\\textures\\ui\\button-exit-active.png");
        const float exW = S(ex.w > 0.f ? ex.w : 32.f), exH = S(ex.h > 0.f ? ex.h : 32.f);
        const float exX = (rbX + trW) - S(kMargin * 2.f) - exW, exY = rbY + S(kMargin);
        ImGui::SetCursorScreenPos(P(exX, exY));
        const bool clicked = ImGui::InvisibleButton("##tc_exit", ImVec2(exW, exH));
        const T et = (ImGui::IsItemHovered() && exA.srv) ? exA : ex;
        if (et.srv)
            Img(dl, et, P(exX, exY), P(exX + exW, exY + exH));
        if (clicked && open)
            *open = false;
    }

    // 5) Tabs (TabbedWindow2.PaintAfterChildren). The SELECTED tab re-draws the window background sampled
    //    at the frame's left border into a left-protruding tab + the window-tab-active overlay, then the
    //    icon. Click area = the 46px sidebar slot (SidebarActiveBounds).
    {
        const T tabActive = File("data\\textures\\ui\\window-tab-active.png");
        for (int i = 0; i < tabCount; ++i)
        {
            const float tabTop = saY + S(kTabVOff + i * kTabH); // 79 + i*50
            ImGui::SetCursorScreenPos(P(saX, tabTop));
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##tc_tab", ImVec2(S(kSideW), S(kTabH)));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool sel = (activeTab && *activeTab == i);

            if (sel)
            {
                const float tbX = saX - S(kTabW - kSideW) + S(2.f); // 1 - 38 + 2 = -35
                if (bg.srv)
                {
                    // Re-draw the window background under the tab, mirroring the fill's UV so the tab body
                    // matches the window beside it. The protrusion (tbX < 0) maps BELOW kWinTexU0 into the
                    // transparent left margin -> the see-through carved edge; the body part sits on opaque grunge.
                    auto ux = [&](float lx)
                    { return std::clamp(kWinTexU0 + (lx / W) * (1.f - kWinTexU0), 0.f, 1.f); };
                    auto uy = [&](float ly)
                    { return std::clamp(ly / H, 0.f, 1.f); };
                    Img(dl, bg, P(tbX, tabTop), P(tbX + S(kTabW), tabTop + S(kTabH)),
                        ImVec2(ux(tbX), uy(tabTop)), ImVec2(ux(tbX + S(kTabW)), uy(tabTop + S(kTabH))));
                }
                if (tabActive.srv)
                    Img(dl, tabActive, P(tbX, tabTop), P(tbX + S(kTabW), tabTop + S(kTabH)));
            }
            else if (hov)
                dl->AddRectFilled(P(saX, tabTop), P(saX + S(kSideW), tabTop + S(kTabH)), IM_COL32(255, 255, 255, 22));

            {
                const float cx = saX + S(kSideW * 0.5f), cy = tabTop + S(kTabH * 0.5f);
                if (tabs[i].file) // a bundled data/ texture (e.g. the Wiki tab's gw2.png)
                {
                    if (const T t = File(tabs[i].file); t.srv)
                        Img(dl, t, P(cx - S(16.f), cy - S(16.f)), P(cx + S(16.f), cy + S(16.f)));
                }
                else if (tabs[i].glyph >= 0) // procedural glyph icon (e.g. Bookmarks/History) -- no gw2dat asset
                    Render::DrawGlyph(dl, P(cx, cy), S(26.f), (Render::Glyph)tabs[i].glyph, IM_COL32(223, 209, 178, 255), Render::GlyphStyle{});
                else if (const T icon = Asset(tabs[i].icon); icon.srv)
                    Img(dl, icon, P(cx - S(16.f), cy - S(16.f)), P(cx + S(16.f), cy + S(16.f)));
            }
            if (hov)
                Tooltip(tabs[i].name);
            if (clicked && activeTab)
                *activeTab = i;
        }
    }

    // 6) Bottom-right corner cap anchored to the window's bottom-right at ANY size
    //	kCornerNudge pushes the cap down-right into the true corner:
    //	raise it to seat the cap deeper, lower to pull it back.
    const float kCornerNudge = S(4.f);
    if (const T corner = Asset(156008); corner.srv)
        Img(dl, corner, P(W - S(corner.w) + kCornerNudge, H - S(corner.h) + kCornerNudge),
            P(W + kCornerNudge, H + kCornerNudge));

    // Resize grip: WE drive the resize (the window is NoResize). An invisible button at the bottom-right corner
    // edits the persisted width on drag (height follows the locked aspect); next frame the forced size grows to
    // match. Drawn on the foreground list so the glyph sits above the chrome + content child.
    if (resizable)
    {
        const float ghit = S(22.f);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + W - ghit, wp.y + H - ghit));
        ImGui::InvisibleButton("##tc_resize", ImVec2(ghit, ghit));
        const bool ghover = ImGui::IsItemHovered(), gactive = ImGui::IsItemActive();
        if (ghover || gactive)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        if (gactive)
        {
            const ImVec2 d = ImGui::GetIO().MouseDelta; // grow by whichever axis the user moves more (aspect-scaled)
            const float deltaW = (std::fabs(d.x) >= std::fabs(d.y * aspect)) ? d.x : d.y * aspect;
            if (deltaW != 0.f)
            {
                const float nw = std::clamp(W + deltaW, minW, capW);
                *ioW = nw;
                *ioH = nw / aspect;
            }
        }
        Render::GlyphStyle gs;
        gs.shadow = false;
        Render::DrawGlyph(ImGui::GetForegroundDrawList(), ImVec2(wp.x + W - S(9.f), wp.y + H - S(9.f)), S(14.f),
                          Render::Glyph::ResizeGrip,
                          (ghover || gactive) ? IM_COL32(255, 220, 150, 255) : IM_COL32(150, 128, 86, 205), gs);
    }

    dl->PopClipRect();

    // 7) Content region: GW2 theme + font applied, then the live-sized content child below.
    s_ncol = PushGw2Colors();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(2.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, S(1.f));
    ImFont *uf = (NexusLink && NexusLink->Font) ? (ImFont *)NexusLink->Font : nullptr;
    s_font = uf != nullptr;
    if (s_font)
        ImGui::PushFont(uf);

    // 7) Content child = ContentRegion, sized from the LIVE window so every tab gets the extra room. Insets are
    //    derived from the defaults (902-60-821 = 21 right, 705-71-572 = 62 bottom) -> at default size this is
    //    exactly 821x572.
    constexpr float kContentX = 60.f, kContentY = 71.f;
    constexpr float kContentRightInset = kWinW - kContentX - 821.f;  // 21
    constexpr float kContentBottomInset = kWinH - kContentY - 572.f; // 62
    ImGui::SetCursorScreenPos(P(S(kContentX), S(kContentY)));
    ImGui::BeginChild("##tc_content",
                      ImVec2(W - S(kContentX) - S(kContentRightInset), H - S(kContentY) - S(kContentBottomInset)),
                      false, ImGuiWindowFlags_NoBackground);
    return true;
}

void Gw2Ui::EndWindow()
{
    ImGui::EndChild();
    if (s_font)
        ImGui::PopFont();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(s_ncol);
    ImGui::End();
}

// The ONE reusable resize primitive: a draggable vertical splitter that edits *width in place. Used for rail/
// body splits and (one per column) table-column dividers. The grab area is a few px wider than the drawn line
// so it's easy to catch; the handle is its own item id, so a drag here never triggers a neighbouring click.
bool Gw2Ui::VSplitter(const char *id, float x, float y, float height, float *width, float minW, float maxW)
{
    if (!width || height <= 0.f)
        return false;
    const float sc = GlobalScale();
    const float kHit = 8.f * sc, kLine = 2.f * sc;
    ImGui::SetCursorScreenPos(ImVec2(x - kHit * 0.5f, y));
    ImGui::InvisibleButton(id, ImVec2(kHit, height));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    if (hov || act)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    bool changed = false;
    if (act)
    {
        const float nw = std::clamp(*width + ImGui::GetIO().MouseDelta.x, minW, maxW);
        if (nw != *width)
        {
            *width = nw;
            changed = true;
        }
    }

    // Subtle vertical rule, brighter on hover/drag (GW2 gold), inset a touch top/bottom.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 col = act ? IM_COL32(255, 224, 158, 210) : hov ? IM_COL32(230, 206, 150, 150)
                                                               : IM_COL32(150, 138, 110, 70);
    dl->AddRectFilled(ImVec2(x - kLine * 0.5f, y + 3.f * sc), ImVec2(x + kLine * 0.5f, y + height - 3.f * sc), col);
    return changed;
}

bool Gw2Ui::ColumnBorder(const char *id, float x, float y, float height,
                         float *leftW, float minL, float maxL, float *rightW, float minR, float maxR)
{
    if (height <= 0.f || (!leftW && !rightW))
        return false;
    const float sc = GlobalScale();
    const float kHit = 8.f * sc, kLine = 1.f * sc;
    ImGui::SetCursorScreenPos(ImVec2(x - kHit * 0.5f, y));
    ImGui::InvisibleButton(id, ImVec2(kHit, height));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    if (hov || act)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    bool changed = false;
    if (act)
    {
        float d = ImGui::GetIO().MouseDelta.x;
        if (leftW && rightW)
        {
            // Neighbor-absorb: bound d so neither side leaves its [min,max]; the pair's total stays constant.
            const float lo = std::max(minL - *leftW, *rightW - maxR);
            const float hi = std::min(maxL - *leftW, *rightW - minR);
            d = (lo > hi) ? 0.f : std::clamp(d, lo, hi);
            if (d != 0.f)
            {
                *leftW += d;
                *rightW -= d;
                changed = true;
            }
        }
        else if (rightW) // flex column on the LEFT: dragging right shrinks the right (fixed) column, flex grows
        {
            const float nw = std::clamp(*rightW - d, minR, maxR);
            if (nw != *rightW)
            {
                *rightW = nw;
                changed = true;
            }
        }
        else // flex column on the RIGHT: dragging right grows the left (fixed) column, flex shrinks
        {
            const float nw = std::clamp(*leftW + d, minL, maxL);
            if (nw != *leftW)
            {
                *leftW = nw;
                changed = true;
            }
        }
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 col = act ? IM_COL32(255, 224, 158, 200) : hov ? IM_COL32(230, 206, 150, 130)
                                                               : IM_COL32(150, 138, 110, 55);
    dl->AddRectFilled(ImVec2(x - kLine * 0.5f, y + 2.f * sc), ImVec2(x + kLine * 0.5f, y + height - 2.f * sc), col);
    return changed;
}

// Escape-to-close plumbing (see header). NewFrameEscReset clears the per-frame "focused" flag so a closed
// window stops eating Escape; BeginWindow re-sets it while focused. The WndProc reads WindowEatsEscape and,
// when it consumes an Escape, calls RequestEscapeClose for BeginWindow to action.
void Gw2Ui::NewFrameEscReset() { s_escClosable = false; }
bool Gw2Ui::WindowEatsEscape() { return s_escClosable; }
void Gw2Ui::RequestEscapeClose() { s_escRequest = true; }
void Gw2Ui::WarmAsset(uint32_t assetId)
{
    Asset(assetId); // local-first; kicks the bundled (or CDN-fallback) load before the first open
}
