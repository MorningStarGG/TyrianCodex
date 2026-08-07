#include "model/EventTimers.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{
    std::string JStr(const json& o, const char* key)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }

    int JInt(const json& o, const char* key, int def = -1)
    {
        auto it = o.find(key);
        return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
    }

    std::vector<uint32_t> JMapIds(const json& o)
    {
        std::vector<uint32_t> out;
        auto it = o.find("mapIds");
        if (it == o.end() || !it->is_array()) return out;
        for (const auto& v : *it)
            if (v.is_number_unsigned()) out.push_back(v.get<uint32_t>());
            else if (v.is_number_integer() && v.get<int>() > 0) out.push_back((uint32_t)v.get<int>());
        return out;
    }

    std::vector<int> JIntArray(const json& o, const char* key)
    {
        std::vector<int> out;
        auto it = o.find(key);
        if (it == o.end() || !it->is_array()) return out;
        for (const auto& v : *it)
            if (v.is_number_integer()) out.push_back(v.get<int>());
        return out;
    }

    void ReadEffectiveLevel(const json& o, int& lo, int& hi, std::string& label)
    {
        auto it = o.find("effectiveLevel");
        if (it == o.end() || !it->is_object()) return;
        lo = JInt(*it, "min", -1);
        hi = JInt(*it, "max", -1);
        label = JStr(*it, "label");
    }

    // Continent coords resolved from the waypoint chat code by the builder ("continent": [x, y]).
    void ReadContinent(const json& o, float& x, float& y, bool& has)
    {
        auto it = o.find("continent");
        if (it != o.end() && it->is_array() && it->size() >= 2 && (*it)[0].is_number() && (*it)[1].is_number())
        { x = (*it)[0].get<float>(); y = (*it)[1].get<float>(); has = true; }
    }

    EventTimer ParseEvent(const json& e)
    {
        EventTimer out;
        out.Key                = JStr(e, "key");
        out.Name               = JStr(e, "name");
        out.Subtitle           = JStr(e, "subtitle");
        out.Category           = JStr(e, "category");
        out.SuggestedCategory  = JStr(e, "suggestedCategory");
        out.MapIds             = JMapIds(e);
        out.Waypoint           = JStr(e, "waypoint");
        out.ScheduleMinutes    = JIntArray(e, "scheduleMinutes");
        out.DurationMinutes    = JInt(e, "durationMinutes", -1);
        out.WikiTitle          = JStr(e, "wikiTitle");
        out.WikiUrl            = JStr(e, "wikiUrl");
        out.WikiClassification = JStr(e, "wikiClassification");
        out.WikiConfidence     = JStr(e, "wikiConfidence");
        out.Description        = JStr(e, "description");
        ReadEffectiveLevel(e, out.LevelMin, out.LevelMax, out.LevelLabel);
        ReadContinent(e, out.ContinentX, out.ContinentY, out.HasCoord);
        return out;
    }

    EventChain ParseChain(const json& e)
    {
        EventChain out;
        out.Key                = JStr(e, "key");
        out.Name               = JStr(e, "name");
        out.Subtitle           = JStr(e, "subtitle");
        out.Category           = JStr(e, "category");
        out.SuggestedCategory  = JStr(e, "suggestedCategory");
        out.MapIds             = JMapIds(e);
        out.Waypoint           = JStr(e, "waypoint");
        out.DurationMinutes    = JInt(e, "durationMinutes", -1);
        out.WikiTitle          = JStr(e, "wikiTitle");
        out.WikiUrl            = JStr(e, "wikiUrl");
        out.WikiClassification = JStr(e, "wikiClassification");
        out.WikiConfidence     = JStr(e, "wikiConfidence");
        out.Description        = JStr(e, "description");
        ReadEffectiveLevel(e, out.LevelMin, out.LevelMax, out.LevelLabel);
        ReadContinent(e, out.ContinentX, out.ContinentY, out.HasCoord);
        return out;
    }

    CycleTimer ParseCycle(const json& e)
    {
        CycleTimer out;
        out.Key             = JStr(e, "key");
        out.Name            = JStr(e, "name");
        out.CycleGroup      = JStr(e, "cycleGroup");
        out.Category        = JStr(e, "category");
        out.Recurrence      = JStr(e, "recurrence");
        out.MapIds          = JMapIds(e);
        out.ScheduleMinutes = JIntArray(e, "scheduleMinutes");
        out.DurationMinutes = JInt(e, "durationMinutes", -1);
        return out;
    }
}

