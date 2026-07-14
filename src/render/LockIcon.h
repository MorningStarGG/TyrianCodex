#pragma once
#include <imgui.h>
#include "util/Textures.h"

// The travel padlock, shared by every surface that shows a waypoint's locked/unlocked state (the checklist's
// per-row lock, the Info Panel's "closest waypoint" indicator, ...). closed (confirmed-for-travel) = GOLD
// padlock (lock.png); open (not yet reached) = GREY open padlock (lock-open.png).
// Takes an explicit draw list so it works in any window/context.
namespace Render
{
    inline void DrawLock(ImDrawList *dl, ImVec2 p, float s, bool closed)
    {
        const char *file = closed ? "data\\textures\\ui\\lock.png" : "data\\textures\\ui\\lock-open.png";
        const ImU32 tint = closed ? IM_COL32(255, 221, 138, 255) : IM_COL32(150, 150, 150, 255);
        if (const Texture_t *t = Tex::GetFileTex(file); t && t->Resource)
            dl->AddImage((ImTextureID)t->Resource, p, ImVec2(p.x + s, p.y + s), ImVec2(0, 0), ImVec2(1, 1), tint);
    }
}
