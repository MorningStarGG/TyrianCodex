#include "guide/Guidance.h"
#include "guide/RouteMode.h"   // ModeIs*/FollowsRibbon
#include "guide/Completion.h"  // CompleteStep, IsAutoType
#include "guide/CurrentChar.h" // UpdateCurrentChar
#include "guide/GuidanceSnapshot.h"
#include "guide/StepStatus.h"
#include "util/Coords.h"          // ContinentToWorldXZ, RectDistance, ClampToRect
#include "model/ObjectiveTypes.h" // Objective::TryIconAssetId (the type icon on the arrow / datatext / widget)
#include "util/Textures.h"        // Tex::GetTextureFromAssetId
#include "Shared.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

// -- Open-world guidance + 2D HUD compass arrow: Auto-completes
// auto-types on proximity, picks the current objective (clicked row, else first incomplete / nearest per
// mode), routes to it (Direct/Trail/Hybrid via the follower), draws the arrow (returning a menu action the
// caller dispatches) and keeps the smoothed speed for the ETA caption. --
void Guidance::Update(float W, float H, const Math::Vec3 &avatar, const Math::Vec3 &camFwd, float dt,
                      Config &cfg, GuideState &st, bool &settingsDirty, ProgressStore &progress,
                      Travel::Controller &travelCtl, Follow::TrailFollower &follower, GpsArrow &gpsArrow,
                      TargetOverrideKind targetKind, const std::function<void(int)> &dispatch)
{
    GuidanceSnapshotUtil::Clear(st);
    if (st.zone.Steps.empty() || !st.zone.HasRects)
        return;
    UpdateCurrentChar(st);
    EnsureDoneSets(progress, st.currentChar);
    const Follow::Vec2 playerXz{avatar.x, avatar.z};
    const int nTrail = (int)st.zone.Trail.size();

    // Ground speed for the ETA. MumbleLink delivers AvatarPosition in game-tick BURSTS, so across the addon's
    // faster render frames the displacement is 0,0,0,jump,0,0,0,jump; dividing EACH render frame's displacement
    // by that frame's dt produced a 0,0,0,spike pattern whose EMA wobbled and made the ETA readout DANCE
    // (31<->30) at each second boundary before dropping. Instead accumulate distance + time and take ONE speed
    // sample per ~0.2s of wall time -- the burst averages out within the window -> a steady AND correct speed
    // that rises when you slow, falls when you speed up, and goes to 0 ("--") when you stop. SMOOTH 2.0s EMA =
    // the ETA divisor; FAST 0.4s EMA = the hysteretic "moving" flag. (Teleports + frame hitches discard the
    // partial window so neither poisons the average.)
    const float frameDisp = haveLastXz_ ? Follow::Dist(playerXz, lastXz_) : 0.f;
    lastXz_ = playerXz;
    haveLastXz_ = true;
    if (dt > 0.f && dt < 0.5f && frameDisp < 30.f)
    {
        accumDist_ += frameDisp;
        accumDt_ += dt;
        if (accumDt_ >= 0.2f)
        {
            const float inst = accumDist_ / accumDt_; // average speed over the window (m/s)
            speedEma_ += (inst - speedEma_) * (1.f - std::exp(-accumDt_ / 2.0f));
            speedFast_ += (inst - speedFast_) * (1.f - std::exp(-accumDt_ / 0.4f));
            if (speedFast_ > 1.5f)
                moving_ = true;
            else if (speedFast_ < 0.6f)
                moving_ = false; // hysteresis band -> no on/off chatter at walking pace
            accumDist_ = 0.f;
            accumDt_ = 0.f;
        }
    }
    else
    {
        accumDist_ = 0.f;
        accumDt_ = 0.f;
    } // teleport / hitch -> discard the partial window

    // A POINT objective (POI/waypoint) counts as "reached" at the SAME radius the blur ring is drawn -- the game's
    // generous unlock range -- NOT the tighter Config.arrival, which made a POI you'd already unlocked in-game wait
    // until you walked ~5 m further in. Convert the ring's continent radius to metres for THIS map (ContinentToWorldXZ:
    // world_inches = cont * mapSpan/contSpan; metres = inches / 39.3701). Kept as a FLOOR under cfg.arrival, so a
    // larger Arrival-distance setting still widens it and a degenerate rect falls back to the setting.
    const float contSpan_ = st.zone.ContRect[2] - st.zone.ContRect[0];
    const float mapSpan_ = st.zone.MapRect[2] - st.zone.MapRect[0];
    const float mPerCont_ = (contSpan_ != 0.f) ? (mapSpan_ / contSpan_) / 39.3701f : 0.f;
    auto reachM = [&](const std::string &type)
    {
        // Complete just INSIDE the visible ring: the ring's geometric radius reads a hair outside its PAINTED edge,
        // so firing at the raw radius advanced the step ~1-2 m too soon -- trim a small margin to land on the ring
        // you actually see.
        const float ring = Objective::ReachContinent(type) * mPerCont_ - 2.0f;
        return ring > cfg.arrival ? ring : cfg.arrival;
    };

    // Vertical arrival gate. Continent coords carry NO height, so an objective a floor above/below you passed the
    // horizontal reach test and auto-advanced while the game hadn't registered it. Use the objective's height ON
    // THE ROUTE -- its trail point's Y (Dataset.h: TrailIndex already sources a marker's height this way) -- and
    // require the player within ~a floor of it. No / out-of-range trail index -> horizontal-only (never blocks).
    const float kVertReachM = 6.0f; // metres; raise if it blocks a real arrival, lower if a floor still slips through
    auto vReached = [&](const Step &s)
    {
        if (s.TrailIndex < 0 || s.TrailIndex >= nTrail)
            return true; // no route height -> don't gate vertically
        return std::abs(avatar.y - st.zone.Trail[s.TrailIndex].y) <= kVertReachM;
    };

    // Physically reaching a WAYPOINT unlocks it -> confirm it for travel, ALWAYS (even if its checklist
    // step is already ticked or auto-advance is off; reset/undo never un-confirm). This is the travel
    // system's "known/unlocked" source.
    for (const Step &s : st.zone.Steps)
    {
        if (s.Type != "waypoint" || confirmedSet_.count(s.StepId))
            continue;
        float wx, wz;
        Coords::ContinentToWorldXZ(s.CX, s.CY, st.zone.ContRect, st.zone.MapRect, wx, wz);
        if (Follow::Dist(playerXz, {wx, wz}) <= reachM(s.Type) && vReached(s))
            progress.SetConfirmed(st.currentChar, s.StepId, true);
    }
    EnsureDoneSets(progress, st.currentChar); // refresh after the confirm loop so a just-confirmed waypoint is skipped below

    // Proximity auto-check-off: visiting completes POIs / waypoints (only when "Auto-complete on arrival" is
    // on; off = every objective stays current until you tick it). Hearts/hero points/vistas are NOT auto -
    // they need an in-world interact, so they wait for a manual tick even when in range.
    if (cfg.autoAdvance)
        for (const Step &s : st.zone.Steps)
        {
            if (!IsAutoType(s.Type) || doneSet_.count(s.StepId))
                continue;
            float wx, wz;
            Coords::ContinentToWorldXZ(s.CX, s.CY, st.zone.ContRect, st.zone.MapRect, wx, wz);
            if (Follow::Dist(playerXz, {wx, wz}) <= reachM(s.Type) && vReached(s))
                CompleteStep(progress, st, s.StepId);
        }

    // Smart-travel override: a personal target / "Travel here" destination steers the arrow STRAIGHT to the
    // spot (in this zone), or toward the way-in (another zone), replacing the current-objective aim. Runs
    // after the confirm/auto-complete loops above (so waypoints still confirm en route).
    // The panel renders its own Travel branch + checklist.
    if (st.viewerMode == ViewerMode::Travel)
    {
        float acx, acy;
        uint32_t aMapId;
        if (travelCtl.GetAnchor(acx, acy, aMapId))
        {
            const bool sameZone = (aMapId == st.zone.MapId) || (Coords::RectDistance(acx, acy, st.zone.ContRect) <= 0.f);
            float tx, tz;
            if (sameZone)
                Coords::ContinentToWorldXZ(acx, acy, st.zone.ContRect, st.zone.MapRect, tx, tz); // straight to the spot
            else
            {
                // Aim at the way-in: the real entry if we're its from-zone, else the entry clamped to this zone.
                const Travel::Entry e = travelCtl.GetZoneEntry(aMapId);
                float ecx = acx, ecy = acy;
                if (e.valid)
                {
                    if (st.zone.MapId == e.fromMapId)
                    {
                        ecx = e.ex;
                        ecy = e.ey;
                    }
                    else
                        Coords::ClampToRect(e.ex, e.ey, st.zone.ContRect, ecx, ecy);
                }
                else
                    Coords::ClampToRect(acx, acy, st.zone.ContRect, ecx, ecy);
                Coords::ContinentToWorldXZ(ecx, ecy, st.zone.ContRect, st.zone.MapRect, tx, tz);
            }
            const Follow::Vec2 tgtXz{tx, tz};
            const float tdist = Follow::Dist(playerXz, tgtXz);
            // Travel targets are NOT on the leveling route, so aim STRAIGHT at the spot (same zone) or the way-in
            // border (another zone). Following the zone's objective trail here pointed the arrow BACKWARD onto the
            // route first instead of toward the target.
            const Follow::Vec2 taim = tgtXz;
            st.curTravel = tdist;
            st.curArrived = tdist <= cfg.arrival;
            st.offRoute = false;
            st.curStep = -1;
            const std::string cap = travelCtl.IsPersonal() ? std::string("Personal target")
                                                           : ("Travel: " + travelCtl.GoalZoneName());
            GuidanceSnapshotUtil::Set(st, taim, playerXz, camFwd, st.curArrived, tdist, cap.c_str(),
                                      /*dungeon*/ false, /*travel*/ true, SpeedEma());
            dispatch(gpsArrow.Draw(taim, playerXz, camFwd, st.curArrived, tdist, cap.c_str(), W, H, /*dungeon*/ false,
                                   cfg, st, settingsDirty, SpeedEma(), targetKind));
            return;
        }
    }

    // Pick the current objective (shared with the map-open path in entry.cpp, so the panel keeps advancing +
    // re-ordering while the fullscreen map is up - where the arrow/trail below are skipped).
    PickCurrentStep(playerXz, cfg, st, progress);
    const int cur = st.curStep;
    if (cur < 0)
    {
        st.curArrived = false;
        GuidanceSnapshotUtil::Clear(st);
        return;
    } // zone complete

    const Step &c = st.zone.Steps[cur];
    float wx, wz;
    Coords::ContinentToWorldXZ(c.CX, c.CY, st.zone.ContRect, st.zone.MapRect, wx, wz);
    const Follow::Vec2 curXz{wx, wz};
    const float straight = Follow::Dist(playerXz, curXz);

    // Route to the objective per the selected RouteMode (ComputeRoute in GuideModule.Trail.cs): Direct =
    // straight line; Trail = follow the whole ribbon; Hybrid = head to the trail entrance, then follow in.
    // Also sets st.offRoute (strayed from the path you should be on) for the warning recolor.
    Follow::Vec2 aim;
    float travel;
    const bool haveTrail = follower.HasTrail() && c.TrailIndex >= 0 && c.TrailIndex < nTrail;
    st.offRoute = false;
    if (ModeIsDirect(cfg.routeMode) || !haveTrail)
    {
        aim = curXz;
        travel = straight;
    }
    else if (ModeFollowsRibbon(cfg.routeMode))
    {
        aim = follower.ComputeTrailAim(playerXz, c.TrailIndex, curXz);
        travel = follower.AlongTrailDistance(playerXz, curXz, c.TrailIndex);
        const int ni = follower.NearestTrailIndex(playerXz);
        if (ni >= 0 && ni < nTrail)
            st.offRoute = Follow::Dist(playerXz, {st.zone.Trail[ni].x, st.zone.Trail[ni].z}) > 15.f;
    }
    else // Hybrid
    {
        if (follower.HybridOnApproach(playerXz, c.TrailIndex, cfg.approachMeters))
        {
            aim = follower.ComputeTrailAim(playerXz, c.TrailIndex, curXz);
            travel = follower.AlongTrailDistance(playerXz, curXz, c.TrailIndex);
            const int start = follower.IndexBackAlongTrail(c.TrailIndex, (float)cfg.approachMeters);
            const int nearI = follower.NearestTrailIndexInRange(playerXz, start, c.TrailIndex);
            if (nearI >= 0 && nearI < nTrail)
                st.offRoute = Follow::Dist(playerXz, {st.zone.Trail[nearI].x, st.zone.Trail[nearI].z}) > 15.f;
        }
        else
        {
            // Not on the approach corridor yet -> head to the approach point (the trail entrance approachMeters
            // back from the objective), then HybridOnApproach (above) latches to the trail. Aiming straight at
            // the objective is DIRECT mode's job; hybrid routes via the entrance even when the objective is
            // spatially nearer. Two refinements: (1) SMOOTH -- interpolate the entrance along the segment rather
            // than snapping to a vertex; (2) DYNAMIC -- never aim BEHIND your progress: if you've already moved
            // past the static entrance along the corridor, aim at your current trail point instead of backtracking.
            const int staticEntrance = follower.IndexBackAlongTrail(c.TrailIndex, (float)cfg.approachMeters);
            int eSeg;
            float eT;
            const Follow::Vec2 approachPt = follower.PointBackAlongTrail(c.TrailIndex, (float)cfg.approachMeters, eSeg, eT);
            const int prog = follower.NearestTrailIndexInRange(playerXz, staticEntrance, c.TrailIndex);
            int entranceIdx;
            Follow::Vec2 entXz;
            if (prog > staticEntrance)
            {
                entranceIdx = prog;
                entXz = {st.zone.Trail[prog].x, st.zone.Trail[prog].z};
            }
            else
            {
                entranceIdx = staticEntrance;
                entXz = approachPt;
            }
            const float toEntrance = Follow::Dist(playerXz, entXz);
            const Follow::Vec2 tiXz{st.zone.Trail[c.TrailIndex].x, st.zone.Trail[c.TrailIndex].z};
            travel = toEntrance + follower.TrailArc(entranceIdx, c.TrailIndex) + Follow::Dist(tiXz, curXz);
            aim = entXz;
        }
    }
    st.curTravel = travel;
    const bool arrived = straight <= cfg.arrival; // arrival uses straight-line proximity
    st.curArrived = arrived;

    // Hybrid (en route): the DIRECT objective bearing is available -- stash it in the snapshot so EVERY arrow
    // surface (in-world arrow, route-arrow widget, Info Panel datatext) can show the direct-objective glow,
    // independent of where the arrow points (the trail entrance). Off in Direct/Trail and once arrived; each
    // surface decides whether to DRAW it via its own toggle.
    const bool hybridDirect = ModeIsHybrid(cfg.routeMode) && !arrived;
    const Follow::Vec2 *snapDirect = hybridDirect ? &curXz : nullptr;
    uint32_t objIcon = 0;
    Objective::TryIconAssetId(c.Type, objIcon); // the objective type icon (heart/POI/...)
    void *iconTex = objIcon ? Tex::GetTextureFromAssetId(objIcon) : nullptr;
    GuidanceSnapshotUtil::Set(st, aim, playerXz, camFwd, arrived, travel,
                              c.Name.empty() ? c.Type.c_str() : c.Name.c_str(),
                              /*dungeon*/ false, /*travel*/ false, SpeedEma(), snapDirect, iconTex);
    const Follow::Vec2 *directPtr = (cfg.showDirectIndicator && hybridDirect) ? &curXz : nullptr; // in-world arrow's own toggle
    dispatch(gpsArrow.Draw(aim, playerXz, camFwd, arrived, travel,
                           c.Name.empty() ? c.Type.c_str() : c.Name.c_str(), W, H, /*dungeon*/ false,
                           cfg, st, settingsDirty, SpeedEma(), targetKind, directPtr, iconTex));
}

