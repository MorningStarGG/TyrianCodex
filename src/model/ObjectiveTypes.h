#pragma once
#include <cstdint>
#include <string>
#include <imgui.h>

// Single source of truth for the open-world objective types
// display group name, tracker accent colour, in-world marker kind, and GW2 dat icon
// asset id. Change a value here and every consumer (markers, step/trail colours, future settings
// groups) follows. Asset ids are gw2dat CDN ids - see Tex::GetTextureFromAssetId.
namespace Objective
{
    // The toggle TYPE a marker belongs to. Each value is an independent
    // show/hide group. M1.4 uses the first five; the rest are reserved for route signs / gathering /
    // dungeon-instruction markers (later milestones / instances).
    enum class MarkerKind
    {
        Waypoint,
        Poi,
        Vista,
        HeroPoint,
        Heart,
        RouteSigns,
        Gathering,
        DungeonInstruction,
    };

    struct Info
    {
        const char *Type;
        const char *GroupName;
        uint8_t R, G, B; // tracker accent colour (alpha supplied at draw time)
        MarkerKind Kind;
        uint32_t IconAssetId; // gw2dat asset id
        bool AutoComplete;    // visiting completes it (poi/waypoint); hearts/hero/vista need a manual tick (interact)
    };

    // Values lifted verbatim from ObjectiveTypes.cs; order echoes the GW2 map-icon palette.
    inline const Info *Get(const std::string &type)
    {
        static const Info kTable[] = {
            {"waypoint", "Waypoints", 130, 230, 150, MarkerKind::Waypoint, 157353, true},
            {"poi", "Points of Interest", 190, 220, 255, MarkerKind::Poi, 97461, true},
            {"vista", "Vistas", 140, 205, 255, MarkerKind::Vista, 358415, false},
            {"hero", "Hero Points", 190, 150, 255, MarkerKind::HeroPoint, 156626, false},
            {"heart", "Hearts", 255, 207, 110, MarkerKind::Heart, 102439, false},
        };
        for (const Info &i : kTable)
            if (type == i.Type)
                return &i;
        return nullptr;
    }

    // Proximity REACH radius (in CONTINENT units) at which a POINT objective counts as "reached" -- i.e. where the
    // blur ring is drawn AND where the guide auto-completes it. POIs + waypoints unlock in-game from a GENEROUS
    // range, so they get a wider reach than vistas/hero points (which anyway need an in-world interact). ONE source
    // of truth: world/MapRenderer draws the ring at this, and guide/Guidance fires the auto-complete at it
    // (converted to metres per map). Previously the ring showed this but completion used the tighter flat
    // Config.arrival -- so a POI you'd already unlocked in-game wouldn't tick until you walked ~5 m further in.
    inline float ReachContinent(const std::string &type)
    {
        return (type == "poi" || type == "waypoint") ? 42.f : 21.f;
    }

    // Tracker accent colour for a type at the given alpha (light grey fallback for unknown types).
    inline ImU32 ColorOf(const std::string &type, int alpha)
    {
        const Info *i = Get(type);
        return i ? IM_COL32(i->R, i->G, i->B, alpha) : IM_COL32(225, 225, 225, alpha);
    }

    // gw2dat icon asset id for a type; 0 (and false) when the type is unknown.
    inline bool TryIconAssetId(const std::string &type, uint32_t &id)
    {
        const Info *i = Get(type);
        id = i ? i->IconAssetId : 0u;
        return i != nullptr;
    }

    // In-world marker toggle group for a type; false when the type has no marker.
    inline bool TryMarkerKind(const std::string &type, MarkerKind &kind)
    {
        const Info *i = Get(type);
        if (i)
        {
            kind = i->Kind;
            return true;
        }
        return false;
    }
}
