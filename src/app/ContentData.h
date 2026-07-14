#pragma once
#include <map>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// ContentData: the bundled STATIC structure + deterministic rotations for the Content/Instances tab trackers.
// Loads data/content.json (raids wing->boss ids + dungeons path ids/types, from /v2/raids + /v2/dungeons) and
// data/rotations.json (the wiki's deterministic daily/weekly fractal + strike rotations, built by
// builder/build_rotations.py). The LIVE progress (cleared bosses, done paths) comes from AccountData; this owns
// the static structure + the "what is today" math. Today's daily entries are computed locally to match the wiki:
// every daily rotation uses the wiki {{day of year index}} selector -- a 0-based UTC day-of-year ({{#time:z}})
// that SKIPS index 59 in non-leap years -- then `mod period`. Bjora is weekly (a 3-cycle off anchorEpoch).
// Owned by App as `app.content`; loaded once in AddonLoad.
// -----------------------------------------------------------------------------------------------------
class ContentData
{
public:
    struct Wing        { std::string id; std::vector<std::string> bosses; };
    struct Raid        { std::string id; std::vector<Wing> wings; };
    struct DungeonPath { std::string id; std::string type; };   // type: "Story" / "Explorable"
    struct Dungeon     { std::string id; std::vector<DungeonPath> paths; };
    struct RecFractal  { std::string name; int scale = 0; };    // name empty when the scale->name map lacks it

    void Load(const std::string& contentPath, const std::string& rotationsPath);
    bool Empty() const { return raids_.empty() && dungeons_.empty(); }
    bool HaveRotations() const { return !fracDaily_.sets.empty(); }

    const std::vector<Raid>&    Raids()    const { return raids_; }
    const std::vector<Dungeon>& Dungeons() const { return dungeons_; }

    // Today's deterministic rotation entries (UTC day-of-year mod period, matching the wiki/game).
    std::vector<std::string> TodaysDailyFractals()       const;   // 3 fractal names
    std::vector<RecFractal>  TodaysRecommendedFractals() const;   // 3 {name, scale} (name may be empty)
    std::vector<std::string> TodaysPriorityStrikes()     const;   // up to 3 strikes (Icebrood / EoD / SoO)
    std::string              ThisWeeksBjoraStrike()      const;   // the weekly Bjora Marches strike

private:
    struct DailyRot  { int period = 1, offset = 0; std::vector<std::vector<std::string>> sets; };   // fractal name sets
    struct RecRot    { int period = 1, offset = 0; std::vector<std::vector<int>> sets; std::map<int, std::string> scaleNames; };
    struct ListRot   { int period = 1, offset = 0; std::vector<std::string> list; };
    struct WeeklyRot { int period = 1; long long anchorEpoch = 0; std::vector<std::string> list; };

    std::vector<Raid>    raids_;
    std::vector<Dungeon> dungeons_;
    DailyRot  fracDaily_;
    RecRot    fracRec_;
    ListRot   strikeIce_, strikeEoD_, strikeSoO_;
    WeeklyRot bjora_;

    static int DayOfYearIndex();   // the wiki {{day of year index}}: UTC tm_yday, 0-based, skips 59 in non-leap years
};
