#include "app/AchievementPoints.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    struct AchPts { std::vector<std::pair<int, int>> tiers; bool repeatable = false; int pointCap = 0; };
    std::unordered_map<int, AchPts> g_tbl;
    bool g_loaded = false;
}

void AchPoints::Load(const std::string& path)
{
    g_tbl.clear();
    g_loaded = false;

    std::ifstream f(path);
    if (!f) return;
    nlohmann::json j;
    try { f >> j; } catch (...) { return; }   // never let a malformed bundle escape into the game loop
    if (!j.is_object()) return;

    auto ach = j.find("ach");
    if (ach == j.end() || !ach->is_object()) return;

    for (auto el = ach.value().begin(); el != ach.value().end(); ++el)
    {
        int id = 0;
        try { id = std::stoi(el.key()); } catch (...) { continue; }
        const nlohmann::json& e = el.value();
        if (!e.is_object()) continue;

        AchPts a;
        if (auto t = e.find("t"); t != e.end() && t->is_array())
            for (const auto& pr : *t)
                if (pr.is_array() && pr.size() >= 2 && pr[0].is_number_integer() && pr[1].is_number_integer())
                    a.tiers.emplace_back(pr[0].get<int>(), pr[1].get<int>());
        if (a.tiers.empty()) continue;

        if (auto r = e.find("r"); r != e.end() && r->is_number_integer()) a.repeatable = r->get<int>() != 0;
        if (auto c = e.find("c"); c != e.end() && c->is_number_integer()) a.pointCap   = c->get<int>();
        g_tbl.emplace(id, std::move(a));
    }
    g_loaded = !g_tbl.empty();
}

bool AchPoints::Loaded() { return g_loaded; }

int AchPoints::PointsFor(int id, int current, int repeated, bool done)
{
    auto it = g_tbl.find(id);
    if (it == g_tbl.end()) return 0;
    const AchPts& a = it->second;

    int base = 0, perCycle = 0;
    for (const auto& tier : a.tiers)
    {
        perCycle += tier.second;
        if (current >= tier.first) base += tier.second;
    }
    if (done) base = perCycle;   // all tiers complete (the API often omits `current` for done achievements)

    if (a.repeatable && a.pointCap > 0)
    {
        // perCycle per completion (`repeated`), plus the current cycle's partial -- but only when NOT done, since
        // a completed cycle is already counted in `repeated`. Capped at point_cap.
        long long earned = (long long)perCycle * (repeated > 0 ? repeated : 0) + (done ? 0 : base);
        return (int)(earned > a.pointCap ? a.pointCap : earned);
    }
    return base;
}
