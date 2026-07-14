#include "world/PersonalMarker.h"
#include "world/Markers.h"   // kMarkerMinPx / kMarkerMaxPx
#include "util/Draw.h"       // DrawPersonalPin
#include "util/Coords.h"          // Coords::RectDistance / ContinentToWorldXZ
#include <imgui.h>
#include <algorithm>

void PersonalMarker::Draw(const FrameCtx& fc, const Config& cfg, const GuideState& st,
                          Travel::Controller& travel, Follow::TrailFollower& follower)
{
    const Math::Mat4& vp = fc.vp; const float W = fc.W, H = fc.H; const Math::Vec3& camPos = fc.camPos; const float pxScale = fc.pxScale;

    float pcx, pcy;
    if (!st.zone.HasRects || !travel.HasPersonalAt(pcx, pcy)) return;
    if (Coords::RectDistance(pcx, pcy, st.zone.ContRect) > 0.f) return;   // target is in another zone -> no in-world pos

    float wx, wz;
    Coords::ContinentToWorldXZ(pcx, pcy, st.zone.ContRect, st.zone.MapRect, wx, wz);
    const int nTrail = (int)st.zone.Trail.size();
    float ground = camPos.y;
    if (nTrail > 0)
    {
        const int ni = follower.NearestTrailIndex({ wx, wz });        // best ground height for a free-floating click
        if (ni >= 0 && ni < nTrail) ground = st.zone.Trail[ni].y;
    }
    const Math::Vec3 pos{ wx, ground + cfg.markerExtraH, wz };

    float sx, sy, depth;
    if (!Math::WorldToScreen(pos, vp, W, H, sx, sy, depth)) return;     // behind the camera / off-screen

    float px = cfg.markerSize * pxScale / std::max((pos - camPos).Length(), 0.01f);
    px = std::clamp(px, kMarkerMinPx, kMarkerMaxPx) * 1.15f;            // a touch larger - it's your destination
    const float h = px * 0.5f;

    DrawPersonalPin(ImGui::GetBackgroundDrawList(), ImVec2(sx, sy), h);   // shared with the map pin -> they match
}
