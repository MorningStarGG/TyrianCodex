#include "app/FishData.h"

#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{
    // Null-safe readers (fish.json is ours, but never let a bad field throw into GW2's render loop).
    std::string JS(const json& o, const char* k)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }
    int JI(const json& o, const char* k, int def = 0)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
    }
    bool JB(const json& o, const char* k, bool def = false)
    {
        auto it = o.find(k);
        return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
    }
}

void FishData::Load(const std::string& path)
{
    byId_.clear();
    collections_.clear();
    ourAch_.clear();

    std::ifstream f(path);
    if (!f) return;
    json j;
    try { f >> j; } catch (...) { return; }
    if (!j.is_object()) return;

    auto colls = j.find("collections");
    if (colls != j.end() && colls->is_object())
    {
        for (auto it = colls->begin(); it != colls->end(); ++it)
        {
            const json& c = it.value();
            if (!c.is_object()) continue;
            Collection col;
            col.id        = std::atoi(it.key().c_str());
            col.name      = JS(c, "name");
            col.avid      = JB(c, "avid");
            col.worldwide = JB(c, "worldwide");
            auto fish = c.find("fish");
            if (fish != c.end() && fish->is_array())
                for (const auto& fid : *fish)
                    if (fid.is_number_integer()) col.fish.push_back(fid.get<int>());
            collections_.push_back(std::move(col));
            ourAch_.insert(col.id);
        }
    }

    auto fishMap = j.find("fish");
    if (fishMap != j.end() && fishMap->is_object())
    {
        for (auto it = fishMap->begin(); it != fishMap->end(); ++it)
        {
            const json& e = it.value();
            if (!e.is_object()) continue;
            Fish fi;
            fi.id       = std::atoi(it.key().c_str());
            fi.name     = JS(e, "name");
            fi.icon     = JS(e, "icon");
            fi.rarity   = JS(e, "rarity");
            fi.region   = JS(e, "region");
            fi.location = JS(e, "location");
            fi.hole     = JS(e, "hole");
            fi.bait     = JS(e, "bait");
            fi.time     = JS(e, "time");
            fi.power    = JI(e, "power");
            auto maps = e.find("mapIds");
            if (maps != e.end() && maps->is_array())
                for (const auto& m : *maps)
                    if (m.is_number_integer()) fi.mapIds.push_back((uint32_t)m.get<int>());
            byId_[fi.id] = std::move(fi);
        }
    }
}

const FishData::Fish* FishData::Find(int itemId) const
{
    auto it = byId_.find(itemId);
    return it != byId_.end() ? &it->second : nullptr;
}

bool FishData::IsCaught(int achId, int bitIndex) const
{
    auto it = caughtBits_.find(achId);
    return it != caughtBits_.end() && it->second.count(bitIndex) != 0;
}

std::pair<int, int> FishData::Progress(int achId) const
{
    // Find the collection's total fish count.
    int total = 0;
    for (const Collection& c : collections_) if (c.id == achId) { total = (int)c.fish.size(); break; }
    auto pit = progress_.find(achId);
    if (pit != progress_.end() && pit->second.second > 0)
        return { pit->second.first, pit->second.second };   // account current/max (authoritative)
    auto bit = caughtBits_.find(achId);                      // else count the completed bits we have
    return { bit != caughtBits_.end() ? (int)bit->second.size() : 0, total };
}

void FishData::Refresh(bool force)
{
    if (fetching_ || !api_) return;
    if (!api_->HasPermission(Api::TokenPermission::Account) || !api_->HasPermission(Api::TokenPermission::Progression))
        return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && fetchedOnce_ && now - lastFetch_ < std::chrono::seconds(60)) return;
    fetchedOnce_ = true;
    lastFetch_   = now;
    fetching_    = true;
    api_->V2().Account().Achievements([this](Api::Result<std::vector<Api::V2::AccountAchievement>> r) {
        fetching_ = false;
        if (!r.ok) return;   // F10: a failed fetch never clobbers good caught data
        std::map<int, std::set<int>> bits;
        std::map<int, std::pair<int, int>> prog;
        for (const auto& a : r.value)
        {
            if (ourAch_.count(a.id) == 0) continue;          // only our fishing collections
            prog[a.id] = { a.current, a.max };
            auto bj = a.raw.find("bits");                    // completed bit indices (collections)
            if (bj != a.raw.end() && bj->is_array())
            {
                std::set<int>& s = bits[a.id];
                for (const auto& b : *bj) if (b.is_number_integer()) s.insert(b.get<int>());
            }
        }
        caughtBits_ = std::move(bits);
        progress_   = std::move(prog);
        ++ver_;
    });
}

