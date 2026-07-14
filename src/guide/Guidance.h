#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "guide/Progress.h"
#include "guide/Travel.h"
#include "guide/TrailFollower.h"
#include "world/GpsArrow.h"  // GpsArrow + TargetOverrideKind (via app/State.h)
#include "util/Projection.h" // Math::Vec3
#include <functional>
#include <string>
#include <unordered_set>
#include <cstdint>

// -----------------------------------------------------------------------------------------------------
// Guidance: the open-world routing brain + 2D HUD arrow driver. Owns
// only the smoothed-speed state for the ETA caption. Update() picks the current objective, routes to it per
// the RouteMode (Direct/Trail/Hybrid via the follower), draws the arrow via GpsArrow, and hands any arrow-menu
// action back through `dispatch` (the caller runs CompleteStep/CopyWaypoint/OpenSettings, which live in
// entry.cpp). Auto-completion of auto-types uses the shared CompleteStep helper. The narrow refs it needs are
// passed in (Model 2); it never sees the whole App.
// -----------------------------------------------------------------------------------------------------
class Guidance
{
public:
    void Update(float W, float H, const Math::Vec3 &avatar, const Math::Vec3 &camFwd, float dt,
                Config &cfg, GuideState &st, bool &settingsDirty, ProgressStore &progress,
                Travel::Controller &travelCtl, Follow::TrailFollower &follower, GpsArrow &gpsArrow,
                TargetOverrideKind targetKind, const std::function<void(int)> &dispatch);

    // Choose the current objective: a clicked checklist row (until done) overrides; else, per RouteMode, the
    // CLOSEST unfinished objective (Nearest modes) or the first unfinished in authored order. Sets st.curStep.
    // Split out of Update so the caller can run it WHILE THE MAP IS OPEN (the arrow/trail are paused then, but
    // the guide panel + checklist keep rendering, so the current objective must keep advancing + re-ordering -
    // otherwise curStep freezes and the list reverts to authored order until the map closes). Camera-free: it
    // only needs the player position.
    void PickCurrentStep(const Follow::Vec2 &playerXz, const Config &cfg, GuideState &st, ProgressStore &progress);

    // Ground speed (m/s) the ETA divides by. While MOVING: the 2s-smoothed speed, floored at 1 m/s so a
    // just-starting / crawling pace can't blow up dist/speed. While STOPPED: 0, so the ETA reads "--" instead
    // of ballooning. "Moving" is a hysteretic flag off a fast 0.4s EMA (see Guidance.cpp), so the readout cuts
    // off promptly on stop -- the smooth divisor alone would decay too slowly and balloon first.
    float SpeedEma() const { return moving_ ? (speedEma_ > 1.0f ? speedEma_ : 1.0f) : 0.f; }

private:
    // Cached completion lookup so Update + PickCurrentStep don't run O(steps x list) linear IsComplete/IsConfirmed
    // scans every frame. Rebuilt only when the progress version or character changes (incl. mid-frame, after the
    // confirm/auto-complete loops mutate progress -- so a just-confirmed waypoint still advances correctly).
    void EnsureDoneSets(ProgressStore &progress, const std::string &character);
    std::unordered_set<std::string> doneSet_;      // completed OR confirmed (== StepIsDone for any step)
    std::unordered_set<std::string> confirmedSet_; // reached waypoints (the confirm-loop skip test)
    uint64_t doneVer_ = (uint64_t)-1;
    std::string doneChar_;

    Follow::Vec2 lastXz_;
    bool haveLastXz_ = false;
    float accumDist_ = 0.f; // path travelled since the last speed sample (m)
    float accumDt_ = 0.f;   // wall time since the last speed sample (s) -- sample every ~0.2s
    float speedEma_ = 0.f;  // smooth ETA divisor (2.0s time constant)
    float speedFast_ = 0.f; // fast stop sensor (0.4s time constant)
    bool moving_ = false;   // hysteretic moving flag (set >1.5 m/s, cleared <0.6 m/s)
};
