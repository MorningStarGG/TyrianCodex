#include "guide/EventList.h"
#include <set>

EventList BuildEventList(const std::vector<NearbyEventArea>& nearby, const EventProgressStore& history,
                        const std::string& character, int limit, bool showCompleted,
                        const std::function<bool(const EventArea&)>& matches)
{
    EventList list;
    if (limit <= 0) return list;
    const auto& ids = history.CompletedIds(character);
    const std::set<std::string> completed(ids.begin(), ids.end());
    for (const auto& entry : nearby)
    {
        if (!entry.Area || (matches && !matches(*entry.Area))) continue;
        const bool done = completed.count(entry.Area->EventId) != 0;
        auto& count = done ? list.CompletedCount : list.RemainingCount;
        ++count;
        if (done && !showCompleted) continue;
        auto& visible = done ? list.Completed : list.Remaining;
        if ((int)visible.size() < limit) visible.push_back(entry);
    }
    return list;
}
