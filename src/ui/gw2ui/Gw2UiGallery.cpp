#include "ui/gw2ui/Gw2UiGallery.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

// The canonical gallery SHELL: ONE toolbar + header + (optional) progress + (optional) rail + grid/list body,
// composed from the existing Gw2Ui primitives so every Collections/Items gallery stops hand-rolling the layout.
// Takes no App& -- the Config bits arrive by pointer in the descriptor. The shell owns the per-id order cache +
// the grid/list virtualization; the per-scope hatches draw each item (keeping APNG renders, swatches, badges,
// click + tooltip per-scope).
namespace Gw2Ui
{
    namespace
    {
        struct OrderCache { uint64_t tok = ~0ull; std::vector<int> order; };
        std::unordered_map<std::string, OrderCache> g_orderCaches;   // keyed by descriptor id (survives scope switches)

        // ---- frame helpers shared by BOTH GalleryBrowser and ChecklistBrowser ----

        // The cached display order for a widget id, rebuilt only when its token moves (no per-frame churn).
        const std::vector<int>& Order(const char* id, uint64_t tok, const std::function<void(std::vector<int>&)>& rebuild)
        {
            OrderCache& oc = g_orderCaches[id ? id : ""];
            if (oc.tok != tok) { oc.order.clear(); if (rebuild) rebuild(oc.order); oc.tok = tok; }
            return oc.order;
        }

        // "Title  --  N / M verb" + right status, then the progress bar (or a custom viz).
        void DrawHeaderProgress(const char* title, int have, int total, const char* tallyVerb, const char* rightStatus,
                                float headerFontSize, bool showProgress, const std::function<void(float)>& drawProgress)
        {
            if (title && *title)
            {
                char hbuf[176];
                if (have >= 0 && total >= 0)
                    std::snprintf(hbuf, sizeof(hbuf), "%s  --  %d / %d %s", title, have, total, tallyVerb ? tallyVerb : "");
                else
                    std::snprintf(hbuf, sizeof(hbuf), "%s", title);
                SectionHeader(hbuf, rightStatus, headerFontSize, kGold, true);
            }
            if (drawProgress)
                drawProgress(ImGui::GetContentRegionAvail().x);
            else if (showProgress && have >= 0 && total > 0)
                ProgressBar((float)have / (float)total, nullptr, 0.f, 10.f);
        }

        // The shared toolbar: search fills the row; filters + optional sort + optional grid|list cluster sits
        // right (one row) or wraps below (narrow). sortOptions/viewMode null => those controls are omitted.
        struct ToolbarSpec
        {
            bool search; const char* searchHint; char* searchBuf; int searchBufSize;
            const GalleryBrowser::FilterDropdown* filters; int filterCount;
            const char* const* sortOptions; int sortCount; int* sortSelected; bool* sortAsc;
            bool gridListToggle; int* viewMode; bool* settingsDirty;
        };

