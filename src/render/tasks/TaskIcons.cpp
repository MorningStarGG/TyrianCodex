#include "render/tasks/TaskIcons.h"
#include "ui/Gw2Ui.h"        // Gw2Ui::Alpha
#include "util/Textures.h"   // Tex::GetFileTex, Texture_t
#include "Shared.h"          // Texture_t (Nexus)
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// Procedural task-objective icon art -- extracted from ViewerTaskChips (see TaskIcons.h).

ImU32 TaskChipAccent(const std::string& category, ImU32 fallback)
{
    if (category == "kill")     return IM_COL32(226, 100, 64, 235);
    if (category == "collect")  return IM_COL32(222, 176, 82, 235);
    if (category == "interact") return IM_COL32(115, 174, 224, 235);
    if (category == "revive")   return IM_COL32(112, 204, 132, 235);
    if (category == "talk")     return IM_COL32(193, 145, 230, 235);
    if (category == "repair")   return IM_COL32(190, 154, 112, 235);
    if (category == "movement") return IM_COL32(128, 205, 220, 235);
    if (category == "defend")   return IM_COL32(235, 207, 105, 235);
    if (category == "event")    return IM_COL32(255, 142, 78, 235);
    return Gw2Ui::Alpha(fallback, 220);
}

static bool TaskLabelIs(const TaskChip& chip, const char* label)
{
    return std::strcmp(chip.Label.c_str(), label) == 0;
}

static const char* TaskChipSimpleIconKind(const TaskChip& chip)
{
    if (chip.Category == "kill") return "kill";
    if (chip.Category == "collect") return "collect";
    if (chip.Category == "revive") return "revive";
    if (chip.Category == "repair") return "repair";
    if (chip.Category == "movement") return "movement";
    if (chip.Category == "defend") return "defend";
    if (chip.Category == "event") return "event";
    if (chip.Category == "talk" || TaskLabelIs(chip, "Talk") || TaskLabelIs(chip, "Trade") ||
        TaskLabelIs(chip, "Give") || TaskLabelIs(chip, "Return"))
        return "talk";
    if (chip.Category == "interact" &&
        (TaskLabelIs(chip, "Search") || TaskLabelIs(chip, "Investigate") || TaskLabelIs(chip, "Find")))
        return "search";
    if (chip.Category == "interact" &&
        (TaskLabelIs(chip, "Place") || TaskLabelIs(chip, "Use") || TaskLabelIs(chip, "Activate") ||
         TaskLabelIs(chip, "Feed") || TaskLabelIs(chip, "Clean") || TaskLabelIs(chip, "Break")))
        return "use";
    if (chip.Category == "interact") return "interact";
    return "generic";
}

static const char* TaskChipExpandedIconKind(const TaskChip& chip)
{
    if (chip.Category == "kill")
    {
        if (TaskLabelIs(chip, "Fight")) return "fight";
        if (TaskLabelIs(chip, "Hunt")) return "hunt";
        return "kill";
    }
    if (chip.Category == "collect")
    {
        if (TaskLabelIs(chip, "Pick Up")) return "pickup";
        if (TaskLabelIs(chip, "Return")) return "return";
        if (TaskLabelIs(chip, "Give")) return "give";
        if (TaskLabelIs(chip, "Trade")) return "trade";
        if (TaskLabelIs(chip, "Catch")) return "catch";
        if (TaskLabelIs(chip, "Steal")) return "steal";
        return "collect";
    }
    if (chip.Category == "revive")
    {
        if (TaskLabelIs(chip, "Rescue")) return "rescue";
        if (TaskLabelIs(chip, "Treat")) return "treat";
        return "revive";
    }
    if (chip.Category == "repair") return "repair";
    if (chip.Category == "movement")
    {
        if (TaskLabelIs(chip, "Reach")) return "reach";
        if (TaskLabelIs(chip, "Run")) return "run";
        return "run";
    }
    if (chip.Category == "defend")
    {
        if (TaskLabelIs(chip, "Escort")) return "escort";
        return "defend";
    }
    if (chip.Category == "event") return "event";
    if (TaskLabelIs(chip, "Trade")) return "trade";
    if (chip.Category == "talk" || TaskLabelIs(chip, "Talk")) return "talk";

    if (TaskLabelIs(chip, "Stomp")) return "stomp";
    if (TaskLabelIs(chip, "Destroy") || TaskLabelIs(chip, "Break") || TaskLabelIs(chip, "Pull")) return "destroy";
    if (TaskLabelIs(chip, "Disable") || TaskLabelIs(chip, "Defuse") || TaskLabelIs(chip, "Reset") ||
        TaskLabelIs(chip, "Block")) return "disable";
    if (TaskLabelIs(chip, "Search") || TaskLabelIs(chip, "Investigate") || TaskLabelIs(chip, "Find"))
        return "search";
    if (TaskLabelIs(chip, "Place")) return "place";
    if (TaskLabelIs(chip, "Feed")) return "feed";
    if (TaskLabelIs(chip, "Clean")) return "clean";
    if (TaskLabelIs(chip, "Light")) return "light";
    if (TaskLabelIs(chip, "Open")) return "open";
    if (TaskLabelIs(chip, "Treat")) return "treat";
    if (TaskLabelIs(chip, "Throw")) return "throw";
    if (TaskLabelIs(chip, "Train")) return "train";
    if (TaskLabelIs(chip, "Cook")) return "cook";
    if (TaskLabelIs(chip, "Herd") || TaskLabelIs(chip, "Lure")) return "herd";
    if (TaskLabelIs(chip, "Wake")) return "wake";
    if (TaskLabelIs(chip, "Kneel")) return "kneel";
    if (TaskLabelIs(chip, "Reach")) return "reach";
    if (TaskLabelIs(chip, "Help")) return "help";
    if (TaskLabelIs(chip, "Use") || TaskLabelIs(chip, "Interact") || TaskLabelIs(chip, "Complete"))
        return "use";

    return TaskChipSimpleIconKind(chip);
}

