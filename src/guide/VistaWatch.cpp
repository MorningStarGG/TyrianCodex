#include "guide/VistaWatch.h"
#include "guide/Completion.h"   // CompleteStep
#include "app/App.h"
#include "util/Coords.h"        // ContinentToWorldXZ
#include <algorithm>
#include <string>

namespace
{
    inline float Dist2(float ax, float az, float bx, float bz)
    {
        const float dx = ax - bx, dz = az - bz;
        return dx * dx + dz * dz;
    }
}

void VistaWatch::Update(App& app, const Math::Vec3& avatar, uint32_t mapId, bool isGameplay)
{
    VistaWatchState& w = app.state.vistaWatch;

    // Edge tracking: capture last frame's gameplay, then store this frame's (always, so active<->inactive
    // transitions don't fabricate a blip).
    const bool prev = w.have ? w.prev : isGameplay;
    w.prev = isGameplay;
    w.have = true;

    // Only meaningful in the open-world zone we have data for (no vistas in dungeons / off-coverage maps).
    const GuideState& st = app.state;
    const bool active = !st.inDungeon && st.zone.Loaded && st.zone.HasRects && mapId == st.zone.MapId;
    if (!active) { w.armedStepId.clear(); return; }

    // Build the per-map vista position cache ONCE on a map change -> the hot path below is just a short loop
    // over this zone's few vistas (no per-frame step scan / coordinate transforms).
    if (!w.cacheBuilt || w.cachedMapId != mapId)
    {
        w.vistas.clear();
        for (const Step& s : st.zone.Steps)
        {
            if (s.Type != "vista") continue;
            VistaWatchState::CachedVista cv; cv.stepId = s.StepId;
            Coords::ContinentToWorldXZ(s.CX, s.CY, st.zone.ContRect, st.zone.MapRect, cv.wx, cv.wz);
            w.vistas.push_back(std::move(cv));
        }
        w.cachedMapId = mapId; w.cacheBuilt = true;
    }
    if (w.vistas.empty()) { w.armedStepId.clear(); return; }   // no vistas on this map -> nothing to watch

    // Vistas are a close interaction; keep the arm radius tight so a far-off cutscene can't arm one.
    const float armR  = std::min(app.config.arrival, 14.f);
    const float armR2 = armR * armR;

    // Blip end: gameplay just returned (false->true) with a vista armed, on the SAME map, and you're STILL at
    // the vista. A vista cinematic returns you to the same spot; a waypoint/loading teleport lands you else-
    // where (or on another map) -> fails one of these guards -> no completion.
    if (!prev && isGameplay && !w.armedStepId.empty() && mapId == w.armedMapId)
    {
        if (app.config.autoAdvance && Dist2(avatar.x, avatar.z, w.armedX, w.armedZ) <= armR2)
            CompleteStep(app.progress, app.state, w.armedStepId);   // idempotent; the guide advances next frame
        w.armedStepId.clear();
    }

    // Re-arm (gameplay frames only -- the link is frozen during the blip, so the value armed on the last
    // gameplay frame before the cinematic carries through it): the nearest incomplete vista in reach. Loops
    // only the cached vistas, so off-vista frames cost ~nothing (a few distance checks, usually 0 in range).
    if (isGameplay)
    {
        std::string best; float bestD2 = armR2, bx = 0.f, bz = 0.f;
        for (const VistaWatchState::CachedVista& v : w.vistas)
        {
            if (app.progress.IsComplete(st.currentChar, v.stepId)) continue;
            const float d2 = Dist2(avatar.x, avatar.z, v.wx, v.wz);
            if (d2 <= bestD2) { bestD2 = d2; best = v.stepId; bx = v.wx; bz = v.wz; }
        }
        w.armedStepId = best;   // empty when none in reach -> a later blip elsewhere completes nothing
        w.armedMapId  = mapId;
        w.armedX = bx; w.armedZ = bz;
    }
}
