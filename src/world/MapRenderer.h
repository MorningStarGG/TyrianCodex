#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "guide/Travel.h"
#include "guide/TrailFollower.h"
#include "guide/Progress.h"
#include "util/DpiWatcher.h" // DpiState
#include <imgui.h>

// The in-game map/minimap projection (continent -> screen), PUBLISHED by MapRenderer::Draw each frame so the
// click handler reuses the EXACT same math (no duplicated geometry that could drift). Compass rotation applies
// on the minimap, not the open map.
struct MapProj
{
    bool valid = false, mapOpen = false, rotate = false;
    ImVec2 bMin, bSize;
    float scale = 0.f, cr = 1.f, sr = 0.f, cenX = 0.f, cenY = 0.f;
};

// -----------------------------------------------------------------------------------------------------
// MapRenderer: draws the active route as a flat polyline + objective/travel/personal pins on the in-game
// FULLSCREEN MAP or the MINIMAP/COMPASS corner. Owns the published MapProj so the
// Alt+click handler (HandleClick) places/clears a personal target using the SAME projection -> the pin lands
// exactly on GW2's own marker. Draw publishes MapProj UNCONDITIONALLY (so a minimap click works even when
// nothing is drawn). The DPI-corrected scale comes from DpiWatcher's DpiState.
// -----------------------------------------------------------------------------------------------------
class MapRenderer
{
public:
    void Draw(const Config &cfg, const GuideState &st, Travel::Controller &travel,
              Follow::TrailFollower &follower, ProgressStore &progress, const DpiState &dpi);
    void UpdateAssist(const Config &cfg, GuideState &st, ProgressStore &progress);
    void HandleClick(const Config &cfg, GuideState &st, Travel::Controller &travel);

private:
    ImVec2 ContinentToMapScreen(float ccx, float ccy) const;           // continent -> screen (reads mapProj_)
    void MapScreenToContinent(ImVec2 s, float &ccx, float &ccy) const; // screen -> continent (exact inverse)

    MapProj mapProj_; // published by Draw, read by the projection methods + HandleClick
};
