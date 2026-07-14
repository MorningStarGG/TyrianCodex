#include "util/Draw.h"
#include "util/Textures.h" // Tex::GetTextureFromAssetId
#include <algorithm>       // std::max
#include <cmath>           // std::fmod
#include <cstdio>          // std::snprintf

int KindIndex(Objective::MarkerKind k)
{
    switch (k)
    {
    case Objective::MarkerKind::Waypoint:
        return 0;
    case Objective::MarkerKind::Poi:
        return 1;
    case Objective::MarkerKind::Vista:
        return 2;
    case Objective::MarkerKind::HeroPoint:
        return 3;
    case Objective::MarkerKind::Heart:
        return 4;
    default:
        return -1;
    }
}

// TrailColorChoice -> RGBA (same table as GuideModule.Markers.cs TrailColorFor).
ImU32 TrailColorFor(int choice, int a)
{
    switch (choice)
    {
    case 1:
        return IM_COL32(255, 170, 60, a); // Amber
    case 2:
        return IM_COL32(120, 220, 255, a); // Cyan
    case 3:
        return IM_COL32(120, 235, 130, a); // Green
    case 4:
        return IM_COL32(255, 130, 220, a); // Magenta
    case 5:
        return IM_COL32(255, 255, 255, a); // White
    default:
        return IM_COL32(255, 226, 130, a); // Gold
    }
}

// Wiki summary leads; a follow-up line carries the quick-actions -- "Tasks:" for a heart's checklist,
// "Answer:" for a hero challenge's completion answers (the wiki Walkthrough: quiz/puzzle solutions).
std::string DetailText(const Step &s)
{
    const bool w = !s.WikiText.empty(), t = !s.WhatToDo.empty();
    if (w && t)
        return s.WikiText + (s.Type == "hero" ? "\nAnswer: " : "\nTasks: ") + s.WhatToDo;
    return w ? s.WikiText : s.WhatToDo;
}

// ETA at the current smoothed speed, "m:ss", or "--" when stationary. Per-second readout:
// the de-twitching is handled upstream by the smoothed speed (Guidance), so we keep a live 1s countdown here
// (round-to-nearest so the displayed second is accurate) instead of coarse-bucketing it.
std::string FormatEta(float distMeters, float speedEma)
{
    if (speedEma < 0.3f)
        return "--";
    int secs = (int)(distMeters / speedEma + 0.5f);
    if (secs >= 3600)
        return "--";
    char b[16];
    std::snprintf(b, sizeof(b), "%d:%02d", secs / 60, secs % 60);
    return b;
}

// A rough completion-time estimate label from a minute count: "~52m", "~3h 40m", "~2h" (drops a zero-minute
// tail). For the Journal story-time estimates (gw2storytimes.com). <= 0 returns "" (no data).
std::string EstimateText(float minutes)
{
    if (minutes <= 0.f)
        return "";
    const int total = (int)(minutes + 0.5f);
    const int h = total / 60, m = total % 60;
    char b[24];
    if (h <= 0)
        std::snprintf(b, sizeof(b), "~%dm", m);
    else if (m == 0)
        std::snprintf(b, sizeof(b), "~%dh", h);
    else
        std::snprintf(b, sizeof(b), "~%dh %dm", h, m);
    return b;
}

// The canonical distance label, "<n> m". Buffer-filling (no per-frame allocation -- these draw every frame).
// The GPS arrow keeps its own compact "<n>m" caption + ETA-suffix logic (the correct reference, untouched).
void FormatDistance(char *buf, size_t bufSize, float meters)
{
    std::snprintf(buf, bufSize, "%.0f m", meters < 0.f ? 0.f : meters);
}

// The PERSONAL-target pin, drawn (not a tinted texture - the gw2dat waypoint icon has a TEAL centre that
// muddies to green under an orange multiply). Three stacked orange diamonds (dark edge -> body -> warm light
// centre) + a ring. SHARED by the in-world billboard AND the map/minimap pin so the two always match.
void DrawPersonalPin(ImDrawList *dl, ImVec2 ctr, float halfExtent, bool pulse)
{
    const float d = halfExtent;
    const float de = d + std::max(1.5f, d * 0.14f); // outline = a slightly larger dark diamond behind
    const float di = d * 0.44f;                     // light inner diamond (replaces the muddy centre)
    auto diamond = [&](float e, ImU32 col)
    {
        const ImVec2 p[4] = {ImVec2(ctr.x, ctr.y - e), ImVec2(ctr.x + e, ctr.y),
                             ImVec2(ctr.x, ctr.y + e), ImVec2(ctr.x - e, ctr.y)};
        dl->AddConvexPolyFilled(p, 4, col);
    };
    diamond(de, IM_COL32(60, 30, 0, 235));                              // dark edge
    diamond(d, IM_COL32(245, 140, 30, 255));                            // orange body
    diamond(di, IM_COL32(255, 214, 150, 255));                          // warm light centre
    dl->AddCircle(ctr, de + 3.f, IM_COL32(255, 185, 95, 235), 28, 2.f); // ring marks it the personal target
    if (pulse)                                                          // map only: the same expanding "radar" ring the destination guide pin uses (1.5s, orange)
    {
        const float ph = std::fmod((float)ImGui::GetTime(), 1.5f) / 1.5f;
        dl->AddCircle(ctr, de + 3.f + ph * 11.f, IM_COL32(255, 185, 95, (unsigned)(170.f * (1.f - ph))), 28, 2.f);
    }
}

// The "travel destination" icon: a dark disc + accent ring + the GW2 waypoint glyph (dat 157353) centred.
// Shared by the rich + compact viewer travel cards, which drew this inline at slightly different proportions
// (radius / texHalf are passed so each keeps its exact look).
void DrawWaypointDestIcon(ImDrawList *dl, ImVec2 c, float radius, float texHalf, ImU32 accent)
{
    dl->AddCircleFilled(c, radius, IM_COL32(2, 5, 6, 175), 28);
    dl->AddCircle(c, radius, accent, 28, 1.6f);
    if (void *wp = Tex::GetTextureFromAssetId(157353u)) // the GW2 waypoint icon = clear "travel destination"
        dl->AddImage((ImTextureID)wp, ImVec2(c.x - texHalf, c.y - texHalf), ImVec2(c.x + texHalf, c.y + texHalf));
}

// One small type icon (gw2dat), if its texture has loaded; advances the ImGui cursor either way.
void TypeIcon(const std::string &type, float size)
{
    uint32_t aid = 0;
    void *tex = Objective::TryIconAssetId(type, aid) ? Tex::GetTextureFromAssetId(aid) : nullptr;
    if (tex)
    {
        ImGui::Image((ImTextureID)tex, ImVec2(size, size));
        ImGui::SameLine();
    }
}
