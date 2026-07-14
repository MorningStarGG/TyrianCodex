#include "guide/InstanceGuidance.h"
#include "guide/CurrentChar.h"   // UpdateCurrentChar
#include "guide/GuidanceSnapshot.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

// Localize tuning. kWindow is the continuity search half-window in trail INDICES around last frame's anchor.
// The localizer projects onto trail SEGMENTS, not vertices, so steep/vertical trail runs stay sticky between
// recorded points. A nearby parallel strand is allowed to steal the anchor only after the local projection is
// genuinely far for more than one frame, or when the player is plainly relocated far away from the local strand.
static constexpr int kWindow = 150;
static constexpr float kStayLocal = 22.f;
static constexpr float kHardLost = 45.f;
static constexpr float kSwitchRatio = 0.45f;
static constexpr float kSwitchMargin = 12.f;
static constexpr int kSwitchFrames = 2;

namespace
{
    struct ProjectionHit
    {
        int Seg = 0;
        float T = 0.f;
        float DistSq = FLT_MAX;
        bool Valid = false;

        int AnchorIndex() const
        {
            return Seg + (T >= 0.5f ? 1 : 0);
        }
    };

    ProjectionHit ProjectPointRange(const std::vector<Math::Vec3>& trail, const Math::Vec3& avatar, int lo, int hi)
    {
        const int n = (int)trail.size();
        ProjectionHit best;
        if (n <= 0)
            return best;
        lo = std::clamp(lo, 0, n - 1);
        hi = std::clamp(hi, lo, n - 1);
        for (int i = lo; i <= hi; ++i)
        {
            const float dx = trail[i].x - avatar.x;
            const float dy = trail[i].y - avatar.y;
            const float dz = trail[i].z - avatar.z;
            const float d = dx * dx + dy * dy + dz * dz;
            if (d < best.DistSq)
            {
                best.Seg = i;
                best.T = 0.f;
                best.DistSq = d;
                best.Valid = true;
            }
        }
        return best;
    }

    ProjectionHit ProjectSegmentRange(const std::vector<Math::Vec3>& trail, const Math::Vec3& avatar, int firstSeg, int lastSeg)
    {
        const int n = (int)trail.size();
        if (n < 2)
            return ProjectPointRange(trail, avatar, 0, n - 1);

        firstSeg = std::clamp(firstSeg, 0, n - 2);
        lastSeg = std::clamp(lastSeg, firstSeg, n - 2);

        ProjectionHit best;
        for (int i = firstSeg; i <= lastSeg; ++i)
        {
            const Math::Vec3& a = trail[i];
            const Math::Vec3& b = trail[i + 1];
            const float abx = b.x - a.x;
            const float aby = b.y - a.y;
            const float abz = b.z - a.z;
            const float lenSq = abx * abx + aby * aby + abz * abz;
            const float pax = avatar.x - a.x;
            const float pay = avatar.y - a.y;
            const float paz = avatar.z - a.z;
            const float t = lenSq > 1e-6f ? std::clamp((pax * abx + pay * aby + paz * abz) / lenSq, 0.f, 1.f) : 0.f;
            const float qx = a.x + abx * t;
            const float qy = a.y + aby * t;
            const float qz = a.z + abz * t;
            const float dx = qx - avatar.x;
            const float dy = qy - avatar.y;
            const float dz = qz - avatar.z;
            const float d = dx * dx + dy * dy + dz * dz;
            if (d < best.DistSq)
            {
                best.Seg = i;
                best.T = t;
                best.DistSq = d;
                best.Valid = true;
            }
        }
        return best;
    }

    ProjectionHit ProjectAllSections(const GuideState& st, const Math::Vec3& avatar)
    {
        const std::vector<Math::Vec3>& trail = st.zone.Trail;
        const int n = (int)trail.size();
        ProjectionHit best;
        if (n <= 0)
            return best;
        if (n == 1)
            return ProjectPointRange(trail, avatar, 0, 0);

        auto consider = [&](const ProjectionHit& hit)
        {
            if (hit.Valid && hit.DistSq < best.DistSq)
                best = hit;
        };

        if (st.instTrailSections.empty())
        {
            consider(ProjectSegmentRange(trail, avatar, 0, n - 2));
            return best;
        }

        for (const TrailSectionRange& s : st.instTrailSections)
        {
            const int lo = std::clamp(s.Start, 0, n - 1);
            const int hi = std::clamp(s.End, lo, n - 1);
            if (hi > lo)
                consider(ProjectSegmentRange(trail, avatar, lo, hi - 1));
            else
                consider(ProjectPointRange(trail, avatar, lo, hi));
        }
        return best;
    }
}

void InstanceGuidance::TrailSectionBounds(const GuideState& st, int index, int& lo, int& hi)
{
    const int n = (int)st.zone.Trail.size();
    lo = 0;
    hi = std::max(0, n - 1);
    if (n <= 0 || st.instTrailSections.empty())
        return;
    for (const TrailSectionRange& s : st.instTrailSections)
        if (index >= s.Start && index <= s.End)
        {
            lo = std::clamp(s.Start, 0, n - 1);
            hi = std::clamp(s.End, lo, n - 1);
            return;
        }
}

