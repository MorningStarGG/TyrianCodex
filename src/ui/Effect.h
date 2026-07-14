#pragma once
#include "imgui.h"

// It reveals the gold overlay (dat-156071) through the mask (dat-156072) by the shader's
// EXACT per-pixel formula as a rolleranimates 0..1 -- the GW2 menu / list-row highlight.
// One primitive; every row reuses it (the settings section menu, and later the story rows, zone rows, checklist),
// so the highlight is right by construction rather than re-approximated per control.
namespace Fx
{
    // Queue one highlight quad spanning [a,b] at the given roller (0 = none .. 1 = full) onto `dl`.
    // Internally: AddCallback(bind our PS + overlay + roller) -> AddImage(mask) -> reset render state.
    // Cheap no-op when roller ~ 0, or while the device / shader / textures aren't ready yet.
    void ScrollingHighlight(ImDrawList *dl, ImVec2 a, ImVec2 b, float roller);

    // A moving gold light-band ("sheen") swept across the rect [a,b], computed procedurally in a real D3D11
    // pixel shader (smooth gaussian band + vertical falloff) -- the Zone Display banner's "sweep" shine.
    // `phase` is the band centre across the rect (sweep it ~ -0.2 .. 1.2); `intensity` (0..1) the peak
    // brightness; `alpha` folds in the banner's master opacity; `band` is the gaussian width (smaller = thinner
    // streak, e.g. 0.004 thin / 0.02 thick). Cheap no-op when intensity/alpha ~ 0. Its own PS + cbuffer, released
    // in Shutdown alongside the highlight's.
    void ZoneSheen(ImDrawList *dl, ImVec2 a, ImVec2 b, float phase, float intensity, float alpha, float band);

    // True once the sheen pixel shader has compiled (lazily inits on first call). The Zone Display uses this to
    // fall back to its draw-list letter-glint when the GPU sweep isn't available.
    bool ZoneSheenReady();

    // Erode a font-atlas glyph by a screen-space value-noise threshold (the Zone Display per-letter "Dissolve").
    // Samples the texture's ALPHA as the glyph shape, removes pixels where the noise < threshold, and adds a
    // glowing ember edge near the front. threshold 0 = intact .. 1 = gone. `tint`/`edge` are the body + ember
    // colours; `noiseScale` ~ 1/pixels; `alpha` folds in the line opacity. Rides ImGui's pass; no-op until ready.
    void Dissolve(ImDrawList *dl, ImTextureID tex, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1,
                  float threshold, ImU32 tint, ImU32 edge, float noiseScale, float alpha);
    bool DissolveReady();

    // Per-glyph dissolve applied to ONE font-atlas glyph quad (each character is its own atlas sprite, so the shader gets the
    // glyph's atlas UV). `color` = the glyph tint (text colour; its alpha carries the line opacity). `amount`
    // 0 = intact .. 1 = gone. `mode`: 0 = atlas slide (x+y in atlas UV -> the per-letter "decode" scramble),
    // 1 = spatial slide across `rect` (minX, minY, 1/w, 1/h) -> a banner-wide diagonal wipe (Slide / Twist),
    // 2 = perlin (the organic per-letter "Burn"). `glowKind`: 0 = none, 1 = thin ember at the front
    // (Decode/Slide swap), 2 = wide molten fire + hot core (the Burn exit). Needs a Mirror sampler (bound
    // internally). No-op until the shader is ready / when fully transparent or gone.
    void DissolveGlyph(ImDrawList *dl, ImTextureID tex, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1,
                       ImU32 color, float amount, int mode, int glowKind, ImVec4 rect);
    bool BurnReady();

    // ---- 3D trail render pass -------------------------------------------------------------------------------
    // A real GPU pass for the in-world route ribbon: the CPU uploads WORLD-space geometry (a triangle list) and
    // our vertex shader transforms it with the camera matrix, so the GPU does projection, near-plane + frustum
    // CLIPPING (free + correct -- no snap, no off-screen stretch) and PERSPECTIVE-CORRECT texturing (no
    // foreshorten "swim" that ImGui's 2D draw list can't avoid). Rides ImGui's pass via a draw-list callback.
    struct TrailVert
    {
        float x, y, z; // world position (raw MumbleLink, Y-up)
        float u, v;    // texture coords (u across width, v along length)
        unsigned int col;
    }; // IM_COL32 tint (carries the per-vertex alpha)
    // `vpColMajor` = the 16 floats of Math::Mat4 (m[col][row] order) -- fed to the VS as 4 explicit columns and
    // multiplied exactly like our CPU Mat4::Transform, so there is no transpose ambiguity. `texSrv` = the trail
    // texture's Nexus Resource (an ID3D11ShaderResourceView*). The last four args drives "FadeCenter"
    // (fade trails around the character) per-pixel in the shader: `playerSX/SY` the character's SCREEN position,
    // `fadeRadiusPx` the fade-hole radius in pixels, `fadeOn` gates it (toggle + not-first-person). No-op until
    // the shader/device is ready.
    void DrawTrail3D(ImDrawList *dl, const TrailVert *verts, int count, const float vpColMajor[16], void *texSrv,
                     float playerSX, float playerSY, float fadeRadiusPx, bool fadeOn);

    // Pre-load the bundled mask/overlay textures at addon start so the highlight is ready before the
    // settings window is first opened (avoids the "nothing for a few seconds, then pops in" delay).
    void Warm();

    // Release the D3D objects we hold (pixel shader + constant buffer we created; device + context we
    // AddRef'd from Nexus's swap chain). Call from AddonUnload so a disable/re-enable cycle doesn't leak
    // COM references. Safe to call when nothing was initialized.
    void Shutdown();
}
