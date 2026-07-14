#pragma once
#include "app/State.h"
#include "guide/Progress.h"
#include "model/ObjectiveTypes.h"
#include <algorithm>
#include <string>

// Visiting completes a type when its Objective::Info says so (poi/waypoint); hearts/hero points/vistas need a
// manual tick (no API signal, and they require an in-world interact). The auto-complete flag lives in the one
// type table (model/ObjectiveTypes.h) alongside the rest of the type metadata.
inline bool IsAutoType(const std::string& t) { const Objective::Info* i = Objective::Get(t); return i && i->AutoComplete; }

// Mark a step complete and record it on the undo stack so the "Back" keybind can undo the most recent tick
// (auto or manual). Shared by the guidance auto-complete, the arrow/tray/viewer/keybind actions -> a single
// free helper over the progress store + runtime state (no-op if the id is empty or already complete).
inline void CompleteStep(ProgressStore& progress, GuideState& st, const std::string& id)
{
    if (id.empty() || progress.IsComplete(st.currentChar, id)) return;
    progress.MarkComplete(st.currentChar, id);
    st.undoStack.push_back(id);
}

// The viewer/checklist "uncheck": clears the completed flag for a step. Leaves a confirmed waypoint unlocked
// (confirmed is immune to reset, like the Checklist tab), and drops the id from the undo stack.
inline void UncompleteStep(ProgressStore& progress, GuideState& st, const std::string& id)
{
    if (id.empty()) return;
    progress.SetComplete(st.currentChar, id, false);
    st.undoStack.erase(std::remove(st.undoStack.begin(), st.undoStack.end(), id), st.undoStack.end());
}
