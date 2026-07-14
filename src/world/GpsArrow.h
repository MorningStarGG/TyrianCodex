#pragma once
#include "app/Config.h"
#include "app/State.h"
#include "guide/TrailFollower.h" // Follow::Vec2
#include "util/Projection.h"     // Math::Vec3

// -----------------------------------------------------------------------------------------------------
// GpsArrow: the 2D HUD compass arrow + caption. Owns its eased screen rotation.
// Draw() points the arrow by the horizontal bearing from the camera heading to `aim`, tints by alignment,
// runs its own invisible interaction window (right-click menu + unlocked left-drag to reposition), draws the
// TCModernNav nav texture + an objective/distance/ETA caption, and returns any menu action the caller must handle.
// -----------------------------------------------------------------------------------------------------
class GpsArrow
{
public:
    // directAim (optional): the objective's straight-line world XZ. When non-null, a glowing arc is drawn on a
    // ring around the arrow at that bearing -- the DIRECT direction to the objective, independent of where the
    // arrow points (Hybrid aims at the trail entrance). The caller passes it only when it should show.
    // Returns the right-click menu id the caller must dispatch via QuickMenu::ArrowDispatch (-1 = nothing picked).
    int Draw(const Follow::Vec2 &aim, const Follow::Vec2 &playerXz, const Math::Vec3 &camFwd,
             bool arrived, float travel, const char *captionName, float W, float H, bool dungeon,
             Config &cfg, GuideState &st, bool &settingsDirty, float speedEma, TargetOverrideKind targetKind,
             const Follow::Vec2 *directAim = nullptr, void *iconTex = nullptr);

    void ResetEase() { haveArrowRot_ = false; } // re-snap on zone switch / target change / progress reset

private:
    float arrowRot_ = 0.f; // eased screen rotation
    bool haveArrowRot_ = false;
};
