#include "guide/TrailFollower.h"

#include <algorithm>
#include <limits>

using namespace Follow;

// Pure-pursuit: project the player onto the route polyline, then aim a fixed ARC distance ahead along
// it. Because the aim is measured forward from the player's own projection, dir = aim - player can never
// reverse (no 180 flip), independent of the (uneven) downsample spacing.
Vec2 TrailFollower::ComputeTrailAim(Vec2 playerXz, int targetIndex, Vec2 targetXz)
{
    const int n = (int)_trail.size();
    if (targetIndex < 0 || targetIndex >= n)
        targetIndex = n - 1;
    if (_trailProgress > targetIndex)
        _trailProgress = NearestTrailIndexUpTo(playerXz, targetIndex);
    if (_trailProgress >= targetIndex)
        return targetXz; // already at the objective's trail point

    int seg;
    float t;
    ProjectOntoTrail(playerXz, _trailProgress, TrailWindowArc, targetIndex, seg, t);
    float off = Dist(playerXz, TrailPointAt(seg, t));

    // Teleport / lost the window: re-project globally up to the objective.
    if (off > TrailResetDist)
    {
        ProjectOntoTrail(playerXz, 0, std::numeric_limits<float>::max(), targetIndex, seg, t);
        off = Dist(playerXz, TrailPointAt(seg, t));
    }
    _trailProgress = seg; // monotonic forward

    // Strayed off-route (but not a teleport): steer back to the nearest on-route point.
    if (off > TrailReturnDist)
        return TrailPointAt(seg, t);

    bool reached;
    Vec2 aim = LookAheadPoint(seg, t, TrailLookahead, targetIndex, reached);
    return reached ? targetXz : aim;
}

// Pure-pursuit aim from a caller-supplied anchor (see the header). The projection window starts just behind
// the anchor and runs a short arc forward, so it only ever sees the anchor's own strand -- strand selection
// is entirely the anchor's job (InstanceGuidance::Localize), never re-decided here.
Vec2 TrailFollower::ComputeTrailAimFrom(Vec2 playerXz, int anchorIndex, int targetIndex, Vec2 targetXz) const
{
    const int n = (int)_trail.size();
    if (n < 2)
        return targetXz;
    if (targetIndex < 0 || targetIndex >= n)
        targetIndex = n - 1;
    if (anchorIndex < 0)
        anchorIndex = 0;
    if (anchorIndex >= targetIndex)
        return targetXz; // at or past the objective's trail point -- aim straight at it

    int seg;
    float t;
    ProjectOntoTrail(playerXz, anchorIndex > 0 ? anchorIndex - 1 : 0, TrailLookahead * 2.f, targetIndex, seg, t);
    const Vec2 onTrail = TrailPointAt(seg, t);
    if (Dist(playerXz, onTrail) > TrailReturnDist)
        return onTrail; // strayed off-route: steer back to the nearest point of OUR strand (never a far one)

    bool reached;
    const Vec2 aim = LookAheadPoint(seg, t, TrailLookahead, targetIndex, reached);
    return reached ? targetXz : aim;
}

Vec2 TrailFollower::TrailPointAt(int seg, float t) const
{
    if (seg >= (int)_trail.size() - 1)
        return _trail.back();
    return _trail[seg] + (_trail[seg + 1] - _trail[seg]) * t;
}

void TrailFollower::ProjectOntoTrail(Vec2 p, int fromIndex, float windowArc, int maxIndex, int &seg, float &t) const
{
    if (fromIndex < 0)
        fromIndex = 0;
    if (maxIndex >= (int)_trail.size())
        maxIndex = (int)_trail.size() - 1;
    seg = fromIndex;
    t = 0.f;
    float bestSq = std::numeric_limits<float>::max(), arc = 0.f;
    for (int i = fromIndex; i < maxIndex; ++i)
    {
        Vec2 a = _trail[i], b = _trail[i + 1], ab = b - a;
        float len2 = ab.LenSq();
        float ti = len2 < 1e-6f ? 0.f : std::clamp((p - a).Dot(ab) / len2, 0.f, 1.f);
        float dd = DistSq(p, a + ab * ti);
        if (dd < bestSq)
        {
            bestSq = dd;
            seg = i;
            t = ti;
        }
        arc += std::sqrt(len2);
        if (arc >= windowArc)
            break; // (the first segment is always tested before this check)
    }
}

Vec2 TrailFollower::LookAheadPoint(int seg, float t, float dist, int maxIndex, bool &reachedTarget) const
{
    if (maxIndex >= (int)_trail.size())
        maxIndex = (int)_trail.size() - 1;
    float remaining = dist, frac = t;
    for (int i = seg; i < maxIndex; ++i)
    {
        Vec2 a = _trail[i], b = _trail[i + 1];
        float segLen = Dist(a, b);
        float fromHere = segLen * (1.f - frac);
        if (fromHere >= remaining)
        {
            float newFrac = frac + remaining / std::max(segLen, 1e-6f);
            reachedTarget = false;
            return a + (b - a) * newFrac;
        }
        remaining -= fromHere;
        frac = 0.f;
    }
    reachedTarget = true;
    return _trail[maxIndex];
}

