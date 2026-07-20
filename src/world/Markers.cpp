#include "world/Markers.h"
#include "util/Draw.h"     // KindIndex
#include "util/Coords.h"   // Coords::ContinentToWorldXZ
#include "util/Textures.h" // Tex::GetTextureFromAssetId / GetBundledTexture
#include "guide/StepStatus.h"
#include "model/ObjectiveTypes.h" // Objective::
#include "world/MarkerFx.h"       // combat fade / global caps / between-camera-and-character
#include "render/glyphs/Glyphs.h" // Render::DrawGlyph (above/below chevrons)
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    void *RouteUseMarkerTexture(const RouteMarker &m)
    {
        return m.Icon.empty() ? nullptr
                              : Tex::GetBundledTexture(("data\\textures\\markers\\mounts\\" + m.Icon + ".png").c_str());
    }

    ImU32 RouteUseMarkerFallback(const RouteMarker &m, int alpha)
    {
        if (m.Icon == "glider")
            return IM_COL32(120, 200, 255, alpha);
        if (m.Icon == "skyscale" || m.Icon == "griffon")
            return IM_COL32(180, 150, 255, alpha);
        if (m.Icon == "skimmer")
            return IM_COL32(80, 180, 220, alpha);
        if (m.Icon == "springer")
            return IM_COL32(150, 220, 120, alpha);
        if (m.Icon == "beetle" || m.Icon == "warclaw")
            return IM_COL32(230, 170, 80, alpha);
        return IM_COL32(255, 220, 120, alpha);
    }

    void DrawRouteUseMarkers(const FrameCtx &fc, const Config &cfg, const GuideState &st,
                             const MarkerFx::Between &between, float combatFade)
    {
        if (st.zone.RouteMarkers.empty())
            return;

        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        const float fadeNear = cfg.markerFadeNear;
        const float fadeFar = std::max(cfg.markerFadeFar, fadeNear + 1.f);

        struct RM
        {
            float dist, sx, sy;
            const RouteMarker *m;
            int alpha;
        };
        static std::vector<RM> ms;
        ms.clear();
        ms.reserve(st.zone.RouteMarkers.size());

        for (const RouteMarker &m : st.zone.RouteMarkers)
        {
            const Math::Vec3 pos{m.X, m.Y + cfg.markerExtraH, m.Z};
            const float dist = (pos - fc.camPos).Length();
            if (dist > fadeFar)
                continue;
            float sx, sy, depth;
            if (!Math::WorldToScreen(pos, fc.vp, fc.W, fc.H, sx, sy, depth))
                continue;
            const float fade = 1.f - std::clamp((dist - fadeNear) / (fadeFar - fadeNear), 0.f, 1.f);
            if (fade <= 0.01f)
                continue;
            const float vis = MarkerFx::OpacityCap(fade * combatFade * MarkerFx::BetweenDim(between, ImVec2(sx, sy), dist), cfg);
            if (vis <= 0.004f)
                continue;
            ms.push_back({dist, sx, sy, &m, (int)(vis * 255.f)});
        }

        std::sort(ms.begin(), ms.end(), [](const RM &a, const RM &b)
                  { return a.dist > b.dist; });

        for (const RM &it : ms)
        {
            const float px = std::clamp(cfg.markerSize * fc.pxScale / std::max(it.dist, 0.01f),
                                        kMarkerMinPx, kMarkerMaxPx);
            const float h = px * 0.5f;
            if (void *tex = RouteUseMarkerTexture(*it.m))
            {
                dl->AddImage((ImTextureID)tex, ImVec2(it.sx - h, it.sy - h), ImVec2(it.sx + h, it.sy + h),
                             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, it.alpha));
            }
            else
            {
                const ImU32 col = RouteUseMarkerFallback(*it.m, it.alpha);
                dl->AddCircleFilled(ImVec2(it.sx, it.sy), h * 0.62f, col, 18);
                dl->AddCircle(ImVec2(it.sx, it.sy), h * 0.62f, IM_COL32(255, 255, 255, it.alpha / 2), 18, 1.5f);
            }
        }
    }
}

