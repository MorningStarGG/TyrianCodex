#include "render/glyphs/Glyphs.h"
#include <algorithm>
#include <cmath>

namespace
{
    inline ImU32 Alpha(ImU32 col, int a)
    {
        return (col & 0x00FFFFFFu) | ((ImU32)(a < 0 ? 0 : (a > 255 ? 255 : a)) << 24);
    }

    void DrawGuide(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 20.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
        auto line = [&](ImVec2 a, ImVec2 b, float t) {
            if (shadow) dl->AddLine(ImVec2(a.x + s, a.y + s), ImVec2(b.x + s, b.y + s), shade, t + s);
            dl->AddLine(a, b, col, t);
        };

        if (shadow) dl->AddRect(P(-7.f, -8.f), P(8.f, 10.f), shade, 1.5f * s, 0, 2.f * s);
        dl->AddRect(P(-8.f, -9.f), P(7.f, 9.f), col, 1.5f * s, 0, 1.6f * s);
        line(P(-2.f, -8.f), P(-2.f, 8.f), 1.2f * s);
        line(P(1.f, -4.f), P(5.f, -4.f), 1.2f * s);
        line(P(1.f, 1.f), P(5.f, 1.f), 1.2f * s);
    }

    void DrawMapPin(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto pin = [&](ImVec2 o, ImU32 pc, float thick) {
            dl->PathLineTo(ImVec2(c.x + o.x, c.y + 9.5f * s + o.y));
            dl->PathBezierCubicCurveTo(ImVec2(c.x - 7.0f * s + o.x, c.y + 2.5f * s + o.y),
                                       ImVec2(c.x - 8.5f * s + o.x, c.y - 5.5f * s + o.y),
                                       ImVec2(c.x + o.x,           c.y - 9.5f * s + o.y), 18);
            dl->PathBezierCubicCurveTo(ImVec2(c.x + 8.5f * s + o.x, c.y - 5.5f * s + o.y),
                                       ImVec2(c.x + 7.0f * s + o.x, c.y + 2.5f * s + o.y),
                                       ImVec2(c.x + o.x,           c.y + 9.5f * s + o.y), 18);
            dl->PathStroke(pc, true, thick);
        };

        if (shadow) pin(ImVec2(s, s), shade, 3.0f * s);
        pin(ImVec2(0.f, 0.f), col, 1.8f * s);
        if (shadow) dl->AddCircle(ImVec2(c.x + s, c.y - 2.4f * s + s), 3.2f * s, shade, 18, 2.8f * s);
        dl->AddCircle(ImVec2(c.x, c.y - 2.4f * s), 3.2f * s, col, 18, 1.7f * s);
    }

    void DrawCopy(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 20.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
        auto line = [&](ImVec2 a, ImVec2 b, float thick) {
            if (shadow) dl->AddLine(ImVec2(a.x + s, a.y + s), ImVec2(b.x + s, b.y + s), shade, thick + s);
            dl->AddLine(a, b, col, thick);
        };
        auto rect = [&](ImVec2 a, ImVec2 b, float rounding) {
            if (shadow) dl->AddRect(ImVec2(a.x + s, a.y + s), ImVec2(b.x + s, b.y + s), shade, rounding, 0, 2.f * s);
            dl->AddRect(a, b, col, rounding, 0, 1.6f * s);
        };

        rect(P(-5.f, -9.f), P(8.f, 5.f), 1.5f * s);
        rect(P(-9.f, -5.f), P(4.f, 9.f), 1.5f * s);
        line(P(-6.f, 0.f), P(1.f, 0.f), 1.1f * s);
        line(P(-6.f, 4.f), P(1.f, 4.f), 1.1f * s);
    }

    void DrawNavigate(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        ImVec2 pts[3] = {
            ImVec2(c.x + 8.f * s, c.y - 9.f * s),
            ImVec2(c.x - 8.f * s, c.y + 8.f * s),
            ImVec2(c.x - 1.f * s, c.y + 3.f * s)
        };
        auto draw = [&](float ox, float oy, ImU32 pc, float thick) {
            ImVec2 p[3] = {
                ImVec2(pts[0].x + ox, pts[0].y + oy),
                ImVec2(pts[1].x + ox, pts[1].y + oy),
                ImVec2(pts[2].x + ox, pts[2].y + oy)
            };
            dl->AddTriangleFilled(p[0], p[1], p[2], Alpha(pc, 72));
            dl->AddPolyline(p, 3, pc, true, thick);
            dl->AddLine(p[2], p[0], pc, thick);
        };
        if (shadow) draw(s, s, shade, 3.0f * s);
        draw(0.f, 0.f, col, 1.7f * s);
    }

