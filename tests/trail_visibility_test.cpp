#include "world/TrailVisibility.h"
#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void ExpectSurfaces(const Config& config, const GuideState& state, int expected)
    {
        using Surface = TrailVisibility::Surface;
        const int actual = (TrailVisibility::Visible(config, state, Surface::World) ? 1 : 0) |
                           (TrailVisibility::Visible(config, state, Surface::Map) ? 2 : 0) |
                           (TrailVisibility::Visible(config, state, Surface::Minimap) ? 4 : 0);
        Check(actual == expected, "Completion trail visibility must match the independent surface choices and active mode");
    }

    void CheckVisibility()
    {
        Config config;
        GuideState state;
        Instance farm;
        state.farm = &farm;
        state.farmRunActive = true;
        state.activeContent = ContentType::Farming;
        ExpectSurfaces(config, state, 7); // New settings are opt-in.
        for (int hide = 0; hide < 8; ++hide)
        for (int shown = 0; shown < 8; ++shown)
        {
            config.hideCompletionTrailWhileFarming = (hide & 1) != 0;
            config.hideMapCompletionTrailWhileFarming = (hide & 2) != 0;
            config.hideMiniMapCompletionTrailWhileFarming = (hide & 4) != 0;
            config.showTrail = (shown & 1) != 0;
            config.showMapTrail = (shown & 2) != 0;
            config.showMiniMapTrail = (shown & 4) != 0;
            state.activeContent = ContentType::Farming;
            state.farm = &farm;
            state.inDungeon = false;
            state.farmRunComplete = false;
            ExpectSurfaces(config, state, shown & ~hide);

            // Idle/completed gathering and "show when complete" must not re-enable a suppressed trail.
            state.guidanceSnapshot.active = false;
            state.farmRunComplete = true;
            config.showTrailWhenComplete = config.showMapTrailWhenComplete = config.showMiniMapTrailWhenComplete = true;
            ExpectSurfaces(config, state, shown & ~hide);
            state.guidanceSnapshot.active = true;
            ExpectSurfaces(config, state, shown & ~hide);

            // Stopping or pausing for travel restores the exact master-toggle combination, including OFFs.
            state.activeContent = ContentType::OpenWorld;
            ExpectSurfaces(config, state, shown);
            state.activeContent = ContentType::Fishing;
            ExpectSurfaces(config, state, shown);
            state.activeContent = ContentType::Dungeon;
            state.inDungeon = true;
            ExpectSurfaces(config, state, shown);
            // A transition with a stale farming mode must still preserve the instance route.
            state.activeContent = ContentType::Farming;
            ExpectSurfaces(config, state, shown);

            // A requested run on a map without farming data is not an effective farming run.
            state.inDungeon = false;
            state.farm = nullptr;
            ExpectSurfaces(config, state, shown);
            // Returning to a supported farming map applies the choices without changing their settings.
            state.farm = &farm;
            ExpectSurfaces(config, state, shown & ~hide);
        }
    }
}

int main()
{
    try
    {
        CheckVisibility();
        std::cout << "Completion trail visibility checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