// In-world objective markers: the real GW2 dat icon for each objective type as a billboard at the objective
// (continent coord -> world, height from the nearest trail point), distance-faded from the camera and
// size-clamped on screen. Completed objectives drop out. White tint = the icon's
// own colours; while the gw2dat icon is still downloading we draw a fallback dot.
void Markers::DrawWorld(const FrameCtx &fc, const Config &cfg, const GuideState &st, ProgressStore &progress)
{
    const Math::Mat4 &vp = fc.vp;
    const float W = fc.W, H = fc.H;
    const Math::Vec3 &camPos = fc.camPos;
    const float pxScale = fc.pxScale;

    if (!st.zone.HasRects || st.zone.Steps.empty())
        return;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const int nTrail = (int)st.zone.Trail.size();
    const float fadeNear = cfg.markerFadeNear;
    const float fadeFar = std::max(cfg.markerFadeFar, fadeNear + 1.f);
    const float combatFade = MarkerFx::CombatFade(cfg.markerFadeInCombat, fc);
    const MarkerFx::Between between = MarkerFx::BeginBetween(cfg, fc);
    const bool showGhost = cfg.viewerShowCompleted; // completed objectives -> dim ghosts (else they vanish)

    // Mount/glider route-use markers are source-authored route annotations, not objectives. Draw them first so
    // objective icons stay visually dominant when both sit near the same spot.
    DrawRouteUseMarkers(fc, cfg, st, between, combatFade);

    // Collect the visible markers, then DEPTH-SORT them far -> near and draw in that order, so a nearer marker
    // always covers a farther one (a CPU z-test -- the ImGui draw list has no depth buffer, so list order alone
    // would let a distant marker paint over a closer one).
    struct WM
    {
        float dist, sx, sy, dy;
        const Step *s;
        int alpha;
    };
    static std::vector<WM> ms;
    ms.clear();
    ms.reserve(st.zone.Steps.size());

    for (const Step &s : st.zone.Steps)
    {
        Objective::MarkerKind kind;
        if (!Objective::TryMarkerKind(s.Type, kind))
            continue; // type carries no in-world marker
        const int ki = KindIndex(kind);
        if (ki < 0 || !cfg.kindEnabled[ki])
            continue;
        const bool done = StepIsDone(progress, st.currentChar, s);
        if (done && !showGhost)
            continue; // completed objectives vanish unless Show-completed ghosts them

        float wx, wz;
        Coords::ContinentToWorldXZ(s.CX, s.CY, st.zone.ContRect, st.zone.MapRect, wx, wz);
        const float ground = (s.TrailIndex >= 0 && s.TrailIndex < nTrail) ? st.zone.Trail[s.TrailIndex].y : camPos.y;
        const Math::Vec3 pos{wx, ground + cfg.markerExtraH, wz};

        const float dist = (pos - camPos).Length();
        if (dist > fadeFar)
            continue; // fully faded - skip
        float sx, sy, depth;
        if (!Math::WorldToScreen(pos, vp, W, H, sx, sy, depth))
            continue;
        const float fade = 1.f - std::clamp((dist - fadeNear) / (fadeFar - fadeNear), 0.f, 1.f);
        if (fade <= 0.01f)
            continue;
        float vis = MarkerFx::OpacityCap(fade * combatFade * MarkerFx::BetweenDim(between, ImVec2(sx, sy), dist), cfg);
        if (done)
            vis *= 0.30f; // ghost: dim a completed objective
        if (vis <= 0.004f)
            continue;
        ms.push_back({dist, sx, sy, pos.y - fc.avatar.y, &s, (int)(vis * 255.f)});
    }

    std::sort(ms.begin(), ms.end(), [](const WM &a, const WM &b)
              { return a.dist > b.dist; }); // far -> near

    for (const WM &m : ms)
    {
        const float px = std::clamp(cfg.markerSize * pxScale / std::max(m.dist, 0.01f), kMarkerMinPx, kMarkerMaxPx);
        const float h = px * 0.5f;
        uint32_t assetId = 0;
        Objective::TryIconAssetId(m.s->Type, assetId);
        void *tex = assetId ? Tex::GetTextureFromAssetId(assetId) : nullptr;
        if (tex)
            dl->AddImage((ImTextureID)tex, ImVec2(m.sx - h, m.sy - h), ImVec2(m.sx + h, m.sy + h),
                         ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, m.alpha));
        else
        {
            // Fallback dot only while the gw2dat icon is still loading
            const float r = std::clamp(h, 5.f, 14.f);
            dl->AddCircleFilled(ImVec2(m.sx, m.sy), r, Objective::ColorOf(m.s->Type, (int)(m.alpha * 0.92f)), 20);
            dl->AddCircle(ImVec2(m.sx, m.sy), r, IM_COL32(255, 255, 255, (int)(m.alpha * 0.78f)), 20, 1.5f);
        }
        // Above/below: when the objective is on a different floor (its height differs from yours by > ~5 m), a
        // chevron just outside the marker pointing the way -- up if it's above you, down if below.
        if (cfg.worldAboveBelow && std::fabs(m.dy) > 5.f)
        {
            const bool up = m.dy > 0.f;
            Render::DrawGlyph(dl, ImVec2(m.sx, up ? m.sy - h - 7.f : m.sy + h + 7.f), 13.f,
                              up ? Render::Glyph::ArrowUp : Render::Glyph::ArrowDown, IM_COL32(255, 236, 170, m.alpha), {});
        }
    }
}

