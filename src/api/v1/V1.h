#pragma once
// The v1 (legacy) endpoint facade - the analogue of V2::Endpoints. Reached via `client.V1().<Family>()`.
// v1 is frozen + fully typed; every accessor returns a typed model (no bare raw). One header per root.
#include "Build.h"
#include "Colors.h"
#include "Continents.h"
#include "EventDetails.h"
#include "EventNames.h"
#include "Events.h"
#include "Files.h"
#include "GuildDetails.h"
#include "ItemDetails.h"
#include "Items.h"
#include "MapFloor.h"
#include "MapNames.h"
#include "Maps.h"
#include "RecipeDetails.h"
#include "Recipes.h"
#include "SkinDetails.h"
#include "Skins.h"
#include "WorldNames.h"
#include "Wvw.h"

namespace Api { class Connection; }
namespace Api::V1
{
    struct Endpoints
    {
        Connection* c = nullptr;

        BuildEndpoint         Build()         const { return BuildEndpoint(c); }
        ColorsEndpoint        Colors()        const { return ColorsEndpoint(c); }
        ContinentsEndpoint    Continents()    const { return ContinentsEndpoint(c); }
        EventDetailsEndpoint  EventDetails()  const { return EventDetailsEndpoint(c); }
        EventNamesEndpoint    EventNames()    const { return EventNamesEndpoint(c); }
        EventsEndpoint        Events()        const { return EventsEndpoint(c); }
        FilesEndpoint         Files()         const { return FilesEndpoint(c); }
        GuildDetailsEndpoint  GuildDetails()  const { return GuildDetailsEndpoint(c); }
        ItemDetailsEndpoint   ItemDetails()   const { return ItemDetailsEndpoint(c); }
        ItemsEndpoint         Items()         const { return ItemsEndpoint(c); }
        MapFloorEndpoint      MapFloor()      const { return MapFloorEndpoint(c); }
        MapNamesEndpoint      MapNames()      const { return MapNamesEndpoint(c); }
        MapsEndpoint          Maps()          const { return MapsEndpoint(c); }
        RecipeDetailsEndpoint RecipeDetails() const { return RecipeDetailsEndpoint(c); }
        RecipesEndpoint       Recipes()       const { return RecipesEndpoint(c); }
        SkinDetailsEndpoint   SkinDetails()   const { return SkinDetailsEndpoint(c); }
        SkinsEndpoint         Skins()         const { return SkinsEndpoint(c); }
        WorldNamesEndpoint    WorldNames()    const { return WorldNamesEndpoint(c); }
        WvwEndpoint           Wvw()           const { return WvwEndpoint(c); }
    };
}
