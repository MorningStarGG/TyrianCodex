#pragma once
#include "app/State.h"
#include "api/Client.h"
#include <map>
#include <set>
#include <string>

// -----------------------------------------------------------------------------------------------------
// CharacterLevel: owns the additive, throttled per-character LEVEL fetch (/v2/characters/{name}). It writes only
// GuideState::charLevel (RecommendZones reads it live). Self-contained: the API client + runtime state are passed
// by reference, and the active character name by the caller (CurrentCharName); empty name -> no-op (char-select /
// loading).
// -----------------------------------------------------------------------------------------------------
class CharacterLevel
{
public:
    // Load the persisted per-character level cache (charlevels.json) so recommendations can show the
    // last-known level INSTANTLY at login instead of waiting on the API. Call once at addon load.
    void Load(const std::string &path);

    // Throttled per-frame driver (Render): immediately on a character change, else every 20s once a level is
    // known / 3s until then. dtSeconds = this frame's delta.
    void MaybeRefresh(Api::Client &api, GuideState &st, const std::string &charName, float dtSeconds);
    // The actual level fetch (also called directly when the key's scopes resolve).
    void RefreshNow(Api::Client &api, GuideState &st, const std::string &charName);
    void ResetAttempt() { levelAttemptedFor_.clear(); } // force a re-fetch (scopes changed)

    // Record a known character level (persist + apply live if it's the active character). The personal-story
    // login preload calls this for EVERY character (it already fetches /core, which carries level), so the whole
    // account is warmed at login with no extra requests.
    void SetCachedLevel(GuideState &st, const std::string &name, int level);

    bool LevelLoading(Api::Client &api, const GuideState &st) const; // true while still waiting on the level

    // -- per-character maintenance (rename detector + Diagnostics cleanup) --
    void RenameChar(const std::string &from, const std::string &to); // move cached level (keep `to` if set) + save
    void PurgeChar(const std::string &name);                         // drop cached level + save
    void CollectCharNames(std::set<std::string> &out) const;         // every character with a cached level
    int CachedLevel(const std::string &name) const;                  // last-known level (0 if none) -- footprint

private:
    void SaveCache() const; // best-effort write of levelCache_ to cachePath_

    double sinceLevelMs_ = 0.0;
    std::string levelAttemptedFor_;
    bool levelRefreshing_ = false;
    std::string cachePath_;                 // charlevels.json
    std::map<std::string, int> levelCache_; // character -> last-known level (persisted, stale-ok)
};
