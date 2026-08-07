#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// The story spine + manual-completion store
// The bundled data/stories.json lists, per release, the story episodes/acts and the achievement ids that
// mean "this is finished". Completion is layered: auto from /v2/account/achievements (when the key has the
// account+progression scopes) OR a manual tick. Manual ticks live in TWO scopes so per-character state never
// clobbers account state: the personal story (release "core") is per-character; everything else is account-
// wide. The finale stays isolated by virtue of being a normal account-wide achievement, never cascaded.

struct StoryEpisode
{
    int storyId = 0;                 // /v2/stories id -- the Journal's row key; 0 for the synthesized core chapters
    std::string name;
    int order = 0;                   // the season's own order = true in-game Story Journal sequence
    std::string description;
    std::vector<int> achievementIds; // ALL must be unlocked (or ANY, when `any`) for auto-completion
    bool any = false;                // one-of the ids suffices (cumulative personal-story milestones)
    bool cumulative = false;         // a personal-story chapter run: ticking N (un)marks 1..N
};

// release key -> its episodes (sorted by order). Plus the canonical release order + display names.
class StoryData
{
public:
    bool Load(const std::string &path); // data/stories.json; returns true if any release loaded

    const std::vector<StoryEpisode> *Episodes(const std::string &release) const;
    const std::map<std::string, std::vector<StoryEpisode>> &ByRelease() const { return _byRelease; }
    bool Empty() const { return _byRelease.empty(); }

    static const std::vector<std::string> &ReleaseOrder();      // core, lws1, lws2, hot, ... voe
    static std::string ReleaseName(const std::string &release); // "Heart of Thorns", ...
    static bool PerCharacter(const std::string &release) { return release == "core"; }

    // A /v2/stories season NAME -> our release key ("Living World Season 3" -> "lws3"). Mirrors the builder's
    // season_to_release. Empty when we don't track that season.
    static std::string ReleaseForSeason(const std::string &seasonName);

    // Can this account actually PLAY this release? `access` is /v2/account access[]. FAILS OPEN: true when
    // access is empty (unknown -- no key/scope/not fetched) or the release has no expansion gate at all
    // (core + every Living World season; LW episode unlocks are not exposed in access[]). So a wrong or
    // missing access list can never hide content, only a positively-known absence gates it.
    static bool ReleasePlayable(const std::string &release, const std::vector<std::string> &access);

private:
    std::map<std::string, std::vector<StoryEpisode>> _byRelease;
};

// Manual "I finished this" marks, keyed by "release:episodeName", in two files/scopes (account-wide
// story-account.json + per-character story-characters.json). Pass a character name for the per-character
// scope (the personal story), or an empty string for the account scope. Auto-completion from achievements is
// layered ON TOP of this by the caller - this store is only the manual overrides + no-achievement content.
class StoryProgressStore
{
public:
    void Load(const std::string &accountPath, const std::string &charPath);

    // Account scope is always consulted; if `character` is non-empty, that character's marks are checked too.
    bool IsDone(const std::string &key, const std::string &character) const;
    // Writes the per-character scope when `character` is non-empty, else the account scope. Saves on change.
    void Set(const std::string &key, bool done, const std::string &character);
    // Bumps on every manual mark change -> a change token for immediate-mode surfaces (PersonalStory widget).
    uint64_t Version() const { return _ver; }

    // -- per-character maintenance (rename detector + Diagnostics cleanup); the account scope is never touched --
    void RenameChar(const std::string &from, const std::string &to); // union from's per-char marks into to + save
    void PurgeChar(const std::string &name);                         // drop a character's per-char marks + save
    void CollectCharNames(std::set<std::string> &out) const;         // every character with per-char marks

private:
    void SaveAccount() const;
    void SaveChars() const;

    std::string _acctPath;
    std::string _charPath;
    std::set<std::string> _account;
    std::map<std::string, std::vector<std::string>> _byChar;
    uint64_t _ver = 0;
};
