#pragma once
#include <imgui.h>

namespace Render
{
    enum class Glyph
    {
        Grip,
        Bell,       // event reminders: filled = reminders on, disabled = event has no schedule
        Trash,
        ArrowUp,
        ArrowDown,
        Plus,
        Minus,
        Pencil,
        Duplicate,
        ChromeBar,
        ChromeHover,
        WidthFull,
        WidthHalf,
        Guide,
        MapPin,
        Copy,
        Star,
        Refresh,
        Navigate,
        Trail,
        Marker,
        Map,
        MiniMap,
        Cog,
        Bag,
        Items,      // item catalogue (a crate) -- distinct from Bag (inventory)
        Checklist,
        Dungeon,
        Grid,
        Eye,        // reveal (secret shown)
        EyeOff,     // hidden (secret masked) -- slashed eye
        Check,      // status: granted / done
        Cross,      // status: missing / not granted
        CaretRight, // collapsed (expandable, not expanded)
        CaretDown,  // expanded
        ResizeGrip, // window corner resize affordance (parallel diagonal hatches); mirrorX for the left corner
        Clock,      // event timers: a clock face + two hands
        Coins,      // trading post: two overlapping coins
        Cart,       // crafting cart: a shopping cart / basket
        ExternalLink, // open-in-browser: a window frame + an arrow leaving to the top-right
        Globe,        // browser: a globe (circle + equator + meridians)
        Book,         // library: a closed book (cover + spine + pages)
        Chart,        // session tracker: three rising bars on a baseline (earnings over time)
        Fish,         // fishing: a round body + a triangular tail + an eye dot
        Sun,          // day phase: a disc with eight rays
        Moon,         // night phase: a thick crescent
        Horizon       // dusk/dawn phase: a half-sun rising over a horizon line (twilight)
    };

    struct GlyphStyle
    {
        bool filled = false;
        bool disabled = false;
        bool shadow = true;
        bool mirrorX = false;   // horizontally mirror (e.g. ResizeGrip on a left corner)
    };

    // Draw-only procedural glyph art. Input, layout, and tooltips stay with the UI caller.
    void DrawGlyph(ImDrawList* dl, ImVec2 center, float size, Glyph glyph, ImU32 color,
                   GlyphStyle style = {});

    // A horizontal ornamental gold rule -- a small center diamond flanked by tapered, edge-fading bars -- used
    // as the Zone Display banner's divider. Spans up to +/-halfW around `center`; `reveal` (0..1) wipes it out
    // from the centre (the bars grow, the diamond pops in first). `height` sets the diamond size + bar thickness.
    // `minor` = a subtler secondary divider: a thin tapered hairline with NO diamond (sets the major rule apart).
    void DrawZoneRule(ImDrawList* dl, ImVec2 center, float halfW, float height, ImU32 color, float reveal = 1.f, bool minor = false);
}