void Guidance::PickCurrentStep(const Follow::Vec2 &playerXz, const Config &cfg, GuideState &st, ProgressStore &progress)
{
    EnsureDoneSets(progress, st.currentChar); // fresh here too: this also runs from the map-open path in entry.cpp
    // A clicked checklist row overrides auto-advance until it's done; else the first incomplete in order.
    int cur = -1;
    if (!st.manualTarget.empty())
    {
        for (size_t i = 0; i < st.zone.Steps.size(); ++i)
            if (st.zone.Steps[i].StepId == st.manualTarget)
            {
                if (!doneSet_.count(st.zone.Steps[i].StepId))
                    cur = (int)i;
                break;
            }
        if (cur < 0)
            st.manualTarget.clear(); // completed / gone -> resume auto-advance
    }
    if (cur < 0)
    {
        if (ModeIsNearest(cfg.routeMode)) // Hybrid/Direct Nearest: the closest unfinished objective (live)
        {
            float best = 1e30f;
            for (size_t i = 0; i < st.zone.Steps.size(); ++i)
            {
                const Step &s = st.zone.Steps[i];
                if (doneSet_.count(s.StepId))
                    continue;
                float wx, wz;
                Coords::ContinentToWorldXZ(s.CX, s.CY, st.zone.ContRect, st.zone.MapRect, wx, wz);
                const float d = Follow::Dist(playerXz, {wx, wz});
                if (d < best)
                {
                    best = d;
                    cur = (int)i;
                }
            }
        }
        else // Trail/Hybrid/Direct Trail: first unfinished in authored order
            for (size_t i = 0; i < st.zone.Steps.size(); ++i)
                if (!doneSet_.count(st.zone.Steps[i].StepId))
                {
                    cur = (int)i;
                    break;
                }
    }
    st.curStep = cur;
}

void Guidance::EnsureDoneSets(ProgressStore &progress, const std::string &character)
{
    if (doneVer_ == progress.Version() && doneChar_ == character)
        return;
    const std::vector<std::string> &comp = progress.CompletedIds(character);
    const std::vector<std::string> &conf = progress.ConfirmedIds(character);
    doneSet_.clear();
    confirmedSet_.clear();
    doneSet_.reserve(comp.size() + conf.size());
    doneSet_.insert(comp.begin(), comp.end());
    doneSet_.insert(conf.begin(), conf.end());
    confirmedSet_.reserve(conf.size());
    confirmedSet_.insert(conf.begin(), conf.end());
    doneVer_ = progress.Version();
    doneChar_ = character;
}