int TrailFollower::NearestTrailIndex(Vec2 p) const
{
    int best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (int i = 0; i < (int)_trail.size(); ++i)
    {
        float d = DistSq(p, _trail[i]);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

int TrailFollower::NearestTrailIndexUpTo(Vec2 p, int maxIndex) const
{
    if (maxIndex >= (int)_trail.size())
        maxIndex = (int)_trail.size() - 1;
    int best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (int i = 0; i <= maxIndex; ++i)
    {
        float d = DistSq(p, _trail[i]);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

float TrailFollower::AlongTrailDistance(Vec2 playerXz, Vec2 targetXz, int trailIndex) const
{
    if (_trail.empty() || trailIndex < 0 || trailIndex >= (int)_trail.size())
        return Dist(playerXz, targetXz);

    int targetIndex = trailIndex;
    int playerIndex = NearestTrailIndexUpTo(playerXz, targetIndex);
    float arc = Dist(playerXz, _trail[playerIndex]); // player -> trail
    for (int i = playerIndex; i < targetIndex; ++i)
        arc += Dist(_trail[i], _trail[i + 1]);  // along the trail
    arc += Dist(_trail[targetIndex], targetXz); // trail -> objective
    return arc;
}

int TrailFollower::IndexBackAlongTrail(int fromIndex, float meters) const
{
    if (fromIndex >= (int)_trail.size())
        fromIndex = (int)_trail.size() - 1;
    float acc = 0.f;
    int i = fromIndex;
    while (i > 0 && acc < meters)
    {
        acc += Dist(_trail[i], _trail[i - 1]);
        --i;
    }
    return i;
}

Vec2 TrailFollower::PointBackAlongTrail(int fromIndex, float meters, int &outSeg, float &outT) const
{
    const int n = (int)_trail.size();
    outSeg = 0;
    outT = 0.f;
    if (n == 0)
        return Vec2{};
    if (fromIndex >= n)
        fromIndex = n - 1;
    if (fromIndex <= 0 || meters <= 0.f)
    {
        outSeg = (fromIndex > 0) ? fromIndex - 1 : 0;
        outT = (fromIndex > 0) ? 1.f : 0.f;
        return _trail[fromIndex < 0 ? 0 : fromIndex];
    }

    float acc = 0.f;
    for (int i = fromIndex; i > 0; --i)
    {
        const float segLen = Dist(_trail[i], _trail[i - 1]);
        if (acc + segLen >= meters)
        {
            const float f = segLen > 1e-6f ? (meters - acc) / segLen : 0.f; // 0 at vertex i -> 1 at vertex i-1
            outSeg = i - 1;
            outT = 1.f - f; // == TrailPointAt(i-1, 1-f)
            return _trail[i] + (_trail[i - 1] - _trail[i]) * f;
        }
        acc += segLen;
    }
    outSeg = 0;
    outT = 0.f;
    return _trail[0]; // ran off the start of the trail
}

int TrailFollower::NearestTrailIndexInRange(Vec2 p, int lo, int hi) const
{
    if (lo < 0)
        lo = 0;
    if (hi >= (int)_trail.size())
        hi = (int)_trail.size() - 1;
    int best = lo;
    float bestD = std::numeric_limits<float>::max();
    for (int i = lo; i <= hi; ++i)
    {
        float d = DistSq(p, _trail[i]);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

bool TrailFollower::OnApproach(Vec2 playerXz, int targetIndex, int approachMeters) const
{
    if (_trail.empty() || targetIndex < 0 || targetIndex >= (int)_trail.size())
        return false;
    int approachStart = IndexBackAlongTrail(targetIndex, (float)approachMeters);
    int nearest = NearestTrailIndexInRange(playerXz, approachStart, targetIndex);
    return Dist(playerXz, _trail[nearest]) <= TrailJoinDist;
}

bool TrailFollower::HybridOnApproach(Vec2 playerXz, int targetIndex, int approachMeters)
{
    if (_trail.empty() || targetIndex < 0 || targetIndex >= (int)_trail.size())
    {
        _onTrail = false;
        return false;
    }
    int approachStart = IndexBackAlongTrail(targetIndex, (float)approachMeters);
    int nearest = NearestTrailIndexInRange(playerXz, approachStart, targetIndex);
    float d = Dist(playerXz, _trail[nearest]);
    _onTrail = d <= (_onTrail ? TrailLeaveDist : TrailJoinDist);
    return _onTrail;
}

float TrailFollower::TrailArc(int lo, int hi) const
{
    if (lo < 0)
        lo = 0;
    if (hi >= (int)_trail.size())
        hi = (int)_trail.size() - 1;
    float a = 0.f;
    for (int i = lo; i < hi; ++i)
        a += Dist(_trail[i], _trail[i + 1]);
    return a;
}