int InstanceGuidance::Localize(GuideState& st, const Math::Vec3& avatar)
{
    const std::vector<Math::Vec3>& T = st.zone.Trail;
    const int n = (int)T.size();
    if (n == 0)
    {
        st.instTrailReacquireFrames = 0;
        return -1;
    }

    const ProjectionHit global = ProjectAllSections(st, avatar);
    if (!global.Valid)
    {
        st.instTrailReacquireFrames = 0;
        return 0;
    }

    const int prev = st.instTrailAnchor;
    if (prev < 0 || prev >= n)
    {
        st.instTrailReacquireFrames = 0;
        return std::clamp(global.AnchorIndex(), 0, n - 1); // first frame / new route: global acquire
    }

    int secLo, secHi;
    TrailSectionBounds(st, prev, secLo, secHi);
    const int firstSeg = std::max(secLo, prev - kWindow);
    const int lastSeg = std::min(secHi - 1, prev + kWindow);
    ProjectionHit local = (secHi > secLo)
        ? ProjectSegmentRange(T, avatar, firstSeg, lastSeg)
        : ProjectPointRange(T, avatar, secLo, secHi);
    if (!local.Valid)
        local = ProjectPointRange(T, avatar, secLo, secHi);

    const float localDist = std::sqrt(local.DistSq);
    const float globalDist = std::sqrt(global.DistSq);
    const int localAnchor = std::clamp(local.AnchorIndex(), secLo, secHi);

    if (localDist <= kStayLocal)
    {
        st.instTrailReacquireFrames = 0;
        return localAnchor;
    }

    ++st.instTrailReacquireFrames;
    const bool clearlyBetter = globalDist < localDist * kSwitchRatio && (localDist - globalDist) >= kSwitchMargin;
    const bool hardRelocated = localDist >= kHardLost && globalDist <= kStayLocal;
    if ((hardRelocated || (st.instTrailReacquireFrames >= kSwitchFrames && clearlyBetter)))
    {
        st.instTrailReacquireFrames = 0;
        return std::clamp(global.AnchorIndex(), 0, n - 1);
    }

    return localAnchor;
}

// -- Dungeon spot-to-spot guidance: advance past reached instruction spots, aim along the trail to the
// current one (or the route end), draw the arrow with a "Step N / M" caption. Stateless (reads GuideState's
// instStep/instSteps); no progress or travel. --
void InstanceGuidance::Update(float W, float H, const Math::Vec3& avatar, const Math::Vec3& camFwd,
                              Config& cfg, GuideState& st, bool& settingsDirty, Follow::TrailFollower& follower,
                              GpsArrow& gpsArrow, float speedEma, TargetOverrideKind targetKind, const std::function<void(int)>& dispatch)
{
    GuidanceSnapshotUtil::Clear(st);
    if (!st.inst || st.zone.Trail.size() < 2) return;
    UpdateCurrentChar(st);
    const Follow::Vec2 playerXz{ avatar.x, avatar.z };
    const int nTrail = (int)st.zone.Trail.size();

    // Advance past instruction spots you've reached (manual: never past the last one).
    while (st.instStep < (int)st.instSteps.size() - 1)
    {
        const InstanceMarker* c = st.instSteps[st.instStep];
        if (Follow::Dist(playerXz, { c->X, c->Z }) <= cfg.arrival) st.instStep++;
        else break;
    }

    // Aim along the trail toward the current instruction (or the route end if no instructions).
    Follow::Vec2 target; int ti;
    const InstanceMarker* cur = st.instSteps.empty() ? nullptr
        : st.instSteps[std::min(st.instStep, (int)st.instSteps.size() - 1)];
    if (cur) { target = { cur->X, cur->Z }; ti = (cur->TrailIndex >= 0 && cur->TrailIndex < nTrail) ? cur->TrailIndex : follower.NearestTrailIndex(target); }
    else     { const Math::Vec3& last = st.zone.Trail.back(); target = { last.x, last.z }; ti = nTrail - 1; }

    // Aim from the shared per-frame anchor (st.instTrailAnchor, set by Localize in entry.cpp this frame) --
    // NOT the follower's monotonic _trailProgress, whose global re-acquire after a mid-route reset (addon
    // re-enable / checkpoint respawn) could latch a coincident re-crossing strand and spin the arrow.
    const Follow::Vec2 aim = follower.ComputeTrailAimFrom(playerXz, st.instTrailAnchor, ti, target);
    const float travel = Follow::Dist(playerXz, target);
    st.curTravel  = travel;
    st.curArrived = (travel <= cfg.arrival);
    st.offRoute   = false;

    char cap[48];
    const int total = (int)st.instSteps.size();
    if (total > 0) std::snprintf(cap, sizeof(cap), "Step %d / %d", std::min(st.instStep + 1, total), total);
    else           std::snprintf(cap, sizeof(cap), "Follow the route");
    GuidanceSnapshotUtil::Set(st, aim, playerXz, camFwd, st.curArrived, travel, cap,
                              /*dungeon*/ true, /*travel*/ false, speedEma);
    dispatch(gpsArrow.Draw(aim, playerXz, camFwd, st.curArrived, travel, cap, W, H, /*dungeon*/ true,
                           cfg, st, settingsDirty, speedEma, targetKind));
}
