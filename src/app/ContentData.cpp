#include "app/ContentData.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    int   JI(const json& j, const char* k, int def = 0)       { auto it = j.find(k); return (it != j.end() && it->is_number_integer()) ? it->get<int>() : def; }
    long long JL(const json& j, const char* k, long long def = 0) { auto it = j.find(k); return (it != j.end() && it->is_number()) ? it->get<long long>() : def; }
}

void ContentData::Load(const std::string& contentPath, const std::string& rotationsPath)
{
    raids_.clear();
    dungeons_.clear();

    // ---- content.json: the static raids + dungeons structure (in the builder's release order) ----
    {
        std::ifstream f(contentPath);
        if (f)
        {
            json j;
            try { f >> j; } catch (...) { j = json(); }
            if (j.is_object())
            {
                auto raids = j.find("raids");
                auto rOrder = j.find("raidOrder");
                if (raids != j.end() && raids->is_object() && rOrder != j.end() && rOrder->is_array())
                {
                    for (const auto& idv : *rOrder)
                    {
                        if (!idv.is_string()) continue;
                        const std::string id = idv.get<std::string>();
                        auto rit = raids->find(id);
                        if (rit == raids->end() || !rit->is_object()) continue;
                        Raid r; r.id = id;
                        auto wings = rit->find("wings");
                        if (wings != rit->end() && wings->is_array())
                            for (const auto& w : *wings)
                            {
                                if (!w.is_object()) continue;
                                Wing wg; wg.id = w.value("id", std::string());
                                auto bosses = w.find("bosses");
                                if (bosses != w.end() && bosses->is_array())
                                    for (const auto& b : *bosses) if (b.is_string()) wg.bosses.push_back(b.get<std::string>());
                                r.wings.push_back(std::move(wg));
                            }
                        raids_.push_back(std::move(r));
                    }
                }
                auto dungeons = j.find("dungeons");
                auto dOrder = j.find("dungeonOrder");
                if (dungeons != j.end() && dungeons->is_object() && dOrder != j.end() && dOrder->is_array())
                {
                    for (const auto& idv : *dOrder)
                    {
                        if (!idv.is_string()) continue;
                        const std::string id = idv.get<std::string>();
                        auto dit = dungeons->find(id);
                        if (dit == dungeons->end() || !dit->is_object()) continue;
                        Dungeon d; d.id = id;
                        auto paths = dit->find("paths");
                        if (paths != dit->end() && paths->is_array())
                            for (const auto& p : *paths)
                                if (p.is_object()) d.paths.push_back({ p.value("id", std::string()), p.value("type", std::string()) });
                        dungeons_.push_back(std::move(d));
                    }
                }
            }
        }
    }

    // ---- rotations.json: the wiki's deterministic daily/weekly fractal + strike rotations ----
    {
        std::ifstream f(rotationsPath);
        if (f)
        {
            json j;
            try { f >> j; } catch (...) { j = json(); }
            if (j.is_object())
            {
                auto fd = j.find("fractalsDaily");
                if (fd != j.end() && fd->is_object())
                {
                    fracDaily_.period = JI(*fd, "period", 1); fracDaily_.offset = JI(*fd, "offset", 0);
                    auto sets = fd->find("sets");
                    if (sets != fd->end() && sets->is_array())
                        for (const auto& set : *sets)
                        {
                            std::vector<std::string> s;
                            if (set.is_array()) for (const auto& n : set) if (n.is_string()) s.push_back(n.get<std::string>());
                            fracDaily_.sets.push_back(std::move(s));
                        }
                }
                auto fr = j.find("fractalsRecommended");
                if (fr != j.end() && fr->is_object())
                {
                    fracRec_.period = JI(*fr, "period", 1); fracRec_.offset = JI(*fr, "offset", 0);
                    auto sets = fr->find("sets");
                    if (sets != fr->end() && sets->is_array())
                        for (const auto& set : *sets)
                        {
                            std::vector<int> s;
                            if (set.is_array()) for (const auto& n : set) if (n.is_number_integer()) s.push_back(n.get<int>());
                            fracRec_.sets.push_back(std::move(s));
                        }
                    auto names = fr->find("scaleNames");
                    if (names != fr->end() && names->is_object())
                        for (auto it = names->begin(); it != names->end(); ++it)
                            if (it.value().is_string()) fracRec_.scaleNames[std::atoi(it.key().c_str())] = it.value().get<std::string>();
                }
                auto loadList = [&](const char* key, ListRot& r) {
                    auto o = j.find(key);
                    if (o == j.end() || !o->is_object()) return;
                    r.period = JI(*o, "period", 1); r.offset = JI(*o, "offset", 0);
                    auto list = o->find("list");
                    if (list != o->end() && list->is_array())
                        for (const auto& e : *list) if (e.is_string()) r.list.push_back(e.get<std::string>());
                };
                loadList("strikeIcebrood", strikeIce_);
                loadList("strikeEoD",      strikeEoD_);
                loadList("strikeSoO",      strikeSoO_);
                auto bw = j.find("bjoraWeekly");
                if (bw != j.end() && bw->is_object())
                {
                    bjora_.period = JI(*bw, "period", 1);
                    bjora_.anchorEpoch = JL(*bw, "anchorEpoch", 0);
                    auto list = bw->find("list");
                    if (list != bw->end() && list->is_array())
                        for (const auto& e : *list) if (e.is_string()) bjora_.list.push_back(e.get<std::string>());
                }
            }
        }
    }
}

