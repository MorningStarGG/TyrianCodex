#include "world/FishMarkers.h"
#include "guide/FishHoles.h"      // FishHoles::Color (per-kind tint)
#include "render/glyphs/Glyphs.h" // Render::DrawGlyph + Glyph::Fish
#include "util/Projection.h"      // Math::Vec3 / WorldToScreen
#include "world/MarkerFx.h"       // combat fade / global caps / between-camera-and-character
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // is this hole kind lit (in the watchlist, or the watchlist is empty so everything is lit)?
    bool Watched(const std::set<std::string> &w, const std::string &t) { return w.empty() || w.count(t) != 0; }
}

void FishMarkers::DrawWorld(const FrameCtx &fc, const Config &cfg, const GuideState &st)
{
    if (st.activeFish.empty())
        return;
    const std::set<std::string> &watched = st.fishWatchTypes;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const float fadeNear = cfg.markerFadeNear;
    const float fadeFar = std::max(cfg.markerFadeFar, fadeNear + 1.f);
    const float far2 = fadeFar * fadeFar;
    const float combatFade = MarkerFx::CombatFade(cfg.markerFadeInCombat, fc);
    const MarkerFx::Between between = MarkerFx::BeginBetween(cfg, fc);

    // Collect, then DEPTH-SORT far -> near so a nearer hole covers a farther one (a CPU z-test; no depth buffer).
    struct FM
    {
        float dist, sx, sy;
        const FishHole *h;
        int alpha;
    };
    static std::vector<FM> ms;
    ms.clear();
    ms.reserve(st.activeFish.size());

    for (const auto &g : st.activeFish)
    {
        const FishHole *h = g.Hole;
        if (!h)
            continue;
        const Math::Vec3 pos{g.X, g.Y + cfg.markerExtraH, g.Z};
        const float dx = pos.x - fc.camPos.x, dy = pos.y - fc.camPos.y, dz = pos.z - fc.camPos.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > far2)
            continue;
        float sx, sy, depth;
        if (!Math::WorldToScreen(pos, fc.vp, fc.W, fc.H, sx, sy, depth))
            continue;
        const float dist = std::sqrt(d2);
        float fade = 1.f - std::clamp((dist - fadeNear) / (fadeFar - fadeNear), 0.f, 1.f);
        if (!Watched(watched, h->Type))
            fade *= 0.35f; // a tracked-fish run dims the off-type holes
        if (fade <= 0.01f)
            continue;
        fade = MarkerFx::OpacityCap(fade * combatFade * MarkerFx::BetweenDim(between, ImVec2(sx, sy), dist), cfg);
        if (fade <= 0.004f)
            continue;
        ms.push_back({dist, sx, sy, h, (int)(fade * 255.f)});
    }

    std::sort(ms.begin(), ms.end(), [](const FM &a, const FM &b)
              { return a.dist > b.dist; }); // far -> near

    for (const FM &m : ms)
    {
        const float px = std::clamp(cfg.markerSize * fc.pxScale / std::max(m.dist, 0.01f), kMarkerMinPx, kMarkerMaxPx);
        const ImU32 col = FishHoles::Color(m.h->Type, m.alpha);
        dl->AddCircleFilled(ImVec2(m.sx, m.sy), px * 0.55f, IM_COL32(18, 26, 38, (int)(m.alpha * 0.55f)), 16); // water backing
        Render::DrawGlyph(dl, ImVec2(m.sx, m.sy), px, Render::Glyph::Fish, col, Render::GlyphStyle{});
    }
}

void FishMarkers::DrawMap(const Config &cfg, const GuideState &st,
                          const std::function<ImVec2(ImVec2)> &project, ImVec2 bMin, ImVec2 bSize)
{
    if (st.activeFish.empty())
        return;
    const std::set<std::string> &watched = st.fishWatchTypes;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const ImVec2 bMax(bMin.x + bSize.x, bMin.y + bSize.y);
    for (const auto &g : st.activeFish)
    {
        const FishHole *h = g.Hole;
        if (!h || !g.HasCont)
            continue;
        const ImVec2 p = project(ImVec2(g.CX, g.CY));
        if (p.x < bMin.x || p.x > bMax.x || p.y < bMin.y || p.y > bMax.y)
            continue; // off-screen cull
        const int a = Watched(watched, h->Type) ? 255 : 90;
        dl->AddCircleFilled(p, 3.5f, FishHoles::Color(h->Type, a), 12);
        dl->AddCircle(p, 3.5f, IM_COL32(255, 255, 255, a / 2), 12, 1.f);
    }
}