// -- Tyrian day/night: a deterministic 2h (7200s) UTC-synced cycle. 1 Tyrian hour = 5 real minutes (300s), so
// the Tyrian hour-of-day = (UTC seconds mod 7200) / 300. Tyrian phases: Night 21:00-05:00, Dawn 05:00-06:00,
// Day 06:00-20:00, Dusk 20:00-21:00 (durations Night 40m / Dawn 5m / Day 70m / Dusk 5m of real time).
FishData::Phase FishData::CurrentPhase(int* secondsToNext)
{
    const long long t = (long long)std::time(nullptr);   // UTC seconds since epoch
    const int cur = (int)(((t % 7200) + 7200) % 7200);   // position in the cycle (0 == Tyrian 00:00)
    const double h = cur / 300.0;                         // Tyrian hour [0,24)

    Phase p;
    if      (h < 5.0)  p = Phase::Night;   // 00:00-05:00
    else if (h < 6.0)  p = Phase::Dawn;    // 05:00-06:00
    else if (h < 20.0) p = Phase::Day;     // 06:00-20:00
    else if (h < 21.0) p = Phase::Dusk;    // 20:00-21:00
    else               p = Phase::Night;   // 21:00-24:00

    if (secondsToNext)
    {
        static const int kBounds[] = { 1500, 1800, 6000, 6300, 7200 };   // 5,6,20,21,24 Tyrian hours * 300s
        int nb = 7200;
        for (int b : kBounds) if (b > cur) { nb = b; break; }
        *secondsToNext = nb - cur;
    }
    return p;
}

const char* FishData::PhaseName(Phase p)
{
    switch (p)
    {
        case Phase::Day:   return "Day";
        case Phase::Dusk:  return "Dusk";
        case Phase::Night: return "Night";
        case Phase::Dawn:  return "Dawn";
    }
    return "";
}

// A fish's wiki "Time of Day" string ("Any", "Daytime", "Nighttime", "Dawn", "Dusk", "Dawn and Dusk", ...)
// matched against the current phase. Empty / "Any" == always catchable.
bool FishData::CatchableNow(const std::string& timeOfDay, Phase phase)
{
    std::string l;
    l.reserve(timeOfDay.size());
    for (char c : timeOfDay) l.push_back((char)std::tolower((unsigned char)c));
    if (l.empty() || l.find("any") != std::string::npos) return true;

    if (l.find("night") != std::string::npos && phase == Phase::Night) return true;
    if (l.find("dawn")  != std::string::npos && phase == Phase::Dawn)  return true;
    if (l.find("dusk")  != std::string::npos && phase == Phase::Dusk)  return true;
    if (l.find("day")   != std::string::npos && phase == Phase::Day)   return true;
    return false;
}

// Seconds until a `timeOfDay` fish next becomes catchable (0 if it is right now). Walks the deterministic cycle
// (enum order IS the time order: Day -> Dusk -> Night -> Dawn -> Day) summing each phase's real duration from
// the current phase, starting with the time left in it.
int FishData::SecondsUntilCatchable(const std::string& timeOfDay, Phase phase, int secondsToNext)
{
    if (CatchableNow(timeOfDay, phase)) return 0;
    static const int dur[4] = { 4200, 300, 2400, 300 };   // Day, Dusk, Night, Dawn (real seconds)
    int secs = secondsToNext;                             // time left in the current phase
    int p = ((int)phase + 1) % 4;
    for (int i = 0; i < 4; ++i)                           // at most one full cycle
    {
        if (CatchableNow(timeOfDay, (Phase)p)) return secs;
        secs += dur[p];
        p = (p + 1) % 4;
    }
    return secs;   // unreachable for a real time-of-day string
}
