#include "guide/FarmingGuidance.h"
#include "guide/FarmCategories.h"
#include "app/App.h"
#include "ui/GuideViewer.h"     // CurrentTargetOverride
#include "ui/QuickMenu.h"       // QuickMenu::ArrowDispatch
#include "guide/GuidanceSnapshot.h"
#include "guide/TrailFollower.h"  // Follow::Vec2 / Dist
#include "ui/dashboard/Notify.h"  // run-complete toast
#include "util/Textures.h"        // Tex::GetBundledTexture / GetTextureFromAssetId (the material type icon)
#include "Shared.h"             // MumbleLink (player pos for NearestNodes)
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <utility>

void FarmingGuidance::Update(App& app, float W, float H, const Math::Vec3& avatar, const Math::Vec3& camFwd)
{
    GuideState& st  = app.state;
    Config&     cfg = app.config;
    GuidanceSnapshotUtil::Clear(st);
    if (st.activeContent != ContentType::Farming || !st.farm) return;

    const Follow::Vec2 player{ avatar.x, avatar.z };

    // Nearest enabled, NOT-yet-gathered node in the selected category (crossed off on arrival; nodes respawn so
    // it's session-only). Recomputed every frame, so toggling a chip re-routes live. Also tally total/gathered to
    // announce completion once.
    float best = FLT_MAX; const GuideState::ActiveGatherNode* bn = nullptr;
    int total = 0, got = 0;
    for (const auto& g : st.activeGather)
    {
        if (!g.Node) continue;
        if (!cfg.GatherShown(st.activeGatherMapId, g.Node->Material)) continue;
        if (!Farm::CategoryMatch(cfg.gatherCategory, g.Node->Category)) continue;
        ++total;
        if (st.farmGathered.count(g.Node)) { ++got; continue; }
        const float dx = g.X - avatar.x, dz = g.Z - avatar.z;
        const float d = dx * dx + dz * dz;
        if (d < best) { best = d; bn = &g; }
    }
    // Run complete = every enabled-in-category node gathered. Announce once (latched), reset when there's more.
    const bool complete = total > 0 && got >= total;
    if (complete && !st.farmRunComplete)
        Notify::Push(Notify::Kind::Action, "Gathering run complete",
                     "All nodes in the selected category gathered - reset the run to go again (they respawn).");
    st.farmRunComplete = complete;
    if (!bn) return;   // nothing enabled / all gathered this session -> arrow idle

    const Follow::Vec2 target{ bn->X, bn->Z };
    if (Follow::Dist(player, target) <= cfg.arrival) st.farmGathered.insert(bn->Node);   // gathered -> next frame picks the next

    char cap[80];
    std::snprintf(cap, sizeof(cap), "%s", Farm::MaterialPretty(bn->Node->Material).c_str());

    const float travel = Follow::Dist(player, target);
    st.curTravel = travel; st.curArrived = (travel <= cfg.arrival); st.offRoute = false;
    const float speed = app.guidance.SpeedEma();
    const TargetOverrideKind tk = CurrentTargetOverride(app);

    const FarmNode& nd = *bn->Node;   // the gather node's material icon (bundled per-material PNG, else an asset id)
    void* iconTex = !nd.Icon.empty()
        ? Tex::GetBundledTexture(("data\\textures\\markers\\gather\\" + nd.Icon + ".png").c_str())
        : (nd.IconAssetId ? Tex::GetTextureFromAssetId(nd.IconAssetId) : nullptr);
    GuidanceSnapshotUtil::Set(st, target, player, camFwd, st.curArrived, travel, cap, /*dungeon*/ false, /*travel*/ false, speed, /*directAim*/ nullptr, iconTex);
    const int act = app.gpsArrow.Draw(target, player, camFwd, st.curArrived, travel, cap, W, H, /*dungeon*/ false,
                                      cfg, st, app.settingsDirty, speed, tk, /*directAim*/ nullptr, iconTex);
    if (act >= 0) QuickMenu::ArrowDispatch(app, act);
}

bool FarmingGuidance::Active(const App& app)
{
    return app.state.activeContent == ContentType::Farming && app.state.farm != nullptr;
}

std::vector<const FarmNode*> FarmingGuidance::NearestNodes(const App& app, int maxN)
{
    std::vector<const FarmNode*> out;
    const GuideState& st = app.state; const Config& cfg = app.config;
    if (!st.farm || st.activeGather.empty() || maxN <= 0 || !MumbleLink) return out;
    const float ax = MumbleLink->AvatarPosition.X, az = MumbleLink->AvatarPosition.Z;
    std::vector<std::pair<float, const FarmNode*>> cand;
    cand.reserve(st.activeGather.size());
    for (const auto& g : st.activeGather)
    {
        const FarmNode* nd = g.Node;
        if (!nd || st.farmGathered.count(nd)) continue;
        if (!cfg.GatherShown(st.activeGatherMapId, nd->Material)) continue;
        if (!Farm::CategoryMatch(cfg.gatherCategory, nd->Category)) continue;
        const float dx = g.X - ax, dz = g.Z - az;
        cand.push_back({ dx * dx + dz * dz, nd });
    }
    std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (int i = 0; i < (int)cand.size() && i < maxN; ++i) out.push_back(cand[i].second);
    return out;
}
