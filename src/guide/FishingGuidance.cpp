#include "guide/FishingGuidance.h"
#include "guide/FishHoles.h"
#include "app/App.h"
#include "ui/GuideViewer.h"     // CurrentTargetOverride
#include "ui/QuickMenu.h"       // QuickMenu::ArrowDispatch
#include "guide/GuidanceSnapshot.h"
#include "guide/TrailFollower.h"  // Follow::Vec2 / Dist
#include "ui/dashboard/Notify.h"  // run-complete toast
#include "Shared.h"             // MumbleLink (player pos for NearestHoles)
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <set>
#include <string>
#include <utility>

void FishingGuidance::Update(App& app, float W, float H, const Math::Vec3& avatar, const Math::Vec3& camFwd)
{
    GuideState& st  = app.state;
    Config&     cfg = app.config;
    GuidanceSnapshotUtil::Clear(st);
    if (st.activeContent != ContentType::Fishing || !st.fishMap) return;

    const std::set<std::string>& watched = st.fishWatchTypes;   // computed once/frame in entry.cpp (empty => any hole)
    const Follow::Vec2 player{ avatar.x, avatar.z };

    // Nearest not-yet-visited hole matching the watchlist (crossed off on arrival; session-only). Recomputed every
    // frame so tracking a new fish re-routes live. Tally total/visited to announce completion once.
    float best = FLT_MAX; const GuideState::ActiveFishHole* bn = nullptr;
    int total = 0, got = 0;
    for (const auto& g : st.activeFish)
    {
        if (!g.Hole) continue;
        if (!watched.empty() && !watched.count(g.Hole->Type)) continue;
        ++total;
        if (st.fishVisited.count(g.Hole)) { ++got; continue; }
        const float dx = g.X - avatar.x, dz = g.Z - avatar.z;
        const float d = dx * dx + dz * dz;
        if (d < best) { best = d; bn = &g; }
    }
    const bool complete = total > 0 && got >= total;
    if (complete && !st.fishRunComplete)
        Notify::Push(Notify::Kind::Action, "Fishing run complete",
                     "Reached every matching fishing hole - reset to go again, or track a different fish.");
    st.fishRunComplete = complete;
    if (!bn) return;   // nothing matches / all visited this session -> arrow idle

    const Follow::Vec2 target{ bn->X, bn->Z };
    if (Follow::Dist(player, target) <= cfg.arrival) st.fishVisited.insert(bn->Hole);   // visited -> next frame picks next

    char cap[80];
    std::snprintf(cap, sizeof(cap), "%s hole", FishHoles::Pretty(bn->Hole->Type));

    const float travel = Follow::Dist(player, target);
    st.curTravel = travel; st.curArrived = (travel <= cfg.arrival); st.offRoute = false;
    const float speed = app.guidance.SpeedEma();
    const TargetOverrideKind tk = CurrentTargetOverride(app);

    GuidanceSnapshotUtil::Set(st, target, player, camFwd, st.curArrived, travel, cap, /*dungeon*/ false, /*travel*/ false, speed);
    const int act = app.gpsArrow.Draw(target, player, camFwd, st.curArrived, travel, cap, W, H, /*dungeon*/ false,
                                      cfg, st, app.settingsDirty, speed, tk);
    if (act >= 0) QuickMenu::ArrowDispatch(app, act);
}

bool FishingGuidance::Active(const App& app)
{
    return app.state.activeContent == ContentType::Fishing && app.state.fishMap != nullptr;
}

std::vector<const FishHole*> FishingGuidance::NearestHoles(const App& app, int maxN)
{
    std::vector<const FishHole*> out;
    const GuideState& st = app.state;
    if (!st.fishMap || st.activeFish.empty() || maxN <= 0 || !MumbleLink) return out;
    const std::set<std::string>& watched = st.fishWatchTypes;
    const float ax = MumbleLink->AvatarPosition.X, az = MumbleLink->AvatarPosition.Z;
    std::vector<std::pair<float, const FishHole*>> cand;
    cand.reserve(st.activeFish.size());
    for (const auto& g : st.activeFish)
    {
        const FishHole* h = g.Hole;
        if (!h || st.fishVisited.count(h)) continue;
        if (!watched.empty() && !watched.count(h->Type)) continue;
        const float dx = g.X - ax, dz = g.Z - az;
        cand.push_back({ dx * dx + dz * dz, h });
    }
    std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (int i = 0; i < (int)cand.size() && i < maxN; ++i) out.push_back(cand[i].second);
    return out;
}
