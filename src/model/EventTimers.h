#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct EventTimer
{
    std::string         Key;
    std::string         Name;
    std::string         Subtitle;   // the raw source name when it adds info (shown as a small secondary line)
    std::string         Category;
    std::string         SuggestedCategory;
    std::vector<uint32_t> MapIds;
    std::string         Waypoint;
    std::vector<int>    ScheduleMinutes;
    int                 DurationMinutes = -1;
    int                 LevelMin = -1;
    int                 LevelMax = -1;
    std::string         LevelLabel;
    std::string         WikiTitle;
    std::string         WikiUrl;
    std::string         WikiClassification;
    std::string         WikiConfidence;
    std::string         Description;
    float               ContinentX = 0.f;   // resolved from the waypoint chat code by the builder; the click-to-guide target
    float               ContinentY = 0.f;
    bool                HasCoord   = false;
};

struct EventChain
{
    std::string         Key;
    std::string         Name;
    std::string         Subtitle;
    std::string         Category;
    std::string         SuggestedCategory;
    std::vector<uint32_t> MapIds;
    std::string         Waypoint;
    int                 DurationMinutes = -1;
    int                 LevelMin = -1;
    int                 LevelMax = -1;
    std::string         LevelLabel;
    std::string         WikiTitle;
    std::string         WikiUrl;
    std::string         WikiClassification;
    std::string         WikiConfidence;
    std::string         Description;
    float               ContinentX = 0.f;   // resolved from the waypoint chat code by the builder; the click-to-guide target
    float               ContinentY = 0.f;
    bool                HasCoord   = false;
};

// World-clock cycles (day/night, server reset) and PvP automated tournaments: scheduled non-event timers shown
// in their own scopes. Name = the row label (phase / tournament); CycleGroup = the section header.
struct CycleTimer
{
    std::string         Key;
    std::string         Name;
    std::string         CycleGroup;
    std::string         Category;          // "cycle" or "pvp"
    std::string         Recurrence;        // "daily" / "weekly" / "" (intra-day) -- drives the countdown
    std::vector<uint32_t> MapIds;
    std::vector<int>    ScheduleMinutes;
    int                 DurationMinutes = -1;
};

class EventTimerData
{
public:
    bool Load(const std::string& path);
    bool Loaded() const { return loaded_; }

    const std::vector<EventTimer>& Events() const { return events_; }
    const std::vector<EventChain>& Chains() const { return chains_; }
    const std::vector<CycleTimer>& Cycles() const { return cycles_; }
    const std::vector<CycleTimer>& Pvp() const { return pvp_; }

    std::vector<const EventTimer*> EventsForMap(uint32_t mapId) const;
    std::vector<const EventChain*> ChainsForMap(uint32_t mapId) const;

private:
    bool loaded_ = false;
    std::vector<EventTimer> events_;
    std::vector<EventChain> chains_;
    std::vector<CycleTimer> cycles_;
    std::vector<CycleTimer> pvp_;
    std::map<uint32_t, std::vector<size_t>> eventsByMap_;
    std::map<uint32_t, std::vector<size_t>> chainsByMap_;
};

// -----------------------------------------------------------------------------------------------------
// EventSchedule: when does this event next start? Kept in SECONDS, not minutes -- the reminder ticker
// fires rungs at 30s and 10s, which a minute-resolution answer cannot express, and a per-minute countdown
// otherwise sits on "1m" for a full minute. Display code derives minutes from these.
//
// Lives here rather than in the Timers UI because the reminder ticker has to answer this every tick
// whether or not the tab is open.
// -----------------------------------------------------------------------------------------------------
namespace EventSchedule
{
    struct Occurrence
    {
        bool active = false;         // the event is running right now
        int  secondsUntil = 0;       // until the next start (0 while active)
        int  secondsRemaining = -1;  // left of the running window (only while active)
        int  startSecOfDay = -1;     // UTC second-of-day of the occurrence this describes; -1 when unschedulable.
                                     // The reminder latch keys on this: it identifies WHICH run we already fired for.
    };

    int UtcSecondOfDay();

    // Next start for `ev` at `nowSec` (UTC second-of-day). ScheduleMinutes is minutes past UTC midnight.
    Occurrence Next(const EventTimer& ev, int nowSec);
}
