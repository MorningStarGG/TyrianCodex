#pragma once
// -------------------------------------------------------------------------------------------------
// Gw2Ui INTERNAL shared core -- NOT part of the public API (src/ui/Gw2Ui.h). The widget framework was
// split out of one oversized Gw2Ui.cpp into per-area module .cpp files under src/ui/gw2ui/; this header
// holds the cross-module internals every module needs so the split is clean (one definition, in
// Gw2UiInternal.cpp): the texture helper, the GW2 font/text-render engine, and the keybind-capture flag.
// Each module .cpp does `using namespace Gw2Ui::detail;` after including this, so the carved code keeps
// referring to T/Asset/UiFont/DrawLabelCore/... unqualified exactly as it did inside the monolith.
// -------------------------------------------------------------------------------------------------
#include <cstdint>
#include <vector>
#include <imgui.h>

#include "ui/Gw2Ui.h"   // Gw2Ui::HAlign / VAlign for DrawLabelCore

namespace Gw2Ui
{
namespace detail
{
    // --- texture helper (srv + native pixel size) -- the one local-first GW2/CDN image loader ----------
    struct T { void* srv = nullptr; float w = 0.f, h = 0.f; };
    T    Asset(uint32_t id);              // local-first gw2dat asset (bundled under data/textures/ui/dat, else CDN)
    T    File(const char* relPath);       // a bundled data/ texture by path
    void Img(ImDrawList* dl, const T& t, ImVec2 a, ImVec2 b,
             ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1), ImU32 col = IM_COL32_WHITE);
    void Img3H(ImDrawList* dl, const T& t, ImVec2 a, ImVec2 b, float capPx, ImU32 col = IM_COL32_WHITE);

    // --- GW2 font + text-render engine (Menomonia, faux-bold stroke) -----------------------------------
    // g_gw2Regular/g_gw2Italic are the TOP-OF-LADDER (24px) faces -- the back-compat default + the value
    // Gw2Italic()/UiFontResolved() hand out, AND the sentinels ResolveFace recognizes as "GW2 font, pick the
    // nearest rung". g_fontLadder is the full native-size ladder (set by SetGw2FontLadder); EMPTY = no GW2
    // font (revert to the Nexus font). The ladder is always callback-maintained -- any rung may be null after
    // a Nexus atlas rebuild, and the picker tolerates null rungs / an empty ladder (falls back).
    extern ImFont* g_gw2Regular;          // the real Menomonia (24px rung), set by SetGw2Font*; null = Nexus font
    extern ImFont* g_gw2Italic;
    extern std::vector<Gw2Ui::FontBake> g_fontLadder;   // native-size ladder, sorted ascending by px
    extern int     g_fontRevision;
    extern std::vector<float> g_textScale; // PushTextScale stack; 1.0 = no scaling
    float   CurTextScale();
    ImFont* UiFont(ImFont* f);            // resolve the face (NO size): caller font > Menomonia 24 > Nexus > ImGui

    // Pick the ladder bake NEAREST a requested px (exact-match wins; else min |log(px/rung)|). Returns the
    // italic slot when italic (falling back to that rung's regular). null when the ladder is empty.
    ImFont* FontForSizePx(float px, bool italic);
    // The SINGLE resolver used by BOTH measure and draw so they never diverge: our font sentinels
    // (g_gw2Regular / g_gw2Italic) -> nearest rung (regular/italic); any other non-null caller font -> verbatim;
    // null -> nearest regular rung; ladder empty -> Nexus/ImGui fallback.
    ImFont* ResolveFace(ImFont* callerFont, float requestedPx);

    void    DrawLabelCore(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImFont* font,
                          ImU32 color, bool stroke, Gw2Ui::HAlign h, Gw2Ui::VAlign v, float fontSize,
                          float wrap = 0.f, float weight = -1.f);

    // Which KeybindAssigner is capturing keys (nullptr = none). File-scope so BeginWindow can skip its
    // Escape-to-close while a bind is being captured.
    extern const char* s_kbCapturing;
}
}
