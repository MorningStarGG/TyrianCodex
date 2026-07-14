#pragma once

#include "guide/Progress.h"
#include "model/Dataset.h"

#include <string>

inline bool StepIsDone(const ProgressStore& progress, const std::string& character, const Step& step)
{
    return progress.IsComplete(character, step.StepId) ||
           (step.Type == "waypoint" && progress.IsConfirmed(character, step.StepId));
}

// A zone is "complete" for a character when it has objectives and EVERY one is done -- the same all-done test
// the viewer's "head here next" uses to skip finished zones. An empty/unguided zone is never "complete".
inline bool ZoneComplete(const ProgressStore& progress, const std::string& character, const Zone& zone)
{
    if (zone.Steps.empty()) return false;
    for (const Step& s : zone.Steps)
        if (!StepIsDone(progress, character, s)) return false;
    return true;
}
