#pragma once
#include <imgui.h>   // ImU32

// Small UI helpers shared by more than one settings tab. (Single-tab helpers stay with their tab.)

// Draw a status line CENTERED across the current content width (wraps if long), as a small styled banner.
// Used for the Zones / Dungeons / Journal tab status headings. Defined in ui/tabs/SettingsCommon.cpp.
void CenteredLabel(const char* text, ImU32 color, float size, float weight = -1.f, bool stroke = true);

// Settings text scale -- the ONE source for the hand-drawn text sizes in the settings tab + its sections, so
// the same role reads the same size everywhere (it had drifted to a mix of 14/15/16/17/18/20 literals). The
// setting-row LABELS and the controls keep their own (already-uniform) sizes; these three cover the inline
// text the section bodies draw: headers, help/description prose, status lines and readouts.
namespace SettingsText
{
    inline constexpr float Header = 20.f;  // section sub-headers (Diagnostics groups, search groups, "Hidden widgets")
    inline constexpr float Body   = 18.f;  // help / description prose, status lines, readout rows
    inline constexpr float Hint   = 16.f;  // secondary dim notes, list-row item names
}

// Left-aligned wrapped paragraph at the cursor -- the recurring settings "help prose" block. Measures, reserves,
// and draws `text` wrapped to the content width at `size` (defaults to the Body role). Defined in the .cpp.
void SettingsParagraph(const char* text, ImU32 color, float size = SettingsText::Body);
