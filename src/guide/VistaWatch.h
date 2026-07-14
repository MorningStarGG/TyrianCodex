#pragma once
#include "util/Projection.h"   // Math::Vec3
#include <cstdint>
class App;

// -----------------------------------------------------------------------------------------------------
// Vista auto-complete. Completing a vista plays a camera cinematic during which the game flips IsGameplay
// FALSE and FREEZES MumbleLink (verified in-game: UITick stops, so the camera can't be read). The cinematic
// is therefore detected by the IsGameplay true->false->true BLIP while you're standing at an incomplete
// vista: arm the nearest in-reach incomplete vista on the last gameplay frame, then on return -- same map,
// still at the vista (so a waypoint/loading teleport doesn't count) -- mark it complete via CompleteStep
// (the guide then advances to the next objective). Pure Nexus/MumbleLink state; no memory reads, no pixels.
// Called every frame from Render (even while IsGameplay is false) with the live avatar/map/gameplay state.
// -----------------------------------------------------------------------------------------------------
namespace VistaWatch
{
    void Update(App& app, const Math::Vec3& avatar, uint32_t mapId, bool isGameplay);
}
