#include "world/GpsArrow.h"
#include "util/Draw.h"     // FormatEta, kFontSizePx, kFontSizeCount
#include "util/Textures.h"      // Tex::GetBundledTexture
#include "render/route/RouteArt.h"  // Render::DrawDirectIndicator (shared with the route-arrow surfaces)
#include "ui/Gw2Ui.h"      // Gw2Ui::ContextMenuTree
#include "ui/QuickMenu.h"  // QuickMenu::ArrowNodes (the shared nav-arrow menu)
#include "Shared.h"        // NexusLink
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <cfloat>
#include <cmath>
#include <cstdio>

static float WrapAngle(float a)
{
    const float pi = 3.14159265f, two = 6.2831853f;
    while (a >  pi) a -= two;
    while (a < -pi) a += two;
    return a;
}

int GpsArrow::Draw(const Follow::Vec2& aim, const Follow::Vec2& playerXz, const Math::Vec3& camFwd,
                   bool arrived, float travel, const char* captionName, float W, float H, bool dungeon,
                   Config& cfg, GuideState& st, bool& settingsDirty, float speedEma, TargetOverrideKind targetKind,
                   const Follow::Vec2* directAim, void* iconTex)
{
    int picked = -1;   // right-click menu id for the caller to dispatch (QuickMenu::ArrowDispatch)
    if (!cfg.showGuide || !cfg.showArrow) return picked;   // master "Show guide" + per-feature "Show GPS arrow"

    // Horizontal bearing from where the camera faces to the aim point (yaw-only, compass-style).
    const Follow::Vec2 dir = aim - playerXz;
    const Follow::Vec2 fwd{ camFwd.x, camFwd.z };
    float align = 1.f, cross = 0.f;
    if (dir.LenSq() > 1e-4f && fwd.LenSq() > 1e-4f)
    {
        const float dl = dir.Len(), fl = fwd.Len();
        const Follow::Vec2 dN{ dir.x / dl, dir.y / dl }, fN{ fwd.x / fl, fwd.y / fl };
        align = dN.Dot(fN);                       // 1 = dead ahead, -1 = behind you
        cross = fN.x * dN.y - fN.y * dN.x;        // left/right of where you face
    }
    const float targetRot = -std::atan2(cross, align);

    // Ease the rotation toward the target each frame (shortest angular path) so it glides, not snaps.
    if (!haveArrowRot_) { arrowRot_ = targetRot; haveArrowRot_ = true; }
    else if (!arrived)  arrowRot_ += WrapAngle(targetRot - arrowRot_) * 0.25f;

    // Green = dead on, gold = good, orange = off, red = wrong way (GpsArrow tint bands).
    ImU32 tint = arrived            ? IM_COL32( 90, 230, 120, 255)
               : align >  0.85f     ? IM_COL32( 90, 230, 110, 255)
               : align >  0.30f     ? IM_COL32(255, 210,  80, 255)
               : align > -0.30f     ? IM_COL32(255, 140,  40, 255)
                                    : IM_COL32(235,  60,  60, 255);

    void* navTex = Tex::GetBundledTexture("data\\textures\\ui\\TCModernNav.png");
    void* arrTex = Tex::GetBundledTexture("data\\textures\\ui\\TCModernArrival.png");
    void* tex    = arrived ? (arrTex ? arrTex : navTex) : navTex;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();   // same layer as trail/markers: above the game, BELOW
                                                       // addon windows + popups (so the arrow menu isn't covered)
    const float ui = Gw2Ui::GlobalScale();
    const float hs  = cfg.arrowSize * ui * 0.5f;

    // Arrow anchor: saved position, else default centre-low.
    float ax = (cfg.arrowPosX >= 0.f) ? cfg.arrowPosX : W * 0.5f;
    float ay = (cfg.arrowPosY >= 0.f) ? cfg.arrowPosY : H * 0.68f;

    // The arrow itself draws on the background draw list (above the game, below addon windows -- see `dl` above).
    // A bare draw-list submission can't take input (the game would get the click), so its interaction lives in an
    // invisible ImGui window over it: it
    // captures the mouse only while hovered -> right-click opens the arrow menu (always), and left-drag
    // moves the arrow when unlocked. No always-on indicator.
    ImGui::SetNextWindowSize(ImVec2(hs * 2.f, hs * 2.f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ax - hs, ay - hs), cfg.arrowLocked ? ImGuiCond_Always : ImGuiCond_Once);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGuiWindowFlags awf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing;
    if (cfg.arrowLocked) awf |= ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##tcarrow", nullptr, awf))
    {
        if (!cfg.arrowLocked)
        {
            const ImVec2 wp = ImGui::GetWindowPos();
            const float nx = wp.x + hs, ny = wp.y + hs;
            if (std::fabs(nx - ax) > 0.5f || std::fabs(ny - ay) > 0.5f) { ax = nx; ay = ny; cfg.arrowPosX = ax; cfg.arrowPosY = ay; settingsDirty = true; }
        }
        const bool openNow = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        // Build the menu nodes only when the menu (re)opens. ContextMenuTree still needs them every frame the
        // popup is up (so cache them), but the inputs can't change while it's open, so rebuilding the vector +
        // string labels every frame -- including every frame the menu is CLOSED -- was pure churn.
        static std::vector<Gw2Ui::MenuNode> s_nodes;
        if (openNow || s_nodes.empty())
        {
            s_nodes.clear();
            QuickMenu::ArrowNodes(cfg, targetKind, dungeon, st.activeContent == ContentType::Farming,
                                  st.activeContent == ContentType::Fishing, /*inWorld*/ true, s_nodes);
        }
        const int id = Gw2Ui::ContextMenuTree("##arrowctx", s_nodes, openNow);
        if (id >= 0) picked = id;   // caller runs QuickMenu::ArrowDispatch
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    const float cx  = ax;
    const float cyA = ay + (arrived ? (float)(std::sin(ImGui::GetTime() * 6.0) * 6.0 * ui) : 0.f); // bob on arrival
    const float rot = arrived ? 0.f : arrowRot_;

    const float cr = std::cos(rot), sr = std::sin(rot);
    auto R = [&](float dx, float dy) { return ImVec2(cx + dx * cr - dy * sr, cyA + dx * sr + dy * cr); };
    if (tex)
    {
        dl->AddImageQuad((ImTextureID)tex, R(-hs, -hs), R(hs, -hs), R(hs, hs), R(-hs, hs),
                         ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), tint);
    }
    else
    {
        // Fallback arrowhead (tip up = -y) while the texture is still loading.
        dl->AddTriangleFilled(R(0, -hs), R(-hs * 0.7f, hs * 0.7f), R(hs * 0.7f, hs * 0.7f), tint);
    }

    // Direct-objective glow (Hybrid): the caller passes the objective's world XZ only when this should show (the
    // in-world arrow's own toggle + Hybrid + en route). Compute the camera-relative bearing here (live camFwd),
    // then defer to the shared renderer so every arrow surface -- in-world, route-arrow widget, Info Panel
    // datatext -- draws an identical glow.
    if (directAim)
    {
        const Follow::Vec2 ddir = *directAim - playerXz;
        float dA = 1.f, dC = 0.f;
        if (ddir.LenSq() > 1e-4f && fwd.LenSq() > 1e-4f)
        {
            const float ddl = ddir.Len(), fl2 = fwd.Len();
            const Follow::Vec2 dN{ ddir.x / ddl, ddir.y / ddl }, fN{ fwd.x / fl2, fwd.y / fl2 };
            dA = dN.Dot(fN);
            dC = fN.x * dN.y - fN.y * dN.x;
        }
        const float directRot = -std::atan2(dC, dA);
        Render::DirectIndicatorStyle st{ cfg.directIndicatorChevron, cfg.directIndicatorPing,
                                         cfg.directIndicatorEverySecs, cfg.directIndicatorShowSecs };
        Render::DrawDirectIndicator(dl, ImVec2(cx, ay), hs * 1.32f, directRot, st);
    }

    // Caption: objective name, then "<dist>m   ETA m:ss" (DistColor), centred beneath the arrow.
    const char* nm = captionName;
    const std::string eta = FormatEta(travel, speedEma);
    char line2[64];
    if (eta == "--") std::snprintf(line2, sizeof(line2), "%.0fm", travel);
    else             std::snprintf(line2, sizeof(line2), "%.0fm   ETA %s", travel, eta.c_str());

    // GW2 font at the "Arrow text size" setting (was hardcoded to ImGui's default -> the setting did nothing).
    ImFont*      cf  = (NexusLink && NexusLink->Font) ? (ImFont*)NexusLink->Font : ImGui::GetFont();
    const float  cfs = kFontSizePx[std::clamp(cfg.arrowTextSize, 0, kFontSizeCount - 1)] * ui;
    const float  capY = ay + hs + 8.f * ui;
    const ImVec2 nmSz = cf->CalcTextSizeA(cfs, FLT_MAX, 0.f, nm);
    // Optional objective-type icon, drawn just LEFT of the name; the icon + name are centred as ONE block on cx.
    const bool   showIco = iconTex && cfg.arrowShowTypeIcon;   // "Show objective type icon" setting
    const float  icoSz  = showIco ? cfs * 1.15f : 0.f;
    const float  icoGap = showIco ? 5.f * ui : 0.f;
    const float  blockX = cx - (icoSz + icoGap + nmSz.x) * 0.5f;   // left edge of the [icon + name] block
    const float  nmX    = blockX + icoSz + icoGap;
    if (showIco)
        dl->AddImage((ImTextureID)iconTex, ImVec2(blockX, capY + (nmSz.y - icoSz) * 0.5f),
                     ImVec2(blockX + icoSz, capY + (nmSz.y - icoSz) * 0.5f + icoSz));
    dl->AddText(cf, cfs, ImVec2(nmX + 1.f * ui, capY + 1.f * ui), IM_COL32(0, 0, 0, 200), nm);
    dl->AddText(cf, cfs, ImVec2(nmX,     capY),     IM_COL32(255, 255, 255, 255), nm);
    const float  l2Y  = capY + nmSz.y + 2.f * ui;
    const ImVec2 l2Sz = cf->CalcTextSizeA(cfs, FLT_MAX, 0.f, line2);
    dl->AddText(cf, cfs, ImVec2(cx - l2Sz.x * 0.5f + 1.f * ui, l2Y + 1.f * ui), IM_COL32(0, 0, 0, 200), line2);
    dl->AddText(cf, cfs, ImVec2(cx - l2Sz.x * 0.5f,     l2Y),     Gw2Ui::kTextSelected, line2);

    return picked;
}
