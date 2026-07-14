#pragma once
#include "render/glyphs/Glyphs.h"
#include <string>
#include <vector>

// The HUD launcher bar's button registry (the icons flanking the centre clock). HUD-only data; the Info Panel
// has its own catalog (src/ui/infopanel/InfoData.h). The HUD (Hud.cpp) maps each button key to its action.
namespace HudButtons
{
    struct Button
    {
        const char*   key;
        const char*   label;
        Render::Glyph glyph;
        bool          defaultOn;    // in the default bar (shown out of the box)
        int           defaultSide;  // where it lands: 0 Left / 1 Right
        int           defaultOrder; // order among default-on buttons (0 = unspecified / not-default)
    };
    const std::vector<Button>& All();                 // built once; default left-to-right order
    const Button*              Find(const char* key);
}