    void DrawTrail(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 24.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        ImVec2 p[4] = {
            ImVec2(c.x - 9.f * s, c.y + 7.f * s),
            ImVec2(c.x - 3.f * s, c.y - 4.f * s),
            ImVec2(c.x + 3.f * s, c.y + 4.f * s),
            ImVec2(c.x + 9.f * s, c.y - 7.f * s)
        };
        auto draw = [&](float ox, float oy, ImU32 pc, float thick) {
            ImVec2 q[4] = {
                ImVec2(p[0].x + ox, p[0].y + oy),
                ImVec2(p[1].x + ox, p[1].y + oy),
                ImVec2(p[2].x + ox, p[2].y + oy),
                ImVec2(p[3].x + ox, p[3].y + oy)
            };
            ImVec2 path[17];
            for (int i = 0; i < 17; ++i)
            {
                const float t = i / 16.f;
                const float u = 1.f - t;
                const float uu = u * u, tt = t * t;
                const float w0 = uu * u;
                const float w1 = 3.f * uu * t;
                const float w2 = 3.f * u * tt;
                const float w3 = tt * t;
                path[i] = ImVec2(q[0].x * w0 + q[1].x * w1 + q[2].x * w2 + q[3].x * w3,
                                 q[0].y * w0 + q[1].y * w1 + q[2].y * w2 + q[3].y * w3);
            }
            dl->AddPolyline(path, 17, pc, false, thick);
        };
        if (shadow) draw(s, s, shade, 4.0f * s);
        draw(0.f, 0.f, col, 2.2f * s);
        dl->AddCircleFilled(p[0], 2.2f * s, col, 14);
        dl->AddCircleFilled(p[3], 2.2f * s, col, 14);
    }

    void DrawMarker(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto draw = [&](float ox, float oy, ImU32 pc, float thick) {
            ImVec2 p[4] = {
                ImVec2(c.x + ox, c.y - 9.f * s + oy),
                ImVec2(c.x + 8.f * s + ox, c.y + oy),
                ImVec2(c.x + ox, c.y + 9.f * s + oy),
                ImVec2(c.x - 8.f * s + ox, c.y + oy)
            };
            dl->AddPolyline(p, 4, pc, true, thick);
            dl->AddCircle(ImVec2(c.x + ox, c.y + oy), 3.2f * s, pc, 18, thick);
        };
        if (shadow) draw(s, s, shade, 3.0f * s);
        draw(0.f, 0.f, col, 1.6f * s);
    }

    void DrawMap(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow, bool mini)
    {
        const float s = size / 24.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto drawRect = [&](float ox, float oy, ImU32 pc, float thick) {
            const ImVec2 a(c.x - 10.f * s + ox, c.y - 7.f * s + oy);
            const ImVec2 b(c.x + 10.f * s + ox, c.y + 7.f * s + oy);
            dl->AddRect(a, b, pc, 2.f * s, 0, thick);
            dl->AddLine(ImVec2(c.x - 3.f * s + ox, a.y), ImVec2(c.x - 5.f * s + ox, b.y), pc, thick);
            dl->AddLine(ImVec2(c.x + 4.f * s + ox, a.y), ImVec2(c.x + 2.f * s + ox, b.y), pc, thick);
        };
        auto drawCircle = [&](float ox, float oy, ImU32 pc, float thick) {
            dl->AddCircle(ImVec2(c.x + ox, c.y + oy), 9.f * s, pc, 28, thick);
            dl->AddLine(ImVec2(c.x + ox, c.y - 9.f * s + oy), ImVec2(c.x + ox, c.y + 9.f * s + oy), pc, thick);
            dl->AddLine(ImVec2(c.x - 9.f * s + ox, c.y + oy), ImVec2(c.x + 9.f * s + ox, c.y + oy), pc, thick);
        };
        if (shadow)
        {
            if (mini) drawCircle(s, s, shade, 3.f * s);
            else drawRect(s, s, shade, 3.f * s);
        }
        if (mini) drawCircle(0.f, 0.f, col, 1.55f * s);
        else drawRect(0.f, 0.f, col, 1.55f * s);
    }

    void DrawCog(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto draw = [&](float ox, float oy, ImU32 pc, float thick) {
            const ImVec2 cc(c.x + ox, c.y + oy);
            for (int i = 0; i < 8; ++i)
            {
                const float a = i * 0.78539816f;
                const ImVec2 a0(cc.x + std::cos(a) * 7.f * s, cc.y + std::sin(a) * 7.f * s);
                const ImVec2 a1(cc.x + std::cos(a) * 9.5f * s, cc.y + std::sin(a) * 9.5f * s);
                dl->AddLine(a0, a1, pc, thick);
            }
            dl->AddCircle(cc, 6.5f * s, pc, 28, thick);
            dl->AddCircle(cc, 2.4f * s, pc, 18, thick);
        };
        if (shadow) draw(s, s, shade, 3.f * s);
        draw(0.f, 0.f, col, 1.55f * s);
    }

    void DrawClock(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto draw = [&](float ox, float oy, ImU32 pc, float thick) {
            const ImVec2 cc(c.x + ox, c.y + oy);
            dl->AddCircle(cc, 8.2f * s, pc, 30, thick);                              // face rim
            dl->AddLine(cc, ImVec2(cc.x, cc.y - 5.4f * s), pc, thick);               // minute hand (12 o'clock)
            dl->AddLine(cc, ImVec2(cc.x + 3.7f * s, cc.y + 1.3f * s), pc, thick);     // hour hand (~4-5 o'clock)
            dl->AddCircleFilled(cc, 1.3f * s, pc, 12);                               // hub
        };
        if (shadow) draw(s, s, shade, 3.f * s);
        draw(0.f, 0.f, col, 1.6f * s);
    }

