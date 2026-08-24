// Gw2Ui shared internal core -- definitions for ui/gw2ui/Gw2UiInternal.h. Carved verbatim from the former
// monolithic Gw2Ui.cpp (no behaviour change); the per-area module .cpp files call into these.
#include "ui/gw2ui/Gw2UiInternal.h"
#include "Shared.h"
#include "util/Textures.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Gw2Ui
{
    namespace detail
    {
        // Local-first UI asset: ids we bundle under data/textures/ui/dat resolve from disk in a frame and
        // are never re-downloaded; anything not bundled falls through to the gw2dat CDN. Same reason the
        // effect textures are bundled -- it kills the blank-then-pop-in of tab icons / chrome on first open
        // (and on every relaunch). The image bytes are identical to the CDN's (same dat ids).
        T Asset(uint32_t id)
        {
            // GetAssetTex is itself local-first on data/textures/ui/dat/<id>.png, so a bundled chrome icon loads
            // from disk (no CDN, no cache duplication) and anything unbundled falls through to the CDN.
            const Texture_t *t = Tex::GetAssetTex(id);
            return t ? T{t->Resource, (float)t->Width, (float)t->Height} : T{};
        }
        T File(const char *relPath)
        {
            const Texture_t *t = Tex::GetFileTex(relPath);
            return t ? T{t->Resource, (float)t->Width, (float)t->Height} : T{};
        }
        void Img(ImDrawList *dl, const T &t, ImVec2 a, ImVec2 b,
                 ImVec2 uv0, ImVec2 uv1, ImU32 col)
        {
            if (t.srv)
                dl->AddImage((ImTextureID)t.srv, a, b, uv0, uv1, col);
        }

        // Horizontal 3-slice: fixed-width left/right caps (so a textured box keeps its side borders at native
        // width) + a stretched middle. capPx = the source cap width in pixels.
        void Img3H(ImDrawList *dl, const T &t, ImVec2 a, ImVec2 b, float capPx, ImU32 col)
        {
            if (!t.srv || t.w <= 0.f)
                return;
            float cap = capPx;
            const float maxCap = (b.x - a.x) * 0.15f;
            if (cap > maxCap)
                cap = maxCap;
            const float u = capPx / t.w;
            const ImTextureID id = (ImTextureID)t.srv;
            dl->AddImage(id, a, ImVec2(a.x + cap, b.y), ImVec2(0.f, 0.f), ImVec2(u, 1.f), col);
            dl->AddImage(id, ImVec2(a.x + cap, a.y), ImVec2(b.x - cap, b.y), ImVec2(u, 0.f), ImVec2(1.f - u, 1.f), col);
            dl->AddImage(id, ImVec2(b.x - cap, a.y), b, ImVec2(1.f - u, 0.f), ImVec2(1.f, 1.f), col);
        }

        ImFont *g_gw2Regular = nullptr;
        ImFont *g_gw2Italic = nullptr;
        int g_fontRevision = 0;

        // Global screen UI scale + local text-scale stack (see Gw2Ui::PushTextScale).
        float g_globalScale = 1.f;
        std::vector<float> g_textScale;
        float CurTextScale() { return g_globalScale * (g_textScale.empty() ? 1.f : g_textScale.back()); }

        ImFont *UiFont(ImFont *f)
        {
            if (f)
                return f; // caller-chosen face (e.g. italic)
            if (g_gw2Regular)
                return g_gw2Regular; // our real Menomonia, when enabled
            if (NexusLink && NexusLink->Font)
                return (ImFont *)NexusLink->Font; // Nexus's generic shared font
            return ImGui::GetFont();
        }

        std::vector<Gw2Ui::FontBake> g_fontLadder; // native-size ladder; EMPTY = no GW2 font (Nexus fallback)

        const Gw2Ui::FontBake *LadderBakeFor(float px, bool italic)
        {
            if (g_fontLadder.empty() || px <= 0.f)
                return nullptr;
            const int want = (int)(px + 0.5f);
            const Gw2Ui::FontBake *best = nullptr;
            float bestD = FLT_MAX;
            for (const Gw2Ui::FontBake &fb : g_fontLadder) // sorted ascending by px
            {
                ImFont *face = italic ? (fb.italic ? fb.italic : fb.regular) : fb.regular;
                if (!face)
                    continue; // rung not (yet) loaded -- skip
                if ((int)(fb.px + 0.5f) == want)
                    return &fb;                                  // EXACT px -> scale 1.0, sharpest
                const float d = std::fabs(std::log(px / fb.px)); // log-ratio: treats up/down symmetrically
                if (d <= bestD)
                {
                    bestD = d;
                    best = &fb;
                } // <= so a near-tie picks the LARGER rung (minify > magnify)
            }
            return best;
        }

        ImFont *FontForSizePx(float px, bool italic)
        {
            const Gw2Ui::FontBake *fb = LadderBakeFor(px, italic);
            if (!fb)
                return nullptr;
            return italic ? (fb->italic ? fb->italic : fb->regular) : fb->regular;
        }

        // The requested px SNAPPED to a baked rung, so the face below renders at scale 1.0 instead of being
        // stretched a few percent (that residual is the per-glyph aliasing that reads as "wavy" text). Falls
        // through unchanged when the ladder is empty -- Nexus font, or our bakes not loaded yet: nothing to
        // snap to, and behaviour must stay exactly as before during the async font load.
        // Italic-independent on purpose: a rung is eligible whenever its REGULAR face exists (the italic slot
        // falls back to regular), so LadderBakeFor picks the same bake either way -- the snapped size can never
        // disagree with the face ResolveFace goes on to pick for italic runs.
        float SnapPx(float px)
        {
            const Gw2Ui::FontBake *fb = LadderBakeFor(px, false);
            if (!fb)
                return px;
            // Snap only to a rung that is genuinely NEAR: rounding 19.2 -> 20 is the point, clamping 54 -> 32
            // (the top rung) is a resize and would wreck any caller drawing large display text. Inside the
            // ladder the nearest rung is always within half its 2px spacing, so every normal UI size still
            // snaps; outside it nothing moves and the face is scaled exactly as it was before snapping existed.
            // This also rides out the async font load: with the 12px rung not yet arrived, a 12px request finds
            // 14 (delta 2) and is left alone rather than visibly resizing for a few frames.
            constexpr float kSnapTolerancePx = 1.0f;
            return (std::fabs(fb->px - px) <= kSnapTolerancePx) ? fb->px : px;
        }

        float RequestedPx(float fontSize, ImFont *font)
        {
            const float base = (fontSize > 0.f) ? fontSize
                                                : (g_gw2Regular ? g_gw2Regular->FontSize : UiFont(font)->FontSize);
            const float px = base * CurTextScale();
            // Snapping is a LADDER concept -- it exists so OUR bakes render at scale 1.0. A caller that pinned
            // some other face (the Zone Display's 96px banner/Krytan bakes) owns its own sizing and does its own
            // `fs / font->FontSize` maths, and we have no idea what sizes that face was baked at, so honor the
            // size verbatim exactly as ResolveFace honors the face. Snapping it silently resized their text AND
            // desynced Gw2Ui measurement from their hand-drawn glyphs (issue #4).
            const bool ourFace = (!font || font == g_gw2Regular || font == g_gw2Italic);
            return ourFace ? SnapPx(px) : px;
        }

        ImFont *ResolveFace(ImFont *callerFont, float requestedPx)
        {
            // Our own faces (the 24px sentinels handed out by UiFontResolved()/Gw2Italic()) mean "GW2 font, nearest
            // rung". Any OTHER non-null caller font is an external pin -> honor it verbatim. This is the SINGLE
            // resolver shared by measure + draw, so the two can never pick different bakes (no wrap/clip drift).
            const bool italic = (callerFont && callerFont == g_gw2Italic);
            const bool regular = (callerFont && callerFont == g_gw2Regular);
            if (callerFont && !italic && !regular)
                return callerFont;
            if (ImFont *b = FontForSizePx(requestedPx, italic))
                return b;
            if (callerFont)
                return callerFont; // ladder empty: honor the sentinel ptr
            if (NexusLink && NexusLink->Font)
                return (ImFont *)NexusLink->Font;
            return ImGui::GetFont();
        }

        void DrawLabelCore(ImDrawList *dl, ImVec2 a, ImVec2 b, const char *text, ImFont *font,
                           ImU32 color, bool stroke, Gw2Ui::HAlign h, Gw2Ui::VAlign v, float fontSize,
                           float wrap, float weight, Gw2Ui::TextShadow shadowMode, float shadowStrength)
        {
            if (!text || !*text)
                return;
            // Scaled AND snapped to a baked rung (see RequestedPx), so the measure (CalcTextSizeA) and every
            // AddText pass below share ONE face at scale EXACTLY 1.0 -- no residual stretch, no aliasing.
            const float fs = RequestedPx(fontSize, font);
            ImFont *f = ResolveFace(font, fs);
            if (!f)
                return;
            const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, wrap, text); // wrap > 0 -> measure wrapped (widest x, total y)

            const float w = b.x - a.x, hgt = b.y - a.y;
            // ImGui renders the Menomonia TTF noticeably thinner so GW2 text reads light.
            // Sub-pixel extra passes restore the in-game weight (faux-bold). Default
            // ~0.8 a larger weight (~1.6) draws a 3rd mid pass for genuinely BOLD headers.
            constexpr float kBoldWeight = 0.8f;
            const float bw = (weight < 0.f) ? kBoldWeight : weight;

            // Draw one run [s,e) at (x,y) with the 8-pass outline (faded to the text alpha) + the faux-bold
            // passes. y is snapped to the pixel grid (a fractional Y makes ImGui place pre-rasterized glyphs between
            // pixels, so thin strokes anti-alias unevenly). runWrap > 0 lets ImGui word-wrap a single run.
            auto drawRun = [&](const char *s, const char *e, float x, float y, float runWrap)
            {
                y = std::floor(y);
                if (stroke && shadowMode != Gw2Ui::TextShadow::None)
                {
                    const float Af = ((color >> IM_COL32_A_SHIFT) & 0xFFu) / 255.f;
                    const float strength = std::clamp(shadowStrength, 0.f, 1.f);
                    if (shadowMode == Gw2Ui::TextShadow::Drop)
                    {
                        // A single down-right offset pass -- the classic UI drop-shadow, for callers that want
                        // floating text (no ring) with a strength dial instead of the fixed-alpha Outline ring.
                        const int sa = (int)(Af * 255.f * strength + 0.5f);
                        const ImU32 scol = IM_COL32(0, 0, 0, sa);
                        const float d = 2.f;
                        dl->AddText(f, fs, ImVec2(x + d, y + d), scol, s, e, runWrap);
                    }
                    else   // Outline: the original 8-direction ring. strength=1 reproduces the exact legacy formula.
                    {
                        const int sa = (int)((1.f - std::pow(1.f - Af, 1.f / 8.f)) * 255.f * strength + 0.5f);
                        const ImU32 scol = IM_COL32(0, 0, 0, sa);
                        const float d = 1.f;
                        const ImVec2 off[8] = {{0, -d}, {d, -d}, {d, 0}, {d, d}, {0, d}, {-d, d}, {-d, 0}, {-d, -d}};
                        for (const ImVec2 &o : off)
                            dl->AddText(f, fs, ImVec2(x + o.x, y + o.y), scol, s, e, runWrap);
                    }
                }
                dl->AddText(f, fs, ImVec2(x, y), color, s, e, runWrap);
                if (bw > 0.f)
                {
                    if (bw >= 1.2f)
                        dl->AddText(f, fs, ImVec2(x + bw * 0.5f, y), color, s, e, runWrap);
                    dl->AddText(f, fs, ImVec2(x + bw, y), color, s, e, runWrap);
                }
            };

            float y = a.y;
            if (v == Gw2Ui::VAlign::Middle)
                y += (hgt - ts.y) * 0.5f;
            else if (v == Gw2Ui::VAlign::Bottom)
                y += (hgt - ts.y);

            // Wrapped + centered/right: ImGui's AddText wraps each line LEFT-aligned, so the block-level shift below
            // collapses to ~0 when wrapWidth ~= the box width (e.g. a narrow rail). Lay out each wrapped line here so
            // it aligns per line. Left + non-wrapped text take the single-run fast path (unchanged).
            if (wrap > 0.f && h != Gw2Ui::HAlign::Left)
            {
                const float scale = (f->FontSize > 0.f) ? fs / f->FontSize : 1.f;
                const char *s = text;
                const char *end = text;
                while (*end)
                    ++end;
                float ly = y;
                while (s < end)
                {
                    const char *nl = s;
                    while (nl < end && *nl != '\n')
                        ++nl;                                                      // stop at an explicit newline
                    const char *le = f->CalcWordWrapPositionA(scale, s, nl, wrap); // word-wrap within this paragraph
                    if (le == s)
                        le = (s < nl) ? s + 1 : nl; // guarantee forward progress
                    const ImVec2 lsz = f->CalcTextSizeA(fs, FLT_MAX, 0.f, s, le);
                    const float lx = a.x + (h == Gw2Ui::HAlign::Center ? (w - lsz.x) * 0.5f : (w - lsz.x));
                    drawRun(s, le, lx, ly, 0.f);
                    ly += fs;
                    s = le;
                    while (s < nl && *s == ' ')
                        ++s; // skip the space ImGui consumes at a wrap point
                    if (s < end && *s == '\n')
                        ++s; // consume the explicit newline
                }
                return;
            }

            float x = a.x;
            if (h == Gw2Ui::HAlign::Center)
                x += (w - ts.x) * 0.5f;
            else if (h == Gw2Ui::HAlign::Right)
                x += (w - ts.x);
            drawRun(text, nullptr, x, y, wrap);
        }

        const char *s_kbCapturing = nullptr;
    }
}
