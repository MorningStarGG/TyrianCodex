#pragma once
#include "api/Client.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// FishData: the fishing-collection catalog + live caught status (Phase 10). Loads the bundled data/fish.json
// (collections + per-fish bait/time/hole/region/map, built by builder/build_fish.py from the API + wiki), and
// -- gated on the account+progression scopes, throttled like StoryCompletion -- reads /v2/account/achievements
// so each collection's COMPLETED bit indices give the caught status (a fish's position in a collection's
// ordered `fish` list IS its achievement bit index). Also computes the Tyrian day/night phase (for the
// "catchable now" indicator). Owned by App as `app.fish`; loaded in AddonLoad, Refresh()'d while the tab shows.
// -----------------------------------------------------------------------------------------------------
class FishData
{
public:
    struct Fish
    {
        int id = 0;
        std::string name, icon, rarity, region, location, hole, bait, time;
        int power = 0;
        std::vector<uint32_t> mapIds;   // maps the fish can be caught in (region-level fish -> all region maps)
    };
    struct Collection
    {
        int id = 0;                     // the "* Fisher" achievement id
        std::string name;
        bool avid = false, worldwide = false;
        std::vector<int> fish;          // fish item ids in BIT ORDER (position == achievement bit index)
    };
    enum class Phase { Day, Dusk, Night, Dawn };

    void Load(const std::string& path);            // parse data/fish.json (once, in AddonLoad)
    void Init(Api::Client* api) { api_ = api; }    // wire the API for caught-status refresh
    void Refresh(bool force = false);              // /v2/account/achievements caught bits (throttled 60s, scope-gated)

    bool Empty() const { return collections_.empty(); }
    const std::vector<Collection>& Collections() const { return collections_; }
    const Fish* Find(int itemId) const;
    bool IsCaught(int achId, int bitIndex) const;          // fish at bitIndex in collection achId is complete
    std::pair<int, int> Progress(int achId) const;         // {caught, total} for a collection
    uint64_t Version() const { return ver_; }              // bumped on each successful caught-status refresh

    // Tyrian day/night (deterministic, UTC-synced 2h cycle: Day 70m / Dusk 5m / Night 40m / Dawn 5m).
    static Phase CurrentPhase(int* secondsToNext = nullptr);
    static const char* PhaseName(Phase p);
    static bool CatchableNow(const std::string& timeOfDay, Phase phase);   // does a fish's "Time of Day" match?
    static int  SecondsUntilCatchable(const std::string& timeOfDay, Phase phase, int secondsToNext);  // 0 if catchable now; else seconds to its next window

private:
    std::map<int, Fish>               byId_;
    std::vector<Collection>           collections_;
    std::set<int>                     ourAch_;       // our collection achievement ids (filter the account fetch)
    std::map<int, std::set<int>>      caughtBits_;   // achId -> completed bit indices
    std::map<int, std::pair<int, int>> progress_;    // achId -> (current, max) from the account fetch

    Api::Client* api_ = nullptr;
    bool fetching_ = false, fetchedOnce_ = false;
    std::chrono::steady_clock::time_point lastFetch_;
    uint64_t ver_ = 0;
};