    void DrawCoins(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto draw = [&](float ox, float oy, ImU32 pc, float thick) {
            const ImVec2 back(c.x + ox - 3.0f * s, c.y + oy - 2.6f * s);   // coin behind
            const ImVec2 fr(c.x + ox + 3.0f * s, c.y + oy + 2.6f * s);     // coin in front
            dl->AddCircle(back, 6.1f * s, pc, 26, thick);
            dl->AddCircle(fr, 6.1f * s, pc, 26, thick);
            dl->AddCircleFilled(fr, 1.5f * s, pc, 12);                     // pip on the front coin
        };
        if (shadow) draw(s, s, shade, 3.f * s);
        draw(0.f, 0.f, col, 1.55f * s);
    }

    void DrawStar(ImDrawList* dl, ImVec2 c, float size, ImU32 col, const Render::GlyphStyle& style)
    {
        const float r = size * 0.42f;
        ImVec2 pts[10];
        for (int i = 0; i < 10; ++i)
        {
            const float a = -1.57079633f + i * 0.62831853f;
            const float rr = (i & 1) ? r * 0.44f : r;
            pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
        }
        if (style.filled)
        {
            const ImU32 fill = Alpha(col, style.disabled ? 105 : 215);
            for (int i = 0; i < 10; ++i)
            {
                const ImVec2 tri[3] = { c, pts[i], pts[(i + 1) % 10] };
                dl->AddConvexPolyFilled(tri, 3, fill);
            }
        }
        dl->AddPolyline(pts, 10, col, true, 1.35f);
    }

    void DrawRefresh(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto draw = [&](float ox, float oy, ImU32 pc, float thick) {
            const ImVec2 cc(c.x + ox, c.y + oy);
            const float r = 7.0f * s;
            const float a0 = -2.45f;
            const float a1 = 1.65f;
            dl->PathArcTo(cc, r, a0, a1, 24);
            dl->PathStroke(pc, false, thick);

            const ImVec2 tip(cc.x + std::cos(a1) * r, cc.y + std::sin(a1) * r);
            const float ah = 4.0f * s;
            const float back = a1 - 0.75f;
            const float side = a1 + 0.65f;
            dl->AddTriangleFilled(tip,
                                  ImVec2(tip.x + std::cos(back) * ah, tip.y + std::sin(back) * ah),
                                  ImVec2(tip.x + std::cos(side) * ah, tip.y + std::sin(side) * ah),
                                  pc);
        };
        if (shadow) draw(s, s, shade, 3.0f * s);
        draw(0.f, 0.f, col, 1.8f * s);
    }

    void DrawBag(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto handle = [&](float ox, float oy, ImU32 pc, float t) {
            dl->PathArcTo(ImVec2(c.x + ox, c.y - 3.f * s + oy), 4.f * s, 3.14159f, 6.28318f, 12);
            dl->PathStroke(pc, false, t);
        };
        auto body = [&](float ox, float oy, ImU32 pc, float t) {
            dl->AddRect(ImVec2(c.x - 8.f * s + ox, c.y - 3.f * s + oy), ImVec2(c.x + 8.f * s + ox, c.y + 9.f * s + oy), pc, 2.5f * s, 0, t);
            dl->AddLine(ImVec2(c.x - 8.f * s + ox, c.y + 1.f * s + oy), ImVec2(c.x + 8.f * s + ox, c.y + 1.f * s + oy), pc, t);
        };
        if (shadow) { handle(s, s, shade, 3.f * s); body(s, s, shade, 3.f * s); }
        handle(0.f, 0.f, col, 1.6f * s);
        body(0.f, 0.f, col, 1.6f * s);
    }

    void DrawCart(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto cart = [&](float ox, float oy, ImU32 pc, float t) {
            auto P = [&](float x, float y) { return ImVec2(c.x + x * s + ox, c.y + y * s + oy); };
            dl->AddLine(P(-7.f, -3.f), P(8.f, -3.f), pc, t);   // basket top rim
            dl->AddLine(P(-7.f, -3.f), P(-4.f, 5.f), pc, t);   // left side
            dl->AddLine(P(8.f, -3.f),  P(5.f, 5.f),  pc, t);   // right side
            dl->AddLine(P(-4.f, 5.f),  P(5.f, 5.f),  pc, t);   // bottom
            dl->AddLine(P(-7.f, -3.f), P(-11.f, -7.f), pc, t); // handle up-left
            dl->AddCircleFilled(P(-2.f, 8.f), 1.7f * s, pc, 10);  // wheels
            dl->AddCircleFilled(P(4.f, 8.f),  1.7f * s, pc, 10);
        };
        if (shadow) cart(s, s, shade, 3.f * s);
        cart(0.f, 0.f, col, 1.7f * s);
    }

