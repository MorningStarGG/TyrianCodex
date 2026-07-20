#include "world/TrailRenderer.h"
#include "util/Draw.h"      // TrailColorFor
#include "util/Textures.h"  // Tex::GetBundledTexture
#include "ui/Effect.h"      // Fx::DrawTrail3D + Fx::TrailVert (the GPU ribbon pass)
#include "world/MarkerFx.h" // combat fade + global far cap (shared with the markers)
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

static const float kTrailLift = 0.05f; // metres off the ground          (RouteTrail.TrailLift)
static const float kPassedDim = 0.35f; // already-passed prints dim to this (RouteTrail.PassedDim)

// The ribbon is now a real GPU pass: we build WORLD-space geometry here (a triangle list -- two triangles per
// trail segment, with per-vertex colour/alpha baked in) and hand it to Fx::DrawTrail3D, whose vertex shader
// transforms it with the camera matrix. The GPU then does projection, near-plane + frustum CLIPPING (so the
// trail never snap-vanishes in chunks or stretches off-screen behind you) and PERSPECTIVE-CORRECT texturing
// (no foreshorten "swim" on flat/water stretches that ImGui's 2D draw list couldn't avoid).
void TrailRenderer::Draw(const FrameCtx &fc, const Config &cfg, const GuideState &st, float visLo, int visHi)
{
    const int floorVisLo = (int)std::floor(visLo);     // first visible segment is (floorVisLo -> floorVisLo+1)
    const float visLoFrac = visLo - (float)floorVisLo; // where along it the corridor actually starts (smooth)
    const Math::Vec3 &avatar = fc.avatar;

    const std::vector<Math::Vec3> &trail = st.zone.Trail;
    const int n = (int)trail.size();
    onScreen_ = 0;
    if (n < 2 || !cfg.showTrail)
        return; // "Show route trail" toggle
    const std::vector<TrailSectionRange> &sections = st.inDungeon ? st.instTrailSections : st.zone.TrailSections;
    auto segmentAllowed = [&](int seg)
    {
        if (seg < 0 || seg + 1 >= n)
            return false;
        if (sections.empty())
            return true;
        for (const TrailSectionRange &s : sections)
            if (seg >= s.Start && seg + 1 <= s.End)
                return true;
        return false;
    };

    // Nearest trail point to the player (horizontal) = the progress split: points before it are "passed"
    // (dimmed), points from it on are "ahead" (full) -- so the way forward reads brighter.
    float best = 1e18f;
    int progIdx = 0;
    for (int i = 0; i < n; ++i)
    {
        const float dx = trail[i].x - avatar.x, dz = trail[i].z - avatar.z; // (x,z) horizontal
        const float d = dx * dx + dz * dz;
        if (d < best)
        {
            best = d;
            progIdx = i;
            nearest_ = trail[i];
        }
    }
    nearestDist_ = std::sqrt(best);

    // Instance routes: split at the SHARED per-frame anchor (the strand the player is actually on; set by
    // InstanceGuidance::Localize just before this call) instead of the globally-nearest vertex -- a self-
    // crossing walkthrough can put a vertex from ANOTHER part of the route nearest in 2D, which would wrongly
    // dim the whole visible corridor. Open-world zones keep the global nearest (unchanged behavior).
    if (st.inDungeon && st.instTrailAnchor >= 0 && st.instTrailAnchor < n)
        progIdx = st.instTrailAnchor;

    // Trail STYLE -> the tiled ribbon texture (Footprints + Dashed + Line are Lady Elyssa's baked art; arrows/dots
    // are white templates -- all multiplied by the trail colour, mapped u across width, v along length).
    static const char *const kStylePaths[] = {
        "data\\textures\\markers\\trails\\Footprints.png",
        "data\\textures\\markers\\trails\\arrows.png",
        "data\\textures\\markers\\trails\\dashed.png",
        "data\\textures\\markers\\trails\\line.png",
        "data\\textures\\markers\\trails\\dots.png",
    };
    const int styleIdx = std::clamp(cfg.trailStyle, 0, (int)(sizeof(kStylePaths) / sizeof(kStylePaths[0])) - 1);
    void *footTex = Tex::GetBundledTexture(kStylePaths[styleIdx]);
    if (!footTex)
        return; // texture not ready yet (loads from disk in a frame or two)

    const float half = cfg.trailWidth * 0.5f;
    const float repeat = cfg.trailWidth * 1.5f; // RouteTrail.RepeatMeters
    const float span = std::max(cfg.fadeGone - cfg.fadeFull, 1.f);
    const float combatFade = MarkerFx::CombatFade(cfg.trailFadeInCombat, fc); // low wash while in combat
    // Animated flow: scroll the texture along the trail's length over time (0 = static).
    const float vScroll = (cfg.trailAnimSpeed > 0.f) ? (float)ImGui::GetTime() * cfg.trailAnimSpeed : 0.f;

    // Off-route recolour or the trail colour, with the per-vertex alpha baked into the tint.
    auto colorAt = [&](float a) -> ImU32
    {
        const int ab = (int)(std::clamp(a, 0.f, 1.f) * 255.f);
        return (st.offRoute && cfg.trailOffRoute) ? IM_COL32(255, 90, 60, ab) : TrailColorFor(cfg.trailColor, ab);
    };

    // Build the WORLD-space ribbon (triangle list). Static so the buffer is reused frame to frame (no churn).
    static std::vector<Fx::TrailVert> verts;
    verts.clear();
    verts.reserve((size_t)n * 6);
    auto emit = [&](const Math::Vec3 &w, float u, float v, ImU32 col)
    {
        verts.push_back(Fx::TrailVert{w.x, w.y, w.z, u, v, col});
    };

    bool havePrev = false;
    Math::Vec3 pL{}, pR{};
    float pV = 0.f, pAlpha = 0.f;
    float cumDist = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const Math::Vec3 &p = trail[i];
        const Math::Vec3 &prev = trail[i > 0 ? i - 1 : 0];
        const Math::Vec3 &next = trail[i < n - 1 ? i + 1 : n - 1];

        // Horizontal tangent (x,z), averaged across the adjacent segments for smooth miters.
        float tx = next.x - prev.x, tz = next.z - prev.z;
        float tl = std::sqrt(tx * tx + tz * tz);
        if (tl < 1e-6f)
        {
            tx = 1.f;
            tz = 0.f;
            tl = 1.f;
        }
        tx /= tl;
        tz /= tl;
        const float perpx = tz * half, perpz = -tx * half; // perpendicular in the horizontal plane, half-width

        const bool continuesSection = i > 0 && segmentAllowed(i - 1);
        if (continuesSection)
        {
            const float dx = trail[i].x - trail[i - 1].x, dz = trail[i].z - trail[i - 1].z;
            cumDist += std::sqrt(dx * dx + dz * dz);
        }
        else if (i > 0)
            cumDist = 0.f;
        const float v = -cumDist / repeat + vScroll; // negate so the prints' toes face forward
        const float by = p.y + kTrailLift;           // lift along the height axis (y)
        const Math::Vec3 Lw{p.x + perpx, by, p.z + perpz};
        const Math::Vec3 Rw{p.x - perpx, by, p.z - perpz};

        // Per-point alpha: horizontal distance fade from the player, times the passed/ahead dim, times opacity.
        const float dxp = p.x - avatar.x, dzp = p.z - avatar.z;
        const float distP = std::sqrt(dxp * dxp + dzp * dzp);
        const float fade = 1.f - std::clamp((distP - cfg.fadeFull) / span, 0.f, 1.f);
        const float ahead = (!cfg.trailBrighten || i >= progIdx) ? 1.f : kPassedDim; // "Brighten trail ahead"
        const float alpha = fade * ahead * cfg.trailOpacity * combatFade;
        const ImU32 col = colorAt(alpha);

        // Emit the segment (i-1 -> i) only when point i is inside the corridor [visLo, visHi].
        if (havePrev && continuesSection && i > floorVisLo && i <= visHi)
        {
            Math::Vec3 sL = pL, sR = pR;
            float sV = pV;
            ImU32 sCol = colorAt(pAlpha);
            if (i - 1 == floorVisLo && visLoFrac > 0.001f) // smooth corridor start -- interpolate in WORLD space
            {
                sL = pL + (Lw - pL) * visLoFrac;
                sR = pR + (Rw - pR) * visLoFrac;
                sV = pV + (v - pV) * visLoFrac;
                sCol = colorAt(pAlpha + (alpha - pAlpha) * visLoFrac);
            }
            // Quad sL(0,sV) sR(1,sV) Lw(0,v) Rw(1,v) as two triangles (rasterizer cull is none, winding free).
            emit(sL, 0.f, sV, sCol);
            emit(sR, 1.f, sV, sCol);
            emit(Lw, 0.f, v, col);
            emit(Lw, 0.f, v, col);
            emit(sR, 1.f, sV, sCol);
            emit(Rw, 1.f, v, col);
        }

        pL = Lw;
        pR = Rw;
        pV = v;
        pAlpha = alpha;
        havePrev = true;
    }

    // FadeCenter ("Fade Trails Around Character"): the trail pixel shader fades a soft hole around the
    // player ON SCREEN (front AND back) so the ribbon never paints over your character; off in first person.
    // Centre the hole on the TRAIL (the avatar's feet/ground), NOT the chest: with the camera angled down the
    // ground trail enters the circle at its BOTTOM edge, so a chest-centred hole hid the forward trail (which runs
    // UP-screen, deep into the circle) far more than the trail behind (which runs down, straight out the bottom).
    float pSX = 0.f, pSY = 0.f, pDepth = 0.f;
    const bool charOn = Math::WorldToScreen(Math::Vec3{avatar.x, avatar.y, avatar.z}, fc.vp, fc.W, fc.H, pSX, pSY, pDepth);
    const float cdx = avatar.x - fc.camPos.x, cdy = avatar.y - fc.camPos.y, cdz = avatar.z - fc.camPos.z;
    const float camDist = std::sqrt(cdx * cdx + cdy * cdy + cdz * cdz);
    // The hole is a SCREEN-space radius, but the avatar's on-screen size grows as you zoom in (~1/camDist), so a
    // fixed radius covers them zoomed out yet is too small up close (the trail bleeds over the character). Floor the
    // radius at the avatar's PROJECTED size -- a ~1.1 m world offset at the feet, which auto-scales with zoom +
    // FOV -- so the hole always covers them; the user setting still sets the feather when zoomed out. Capped at 0.6 H.
    float fadeRadPx = std::clamp(cfg.trailFadeAroundRadius, 0.05f, 0.40f) * fc.H;
    if (charOn)
    {
        float px = -(avatar.z - fc.camPos.z), pz = (avatar.x - fc.camPos.x); // horizontal, perpendicular to the view ray
        const float pl = std::sqrt(px * px + pz * pz);
        if (pl > 1e-4f)
        {
            px /= pl;
            pz /= pl;
            float eSX, eSY, eD;
            const Math::Vec3 edge{avatar.x + px * 1.1f, avatar.y, avatar.z + pz * 1.1f};
            if (Math::WorldToScreen(edge, fc.vp, fc.W, fc.H, eSX, eSY, eD))
            {
                const float projR = std::sqrt((eSX - pSX) * (eSX - pSX) + (eSY - pSY) * (eSY - pSY));
                fadeRadPx = std::min(std::max(fadeRadPx, projR), 0.6f * fc.H);
            }
        }
    }
    const bool fadeOn = cfg.trailFadeAroundChar && charOn && camDist > 1.5f;

    if (!verts.empty())
        Fx::DrawTrail3D(ImGui::GetBackgroundDrawList(), verts.data(), (int)verts.size(), &fc.vp.m[0][0], footTex,
                        pSX, pSY, fadeRadPx, fadeOn);
    onScreen_ = (int)(verts.size() / 6); // segments emitted (rough diagnostic)
}