        void DrawToolbar(const ToolbarSpec& d)
        {
            const float barW = ImGui::GetContentRegionAvail().x;
            const float gap = 8.f, gridW = 70.f, sortW = 132.f, dirW = 60.f, h = InputBoxHeight();

            // A dropdown AUTO-FITS to its longest item -- its `width` is only a MAX (see Gw2Ui::Dropdown). So the
            // cluster width must use the SAME fit; a too-wide estimate would leave a gap before the right-pinned
            // grid button. Mirror the Dropdown fit: longest item (18px) + 36 (pads + arrow), capped at the max.
            auto ddFit = [](const char* const* opts, int n, float maxW) {
                float c = 0.f; for (int i = 0; i < n; ++i) c = std::max(c, MeasureWidth(opts[i], 18.f));
                c += 36.f; const float w = (maxW > 0.f) ? std::min(maxW, c) : c; return std::max(w, 40.f);
            };

            // The right-hand cluster width: filters + (sort + asc) + grid|list.
            float clusterW = 0.f;
            for (int i = 0; i < d.filterCount; ++i) clusterW += ddFit(d.filters[i].options, d.filters[i].count, d.filters[i].width) + gap;
            if (d.sortOptions && d.sortCount > 0) { clusterW += ddFit(d.sortOptions, d.sortCount, sortW) + gap; if (d.sortAsc) clusterW += dirW + gap; }
            if (d.gridListToggle && d.viewMode) clusterW += gridW;
            else if (clusterW > 0.f)            clusterW -= gap;   // no trailing grid -> drop the dangling gap

            auto drawCluster = [&]() {
                bool first = true;
                for (int i = 0; i < d.filterCount; ++i)
                {
                    if (!first) ImGui::SameLine(0.f, gap);
                    const GalleryBrowser::FilterDropdown& f = d.filters[i];
                    const std::string fid = std::string("##gf") + std::to_string(i);
                    Dropdown(fid.c_str(), f.options, f.count, f.selected, f.width, nullptr, nullptr, 0, h);
                    first = false;
                }
                if (d.sortOptions && d.sortCount > 0)
                {
                    if (!first) ImGui::SameLine(0.f, gap);
                    Dropdown("##gsort", d.sortOptions, d.sortCount, d.sortSelected, sortW, nullptr, nullptr, 0, h);
                    if (d.sortAsc)
                    {
                        ImGui::SameLine(0.f, gap);
                        if (ActionButton(*d.sortAsc ? "Asc" : "Desc", dirW, h)) { *d.sortAsc = !*d.sortAsc; if (d.settingsDirty) *d.settingsDirty = true; }
                    }
                    first = false;
                }
                if (d.gridListToggle && d.viewMode)
                {
                    SameLineRightCluster(gridW);
                    const bool grid = (*d.viewMode == 0);
                    if (ActionButton(grid ? "Grid" : "List", gridW, h)) { *d.viewMode = grid ? 1 : 0; if (d.settingsDirty) *d.settingsDirty = true; }
                }
            };

            // search fills the row; the cluster sits right (one row) or wraps below (narrow). searchW+gap places
            // the cluster start at barW-clusterW, so the cluster is a tight right-aligned group.
            const bool inlineCluster = (clusterW <= 0.f) || barW >= 180.f + clusterW + gap;
            if (d.search && d.searchBuf)
            {
                const float searchW = (inlineCluster && clusterW > 0.f) ? (barW - clusterW - gap) : barW;
                SearchBox("##gsearch", d.searchBuf, (size_t)d.searchBufSize, searchW, d.searchHint);
                if (clusterW > 0.f)
                {
                    if (inlineCluster) ImGui::SameLine(searchW + gap);   // same row, right-aligned group
                    drawCluster();                                       // else: next row
                }
            }
            else if (clusterW > 0.f)
            {
                drawCluster();
            }
        }
    }

    void DrawGalleryBrowser(const GalleryBrowser& d)
    {
        // 1) toolbar  2) optional extra row
        DrawToolbar({ d.search, d.searchHint, d.searchBuf, d.searchBufSize, d.filters, d.filterCount,
                      d.sortOptions, d.sortCount, d.sortSelected, d.sortAsc, d.gridListToggle, d.viewMode, d.settingsDirty });
        if (d.extraFilterRow) d.extraFilterRow(ImGui::GetContentRegionAvail().x);

        // 3) order cache FIRST (rebuilt only when the caller's token moves) so a headerText count is exact.
        const std::vector<int>& order = Order(d.id, d.orderToken, d.rebuildOrder);

        // 4) header (+ progress); a caller headerText override (e.g. "N items") replaces the SectionHeader tally.
        if (d.headerText)
            Label(d.headerText((int)order.size()).c_str(), kTextDim, false, nullptr, d.headerFontSize);
        else
            DrawHeaderProgress(d.title, d.have, d.total, d.tallyVerb, d.rightStatus, d.headerFontSize, d.showProgress, d.drawProgress);

        // 5) body: [ optional rail | VSplitter | grid-or-list ]
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        float bodyW = avail.x;

        if (d.railRoot && d.railSelectedKey)
        {
            static float s_localW = 200.f;
            float& railW = d.railWidthPx ? *d.railWidthPx : (s_localW = d.railDefaultW, s_localW);
            const float maxRail = std::clamp(avail.x - 280.f, 160.f, 340.f);
            railW = std::clamp(railW, 160.f, std::max(160.f, maxRail));

            GalleryRail((std::string("##grail_") + d.id).c_str(), railW, avail.y, *d.railRoot, d.railSelectedKey, d.railStyle);

            if (VSplitter((std::string("##gsplit_") + d.id).c_str(), origin.x + railW + 3.f, origin.y, avail.y,
                          &railW, 160.f, std::max(160.f, maxRail)))
                if (d.settingsDirty) *d.settingsDirty = true;

            ImGui::SetCursorScreenPos(ImVec2(origin.x + railW + 7.f, origin.y));
            bodyW = avail.x - railW - 7.f;
        }

        ImGui::BeginChild((std::string("##gbody_") + d.id).c_str(), ImVec2(bodyW, avail.y), false);
        if (order.empty())
        {
            EmptyState(d.emptyText);
        }
        else
        {
            const float scale = TextScale();
            const bool list = d.textOnlyList || (d.viewMode && *d.viewMode == 1);
            if (list && d.drawListBody) d.drawListBody(order);   // whole-body hatch (e.g. a DataTable) -- owns its own clipper
            else if (list && d.drawListRow)
            {
                const float rowH = d.listRowH * scale;
                ImGuiListClipper clip; clip.Begin((int)order.size(), rowH);
                while (clip.Step())
                    for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                    {
                        ImGui::PushID(i);
                        const RowHotspot row = Row("##growb", i, rowH, 0.f);
                        d.drawListRow(order[i], row);
                        ImGui::PopID();
                    }
            }
            else if (d.drawGridCell)
            {
                IconGrid((int)order.size(), d.gridCell * scale, d.gridGap,
                         [&](int idx, ImVec2 cmin, float csz) { ImGui::PushID(idx); d.drawGridCell(order[idx], cmin, csz); ImGui::PopID(); });
            }
        }
        ImGui::EndChild();
    }

