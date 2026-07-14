#include "guide/StoryCompletion.h"
#include <algorithm>

void StoryCompletion::Init(const StoryData *stories, StoryProgressStore *storyStore, Api::Client *api, const GuideState *state)
{
    stories_ = stories;
    storyStore_ = storyStore;
    api_ = api;
    state_ = state;
}

// Story auto-check needs BOTH account + progression scopes.
bool StoryCompletion::AutoCheckScoped() const
{
    return api_->HasPermission(Api::TokenPermission::Account) && api_->HasPermission(Api::TokenPermission::Progression);
}

// An episode is done if its completion achievements are unlocked (auto) OR it was ticked manually. `any`
// episodes (cumulative personal-story milestones) need just ONE id; the rest need all. No-achievement
// episodes (LWS1, personal-story chapters) are manual-only. Manual marks resolve per-character for the
// personal story (core), else account-wide.
bool StoryCompletion::EpisodeDone(const std::string &rel, const StoryEpisode &ep) const
{
    if (!ep.achievementIds.empty())
    {
        const auto has = [this](int id)
        { return done_.count(id) != 0; };
        const bool autoDone = ep.any
                                  ? std::any_of(ep.achievementIds.begin(), ep.achievementIds.end(), has)
                                  : std::all_of(ep.achievementIds.begin(), ep.achievementIds.end(), has);
        if (autoDone)
            return true;
    }
    const std::string scope = StoryData::PerCharacter(rel) ? state_->currentChar : std::string();
    return storyStore_->IsDone(rel + ":" + ep.name, scope);
}

// A release is complete once every one of its episodes is done. Recomputed when achievements load or a manual
// mark toggles; feeds the "suggested next" line.
void StoryCompletion::RecomputeCompletedReleases()
{
    completed_.clear();
    for (const auto &kv : stories_->ByRelease())
    {
        if (kv.second.empty())
            continue;
        bool all = true;
        for (const StoryEpisode &ep : kv.second)
            if (!EpisodeDone(kv.first, ep))
            {
                all = false;
                break;
            }
        if (all)
            completed_.insert(kv.first);
    }
}

// Read /v2/account/achievements and mark the unlocked ones so finished story acts auto-tick. Additive +
// permission-guarded (account + progression). Throttled: refetched at most every 60s unless `force`d (e.g.
// the key's scopes just resolved). (GuideModule.Stories.cs RefreshCompletionAsync.)
void StoryCompletion::RefreshAchievements(bool force)
{
    if (fetching_ || !AutoCheckScoped())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && fetchedOnce_ && now - lastFetch_ < std::chrono::seconds(60))
        return;
    fetchedOnce_ = true;
    lastFetch_ = now;
    fetching_ = true;
    api_->V2().Account().Achievements([this](Api::Result<std::vector<Api::V2::AccountAchievement>> r)
                                      {
        fetching_ = false;
        if (!r.ok) return;
        std::set<int> done;
        for (const auto& a : r.value) if (a.done) done.insert(a.id);
        done_ = std::move(done);
        RecomputeCompletedReleases(); });
}
