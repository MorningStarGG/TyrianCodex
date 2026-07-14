#pragma once
#include "ui/Gw2Ui.h"
#include <cstdio>

// Shared UI copy -- the ONE home for recurring prompts that were hand-typed (and drifted) per surface. The
// "you need an API key" family in particular was phrased ~4 ways across the viewer + dashboard; centralize it
// so the wording stays identical everywhere and a reword is a one-line change.
namespace UiStr
{
    // Internal: a small ring of static buffers so several composed strings can be live at once within a frame
    // (immediate-mode safe -- ImGui copies the glyphs at draw time; we just need the buffer intact until then).
    // inline => one shared instance across TUs; ImGui is single-threaded so no locking is needed.
    inline char* NextBuf()
    {
        static char bufs[4][96];
        static int  i = 0;
        i = (i + 1) & 3;
        return bufs[i];
    }

    // "Add an API key for <purpose>." -- the recurring no-key prompt. `purpose` follows "for " and is the
    // user-facing thing the key unlocks (e.g. "level-based picks"), NOT a raw scope id. The verb/opener is
    // fixed here so every gated surface reads the same; callers only supply what the key is FOR.
    inline const char* NoKey(const char* purpose)
    {
        char* b = NextBuf();
        std::snprintf(b, 96, "Add an API key for %s.", purpose);
        return b;
    }

    // Width-aware NoKey: the long-purpose sentence if it fits one line within availW at fontSize, else the
    // short-purpose one. Use where the prompt MUST stay a single line (a fixed-height card); wrap-aware
    // surfaces can call NoKey(longPurpose) directly (it wraps cleanly). Room-based, not surface-based.
    inline const char* NoKeyPrompt(const char* longPurpose, const char* shortPurpose, float fontSize, float availW)
    {
        const char* full = NoKey(longPurpose);
        return (Gw2Ui::MeasureWidth(full, fontSize) <= availW) ? full : NoKey(shortPurpose);
    }

    // "API key needs the '<scope>' permission." -- a key IS present but is missing a required scope. `scope`
    // is the raw scope id shown to the user (e.g. "characters", "wallet"), matching account.arena.net.
    inline const char* NeedScope(const char* scope)
    {
        char* b = NextBuf();
        std::snprintf(b, 96, "API key needs the '%s' permission.", scope);
        return b;
    }

    // The standalone no-key note for a whole gated panel (dashboard widgets) -- same "Add an API key" opener
    // as NoKey, but points the user at where to fix it rather than naming a purpose (the panel title is the
    // context). Used by DashApi::Gate.
    inline constexpr const char* NoKeyGeneric = "Add an API key in Options to enable this.";
}
