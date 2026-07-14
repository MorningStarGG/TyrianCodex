#pragma once
// Small easing curves for time-based UI animation. The codebase only had linear normalized-time fades
// (Toast / RowBackground); these give the Zone Display banner its choreographed feel (cascade settle,
// rise-in, drift-out). All take a normalized t (clamped to 0..1) and return an eased 0..1.
namespace Ease
{
    inline float Clamp01(float t) { return t < 0.f ? 0.f : (t > 1.f ? 1.f : t); }
    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    // Hermite smoothstep -- gentle ease in AND out (good for opacity).
    inline float SmoothStep(float t) { t = Clamp01(t); return t * t * (3.f - 2.f * t); }

    // Decelerating: fast then settle (good for rise-in / cascade).
    inline float OutCubic(float t) { t = Clamp01(t); const float u = 1.f - t; return 1.f - u * u * u; }
    inline float OutQuad(float t)  { t = Clamp01(t); const float u = 1.f - t; return 1.f - u * u; }

    // Accelerating: ease into a motion (good for drift-out).
    inline float InCubic(float t)  { t = Clamp01(t); return t * t * t; }

    // Slight overshoot then settle (a touch of "pop" on the hero line). c controls the overshoot amount.
    inline float OutBack(float t, float c = 1.70158f)
    {
        t = Clamp01(t);
        const float c3 = c + 1.f;
        const float u = t - 1.f;
        return 1.f + c3 * u * u * u + c * u * u;
    }
}
