#pragma once
#include "app/Config.h"
#include "guide/FarmingGuidance.h"

// Visibility of the guide's completion/instance route, shared by its world and flat-map renderers.
// Farming overlays retain their independent controls. Geometry, completion and master-guide gates remain
// with the renderers; this policy combines the surface's trail toggle with temporary farming suppression.
namespace TrailVisibility
{
    enum class Surface { World, Map, Minimap };

    inline bool Visible(const Config& config, const GuideState& state, Surface surface)
    {
        bool shown = false, hideWhileFarming = false;
        switch (surface)
        {
            case Surface::World:
                shown = config.showTrail;
                hideWhileFarming = config.hideCompletionTrailWhileFarming;
                break;
            case Surface::Map:
                shown = config.showMapTrail;
                hideWhileFarming = config.hideMapCompletionTrailWhileFarming;
                break;
            case Surface::Minimap:
                shown = config.showMiniMapTrail;
                hideWhileFarming = config.hideMiniMapCompletionTrailWhileFarming;
                break;
        }
        return shown && (!hideWhileFarming || state.inDungeon || !FarmingGuidance::Active(state));
    }
}
