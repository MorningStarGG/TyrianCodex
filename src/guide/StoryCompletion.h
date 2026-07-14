#pragma once
#include "app/State.h"
#include "guide/Story.h"
#include "api/Client.h"
#include <chrono>
#include <set>
#include <string>

// -----------------------------------------------------------------------------------------------------
// StoryCompletion: the story-completion model. Tracks the unlocked
// achievement id set (/v2/account/achievements -- additive + scope-guarded) and the set of fully-completed
// releases, and answers "is this episode / release done?" for the Story tab + the "suggested next" line.
// Init'd with pointers to the bundled story spine, the manual-tick store, the API client and runtime state
// (mirrors Travel::Controller::Init), so the query methods stay parameter-free for the many tab call sites.
// -----------------------------------------------------------------------------------------------------
class StoryCompletion
{
public:
    void Init(const StoryData *stories, StoryProgressStore *storyStore, Api::Client *api, const GuideState *state);

    bool AutoCheckScoped() const;                                           // account + progression scopes present
    bool EpisodeDone(const std::string &rel, const StoryEpisode &ep) const; // achievements auto-done OR manual tick
    void RecomputeCompletedReleases();                                      // re-derive from achievements + manual ticks
    void RefreshAchievements(bool force);                                   // /v2/account/achievements (throttled 60s)

    bool AchievementDone(int id) const { return done_.count(id) != 0; }
    bool IsReleaseComplete(const std::string &rel) const { return completed_.count(rel) != 0; }
    bool Fetching() const { return fetching_; }

private:
    const StoryData *stories_ = nullptr;
    StoryProgressStore *storyStore_ = nullptr;
    Api::Client *api_ = nullptr;
    const GuideState *state_ = nullptr;

    std::set<int> done_;              // unlocked achievement ids (/v2/account/achievements)
    std::set<std::string> completed_; // releases whose every episode is done
    bool fetching_ = false, fetchedOnce_ = false;
    std::chrono::steady_clock::time_point lastFetch_;
};
