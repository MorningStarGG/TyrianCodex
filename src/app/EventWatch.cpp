#include "app/EventWatch.h"
#include "app/App.h"
#include "app/EventFavorites.h"
#include "model/EventTimers.h"
#include "ui/dashboard/Notify.h"

#include <algorithm>
#include <imgui.h>
#include <map>
#include <string>
#include <vector>

namespace
{
    // The lead times, longest first. Firing the LONGEST unfired rung that has been crossed (rather than every
    // crossed rung) is what stops a burst when several are passed between ticks.
    struct Rung
    {
        int seconds;
        bool Config::*enabled;
        const char* label;
    };
    const Rung kRungs[] = {
        {600, &Config::eventRemind10m, "10 minutes"},
        {300, &Config::eventRemind5m, "5 minutes"},
        {60, &Config::eventRemind1m, "1 minute"},
        {30, &Config::eventRemind30s, "30 seconds"},
        {10, &Config::eventRemind10s, "10 seconds"},
    };
    constexpr int kRungCount = (int)(sizeof(kRungs) / sizeof(kRungs[0]));

    // Per-event latch. `occurrence` identifies WHICH run we are tracking (UTC second-of-day of its start), so
    // the mask resets by itself when the event comes round again.
    struct Latch
    {
        int occurrence = -1;
        unsigned firedMask = 0;
    };
    std::map<std::string, Latch> g_latch;
    double g_nextTick = 0.0;

    // key -> event, built once. The belled set is small but the event list is not, and this runs on a timer.
    std::map<std::string, const EventTimer*> g_byKey;
    size_t g_indexedFor = (size_t)-1;

    const EventTimer* FindEvent(const App& app, const std::string& key)
    {
        if (g_indexedFor != app.eventTimers.Events().size())
        {
            g_byKey.clear();
            for (const EventTimer& e : app.eventTimers.Events())
                if (!e.Key.empty()) g_byKey[e.Key] = &e;
            g_indexedFor = app.eventTimers.Events().size();
        }
        auto it = g_byKey.find(key);
        return it != g_byKey.end() ? it->second : nullptr;
    }

    std::string PluralEvents(int n)
    {
        return std::to_string(n) + (n == 1 ? " event" : " events");
    }
}

void EventWatch::Tick(App& app)
{
    if (!app.eventTimers.Loaded()) return;
    if (!app.config.notifyEnabled || !app.config.notifyKind[(int)Notify::Kind::Event]) return;   // master gates

    // ~2 Hz: plenty for a 10-second rung and nowhere near per-frame work.
    const double now = ImGui::GetTime();
    if (now < g_nextTick) return;
    g_nextTick = now + 0.5;

    const std::vector<std::string>& keys = EventFavorites::NotifyKeys();
    if (keys.empty())
    {
        if (!g_latch.empty()) g_latch.clear();   // nothing belled -> forget the latches
        return;
    }

    const int nowSec = EventSchedule::UtcSecondOfDay();

    // What fired this tick, grouped by (occurrence start, rung) -- events sharing a start time cross the same
    // rung on the same tick, which is exactly the "starting together" the combine option means.
    std::map<std::pair<int, int>, std::vector<const EventTimer*>> groups;

    for (const std::string& key : keys)
    {
        const EventTimer* ev = FindEvent(app, key);
        if (!ev || ev->ScheduleMinutes.empty()) continue;

        const EventSchedule::Occurrence occ = EventSchedule::Next(*ev, nowSec);
        if (occ.active || occ.startSecOfDay < 0) continue;   // running now -> nothing to count down to

        Latch& latch = g_latch[key];
        const bool newOccurrence = (latch.occurrence != occ.startSecOfDay);
        if (newOccurrence)
        {
            latch.occurrence = occ.startSecOfDay;
            latch.firedMask = 0;
            // Cold start: seeing this run for the first time with only 20s left must not dump 10m/5m/1m/30s all
            // at once. Everything already in the past for this run counts as spent.
            for (int i = 0; i < kRungCount; ++i)
                if (occ.secondsUntil <= kRungs[i].seconds) latch.firedMask |= (1u << i);
            continue;   // never announce on the tick we first saw the run
        }

        // Longest crossed-but-unfired rung only, so a stall between ticks yields one reminder rather than four.
        for (int i = 0; i < kRungCount; ++i)
        {
            if (!(app.config.*(kRungs[i].enabled))) continue;
            if (latch.firedMask & (1u << i)) continue;
            if (occ.secondsUntil > kRungs[i].seconds) continue;
            latch.firedMask |= (1u << i);
            groups[{occ.startSecOfDay, i}].push_back(ev);
            break;
        }
        // Rungs that are disabled or were overtaken still count as spent, or a later tick would fire them late.
        for (int i = 0; i < kRungCount; ++i)
            if (occ.secondsUntil <= kRungs[i].seconds) latch.firedMask |= (1u << i);
    }

    for (auto& g : groups)
    {
        const char* when = kRungs[g.first.second].label;
        std::vector<const EventTimer*>& evs = g.second;
        if (evs.empty()) continue;

        if (app.config.eventRemindCombine && evs.size() > 1)
        {
            std::string body;
            for (size_t i = 0; i < evs.size(); ++i)
            {
                if (i) body += "\n";
                body += evs[i]->Name;
            }
            // No single place to travel to -> the board, so the click still goes somewhere useful.
            Notify::Push(Notify::Kind::Event, PluralEvents((int)evs.size()) + " in " + when, body, 0,
                         Notify::Action::OpenTimers);
            continue;
        }
        for (const EventTimer* ev : evs)
            Notify::Push(Notify::Kind::Event, ev->Name, std::string("Starts in ") + when, 0,
                         Notify::Action::ShowEvent, ev->Key);
    }
}