// In-world dungeon markers for the active route: the FvD icon sprite at each instruction/positional spot
// (textures/markers/dungeon/<icon>.png, tinted -- white icons stay as-is, info bubbles are tinted per
// path), faded with distance; the CURRENT instruction gets a brighter ring. Dungeon coords are world (no
// continent transform). Missing sprite -> a tinted dot. Gated by the "Dungeon markers" setting at the call site.
void Markers::DrawInstance(const FrameCtx &fc, const Config &cfg, const GuideState &st)
{
    const Math::Mat4 &vp = fc.vp;
    const float W = fc.W, H = fc.H;
    const Math::Vec3 &camPos = fc.camPos;
    const float pxScale = fc.pxScale;

    if (!st.inst || st.instRouteIdx >= (int)st.inst->Routes.size())
        return;
    const InstanceRoute &route = st.inst->Routes[st.instRouteIdx];
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const float fadeNear = cfg.markerFadeNear;
    const float fadeFar = std::max(cfg.markerFadeFar, fadeNear + 1.f);
    const float combatFade = MarkerFx::CombatFade(cfg.markerFadeInCombat, fc);
    const MarkerFx::Between between = MarkerFx::BeginBetween(cfg, fc);
    const InstanceMarker *curIns = st.instSteps.empty() ? nullptr
                                                        : st.instSteps[std::min(st.instStep, (int)st.instSteps.size() - 1)];

    // Collect + DEPTH-SORT far -> near (a CPU z-test); the CURRENT instruction always draws LAST so its emphasis
    // ring is never covered by a nearer marker.
    struct DM
    {
        float dist, sx, sy;
        const InstanceMarker *m;
        int alpha;
        bool isCur;
    };
    static std::vector<DM> ms;
    ms.clear();
    ms.reserve(route.Markers.size());

    for (const InstanceMarker &m : route.Markers)
    {
        const Math::Vec3 pos{m.X, m.Y + cfg.markerExtraH, m.Z}; // [x, height, z] world, directly
        const float dist = (pos - camPos).Length();
        if (dist > fadeFar)
            continue;
        float sx, sy, depth;
        if (!Math::WorldToScreen(pos, vp, W, H, sx, sy, depth))
            continue;
        const float fade = 1.f - std::clamp((dist - fadeNear) / (fadeFar - fadeNear), 0.f, 1.f);
        if (fade <= 0.01f)
            continue;
        const float vis = MarkerFx::OpacityCap(fade * combatFade * MarkerFx::BetweenDim(between, ImVec2(sx, sy), dist), cfg);
        if (vis <= 0.004f)
            continue;
        ms.push_back({dist, sx, sy, &m, (int)(vis * 255.f), &m == curIns});
    }

    std::sort(ms.begin(), ms.end(), [](const DM &a, const DM &b)
              {
                  if (a.isCur != b.isCur)
                      return b.isCur;     // the current instruction always draws last (on top)
                  return a.dist > b.dist; // otherwise far -> near
              });

    for (const DM &it : ms)
    {
        const float px = std::clamp(cfg.markerSize * pxScale / std::max(it.dist, 0.01f), kMarkerMinPx, kMarkerMaxPx);
        const float h = px * 0.5f;
        const ImU32 tcol = IM_COL32((it.m->Tint >> 24) & 0xFF, (it.m->Tint >> 16) & 0xFF, (it.m->Tint >> 8) & 0xFF, it.alpha);

        void *tex = nullptr;
        if (!it.m->Icon.empty())
            tex = Tex::GetBundledTexture(("data\\textures\\markers\\dungeon\\" + it.m->Icon + ".png").c_str());

        if (tex)
        {
            dl->AddImage((ImTextureID)tex, ImVec2(it.sx - h, it.sy - h), ImVec2(it.sx + h, it.sy + h),
                         ImVec2(0, 0), ImVec2(1, 1), tcol);
            if (it.isCur)
                dl->AddCircle(ImVec2(it.sx, it.sy), h + 3.f, IM_COL32(255, 255, 255, it.alpha), 24, 2.5f);
        }
        else // sprite still loading / missing: tinted fallback dot (current gets the brighter ring)
        {
            dl->AddCircleFilled(ImVec2(it.sx, it.sy), h, tcol, 20);
            dl->AddCircle(ImVec2(it.sx, it.sy), h, IM_COL32(255, 255, 255, it.isCur ? it.alpha : it.alpha / 2), 20, it.isCur ? 2.5f : 1.5f);
        }
    }
}
