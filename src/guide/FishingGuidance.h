#pragma once
#include "util/Projection.h"   // Math::Vec3
#include <vector>
class App;
struct FishHole;

// -----------------------------------------------------------------------------------------------------
// FishingGuidance: drives the nav arrow for an active fishing RUN (mirrors FarmingGuidance). The run is the shared
// State.fishRunActive flag; when on, entry.cpp sets State.activeContent = Fishing. Update() aims the arrow at the
// NEAREST not-yet-visited hole whose kind matches a WATCHED fish (FishHoles::WatchedTypes; an empty watchlist =>
// the nearest hole of any kind), crossing holes off on arrival (session-only). A no-op when fishing isn't active.
// -----------------------------------------------------------------------------------------------------
namespace FishingGuidance
{
    void Update(App& app, float W, float H, const Math::Vec3& avatar, const Math::Vec3& camFwd);
    bool Active(const App& app);

    // Nearest not-yet-visited holes matching the watchlist, nearest-first (up to maxN). Shared by the dashboard /
    // tab. Uses the live player pos.
    std::vector<const FishHole*> NearestHoles(const App& app, int maxN);
}
