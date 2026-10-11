#pragma once
#include "model/EventAreas.h"
#include "guide/EventProgress.h"
#include <functional>

// Visibility is derived from history, never destructive. Filter before limiting
// either group so a full first page cannot hide remaining or completed entries.
struct EventList
{
    std::vector<NearbyEventArea> Remaining, Completed;
    int RemainingCount = 0, CompletedCount = 0;
};
EventList BuildEventList(const std::vector<NearbyEventArea>& nearby, const EventProgressStore& history,
                        const std::string& character, int limit, bool showCompleted,
                        const std::function<bool(const EventArea&)>& matches = {});
