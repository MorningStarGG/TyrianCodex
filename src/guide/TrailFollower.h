#pragma once
#include <vector>
#include <cmath>
#include <utility>

// Pure-pursuit route follower. Owns the 2D follow polyline (horizontal world metres)
// plus the player's monotonic progress along it and the approach-corridor hysteresis, and answers every trail-geometry query.
// No game/render deps. Coordinate note: follow plane is its Z-up (X,Y) = (mapX, mapY). In raw MumbleLink that same
// horizontal plane is (x, z), so callers build the polyline + player point as (worldX, worldZ).
namespace Follow
{
    struct Vec2
    {
        float x = 0, y = 0;
        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}
        Vec2 operator+(const Vec2 &o) const { return {x + o.x, y + o.y}; }
        Vec2 operator-(const Vec2 &o) const { return {x - o.x, y - o.y}; }
        Vec2 operator*(float s) const { return {x * s, y * s}; }
        float Dot(const Vec2 &o) const { return x * o.x + y * o.y; }
        float LenSq() const { return x * x + y * y; }
        float Len() const { return std::sqrt(LenSq()); }
    };
    inline float Dist(const Vec2 &a, const Vec2 &b) { return (a - b).Len(); }
    inline float DistSq(const Vec2 &a, const Vec2 &b) { return (a - b).LenSq(); }

    class TrailFollower
    {
    public:
        void SetTrail(std::vector<Vec2> trail)
        {
            _trail = std::move(trail);
            _sections.clear();
            NormalizeSections();
        } // does NOT reset progress
        void SetTrail(std::vector<Vec2> trail, std::vector<std::pair<int, int>> sections)
        {
            _trail = std::move(trail);
            _sections = std::move(sections);
            NormalizeSections();
        } // does NOT reset progress
        void Reset()
        {
            _trailProgress = 0;
            _onTrail = false;
        }
        bool HasTrail() const { return _trail.size() >= 2; }
        int Size() const { return (int)_trail.size(); }
        int Progress() const { return _trailProgress; }
        bool OnTrail() const { return _onTrail; }

        Vec2 ComputeTrailAim(Vec2 playerXz, int targetIndex, Vec2 targetXz);
        // ComputeTrailAim with a CALLER-SUPPLIED localization (the trail vertex the player is on) instead of
        // this class's monotonic _trailProgress. Const -- reads no progress state, does no global re-acquire:
        // the anchor is authoritative, so the aim can only land on the anchor's own strand (the projection
        // window is index-local). Instance routes use this so the arrow aims from the SAME anchor that clips
        // the ground ribbon (InstanceGuidance::Localize) -- on a walkthrough that re-crosses its own ground,
        // the monotonic tracker could latch a coincident strand after a mid-route reset; the anchor cannot.
        Vec2 ComputeTrailAimFrom(Vec2 playerXz, int anchorIndex, int targetIndex, Vec2 targetXz) const;
        Vec2 TrailPointAt(int seg, float t) const;
        void ProjectOntoTrail(Vec2 p, int fromIndex, float windowArc, int maxIndex, int &seg, float &t) const;
        Vec2 LookAheadPoint(int seg, float t, float dist, int maxIndex, bool &reachedTarget) const;
        int NearestTrailIndex(Vec2 p) const;
        int NearestTrailIndexUpTo(Vec2 p, int maxIndex) const;
        float AlongTrailDistance(Vec2 playerXz, Vec2 targetXz, int trailIndex) const;
        int IndexBackAlongTrail(int fromIndex, float meters) const;
        // The point exactly `meters` of arc back from `fromIndex`, INTERPOLATED along the segment (not snapped
        // to a vertex like IndexBackAlongTrail), plus its segment + fraction (TrailPointAt convention) so the
        // ribbon can start mid-segment. Smooths the approach point as the approach distance changes.
        Vec2 PointBackAlongTrail(int fromIndex, float meters, int &outSeg, float &outT) const;
        int NearestTrailIndexInRange(Vec2 p, int lo, int hi) const;
        bool OnApproach(Vec2 playerXz, int targetIndex, int approachMeters) const;
        bool HybridOnApproach(Vec2 playerXz, int targetIndex, int approachMeters);
        float TrailArc(int lo, int hi) const;

    private:
        // Trail-following tuning (matches TrailFollower.cs).
        static constexpr float TrailLookahead = 15.f;  // aim this far ahead ALONG the trail (m)
        static constexpr float TrailWindowArc = 120.f; // forward arc the player is projected onto
        static constexpr float TrailResetDist = 40.f;  // re-acquire globally if the window projection misses
        static constexpr float TrailReturnDist = 15.f; // beyond this off the trail, steer back onto it
        static constexpr float TrailJoinDist = 20.f;   // within this of the approach corridor -> snap on
        static constexpr float TrailLeaveDist = 35.f;  // release only past this (hysteresis deadband)

        std::vector<Vec2> _trail;
        std::vector<std::pair<int, int>> _sections;
        int _trailProgress = 0;
        bool _onTrail = false;

        void NormalizeSections();
        std::pair<int, int> SectionBoundsForIndex(int index) const;
    };
}