static const char* TaskChipIconKind(const TaskChip& chip, bool expanded)
{
    return expanded ? TaskChipExpandedIconKind(chip) : TaskChipSimpleIconKind(chip);
}

static const char* TaskIconFileStem(const char* kind)
{
    static constexpr const char* kKeys[] = {
        "kill", "fight", "hunt", "collect", "pickup", "return", "give", "trade",
        "catch", "steal", "talk", "search", "interact", "use", "destroy", "stomp",
        "disable", "place", "feed", "clean", "light", "open", "throw", "train",
        "cook", "herd", "wake", "kneel", "repair", "revive", "rescue", "treat",
        "run", "reach", "defend", "escort", "event", "help", "generic_action",
    };
    if (!kind || !kind[0]) return "generic_action";
    if (std::strcmp(kind, "movement") == 0) return "run";
    if (std::strcmp(kind, "generic") == 0) return "generic_action";
    for (const char* key : kKeys)
        if (std::strcmp(kind, key) == 0)
            return key;
    return "generic_action";
}

static bool DrawTaskFileIcon(ImDrawList* dl, ImVec2 c, float size, const char* kind)
{
    char relPath[128];
    std::snprintf(relPath, sizeof(relPath), "data\\textures\\ui\\task-icons-expanded\\%s.png",
                  TaskIconFileStem(kind));
    const Texture_t* tex = Tex::GetFileTex(relPath);
    if (!tex || !tex->Resource || tex->Width <= 0 || tex->Height <= 0) return false;

    const float r = size * 0.5f;
    dl->AddImage((ImTextureID)tex->Resource, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
    return true;
}

void DrawTaskIcon(ImDrawList* dl, ImVec2 c, float size, const TaskChip& chip, ImU32 accent, bool expanded)
{
    const char* kind = TaskChipIconKind(chip, expanded);
    if (expanded)
    {
        if (DrawTaskFileIcon(dl, c, size, kind))
            return;
        kind = TaskChipSimpleIconKind(chip);
        expanded = false;
    }

    const float r = size * 0.5f;
    const float s = size;
    const ImU32 ring = Gw2Ui::Alpha(accent, 245);
    const ImU32 fill = IM_COL32(7, 9, 8, expanded ? 238 : 222);
    const ImU32 ink = IM_COL32(247, 226, 171, 250);
    const ImU32 shade = IM_COL32(20, 13, 8, 190);
    const ImU32 soft = Gw2Ui::Alpha(accent, expanded ? 88 : 58);
    const ImU32 dim = Gw2Ui::Alpha(ink, 155);
    dl->AddCircleFilled(c, r, fill, 28);
    if (expanded)
    {
        dl->AddCircleFilled(ImVec2(c.x - r * 0.18f, c.y - r * 0.16f), r * 0.66f, Gw2Ui::Alpha(accent, 28), 24);
        dl->AddCircle(c, r * 0.66f, Gw2Ui::Alpha(accent, 74), 24, 1.f);
        dl->AddLine(ImVec2(c.x - r * 0.42f, c.y - r * 0.52f), ImVec2(c.x + r * 0.26f, c.y - r * 0.52f), Gw2Ui::Alpha(ink, 38), 1.f);
    }
    dl->AddCircle(c, r - 0.75f, ring, 28, 1.35f);

    auto P = [&](float x, float y) { return ImVec2(c.x + s * x, c.y + s * y); };
    auto lineRaw = [&](ImVec2 a, ImVec2 b, ImU32 col, float thick) {
        dl->AddLine(a, b, col, thick);
    };
    auto line = [&](ImVec2 a, ImVec2 b, float thick = 1.8f) {
        dl->AddLine(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x + 1.f, b.y + 1.f), shade, thick + 1.6f);
        dl->AddLine(a, b, ink, thick);
    };
    auto poly = [&](const ImVec2* pts, int count, bool closed, float thick = 1.6f) {
        ImVec2 shadowPts[12];
        const int n = std::min(count, 12);
        for (int i = 0; i < n; ++i) shadowPts[i] = ImVec2(pts[i].x + 1.f, pts[i].y + 1.f);
        dl->AddPolyline(shadowPts, n, shade, closed, thick + 1.4f);
        dl->AddPolyline(pts, count, ink, closed, thick);
    };
    auto fillPoly = [&](const ImVec2* pts, int count, ImU32 col) {
        dl->AddConvexPolyFilled(pts, count, col);
        poly(pts, count, true, 1.35f);
    };
    auto circle = [&](ImVec2 p, float radius, float thick = 1.7f) {
        dl->AddCircle(ImVec2(p.x + 1.f, p.y + 1.f), radius, shade, 24, thick + 1.2f);
        dl->AddCircle(p, radius, ink, 24, thick);
    };
    auto rect = [&](ImVec2 a, ImVec2 b, float rounding = 2.f, float thick = 1.6f) {
        dl->AddRect(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x + 1.f, b.y + 1.f), shade, rounding, 0, thick + 1.2f);
        dl->AddRect(a, b, ink, rounding, 0, thick);
    };
    auto drawArrow = [&](ImVec2 a, ImVec2 b, ImU32 col, float thick) {
        dl->AddLine(ImVec2(a.x + 1.f, a.y + 1.f), ImVec2(b.x + 1.f, b.y + 1.f), shade, thick + 1.3f);
        dl->AddLine(a, b, col, thick);
        const float ang = std::atan2(b.y - a.y, b.x - a.x);
        const float head = s * 0.13f;
        const float spread = 2.55f;
        dl->AddLine(ImVec2(b.x + 1.f, b.y + 1.f),
                    ImVec2(b.x + 1.f - std::cos(ang - spread) * head, b.y + 1.f - std::sin(ang - spread) * head),
                    shade, thick + 1.1f);
        dl->AddLine(ImVec2(b.x + 1.f, b.y + 1.f),
                    ImVec2(b.x + 1.f - std::cos(ang + spread) * head, b.y + 1.f - std::sin(ang + spread) * head),
                    shade, thick + 1.1f);
        dl->AddLine(b, ImVec2(b.x - std::cos(ang - spread) * head, b.y - std::sin(ang - spread) * head), col, thick);
        dl->AddLine(b, ImVec2(b.x - std::cos(ang + spread) * head, b.y - std::sin(ang + spread) * head), col, thick);
    };
    auto drawBag = [&](ImU32 col) {
        const ImVec2 a = P(-0.22f, -0.03f), b = P(0.22f, 0.25f);
        dl->AddRectFilled(a, b, soft, s * 0.05f);
        rect(a, b, s * 0.05f, 1.55f);
        line(P(-0.12f, -0.03f), P(-0.05f, -0.17f), 1.35f);
        line(P(0.12f, -0.03f), P(0.05f, -0.17f), 1.35f);
        line(P(-0.05f, -0.17f), P(0.05f, -0.17f), 1.35f);
        (void)col;
    };
    auto drawCross = [&](float scale, ImU32 col, float thick) {
        lineRaw(P(-scale, 0.f), P(scale, 0.f), shade, thick + 1.4f);
        lineRaw(P(0.f, -scale), P(0.f, scale), shade, thick + 1.4f);
        lineRaw(P(-scale, 0.f), P(scale, 0.f), col, thick);
        lineRaw(P(0.f, -scale), P(0.f, scale), col, thick);
    };
    auto drawSpark = [&](ImVec2 p, float m, ImU32 col) {
        dl->AddLine(ImVec2(p.x - m, p.y), ImVec2(p.x + m, p.y), col, 1.2f);
        dl->AddLine(ImVec2(p.x, p.y - m), ImVec2(p.x, p.y + m), col, 1.2f);
    };
    auto shield = [&]() {
        const ImVec2 pts[] = { P(0.f, -0.27f), P(0.23f, -0.14f), P(0.16f, 0.14f), P(0.f, 0.27f), P(-0.16f, 0.14f), P(-0.23f, -0.14f) };
        fillPoly(pts, 6, soft);
    };
    auto diamond = [&]() {
        const ImVec2 pts[] = { P(0.f, -0.25f), P(0.25f, 0.f), P(0.f, 0.25f), P(-0.25f, 0.f) };
        fillPoly(pts, 4, Gw2Ui::Alpha(accent, 62));
    };

    if (std::strcmp(kind, "hunt") == 0)
    {
        circle(c, s * 0.23f, 1.7f);
        circle(c, s * 0.09f, 1.35f);
        line(P(-0.30f, 0.f), P(0.30f, 0.f), 1.25f);
        line(P(0.f, -0.30f), P(0.f, 0.30f), 1.25f);
    }
    else if (std::strcmp(kind, "kill") == 0 || std::strcmp(kind, "fight") == 0)
    {
        line(P(-0.24f, 0.24f), P(0.23f, -0.24f), 2.0f);
        line(P(0.24f, 0.24f), P(-0.23f, -0.24f), 2.0f);
        line(P(-0.04f, 0.12f), P(0.10f, 0.24f), 1.45f);
        line(P(0.04f, 0.12f), P(-0.10f, 0.24f), 1.45f);
    }
    else if (std::strcmp(kind, "collect") == 0)
    {
        drawBag(ink);
        lineRaw(P(-0.16f, 0.08f), P(0.16f, 0.08f), dim, 1.15f);
    }
    else if (std::strcmp(kind, "pickup") == 0)
    {
        drawBag(ink);
        drawArrow(P(0.f, 0.18f), P(0.f, -0.21f), ink, 1.8f);
    }
    else if (std::strcmp(kind, "return") == 0)
    {
        drawBag(ink);
        drawArrow(P(0.24f, -0.18f), P(-0.16f, -0.18f), ink, 1.55f);
        drawArrow(P(-0.16f, 0.16f), P(0.24f, 0.16f), ink, 1.55f);
    }
    else if (std::strcmp(kind, "give") == 0)
    {
        line(P(-0.25f, 0.12f), P(-0.02f, 0.12f), 1.8f);
        dl->AddCircleFilled(P(0.00f, 0.06f), s * 0.08f, ink, 14);
        drawArrow(P(0.02f, -0.08f), P(0.26f, -0.08f), ink, 1.65f);
    }
    else if (std::strcmp(kind, "trade") == 0)
    {
        drawArrow(P(-0.24f, -0.08f), P(0.22f, -0.08f), ink, 1.55f);
        drawArrow(P(0.24f, 0.10f), P(-0.22f, 0.10f), ink, 1.55f);
    }
    else if (std::strcmp(kind, "catch") == 0)
    {
        circle(P(-0.03f, -0.05f), s * 0.19f, 1.6f);
        line(P(0.10f, 0.10f), P(0.24f, 0.25f), 1.8f);
        lineRaw(P(-0.17f, -0.05f), P(0.11f, -0.05f), dim, 1.0f);
        lineRaw(P(-0.03f, -0.20f), P(-0.03f, 0.10f), dim, 1.0f);
    }
    else if (std::strcmp(kind, "steal") == 0)
    {
        drawBag(ink);
        drawArrow(P(0.24f, -0.19f), P(-0.18f, -0.19f), ink, 1.55f);
    }
    else if (std::strcmp(kind, "search") == 0)
    {
        circle(P(-0.05f, -0.04f), s * 0.16f, 1.9f);
        line(P(0.07f, 0.08f), P(0.22f, 0.23f), 2.0f);
    }
    else if (std::strcmp(kind, "destroy") == 0)
    {
        const ImVec2 pts[] = { P(0.f, -0.28f), P(0.08f, -0.08f), P(0.27f, -0.12f), P(0.12f, 0.04f), P(0.21f, 0.25f), P(0.f, 0.12f), P(-0.21f, 0.25f), P(-0.12f, 0.04f), P(-0.27f, -0.12f), P(-0.08f, -0.08f) };
        poly(pts, 10, true, 1.45f);
        line(P(-0.10f, 0.10f), P(0.10f, -0.10f), 1.35f);
    }
    else if (std::strcmp(kind, "stomp") == 0)
    {
        const ImVec2 boot[] = { P(-0.17f, -0.20f), P(0.07f, -0.20f), P(0.07f, 0.05f), P(0.23f, 0.14f), P(0.20f, 0.25f), P(-0.18f, 0.25f), P(-0.18f, 0.07f) };
        fillPoly(boot, 7, soft);
        line(P(-0.25f, -0.28f), P(0.25f, 0.28f), 1.3f);
    }
    else if (std::strcmp(kind, "disable") == 0)
    {
        rect(P(-0.20f, -0.17f), P(0.20f, 0.17f), s * 0.04f, 1.6f);
        circle(c, s * 0.09f, 1.35f);
        line(P(-0.24f, 0.24f), P(0.24f, -0.24f), 2.0f);
    }
    else if (std::strcmp(kind, "place") == 0)
    {
        drawArrow(P(0.f, -0.26f), P(0.f, 0.08f), ink, 1.75f);
        line(P(-0.20f, 0.21f), P(0.20f, 0.21f), 1.65f);
        rect(P(-0.10f, 0.07f), P(0.10f, 0.20f), 1.5f, 1.25f);
    }
    else if (std::strcmp(kind, "feed") == 0)
    {
        line(P(-0.21f, 0.08f), P(0.21f, 0.08f), 1.65f);
        line(P(-0.14f, 0.08f), P(-0.04f, 0.23f), 1.65f);
        line(P(0.14f, 0.08f), P(0.04f, 0.23f), 1.65f);
        dl->AddCircleFilled(P(-0.08f, -0.11f), s * 0.045f, ink, 10);
        dl->AddCircleFilled(P(0.09f, -0.15f), s * 0.04f, ink, 10);
        dl->AddCircleFilled(P(0.02f, -0.02f), s * 0.04f, ink, 10);
    }
    else if (std::strcmp(kind, "clean") == 0)
    {
        line(P(-0.22f, 0.08f), P(0.22f, -0.08f), 1.9f);
        line(P(0.06f, -0.02f), P(0.17f, 0.19f), 1.9f);
        drawSpark(P(-0.14f, -0.16f), s * 0.06f, ink);
        drawSpark(P(0.19f, -0.19f), s * 0.045f, ink);
    }
    else if (std::strcmp(kind, "light") == 0)
    {
        const ImVec2 flame[] = { P(0.f, -0.27f), P(0.16f, 0.05f), P(-0.12f, 0.10f) };
        fillPoly(flame, 3, Gw2Ui::Alpha(accent, 130));
        line(P(-0.17f, 0.22f), P(0.17f, 0.22f), 1.65f);
    }
    else if (std::strcmp(kind, "open") == 0)
    {
        rect(P(-0.18f, -0.25f), P(0.15f, 0.24f), 1.5f, 1.6f);
        line(P(0.15f, -0.25f), P(0.27f, -0.15f), 1.45f);
        line(P(0.27f, -0.15f), P(0.27f, 0.17f), 1.45f);
        dl->AddCircleFilled(P(0.06f, 0.f), s * 0.035f, ink, 8);
    }
    else if (std::strcmp(kind, "throw") == 0)
    {
        drawArrow(P(-0.25f, 0.18f), P(0.18f, -0.17f), ink, 1.75f);
        dl->AddCircleFilled(P(-0.20f, 0.20f), s * 0.055f, ink, 10);
    }
    else if (std::strcmp(kind, "train") == 0)
    {
        circle(c, s * 0.18f, 1.65f);
        line(P(-0.18f, 0.f), P(0.18f, 0.f), 1.25f);
        drawArrow(P(-0.26f, 0.22f), P(0.26f, 0.22f), ink, 1.45f);
    }
    else if (std::strcmp(kind, "cook") == 0)
    {
        rect(P(-0.20f, 0.f), P(0.20f, 0.20f), 2.f, 1.55f);
        line(P(-0.24f, 0.f), P(0.24f, 0.f), 1.55f);
        line(P(-0.10f, -0.22f), P(-0.10f, -0.07f), 1.1f);
        line(P(0.02f, -0.26f), P(0.02f, -0.09f), 1.1f);
        line(P(0.14f, -0.22f), P(0.14f, -0.07f), 1.1f);
    }
    else if (std::strcmp(kind, "herd") == 0)
    {
        dl->AddCircleFilled(P(0.14f, -0.06f), s * 0.08f, ink, 12);
        dl->AddCircleFilled(P(0.04f, 0.13f), s * 0.07f, ink, 12);
        drawArrow(P(-0.25f, 0.f), P(-0.02f, 0.f), ink, 1.6f);
    }
    else if (std::strcmp(kind, "wake") == 0)
    {
        const ImVec2 bell[] = { P(-0.15f, 0.14f), P(0.15f, 0.14f), P(0.f, -0.18f) };
        fillPoly(bell, 3, soft);
        line(P(0.f, 0.14f), P(0.f, 0.25f), 1.45f);
        line(P(-0.25f, -0.22f), P(-0.16f, -0.13f), 1.1f);
        line(P(0.25f, -0.22f), P(0.16f, -0.13f), 1.1f);
    }
    else if (std::strcmp(kind, "kneel") == 0)
    {
        circle(P(-0.05f, -0.17f), s * 0.07f, 1.35f);
        line(P(-0.05f, -0.10f), P(-0.05f, 0.08f), 1.65f);
        line(P(-0.05f, 0.08f), P(0.16f, 0.18f), 1.65f);
        line(P(-0.20f, 0.24f), P(0.20f, 0.24f), 1.2f);
    }
    else if (std::strcmp(kind, "revive") == 0)
    {
        drawCross(0.20f, ink, 3.0f);
    }
    else if (std::strcmp(kind, "rescue") == 0)
    {
        shield();
        drawCross(0.13f, ink, 2.2f);
    }
    else if (std::strcmp(kind, "treat") == 0)
    {
        rect(P(-0.23f, -0.11f), P(0.23f, 0.11f), s * 0.04f, 1.55f);
        drawCross(0.07f, ink, 2.1f);
    }
    else if (std::strcmp(kind, "talk") == 0)
    {
        rect(P(-0.23f, -0.16f), P(0.23f, 0.12f), s * 0.05f, 1.55f);
        const ImVec2 tail[] = { P(-0.06f, 0.12f), P(-0.16f, 0.24f), P(0.03f, 0.12f) };
        dl->AddConvexPolyFilled(tail, 3, ink);
        lineRaw(P(-0.12f, -0.04f), P(0.12f, -0.04f), dim, 1.2f);
    }
    else if (std::strcmp(kind, "use") == 0 || std::strcmp(kind, "interact") == 0)
    {
        diamond();
        dl->AddCircleFilled(c, s * 0.06f, ink, 12);
        drawSpark(P(0.20f, -0.20f), s * 0.045f, ink);
    }
    else if (std::strcmp(kind, "repair") == 0)
    {
        line(P(-0.18f, 0.20f), P(0.13f, -0.12f), 2.4f);
        dl->AddRectFilled(P(0.04f, -0.24f), P(0.25f, -0.10f), ink, 1.5f);
    }
    else if (std::strcmp(kind, "movement") == 0 || std::strcmp(kind, "run") == 0 || std::strcmp(kind, "reach") == 0)
    {
        const ImVec2 arrow[] = { P(-0.18f, 0.18f), P(0.25f, 0.f), P(-0.18f, -0.18f), P(-0.08f, 0.f) };
        fillPoly(arrow, 4, soft);
        if (std::strcmp(kind, "reach") == 0)
            line(P(-0.26f, 0.24f), P(0.24f, -0.24f), 1.2f);
    }
    else if (std::strcmp(kind, "defend") == 0 || std::strcmp(kind, "escort") == 0)
    {
        shield();
        if (std::strcmp(kind, "escort") == 0)
            drawArrow(P(-0.14f, 0.18f), P(0.17f, 0.18f), ink, 1.2f);
    }
    else if (std::strcmp(kind, "event") == 0)
    {
        const ImVec2 top[] = { P(0.f, -0.27f), P(0.12f, 0.02f), P(-0.20f, -0.10f) };
        const ImVec2 bot[] = { P(0.f, 0.27f), P(-0.12f, -0.02f), P(0.20f, 0.10f) };
        fillPoly(top, 3, Gw2Ui::Alpha(accent, 132));
        fillPoly(bot, 3, Gw2Ui::Alpha(accent, 132));
    }
    else if (std::strcmp(kind, "help") == 0)
    {
        diamond();
        drawCross(0.14f, ink, 2.0f);
    }
    else
    {
        diamond();
        dl->AddCircleFilled(c, s * 0.05f, ink, 12);
    }
}
