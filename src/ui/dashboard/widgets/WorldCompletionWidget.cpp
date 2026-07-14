#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "WidgetUtil.h"
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <set>
#include <string>

// Account-wide map completion across every bundled zone (for the current character). Cached behind the
// progress Version + character so it recomputes only when progress actually changes (perf).
namespace
{
    uint64_t    g_ver = ~0ull;
    std::string g_char = "\x01";
    int g_zonesDone = 0, g_zonesTotal = 0, g_objDone = 0, g_objTotal = 0;
}

void DashW::WorldCompletion(App& app, float w)
{
    const std::string ch = app.state.currentChar;
    const uint64_t v = app.progress.Version();
    if (v != g_ver || ch != g_char)
    {
        g_ver = v; g_char = ch;
        g_zonesDone = g_zonesTotal = g_objDone = g_objTotal = 0;
        const std::vector<std::string>& ids = app.progress.CompletedIds(ch);
        const std::vector<std::string>& cfd = app.progress.ConfirmedIds(ch);
        std::set<std::string> done(ids.begin(), ids.end());
        done.insert(cfd.begin(), cfd.end());   // reached waypoints count toward completion (confirmed-set only)
        for (const auto& kv : app.zones)
        {
            const Zone& z = kv.second;
            if (z.Steps.empty()) continue;
            int zt = 0, zd = 0;
            for (const Step& s : z.Steps) { ++zt; if (done.count(s.StepId)) ++zd; }
            g_objTotal += zt; g_objDone += zd;
            ++g_zonesTotal; if (zd == zt) ++g_zonesDone;
        }
    }

    if (g_objTotal == 0) { Gw2Ui::Label("No zone data loaded.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    Gw2Ui::ProgressBar((float)g_objDone / (float)g_objTotal, "World", w, 16.f, IM_COL32(120, 200, 230, 255));
    ImGui::Spacing();

    char zones[48]; std::snprintf(zones, sizeof(zones), "Zones complete  %d/%d", g_zonesDone, g_zonesTotal);
    char objs[48];  std::snprintf(objs,  sizeof(objs),  "Objectives  %d/%d", g_objDone, g_objTotal);
    const ImU32 summaryCol = IM_COL32(190, 184, 166, 255);
    const float fs = 14.f;
    const float rowH = std::max(17.f, fs + 5.f) * Gw2Ui::TextScale();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (DashUtil::Narrow(w))
    {
        ImGui::Dummy(ImVec2(w, rowH * 2.f));
        Gw2Ui::RowLabel(dl, Gw2Ui::RowHotspot{ p, w, rowH, false, false }, 0.f, 0.f, zones,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, summaryCol, false, nullptr, fs, 0.f, 1.0f);
        Gw2Ui::RowLabel(dl, Gw2Ui::RowHotspot{ ImVec2(p.x, p.y + rowH), w, rowH, false, false }, 0.f, 0.f, objs,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, summaryCol, false, nullptr, fs, 0.f, 1.0f);
    }
    else
    {
        ImGui::Dummy(ImVec2(w, rowH));
        const float gap = 10.f * Gw2Ui::TextScale();
        const float leftW = std::min(w * 0.52f, Gw2Ui::MeasureWidth(zones, fs) + 6.f * Gw2Ui::TextScale());
        const Gw2Ui::RowHotspot row{ p, w, rowH, false, false };
        Gw2Ui::RowLabel(dl, row, 0.f, w - leftW, zones,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, summaryCol, false, nullptr, fs, 0.f, 1.0f);
        Gw2Ui::RowLabel(dl, row, leftW + gap, 0.f, objs,
                        Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, summaryCol, false, nullptr, fs, 0.f, 1.0f);
    }
}