    void DrawItems(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto crate = [&](float ox, float oy, ImU32 pc, float t) {
            dl->AddRect(ImVec2(c.x - 8.f * s + ox, c.y - 7.f * s + oy), ImVec2(c.x + 8.f * s + ox, c.y + 8.f * s + oy), pc, 2.f * s, 0, t);  // box
            dl->AddLine(ImVec2(c.x - 8.f * s + ox, c.y - 1.f * s + oy), ImVec2(c.x + 8.f * s + ox, c.y - 1.f * s + oy), pc, t);              // lid seam
            dl->AddLine(ImVec2(c.x + ox, c.y - 1.f * s + oy), ImVec2(c.x + ox, c.y + 8.f * s + oy), pc, t);                                 // lower slat divider
        };
        if (shadow) crate(s, s, shade, 3.f * s);
        crate(0.f, 0.f, col, 1.6f * s);
    }

    void DrawChecklist(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto draw = [&](float ox, float oy, ImU32 pc, float t) {
            dl->AddRect(ImVec2(c.x - 8.f * s + ox, c.y - 9.f * s + oy), ImVec2(c.x + 8.f * s + ox, c.y + 9.f * s + oy), pc, 2.f * s, 0, t);
            for (int r = 0; r < 3; ++r)
            {
                const float y = -5.f + r * 5.f;
                dl->AddLine(ImVec2(c.x - 5.5f * s + ox, c.y + y * s + oy), ImVec2(c.x - 4.f * s + ox, c.y + (y + 1.6f) * s + oy), pc, t);
                dl->AddLine(ImVec2(c.x - 4.f * s + ox, c.y + (y + 1.6f) * s + oy), ImVec2(c.x - 1.5f * s + ox, c.y + (y - 1.6f) * s + oy), pc, t);
                dl->AddLine(ImVec2(c.x + 0.5f * s + ox, c.y + y * s + oy), ImVec2(c.x + 5.5f * s + ox, c.y + y * s + oy), pc, t);
            }
        };
        if (shadow) draw(s, s, shade, 3.f * s);
        draw(0.f, 0.f, col, 1.5f * s);
    }

    void DrawDungeon(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        const float L = -8.f, R = 8.f, B = 9.f, top = -2.f;
        auto draw = [&](float ox, float oy, ImU32 pc, float t) {
            dl->PathLineTo(ImVec2(c.x + L * s + ox, c.y + B * s + oy));
            dl->PathLineTo(ImVec2(c.x + L * s + ox, c.y + top * s + oy));
            dl->PathArcTo(ImVec2(c.x + ox, c.y + top * s + oy), 8.f * s, 3.14159f, 6.28318f, 14);
            dl->PathLineTo(ImVec2(c.x + R * s + ox, c.y + B * s + oy));
            dl->PathStroke(pc, false, t);
            dl->AddLine(ImVec2(c.x + L * s + ox, c.y + B * s + oy), ImVec2(c.x + R * s + ox, c.y + B * s + oy), pc, t);
            dl->AddLine(ImVec2(c.x - 3.f * s + ox, c.y + B * s + oy), ImVec2(c.x - 3.f * s + ox, c.y + (top - 3.f) * s + oy), pc, t);
            dl->AddLine(ImVec2(c.x + 3.f * s + ox, c.y + B * s + oy), ImVec2(c.x + 3.f * s + ox, c.y + (top - 3.f) * s + oy), pc, t);
        };
        if (shadow) draw(s, s, shade, 3.f * s);
        draw(0.f, 0.f, col, 1.6f * s);
    }

    void DrawGrid(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        auto cell = [&](float x0, float y0, float x1, float y1, float ox, float oy, ImU32 pc, float t) {
            dl->AddRect(ImVec2(c.x + x0 * s + ox, c.y + y0 * s + oy), ImVec2(c.x + x1 * s + ox, c.y + y1 * s + oy), pc, 1.5f * s, 0, t);
        };
        auto draw = [&](float ox, float oy, ImU32 pc, float t) {
            cell(-8.f, -8.f, -1.f, -1.f, ox, oy, pc, t); cell(1.f, -8.f, 8.f, -1.f, ox, oy, pc, t);
            cell(-8.f, 1.f, -1.f, 8.f, ox, oy, pc, t);   cell(1.f, 1.f, 8.f, 8.f, ox, oy, pc, t);
        };
        if (shadow) draw(s, s, shade, 3.f * s);
        draw(0.f, 0.f, col, 1.6f * s);
    }

    void DrawEye(ImDrawList* dl, ImVec2 c, float size, ImU32 col, bool shadow, bool slashed)
    {
        const float s = size / 22.f;
        const ImU32 shade = IM_COL32(0, 0, 0, 155);
        const float W = 8.f, K = 4.5f;                       // half-width / lid bulge (glyph units)
        const float yc = (W * W - K * K) / (2.f * K);        // arc-centre offset from the eye centre
        const float er = std::sqrt(W * W + yc * yc);         // arc radius
        const float ea = std::atan2(yc, W);                  // corner half-angle
        const float kPi = 3.14159265f;
        auto draw = [&](float ox, float oy, ImU32 pc, float t) {
            dl->PathArcTo(ImVec2(c.x + ox, c.y + yc * s + oy), er * s, -(kPi - ea), -ea, 16);  // upper lid (up)
            dl->PathStroke(pc, false, t);
            dl->PathArcTo(ImVec2(c.x + ox, c.y - yc * s + oy), er * s, ea, kPi - ea, 16);       // lower lid (down)
            dl->PathStroke(pc, false, t);
            dl->AddCircleFilled(ImVec2(c.x + ox, c.y + oy), 2.6f * s, pc);                      // pupil
            if (slashed) dl->AddLine(ImVec2(c.x - W * s + ox, c.y + W * s + oy),
                                     ImVec2(c.x + W * s + ox, c.y - W * s + oy), pc, t);
        };
        if (shadow) draw(s, s, shade, 2.6f * s);
        draw(0.f, 0.f, col, 1.6f * s);
    }
}

