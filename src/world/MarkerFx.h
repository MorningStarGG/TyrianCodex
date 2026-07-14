#pragma once
#include "app/Config.h"
#include "app/State.h"       // FrameCtx
#include "util/Projection.h" // Math::WorldToScreen / Vec3
#include <imgui.h>
#include <algorithm>
#include <cmath>

// Global marker modifiers, shared by every in-world marker renderer (objective / dungeon / gather /
// fish) and the trail: combat fade, a global far cap, a global opacity ceiling, and the "between camera and
// character" declutter dim. All from MumbleLink geometry -- no game depth buffer. Defaults make each a no-op.
namespace MarkerFx
{
    // Combat fade factor (1 = none): a low wash while you're fighting. `enabled` is the trail's or the markers'
    // own in-combat toggle (they're independent).
    inline float CombatFade(bool enabled, const FrameCtx &fc)
    {
        return (enabled && fc.inCombat) ? 0.15f : 1.f;
    }

    // Clamp a 0..1 marker opacity to the global ceiling ("Max marker opacity"; 1 = no cap).
    inline float OpacityCap(float a, const Config &cfg)
    {
        return std::min(a, std::clamp(cfg.maxMarkerOpacity, 0.f, 1.f));
    }

    // "Between camera and character" -- computed ONCE per marker loop. A marker NEARER the camera than the player
    // AND within a screen radius of the player's projected position is occluding your character, so dim it.
    struct Between
    {
        ImVec2 playerScreen{};
        float playerDistCam = 0.f;
        float radius = 0.f;
        bool on = false;
    };
    inline Between BeginBetween(const Config &cfg, const FrameCtx &fc)
    {
        Between b;
        if (!cfg.fadeBetweenCamChar)
            return b;
        float sx, sy, depth;
        b.on = Math::WorldToScreen(Math::Vec3{fc.avatar.x, fc.avatar.y + 1.0f, fc.avatar.z}, fc.vp, fc.W, fc.H, sx, sy, depth);
        b.playerScreen = ImVec2(sx, sy);
        const float dx = fc.avatar.x - fc.camPos.x, dy = fc.avatar.y - fc.camPos.y, dz = fc.avatar.z - fc.camPos.z;
        b.playerDistCam = std::sqrt(dx * dx + dy * dy + dz * dz);
        b.radius = 0.16f * fc.H;
        return b;
    }
    inline float BetweenDim(const Between &b, ImVec2 markerScreen, float markerDistCam)
    {
        if (!b.on || markerDistCam >= b.playerDistCam)
            return 1.f; // behind/at the player -> not occluding
        const float dx = markerScreen.x - b.playerScreen.x, dy = markerScreen.y - b.playerScreen.y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d >= b.radius)
            return 1.f;
        return std::clamp(d / b.radius, 0.25f, 1.f); // dim toward the centre (floor 0.25)
    }
}