// The wiki {{day of year index}}: {{#time:z}} (UTC day-of-year, 0-based) with a leap-year adjustment that skips
// index 59 (the Feb-29 slot) in non-leap years, so dates from Mar 1 align across leap/non-leap years.
int ContentData::DayOfYearIndex()
{
    std::time_t t = std::time(nullptr);
    std::tm g{};
#if defined(_WIN32)
    gmtime_s(&g, &t);
#else
    g = *std::gmtime(&t);
#endif
    int doy = g.tm_yday;                  // 0-365 (UTC)
    const int year = g.tm_year + 1900;
    const bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (!leap && doy >= 59) doy += 1;     // skip the missing Feb-29 slot -> matches the wiki selector exactly
    return doy;
}

std::vector<std::string> ContentData::TodaysDailyFractals() const
{
    if (fracDaily_.sets.empty() || fracDaily_.period <= 0) return {};
    int idx = ((DayOfYearIndex() + fracDaily_.offset) % fracDaily_.period + fracDaily_.period) % fracDaily_.period;
    if (idx < 0 || idx >= (int)fracDaily_.sets.size()) idx = 0;
    return fracDaily_.sets[idx];
}

std::vector<ContentData::RecFractal> ContentData::TodaysRecommendedFractals() const
{
    if (fracRec_.sets.empty() || fracRec_.period <= 0) return {};
    int idx = ((DayOfYearIndex() + fracRec_.offset) % fracRec_.period + fracRec_.period) % fracRec_.period;
    if (idx < 0 || idx >= (int)fracRec_.sets.size()) idx = 0;
    std::vector<RecFractal> out;
    for (int scale : fracRec_.sets[idx])
    {
        RecFractal rf; rf.scale = scale;
        auto it = fracRec_.scaleNames.find(scale);
        if (it != fracRec_.scaleNames.end()) rf.name = it->second;
        out.push_back(std::move(rf));
    }
    return out;
}

std::vector<std::string> ContentData::TodaysPriorityStrikes() const
{
    auto pick = [](const ListRot& r) -> std::string {
        if (r.list.empty() || r.period <= 0) return {};
        int idx = ((DayOfYearIndex() + r.offset) % r.period + r.period) % r.period;
        if (idx < 0 || idx >= (int)r.list.size()) idx = 0;
        return r.list[idx];
    };
    std::vector<std::string> out;
    std::string s;
    if (!(s = pick(strikeIce_)).empty()) out.push_back(s);
    if (!(s = pick(strikeEoD_)).empty()) out.push_back(s);
    if (!(s = pick(strikeSoO_)).empty()) out.push_back(s);
    return out;
}

std::string ContentData::ThisWeeksBjoraStrike() const
{
    if (bjora_.list.empty() || bjora_.period <= 0 || bjora_.anchorEpoch <= 0) return {};
    const long long now = (long long)std::time(nullptr);
    const long long weeks = (long long)std::floor((double)(now - bjora_.anchorEpoch) / 604800.0);   // 7*86400
    int idx = (int)(((weeks % bjora_.period) + bjora_.period) % bjora_.period);
    if (idx < 0 || idx >= (int)bjora_.list.size()) idx = 0;
    return bjora_.list[idx];
}