void Render::DrawGlyph(ImDrawList* dl, ImVec2 center, float size, Glyph glyph, ImU32 color, GlyphStyle style)
{
    if (!dl || size <= 0.f) return;

    const float sz = size;
    const ImVec2 at(center.x - sz * 0.5f, center.y - sz * 0.5f);
    switch (glyph)
    {
        case Glyph::Grip:
            for (int r = 0; r < 3; ++r)
                dl->AddLine(ImVec2(at.x + 2.f, at.y + 4.f + r * 4.f),
                            ImVec2(at.x + 13.f, at.y + 4.f + r * 4.f), color, 1.4f);
            break;
        case Glyph::Trash:
        {
            const float midx = at.x + sz * 0.5f;
            dl->AddRect(ImVec2(at.x + 3.5f, at.y + 6.f), ImVec2(at.x + sz - 3.5f, at.y + sz - 2.f), color, 1.f, 0, 1.3f);
            dl->AddLine(ImVec2(at.x + 2.f, at.y + 6.f), ImVec2(at.x + sz - 2.f, at.y + 6.f), color, 1.6f);
            dl->AddLine(ImVec2(midx - 2.5f, at.y + 3.f), ImVec2(midx + 2.5f, at.y + 3.f), color, 1.6f);
            for (int i = -1; i <= 1; ++i)
                dl->AddLine(ImVec2(midx + i * 3.f, at.y + 9.f), ImVec2(midx + i * 3.f, at.y + sz - 4.f), color, 1.f);
            break;
        }
        case Glyph::ArrowUp:
            dl->AddTriangleFilled(ImVec2(center.x, center.y - 4.f), ImVec2(center.x - 4.f, center.y + 3.f),
                                  ImVec2(center.x + 4.f, center.y + 3.f), color);
            break;
        case Glyph::ArrowDown:
            dl->AddTriangleFilled(ImVec2(center.x - 4.f, center.y - 3.f), ImVec2(center.x + 4.f, center.y - 3.f),
                                  ImVec2(center.x, center.y + 4.f), color);
            break;
        case Glyph::Plus:
            dl->AddLine(ImVec2(center.x - 5.f, center.y), ImVec2(center.x + 5.f, center.y), color, 1.8f);
            dl->AddLine(ImVec2(center.x, center.y - 5.f), ImVec2(center.x, center.y + 5.f), color, 1.8f);
            break;
        case Glyph::Minus:   // disable (pairs with Plus = enable)
            dl->AddLine(ImVec2(center.x - 5.f, center.y), ImVec2(center.x + 5.f, center.y), color, 1.8f);
            break;
        case Glyph::Pencil:
        {
            const ImVec2 a(at.x + 4.f, at.y + sz - 4.f);
            const ImVec2 b(at.x + sz - 4.f, at.y + 5.f);
            dl->AddLine(a, b, color, 2.f);
            dl->AddLine(ImVec2(b.x - 3.f, b.y - 1.5f), ImVec2(b.x + 1.f, b.y + 2.5f), color, 1.6f);
            dl->AddTriangleFilled(ImVec2(a.x - 1.5f, a.y + 1.5f), ImVec2(a.x + 3.f, a.y), ImVec2(a.x, a.y - 3.f), color);
            break;
        }
        case Glyph::Duplicate:
            dl->AddRect(ImVec2(at.x + 5.f, at.y + 3.f), ImVec2(at.x + sz - 2.f, at.y + sz - 6.f), color, 1.5f, 0, 1.4f);
            dl->AddRect(ImVec2(at.x + 2.f, at.y + 7.f), ImVec2(at.x + sz - 5.f, at.y + sz - 2.f), color, 1.5f, 0, 1.4f);
            break;
        case Glyph::ChromeBar:
        case Glyph::ChromeHover:
        {
            const ImVec2 a(at.x + 1.f, at.y + 3.f), b(at.x + sz - 1.f, at.y + sz - 3.f);
            dl->AddRect(a, b, color, 2.f, 0, 1.4f);
            if (glyph == Glyph::ChromeBar) dl->AddRectFilled(ImVec2(a.x + 1.5f, a.y + 1.5f), ImVec2(b.x - 1.5f, a.y + 5.f), color);
            break;
        }
        case Glyph::WidthFull:
        case Glyph::WidthHalf:
        {
            const ImVec2 a(at.x + 1.f, at.y + 3.f), b(at.x + sz - 1.f, at.y + sz - 3.f);
            dl->AddRect(a, b, color, 2.f, 0, 1.4f);
            if (glyph == Glyph::WidthHalf)
            {
                const float mx = (a.x + b.x) * 0.5f;
                dl->AddRectFilled(ImVec2(a.x + 1.5f, a.y + 1.5f), ImVec2(mx - 1.f, b.y - 1.5f), color);
                dl->AddLine(ImVec2(mx, a.y), ImVec2(mx, b.y), color, 1.2f);
            }
            else dl->AddRectFilled(ImVec2(a.x + 1.5f, a.y + 1.5f), ImVec2(b.x - 1.5f, b.y - 1.5f), color);
            break;
        }
        case Glyph::Guide:
            DrawGuide(dl, center, size, color, style.shadow);
            break;
        case Glyph::MapPin:
            DrawMapPin(dl, center, size, color, style.shadow);
            break;
        case Glyph::Copy:
            DrawCopy(dl, center, size, color, style.shadow);
            break;
        case Glyph::Star:
            DrawStar(dl, center, size, color, style);
            break;
        case Glyph::Refresh:
            DrawRefresh(dl, center, size, color, style.shadow);
            break;
        case Glyph::Navigate:
            DrawNavigate(dl, center, size, color, style.shadow);
            break;
        case Glyph::Trail:
            DrawTrail(dl, center, size, color, style.shadow);
            break;
        case Glyph::Marker:
            DrawMarker(dl, center, size, color, style.shadow);
            break;
        case Glyph::Map:
            DrawMap(dl, center, size, color, style.shadow, false);
            break;
        case Glyph::MiniMap:
            DrawMap(dl, center, size, color, style.shadow, true);
            break;
        case Glyph::Cog:
            DrawCog(dl, center, size, color, style.shadow);
            break;
        case Glyph::Clock:
            DrawClock(dl, center, size, color, style.shadow);
            break;
        case Glyph::Coins:
            DrawCoins(dl, center, size, color, style.shadow);
            break;
        case Glyph::Cart:
            DrawCart(dl, center, size, color, style.shadow);
            break;
        case Glyph::Chart:
        {   // three rising bars on a baseline -- a session/earnings graph
            const float base = at.y + sz - 3.f;
            const float bw   = sz * 0.18f;
            const float xs[3] = { at.x + sz * 0.16f, at.x + sz * 0.41f, at.x + sz * 0.66f };
            const float hs[3] = { sz * 0.30f,        sz * 0.50f,        sz * 0.72f };
            for (int i = 0; i < 3; ++i)
                dl->AddRectFilled(ImVec2(xs[i], base - hs[i]), ImVec2(xs[i] + bw, base), color, 1.f);
            dl->AddLine(ImVec2(at.x + 2.f, base), ImVec2(at.x + sz - 2.f, base), color, 1.4f);
            break;
        }
        case Glyph::Fish:
        {   // a round body + a triangular tail + an eye dot
            const float cx = at.x + sz * 0.56f, cy = at.y + sz * 0.5f, r = sz * 0.27f;
            dl->AddCircleFilled(ImVec2(cx, cy), r, color, 22);
            dl->AddTriangleFilled(ImVec2(cx - r + 1.f, cy), ImVec2(at.x + sz * 0.06f, at.y + sz * 0.24f),
                                  ImVec2(at.x + sz * 0.06f, at.y + sz * 0.76f), color);
            dl->AddCircleFilled(ImVec2(cx + r * 0.42f, cy - r * 0.30f), sz * 0.055f, IM_COL32(22, 24, 28, 255), 10);
            break;
        }
        case Glyph::Sun:
        {   // a disc + eight rays (day)
            dl->AddCircleFilled(center, sz * 0.21f, color, 20);
            static const float dx[8] = { 1.f, 0.7071f, 0.f, -0.7071f, -1.f, -0.7071f, 0.f, 0.7071f };
            static const float dy[8] = { 0.f, 0.7071f, 1.f, 0.7071f, 0.f, -0.7071f, -1.f, -0.7071f };
            const float r0 = sz * 0.31f, r1 = sz * 0.45f;
            for (int i = 0; i < 8; ++i)
                dl->AddLine(ImVec2(center.x + dx[i] * r0, center.y + dy[i] * r0),
                            ImVec2(center.x + dx[i] * r1, center.y + dy[i] * r1), color, 1.6f);
            break;
        }
        case Glyph::Moon:
        {   // a crescent moon: fill ONLY the lit lune (moon disc minus an offset shadow disc) in `color`, scanline
            // by scanline, so the dark side is simply the background -- no carved dark disc that can spill past the
            // moon's edge (which read as a stray dark dot). Right limb = the moon circle; left edge = the terminator.
            const float r  = sz * 0.34f;
            const float d  = r * 0.66f;            // shadow offset left -> a lit crescent on the right
            const float mx = center.x, my = center.y;
            const int   N  = 24;
            bool have = false; float pL = 0.f, pR = 0.f, pY = 0.f;
            for (int i = 0; i <= N; ++i)
            {
                const float y  = my - r + (2.f * r) * ((float)i / (float)N);
                const float m2 = r * r - (y - my) * (y - my);
                if (m2 <= 0.f) { have = false; continue; }
                const float w  = std::sqrt(m2);    // moon AND shadow share radius + y-centre -> same half-width
                const float leftLit  = (std::max)(mx - w, mx - d + w);   // terminator (shadow's right edge)
                const float rightLit = mx + w;                          // moon limb
                if (rightLit <= leftLit) { have = false; continue; }
                if (have) dl->AddQuadFilled(ImVec2(pL, pY), ImVec2(pR, pY), ImVec2(rightLit, y), ImVec2(leftLit, y), color);
                pL = leftLit; pR = rightLit; pY = y; have = true;
            }
            break;
        }
        case Glyph::Horizon:
        {   // a half-sun rising over a horizon line (dusk/dawn twilight)
            const float r = sz * 0.24f;
            const float hy = center.y + sz * 0.12f;
            dl->PathArcTo(ImVec2(center.x, hy), r, 3.14159f, 6.2832f, 20);   // upper half-disc
            dl->PathFillConvex(color);
            dl->AddLine(ImVec2(center.x, hy - r - sz * 0.12f), ImVec2(center.x, hy - r - sz * 0.03f), color, 1.4f);
            dl->AddLine(ImVec2(center.x - r - sz * 0.12f, hy - sz * 0.05f), ImVec2(center.x - r - sz * 0.03f, hy - sz * 0.05f), color, 1.4f);
            dl->AddLine(ImVec2(center.x + r + sz * 0.03f, hy - sz * 0.05f), ImVec2(center.x + r + sz * 0.12f, hy - sz * 0.05f), color, 1.4f);
            dl->AddLine(ImVec2(center.x - sz * 0.42f, hy), ImVec2(center.x + sz * 0.42f, hy), color, 1.6f);
            break;
        }
        case Glyph::Bag:
            DrawBag(dl, center, size, color, style.shadow);
            break;
        case Glyph::Items:
            DrawItems(dl, center, size, color, style.shadow);
            break;
        case Glyph::Checklist:
            DrawChecklist(dl, center, size, color, style.shadow);
            break;
        case Glyph::Dungeon:
            DrawDungeon(dl, center, size, color, style.shadow);
            break;
        case Glyph::Grid:
            DrawGrid(dl, center, size, color, style.shadow);
            break;
        case Glyph::Eye:
            DrawEye(dl, center, size, color, style.shadow, false);
            break;
        case Glyph::EyeOff:
            DrawEye(dl, center, size, color, style.shadow, true);
            break;
        case Glyph::Check:
            dl->AddLine(ImVec2(center.x - 4.5f, center.y + 0.5f), ImVec2(center.x - 1.f, center.y + 4.f), color, 2.f);
            dl->AddLine(ImVec2(center.x - 1.f, center.y + 4.f), ImVec2(center.x + 5.f, center.y - 4.5f), color, 2.f);
            break;
        case Glyph::Cross:
            dl->AddLine(ImVec2(center.x - 4.f, center.y - 4.f), ImVec2(center.x + 4.f, center.y + 4.f), color, 2.f);
            dl->AddLine(ImVec2(center.x - 4.f, center.y + 4.f), ImVec2(center.x + 4.f, center.y - 4.f), color, 2.f);
            break;
        case Glyph::CaretRight:   // collapsed: right-pointing triangle (scales with size; ~+-4 at size 14)
        {
            const float a = size * 0.3f;
            const float side = style.mirrorX ? -1.f : 1.f;
            dl->AddTriangleFilled(ImVec2(center.x - side * a * 0.7f, center.y - a), ImVec2(center.x + side * a, center.y),
                                  ImVec2(center.x - side * a * 0.7f, center.y + a), color);
            break;
        }
        case Glyph::CaretDown:    // expanded: down-pointing triangle
        {
            const float a = size * 0.3f;
            dl->AddTriangleFilled(ImVec2(center.x - a, center.y - a * 0.7f), ImVec2(center.x + a, center.y - a * 0.7f),
                                  ImVec2(center.x, center.y + a), color);
            break;
        }
        case Glyph::ResizeGrip:   // 3 parallel diagonal hatches clustered toward the bottom-right corner (mirrorX -> left)
        {
            const float L = at.x, R = at.x + sz, B = at.y + sz;
            for (int i = 0; i < 3; ++i)
            {
                const float o = 3.f + i * 4.f;
                ImVec2 a(L + o, B - 2.f), b(R - 2.f, at.y + o);   // lower-left -> upper-right
                if (style.mirrorX) { a.x = 2.f * center.x - a.x; b.x = 2.f * center.x - b.x; }
                dl->AddLine(a, b, color, 1.4f);
            }
            break;
        }
        case Glyph::ExternalLink:   // a window frame (lower-left) + an arrow leaving toward the top-right
        {
            const float r = sz * 0.34f;
            dl->AddRect(ImVec2(center.x - r, center.y - r * 0.30f), ImVec2(center.x + r * 0.30f, center.y + r),
                        color, 0.f, 0, 1.5f);
            const ImVec2 tip(center.x + r, center.y - r);
            dl->AddLine(ImVec2(center.x - r * 0.05f, center.y + r * 0.05f), tip, color, 1.5f);
            dl->AddLine(tip, ImVec2(tip.x - r * 0.62f, tip.y), color, 1.5f);
            dl->AddLine(tip, ImVec2(tip.x, tip.y + r * 0.62f), color, 1.5f);
            break;
        }
        case Glyph::Globe:   // browser: a globe -- circle + equator/latitudes + meridians
        {
            const float r = sz * 0.36f;
            dl->AddCircle(center, r, color, 24, 1.5f);
            dl->AddLine(ImVec2(center.x - r, center.y), ImVec2(center.x + r, center.y), color, 1.1f);   // equator
            dl->AddLine(ImVec2(center.x - r * 0.72f, center.y - r * 0.55f), ImVec2(center.x + r * 0.72f, center.y - r * 0.55f), color, 1.f);
            dl->AddLine(ImVec2(center.x - r * 0.72f, center.y + r * 0.55f), ImVec2(center.x + r * 0.72f, center.y + r * 0.55f), color, 1.f);
            dl->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r), color, 1.f);    // central meridian
            ImVec2 mer[17];
            for (int i = 0; i <= 16; ++i) { const float t = (float)i / 16.f * 6.2831853f;
                mer[i] = ImVec2(center.x + std::sin(t) * r * 0.5f, center.y - std::cos(t) * r); }
            dl->AddPolyline(mer, 17, color, false, 1.f);                                                // curved meridian
            break;
        }
        case Glyph::Book:   // library: a closed book -- cover + spine + page lines
        {
            const float w = sz * 0.32f, h = sz * 0.40f;
            dl->AddRect(ImVec2(center.x - w, center.y - h), ImVec2(center.x + w, center.y + h), color, 1.5f, 0, 1.6f);
            dl->AddLine(ImVec2(center.x - w + sz * 0.10f, center.y - h), ImVec2(center.x - w + sz * 0.10f, center.y + h), color, 1.2f);  // spine
            for (int i = 0; i < 2; ++i)
            {
                const float yy = center.y - h * 0.34f + i * h * 0.5f;
                dl->AddLine(ImVec2(center.x - w + sz * 0.18f, yy), ImVec2(center.x + w - sz * 0.08f, yy), color, 1.f);
            }
            break;
        }
    }
}

