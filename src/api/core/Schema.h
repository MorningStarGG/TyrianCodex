#pragma once
#include <string>

// Connection-wide constants for the GW2 Web API. The schema version pins the response shape so the API can
// evolve without breaking our typed models: GW2 rounds an unknown `?v=` DOWN to the nearest published
// schema, and "latest" tracks the newest (recommended by the wiki). The handful of fields we read (token
// scopes, character level, achievement done-flags, map names) are stable across every schema, so "latest"
// is safe; pin a dated schema here instead if a future model needs a frozen shape.
namespace Api
{
    // Host + base path. Web API v2 lives on api.guildwars2.com; map tiles on tiles.guildwars2.com (handled
    // by the raw-bytes transport, not the JSON client). render.guildwars2.com / assets.gw2dat.com are image
    // CDNs used by the texture layer (src/Textures.cpp), not here.
    constexpr const char* kApiHost   = "api.guildwars2.com";
    constexpr const char* kApiScheme = "https";

    // Schema version sent as `?v=`. "latest" = newest published schema (stable for our fields).
    constexpr const char* kSchemaVersion = "latest";

    // Sent as the HTTP User-Agent on every request (etiquette + lets ArenaNet identify the client).
    constexpr const char* kUserAgent = "TyrianCodex/0.1 (+https://github.com/MorningStarGG/TyrianCodex)";
}