bool EventTimerData::Load(const std::string& path)
{
    loaded_ = false;
    events_.clear();
    chains_.clear();
    cycles_.clear();
    pvp_.clear();
    eventsByMap_.clear();
    chainsByMap_.clear();

    std::ifstream f(path);
    if (!f) return false;

    json j;
    try { f >> j; }
    catch (...) { return false; }
    if (!j.is_object()) return false;

    try
    {
        if (j.contains("events") && j["events"].is_array())
        {
            events_.reserve(j["events"].size());
            for (const auto& e : j["events"])
            {
                if (!e.is_object()) continue;
                EventTimer ev = ParseEvent(e);
                if (ev.Key.empty() || ev.Name.empty()) continue;
                const size_t index = events_.size();
                events_.push_back(std::move(ev));
                for (uint32_t mapId : events_.back().MapIds)
                    eventsByMap_[mapId].push_back(index);
            }
        }

        if (j.contains("unscheduledChains") && j["unscheduledChains"].is_array())
        {
            chains_.reserve(j["unscheduledChains"].size());
            for (const auto& e : j["unscheduledChains"])
            {
                if (!e.is_object()) continue;
                EventChain chain = ParseChain(e);
                if (chain.Key.empty() || chain.Name.empty()) continue;
                const size_t index = chains_.size();
                chains_.push_back(std::move(chain));
                for (uint32_t mapId : chains_.back().MapIds)
                    chainsByMap_[mapId].push_back(index);
            }
        }

        for (const char* listKey : { "cycles", "pvp" })
        {
            if (!j.contains(listKey) || !j[listKey].is_array()) continue;
            std::vector<CycleTimer>& dst = (std::string(listKey) == "cycles") ? cycles_ : pvp_;
            for (const auto& e : j[listKey])
            {
                if (!e.is_object()) continue;
                CycleTimer ct = ParseCycle(e);
                if (ct.Key.empty() || ct.Name.empty()) continue;
                dst.push_back(std::move(ct));
            }
        }
    }
    catch (...)
    {
        events_.clear();
        chains_.clear();
        cycles_.clear();
        pvp_.clear();
        eventsByMap_.clear();
        chainsByMap_.clear();
        return false;
    }

    loaded_ = !events_.empty() || !chains_.empty();
    return loaded_;
}

std::vector<const EventTimer*> EventTimerData::EventsForMap(uint32_t mapId) const
{
    std::vector<const EventTimer*> out;
    auto it = eventsByMap_.find(mapId);
    if (it == eventsByMap_.end()) return out;
    out.reserve(it->second.size());
    for (size_t index : it->second)
        if (index < events_.size()) out.push_back(&events_[index]);
    return out;
}

std::vector<const EventChain*> EventTimerData::ChainsForMap(uint32_t mapId) const
{
    std::vector<const EventChain*> out;
    auto it = chainsByMap_.find(mapId);
    if (it == chainsByMap_.end()) return out;
    out.reserve(it->second.size());
    for (size_t index : it->second)
        if (index < chains_.size()) out.push_back(&chains_[index]);
    return out;
}

// ---- EventSchedule ----------------------------------------------------------------------------------
namespace
{
    constexpr int kSecPerDay = 24 * 60 * 60;
}

int EventSchedule::UtcSecondOfDay()
{
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &t);
    return utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec;
}

EventSchedule::Occurrence EventSchedule::Next(const EventTimer& ev, int nowSec)
{
    Occurrence out;
    int best = kSecPerDay;
    int bestStart = -1;
    for (int startMin : ev.ScheduleMinutes)
    {
        const int s = (((startMin * 60) % kSecPerDay) + kSecPerDay) % kSecPerDay;
        const int elapsed = (nowSec - s + kSecPerDay) % kSecPerDay;
        if (ev.DurationMinutes > 0 && elapsed < ev.DurationMinutes * 60)
        {
            out.active = true;
            out.secondsUntil = 0;
            out.secondsRemaining = ev.DurationMinutes * 60 - elapsed;
            out.startSecOfDay = s;
            return out;   // running now wins outright -- there is nothing to count down to
        }
        const int until = (s - nowSec + kSecPerDay) % kSecPerDay;
        if (until < best) { best = until; bestStart = s; }
    }
    if (bestStart < 0)
        return out;       // no schedule -> unschedulable (startSecOfDay stays -1)
    out.secondsUntil = best;
    out.startSecOfDay = bestStart;
    return out;
}