void Render::DrawZoneRule(ImDrawList* dl, ImVec2 center, float halfW, float height, ImU32 color, float reveal, bool minor)
{
    if (!dl) return;
    const float r = reveal < 0.f ? 0.f : (reveal > 1.f ? 1.f : reveal);
    if (r <= 0.001f || halfW <= 1.f) return;

    const float w       = halfW * r;                 // current visible half-width (wipe out from centre)

    if (minor)   // secondary divider: a thin tapered hairline (no diamond) -- full colour at centre, faded at the ends
    {
        const float thM = (std::max)(1.f, height * 0.08f);
        const int   ith = (std::max)(1, (int)(thM + 0.5f));
        // Snap to whole pixels so the hairline is a crisp, CONSISTENT thickness regardless of its fractional Y
        // (an unsnapped 1px rect straddles two rows and AA-feathers thinner at some positions than others).
        const float yTm = std::floor(center.y - thM * 0.5f + 0.5f);
        const float yBm = yTm + (float)ith;
        const ImU32 endM = color & 0x00FFFFFFu;
        dl->AddRectFilledMultiColor(ImVec2(center.x,     yTm), ImVec2(center.x + w, yBm), color, endM, endM, color);
        dl->AddRectFilledMultiColor(ImVec2(center.x - w, yTm), ImVec2(center.x,     yBm), endM, color, color, endM);
        return;
    }

    const float th      = (std::max)(1.f, height * 0.14f);   // bar thickness
    const float diamond = (std::max)(2.f, height * 0.42f) * (r < 0.5f ? r * 2.f : 1.f);  // diamond pops in first
    const float gap     = (std::max)(2.f, height * 0.42f) + height * 0.30f;   // bars start beyond the diamond
    const ImU32 endCol  = color & 0x00FFFFFFu;       // alpha 0 -- the tapered, faded outer end

    // Tapered bars flanking the diamond: inner end = full color, outer end = transparent.
    if (w > gap + 1.f)
    {
        const float yT = center.y - th * 0.5f, yB = center.y + th * 0.5f;
        // right bar (inner at +gap -> outer at +w)
        dl->AddRectFilledMultiColor(ImVec2(center.x + gap, yT), ImVec2(center.x + w, yB),
                                    color, endCol, endCol, color);
        // left bar (outer at -w -> inner at -gap)
        dl->AddRectFilledMultiColor(ImVec2(center.x - w, yT), ImVec2(center.x - gap, yB),
                                    endCol, color, color, endCol);
    }

    // Centre diamond (rotated square).
    dl->AddQuadFilled(ImVec2(center.x, center.y - diamond), ImVec2(center.x + diamond, center.y),
                      ImVec2(center.x, center.y + diamond), ImVec2(center.x - diamond, center.y), color);
}