    // ------------------------------------------------------------------------------------------------------
    // ChecklistBrowser: a sibling of the gallery shell for completion CHECKLISTS (owned/locked collections).
    // Shares the frame helpers (toolbar + header + progress + order cache) and draws a "checked-off" body
    // CENTRALLY (the owned dot + status + name/(2-line sub)); the caller supplies rowData + optional per-row
    // interaction (onRow: tooltip / click / copy). No grid / sort / rail.
    // ------------------------------------------------------------------------------------------------------
    void DrawChecklistBrowser(const ChecklistBrowser& d)
    {
        DrawToolbar({ d.search, d.searchHint, d.searchBuf, d.searchBufSize, d.filters, d.filterCount,
                      nullptr, 0, nullptr, nullptr, false, nullptr, d.settingsDirty });
        DrawHeaderProgress(d.title, d.have, d.total, d.tallyVerb, d.rightStatus, d.headerFontSize, d.showProgress, d.drawProgress);
        const std::vector<int>& order = Order(d.id, d.orderToken, d.rebuildOrder);

        ImGui::BeginChild((std::string("##clbody_") + (d.id ? d.id : "")).c_str(), ImVec2(0.f, 0.f), false);
        if (order.empty()) { EmptyState(d.emptyText); ImGui::EndChild(); return; }

        const float scale = TextScale();
        const float rowH = (d.twoLine ? d.rowHTwoLine : d.rowH) * scale;
        const float availW = ImGui::GetContentRegionAvail().x;
        const float dot = 10.f, statusW = 110.f * scale, textX = 12.f + dot * 2.f + 12.f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiListClipper clip; clip.Begin((int)order.size(), rowH);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
            {
                const int idx = order[i];
                const ChecklistBrowser::RowData rd = d.rowData ? d.rowData(idx) : ChecklistBrowser::RowData{};
                ImGui::PushID(i);
                const RowHotspot row = Row("##clrow", i, rowH, availW);
                const ImVec2 p = row.min;
                const ImVec2 dc(p.x + 12.f + dot, p.y + rowH * 0.5f);
                if (rd.owned) dl->AddCircleFilled(dc, dot, kGold);
                else          dl->AddCircle(dc, dot, IM_COL32(110, 110, 110, 200), 0, 1.4f);
                RowLabel(dl, row, row.width - statusW, 8.f, rd.owned ? d.statusOwned : d.statusLocked,
                         HAlign::Right, VAlign::Middle, rd.owned ? kGold : kTextDim, false, nullptr, 16.f * scale);
                if (d.twoLine && rd.sub && *rd.sub)
                {
                    const float left = p.x + textX, right = p.x + row.width - (statusW + 12.f);
                    const float nameH = 22.f * scale, subH = 18.f * scale, top = p.y + (rowH - nameH - subH) * 0.5f;
                    LabelDL(dl, ImVec2(left, top), ImVec2(right, top + nameH), rd.name ? rd.name : "",
                            HAlign::Left, VAlign::Middle, rd.owned ? IM_COL32(232, 226, 208, 255) : kTextDim, false, nullptr, 18.f * scale);
                    LabelDL(dl, ImVec2(left, top + nameH), ImVec2(right, top + nameH + subH), rd.sub,
                            HAlign::Left, VAlign::Middle, kTextDim, false, nullptr, 16.f * scale);
                }
                else
                {
                    std::string nm = rd.name ? rd.name : "";
                    if (rd.sub && *rd.sub) { nm += "    -    "; nm += rd.sub; }
                    RowLabel(dl, row, textX, statusW + 12.f, nm.c_str(), HAlign::Left, VAlign::Middle,
                             rd.owned ? IM_COL32(228, 222, 204, 255) : kTextDim, false, nullptr, 16.f * scale);
                }
                if (d.onRow) d.onRow(idx, row);
                ImGui::PopID();
            }
        ImGui::EndChild();
    }
}
