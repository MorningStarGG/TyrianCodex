// Gw2Ui :: a reusable time-series LINE chart (price history + any future chart). Bespoke ImDrawList drawing, but
// SHARED (per the centralize-don't-localize rule) so callers don't each reinvent axes/scaling/hover.
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiInternal.h"

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

using namespace Gw2Ui::detail;

void Gw2Ui::LineChart(const char* id, ImVec2 size, const ChartSeries* series, int seriesCount,
                      const std::function<std::string(float)>& fmtY,
                      const std::function<void(float, ImVec2)>& onHover)
{
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (size.x <= 0.f) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0.f) size.y = 130.f;
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Plot rect = the area minus a left gutter for the Y labels + small margins.
    const float gutterL = 64.f, padT = 6.f, padB = 6.f, padR = 8.f;
    const ImVec2 a(p0.x + gutterL, p0.y + padT);
    const ImVec2 b(p0.x + size.x - padR, p0.y + size.y - padB);
    const float plotW = b.x - a.x, plotH = b.y - a.y;
    if (plotW <= 4.f || plotH <= 4.f) return;

    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 60));
    dl->AddRect(a, b, IM_COL32(150, 130, 80, 60));

    // Auto-fit min/max across every point of every series.
    float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
    int total = 0;
    for (int s = 0; s < seriesCount; ++s)
        for (int i = 0; i < series[s].count; ++i)
        {
            const ImVec2& pt = series[s].points[i];
            minX = std::min(minX, pt.x); maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y); maxY = std::max(maxY, pt.y);
            ++total;
        }
    if (total < 2 || maxX <= minX) return;
    if (maxY <= minY) maxY = minY + 1.f;
    const float padY = (maxY - minY) * 0.08f + 1.f;
    minY = std::max(0.f, minY - padY);
    maxY += padY;

    auto sx = [&](float x) { return a.x + (x - minX) / (maxX - minX) * plotW; };
    auto sy = [&](float y) { return b.y - (y - minY) / (maxY - minY) * plotH; };

    // Horizontal gridlines + Y-axis labels. Label size scales with the plot height so the full-size Charts-view
    // chart isn't tiny while the compact inline card stays proportionate.
    const float labFs = std::clamp(plotH * 0.05f, 14.f, 18.f);
    const int kGrid = plotH < 130.f ? 2 : 4;   // fewer gridlines on short charts (the volume strip) so labels don't crowd
    for (int g = 0; g <= kGrid; ++g)
    {
        const float yv = minY + (maxY - minY) * (float)g / (float)kGrid;
        const float yy = sy(yv);
        dl->AddLine(ImVec2(a.x, yy), ImVec2(b.x, yy), IM_COL32(150, 140, 110, g == 0 ? 70 : 26), 1.f);
        const std::string lbl = fmtY ? fmtY(yv) : std::to_string((int)yv);
        LabelIn(ImVec2(p0.x, yy - 10.f), ImVec2(a.x - 5.f, yy + 10.f), lbl.c_str(),
                HAlign::Right, VAlign::Middle, IM_COL32(180, 170, 146, 220), false, nullptr, labFs);
    }

    // Series polylines.
    std::vector<ImVec2> pts;
    for (int s = 0; s < seriesCount; ++s)
    {
        if (series[s].count < 2) continue;
        pts.clear(); pts.reserve(series[s].count);
        for (int i = 0; i < series[s].count; ++i)
            pts.push_back(ImVec2(sx(series[s].points[i].x), sy(series[s].points[i].y)));
        dl->AddPolyline(pts.data(), (int)pts.size(), series[s].color, false, 1.8f);
    }

    // Hover: a vertical crosshair at the nearest data x + a dot on each series; the caller builds the tooltip.
    int hs = -1;   // first non-empty series drives the nearest-point search (series[0] may be empty)
    for (int s = 0; s < seriesCount; ++s) if (series[s].count > 0) { hs = s; break; }
    if (hovered && hs >= 0)
    {
        const float mx = std::clamp(ImGui::GetIO().MousePos.x, a.x, b.x);
        const float xv = minX + (mx - a.x) / plotW * (maxX - minX);
        float nearestX = series[hs].points[0].x, best = FLT_MAX;
        for (int i = 0; i < series[hs].count; ++i)
        {
            const float dx = std::fabs(series[hs].points[i].x - xv);
            if (dx < best) { best = dx; nearestX = series[hs].points[i].x; }
        }
        const float cx = sx(nearestX);
        dl->AddLine(ImVec2(cx, a.y), ImVec2(cx, b.y), IM_COL32(255, 224, 150, 120), 1.f);
        for (int s = 0; s < seriesCount; ++s)
            for (int i = 0; i < series[s].count; ++i)
                if (series[s].points[i].x == nearestX)
                    dl->AddCircleFilled(ImVec2(cx, sy(series[s].points[i].y)), 2.8f, series[s].color, 12);
        if (onHover) onHover(nearestX, ImVec2(cx, a.y));
    }
}
