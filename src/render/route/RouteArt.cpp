#include "render/route/RouteArt.h"
#include "util/Textures.h"
#include <algorithm>
#include <cmath>

void Render::DrawDirectIndicator(ImDrawList* dl, ImVec2 center, float ringR, float directRot,
                                 const DirectIndicatorStyle& style)
{
    if (!dl || ringR <= 1.f) return;

    // Brightness over time. Ping mode is ONE smooth raised-cosine swell across the WHOLE visible window --
    // gradual fade in, a rounded peak, gradual fade out, with NO flat hold. So a long duration is a slow gentle
    // swell (never sits solid) and a short one is a soft hump (not a hard flash); zero slope at both ends, so it
    // never pops. Constant mode keeps a gentle breathing.
    float level = 0.72f + 0.28f * (float)std::sin(ImGui::GetTime() * 3.0);
    if (style.ping)
    {
        const float every = std::max(1.f, (float)style.everySecs);
        const float show  = std::clamp((float)style.showSecs, 0.5f, every - 0.3f);
        const float tc    = (float)std::fmod(ImGui::GetTime(), (double)every);
        level = (tc >= show) ? 0.f : 0.5f - 0.5f * (float)std::cos(6.2831853 * (tc / show));
    }
    if (level <= 0.003f) return;

    const float theta = std::atan2(-std::cos(directRot), std::sin(directRot));   // ImGui arc angle (y is down)
    const float span  = 0.6f;                                                    // ~34 deg arc
    const float k     = std::clamp(ringR / 56.f, 0.55f, 1.5f);                   // scale the art with the ring size
    auto cyan = [&](int a) { return IM_COL32(80, 214, 236, (int)std::clamp((float)a * level, 0.f, 255.f)); };

    const float ths[] = { 9.f * k, 6.f * k, 3.5f * k };   // soft outer bloom (wide + faint) ...
    const int   als[] = { 46, 80, 140 };
    for (int i = 0; i < 3; ++i)
    {
        dl->PathArcTo(center, ringR, theta - span, theta + span, 28);
        dl->PathStroke(cyan(als[i]), false, ths[i]);
    }
    dl->PathArcTo(center, ringR, theta - span, theta + span, 28);                 // ... then a bright core
    dl->PathStroke(cyan(235), false, 2.2f * k);

    // Optional mini chevron OUTSIDE the arc, apex pointing outward at the objective; arms sweep back toward the
    // arc. The outward offset + arm scale with the ring, so it pokes out a consistent amount at any size.
    if (style.chevron)
    {
        const ImVec2 outDir(std::sin(directRot), -std::cos(directRot));
        const ImVec2 perp(-outDir.y, outDir.x);
        const float  arm = std::clamp(ringR * 0.14f, 4.f, 9.f);
        const float  off = std::clamp(ringR * 0.12f, 3.f, 8.f);                          // apex this far beyond the arc
        const ImVec2 apex(center.x + outDir.x * (ringR + off), center.y + outDir.y * (ringR + off));
        const ImVec2 base(apex.x - outDir.x * arm, apex.y - outDir.y * arm);             // arm root, back toward the arc
        const ImVec2 pts[3] = {
            ImVec2(base.x + perp.x * arm, base.y + perp.y * arm),
            apex,
            ImVec2(base.x - perp.x * arm, base.y - perp.y * arm)
        };
        dl->AddPolyline(pts, 3, cyan(120), false, 4.4f * k);   // soft
        dl->AddPolyline(pts, 3, cyan(245), false, 2.2f * k);   // core
    }
}

namespace
{
    ImU32 ArrowTint(const GuidanceSnapshot& g)
    {
        if (g.arrived) return IM_COL32(90, 230, 120, 255);
        if (g.offRoute) return IM_COL32(255, 115, 55, 255);
        if (g.align >  0.85f) return IM_COL32(90, 230, 110, 255);
        if (g.align >  0.30f) return IM_COL32(255, 210, 80, 255);
        if (g.align > -0.30f) return IM_COL32(255, 140, 40, 255);
        return IM_COL32(235, 60, 60, 255);
    }
}

void Render::DrawRouteArrow(ImDrawList* dl, ImVec2 center, float size, const GuidanceSnapshot& guidance,
                            bool showDirect, const DirectIndicatorStyle& style)
{
    if (!dl || size <= 0.f) return;
    const float hs = size * 0.5f;
    // On arrival the arrow points UP and BOBS in place inside the dial -- matching the main GPS arrow's arrival
    // animation (GpsArrow does rot=0 + a vertical sin bob, NOT a rotation). Otherwise it rotates to face the
    // target. The dial ring stays put; only the arrow bobs.
    const float rot = guidance.arrived ? 0.f : guidance.targetRot;
    const float bob = guidance.arrived ? (float)std::sin(ImGui::GetTime() * 6.0) * (size * 0.10f) : 0.f;
    const float cr = std::cos(rot), sr = std::sin(rot);
    auto R = [&](float dx, float dy) {
        return ImVec2(center.x + dx * cr - dy * sr, center.y + bob + dx * sr + dy * cr);
    };

    dl->AddCircleFilled(center, hs, IM_COL32(6, 8, 7, 190), 48);
    dl->AddCircle(center, hs - 1.f, IM_COL32(130, 106, 62, 140), 48, 1.2f);
    dl->AddCircle(center, hs * 0.52f, IM_COL32(130, 106, 62, 75), 40, 1.f);

    void* navTex = Tex::GetBundledTexture(guidance.arrived ? "data\\textures\\ui\\TCModernArrival.png"
                                                           : "data\\textures\\ui\\TCModernNav.png");
    if (!navTex && guidance.arrived) navTex = Tex::GetBundledTexture("data\\textures\\ui\\TCModernNav.png");
    const ImU32 tint = ArrowTint(guidance);
    const float arrowHs = hs * 0.68f;
    if (navTex)
    {
        dl->AddImageQuad((ImTextureID)navTex, R(-arrowHs, -arrowHs), R(arrowHs, -arrowHs),
                         R(arrowHs, arrowHs), R(-arrowHs, arrowHs),
                         ImVec2(0.f, 0.f), ImVec2(1.f, 0.f), ImVec2(1.f, 1.f),
                         ImVec2(0.f, 1.f), tint);
    }
    else
    {
        dl->AddTriangleFilled(R(0.f, -arrowHs), R(-arrowHs * 0.62f, arrowHs * 0.65f),
                              R(arrowHs * 0.62f, arrowHs * 0.65f), tint);
    }

    // Hybrid direct-objective glow, riding the dial's OUTER rim (scales with the dial when resized).
    if (showDirect && guidance.hasDirect)
        DrawDirectIndicator(dl, center, hs - 2.f, guidance.directRot, style);
}
