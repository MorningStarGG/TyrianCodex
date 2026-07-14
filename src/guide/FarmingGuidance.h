#pragma once
#include "util/Projection.h"   // Math::Vec3
#include <vector>
class App;
struct FarmNode;

// -----------------------------------------------------------------------------------------------------
// FarmingGuidance: drives the nav arrow for an active gathering RUN. The run is the shared State.farmRunActive
// flag (set from the viewer Farming chip / the dashboard toggle widget / tray); when on, entry.cpp sets
// State.activeContent = Farming (see ui/viewer/ViewerLayout FarmingRunActive). Update() aims the arrow at the
// NEAREST enabled, not-yet-gathered node in the selected category (Config.gatherCategory + GatherShown), crossing
// nodes off on arrival (session-only; nodes respawn). A no-op when farming isn't active.
// Self-contained (pulls the arrow / snapshot / speed / target-override from App).
// -----------------------------------------------------------------------------------------------------
namespace FarmingGuidance
{
    void Update(App& app, float W, float H, const Math::Vec3& avatar, const Math::Vec3& camFwd);
    bool Active(const App& app);

    // The nearest enabled, not-yet-gathered nodes in the selected category, nearest-first (up to maxN). Shared by
    // the dashboard widgets -- Current Objective shows [0]; Upcoming shows the rest. Uses the live player pos.
    std::vector<const FarmNode*> NearestNodes(const App& app, int maxN);
}
