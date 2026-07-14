#include "world/CategoryMarkers.h"
#include "guide/FarmCategories.h" // Farm::CategoryMatch (shared Row-1 chip <-> node category mapping)
#include "util/Projection.h"      // Math::Vec3 / WorldToScreen
#include "util/Textures.h"        // Tex::GetBundledTexture / GetTextureFromAssetId
#include "world/MarkerFx.h"       // combat fade / global caps / between-camera-and-character
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    void *IconTex(const FarmNode &nd)
    {
        if (!nd.Icon.empty())
            return Tex::GetBundledTexture(("data\\textures\\markers\\gather\\" + nd.Icon + ".png").c_str());
        if (nd.IconAssetId)
            return Tex::GetTextureFromAssetId(nd.IconAssetId);
        return nullptr;
    }

    // Category-tinted fallback colour while the icon is loading / missing.
    ImU32 CatColor(const std::string &cat, int a)
    {
        if (cat == "ore" || cat == "special")
            return IM_COL32(176, 156, 120, a);
        if (cat == "wood")
            return IM_COL32(156, 116, 74, a);
        if (cat == "plant")
            return IM_COL32(118, 186, 96, a);
        if (cat == "seafood")
            return IM_COL32(96, 166, 196, a);
        if (cat == "farmroute" || cat == "misc")
            return IM_COL32(206, 176, 96, a);
        return IM_COL32(186, 186, 186, a);
    }
}

void CategoryMarkers::DrawWorld(const FrameCtx &fc, const Config &cfg, const GuideState &st)
{
    if (st.activeGather.empty())
        return;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const float fadeNear = cfg.markerFadeNear;
    const float fadeFar = std::max(cfg.markerFadeFar, fadeNear + 1.f);
    const float far2 = fadeFar * fadeFar;
    const float combatFade = MarkerFx::CombatFade(cfg.markerFadeInCombat, fc);
    const MarkerFx::Between between = MarkerFx::BeginBetween(cfg, fc);

    // Collect, then DEPTH-SORT far -> near so a nearer node covers a farther one (gather nodes cluster, so this
    // matters); a CPU z-test -- the ImGui draw list has no depth buffer.
    struct GM
    {
        float dist, sx, sy;
        const FarmNode *nd;
        int alpha;
    };
    static std::vector<GM> ms;
    ms.clear();
    ms.reserve(st.activeGather.size());

    for (const auto &g : st.activeGather)
    {
        const FarmNode *nd = g.Node;
        if (!nd || !cfg.GatherShown(st.activeGatherMapId, nd->Material))
            continue;
        if (!Farm::CategoryMatch(cfg.gatherCategory, nd->Category))
            continue;
        const Math::Vec3 pos{g.X, g.Y + cfg.markerExtraH, g.Z};
        const float dx = pos.x - fc.camPos.x, dy = pos.y - fc.camPos.y, dz = pos.z - fc.camPos.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > far2)
            continue; // squared-distance pre-cull (no sqrt for far nodes)
        float sx, sy, depth;
        if (!Math::WorldToScreen(pos, fc.vp, fc.W, fc.H, sx, sy, depth))
            continue;
        const float dist = std::sqrt(d2);
        const float fade = 1.f - std::clamp((dist - fadeNear) / (fadeFar - fadeNear), 0.f, 1.f);
        if (fade <= 0.01f)
            continue;
        const float vis = MarkerFx::OpacityCap(fade * combatFade * MarkerFx::BetweenDim(between, ImVec2(sx, sy), dist), cfg);
        if (vis <= 0.004f)
            continue;
        ms.push_back({dist, sx, sy, nd, (int)(vis * 255.f)});
    }

    std::sort(ms.begin(), ms.end(), [](const GM &a, const GM &b)
              { return a.dist > b.dist; }); // far -> near

    for (const GM &m : ms)
    {
        const float px = std::clamp(cfg.markerSize * fc.pxScale / std::max(m.dist, 0.01f), kMarkerMinPx, kMarkerMaxPx);
        const float h = px * 0.5f;
        if (void *tex = IconTex(*m.nd))
            dl->AddImage((ImTextureID)tex, ImVec2(m.sx - h, m.sy - h), ImVec2(m.sx + h, m.sy + h),
                         ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, m.alpha));
        else
        {
            dl->AddCircleFilled(ImVec2(m.sx, m.sy), h * 0.6f, CatColor(m.nd->Category, m.alpha), 16);
            dl->AddCircle(ImVec2(m.sx, m.sy), h * 0.6f, IM_COL32(255, 255, 255, m.alpha / 2), 16, 1.5f);
        }
    }
}

void CategoryMarkers::DrawMap(const Config &cfg, const GuideState &st,
                              const std::function<ImVec2(ImVec2)> &project, ImVec2 bMin, ImVec2 bSize)
{
    if (st.activeGather.empty())
        return;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const ImVec2 bMax(bMin.x + bSize.x, bMin.y + bSize.y);
    for (const auto &g : st.activeGather)
    {
        const FarmNode *nd = g.Node;
        if (!nd || !g.HasCont || !cfg.GatherShown(st.activeGatherMapId, nd->Material))
            continue;
        if (!Farm::CategoryMatch(cfg.gatherCategory, nd->Category))
            continue;
        const ImVec2 p = project(ImVec2(g.CX, g.CY));
        if (p.x < bMin.x || p.x > bMax.x || p.y < bMin.y || p.y > bMax.y)
            continue; // off-screen cull
        if (void *tex = IconTex(*nd))
        {
            const float r = 6.f;
            dl->AddImage((ImTextureID)tex, ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r));
        }
        else
            dl->AddCircleFilled(p, 3.5f, CatColor(nd->Category, 255), 12);
    }
}
