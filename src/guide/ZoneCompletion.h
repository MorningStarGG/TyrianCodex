#pragma once
#include <set>
#include <string>
#include <vector>
#include "model/Dataset.h"
#include "model/ObjectiveTypes.h"
#include "guide/Progress.h"

// Shared zone-completion accounting. "Done" = in the COMPLETED set (proximity/manual) OR the CONFIRMED set
// (reached waypoints live ONLY there). Counts only the five trackable kinds (waypoint/poi/vista/hero/heart),
// matching the dashboard Zone Progress widget's grand total. ONE owner for the count logic -- the dashboard
// widget and the Info Panel "Zone Completion" text/tooltip both call this instead of each rebuilding the set.
namespace Guide
{
    struct ZoneCounts
    {
        int done = 0, total = 0;                  // overall (trackable objectives)
        int kindDone[5] = {}, kindTotal[5] = {};  // per kind: 0 waypoint, 1 poi, 2 vista, 3 hero, 4 heart
    };

    inline ZoneCounts ZoneCompletion(const ProgressStore& progress, const std::string& character, const Zone& zone)
    {
        ZoneCounts c;
        const std::vector<std::string>& doneIds = progress.CompletedIds(character);
        const std::vector<std::string>& confIds = progress.ConfirmedIds(character);
        std::set<std::string> done(doneIds.begin(), doneIds.end());
        done.insert(confIds.begin(), confIds.end());
        for (const Step& s : zone.Steps)
        {
            const Objective::Info* i = Objective::Get(s.Type);
            const int k = (i && (int)i->Kind <= (int)Objective::MarkerKind::Heart) ? (int)i->Kind : -1;
            if (k < 0) continue;
            ++c.total; ++c.kindTotal[k];
            if (done.count(s.StepId)) { ++c.done; ++c.kindDone[k]; }
        }
        return c;
    }
}
