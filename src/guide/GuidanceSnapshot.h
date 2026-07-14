#pragma once
#include "app/State.h"
#include "guide/TrailFollower.h"
#include "util/Projection.h"
#include <imgui.h>
#include <cmath>

namespace GuidanceSnapshotUtil
{
    // directAim (optional): the objective's straight-line world XZ (Hybrid). When given, the snapshot also
    // carries the DIRECT bearing so the snapshot-driven surfaces (route-arrow widget, Info Panel datatext arrow)
    // can draw the direct-objective glow -- independent of the arrow, which points at the trail entrance.
    inline void Set(GuideState& st, const Follow::Vec2& aim, const Follow::Vec2& playerXz,
                    const Math::Vec3& camFwd, bool arrived, float distance, const char* caption,
                    bool dungeon, bool travel, float speed, const Follow::Vec2* directAim = nullptr,
                    void* iconTex = nullptr)
    {
        const Follow::Vec2 dir = aim - playerXz;
        const Follow::Vec2 fwd{ camFwd.x, camFwd.z };
        float align = 1.f, cross = 0.f;
        if (dir.LenSq() > 1e-4f && fwd.LenSq() > 1e-4f)
        {
            const float dl = dir.Len(), fl = fwd.Len();
            const Follow::Vec2 dN{ dir.x / dl, dir.y / dl };
            const Follow::Vec2 fN{ fwd.x / fl, fwd.y / fl };
            align = dN.Dot(fN);
            cross = fN.x * dN.y - fN.y * dN.x;
        }

        GuidanceSnapshot& g = st.guidanceSnapshot;
        g.active = true;
        g.arrived = arrived;
        g.offRoute = st.offRoute;
        g.dungeon = dungeon;
        g.travel = travel;
        g.distance = distance;
        g.speed = speed;
        g.targetRot = -std::atan2(cross, align);
        g.align = align;

        // Direct-objective bearing -- the same yaw-only compass math for the objective point.
        g.hasDirect = false;
        if (directAim && fwd.LenSq() > 1e-4f)
        {
            const Follow::Vec2 dd = *directAim - playerXz;
            if (dd.LenSq() > 1e-4f)
            {
                const float ddl = dd.Len(), fl2 = fwd.Len();
                const Follow::Vec2 dN2{ dd.x / ddl, dd.y / ddl }, fN2{ fwd.x / fl2, fwd.y / fl2 };
                const float a2 = dN2.Dot(fN2), c2 = fN2.x * dN2.y - fN2.y * dN2.x;
                g.directRot = -std::atan2(c2, a2);
                g.hasDirect = true;
            }
        }

        g.caption = caption ? caption : "";
        g.iconTex = iconTex;   // the objective/material TYPE icon (null on callers that set none -> no stale icon)
        g.updatedAt = ImGui::GetTime();
    }

    inline void Clear(GuideState& st)
    {
        st.guidanceSnapshot.active = false;
    }
}
