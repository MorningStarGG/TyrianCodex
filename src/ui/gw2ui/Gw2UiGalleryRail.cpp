#include "ui/gw2ui/Gw2UiGallery.h"
#include "render/glyphs/Glyphs.h"   // Render::DrawGlyph + Render::Glyph (the expand caret)

#include <imgui.h>
#include <cstdio>

// The shared gallery RAIL: one widget that replaces the per-scope hand-rolled rail-row drawers
// (CatRailRow / LegRailRow / ArmRailRow / RailRow / DrawRail). Same look -- gold "you-are-here" stripe + an
// expand caret on parent rows + a right-aligned "N / M" count badge + the label -- composed from Gw2Ui::Row /
// RowLabel + Render::DrawGlyph, driven by the GalleryRailNode tree (flat / 2-level / 3-level).
namespace Gw2Ui
{
    namespace
    {
        bool SubtreeHasKey(const GalleryRailNode& n, const std::string& key)
        {
            if (n.key == key) return true;
            for (const GalleryRailNode& c : n.children)
                if (SubtreeHasKey(c, key)) return true;
            return false;
        }

        // One rail row. `reserveCaret` = the rail has expandable nodes, so leaf labels indent to the caret column
        // too (keeps every label aligned). Returns row.clicked.
        bool RailRow(const char* label, int have, int total, bool selected, int rowIndex, float w,
                     int level, bool hasChild, bool expanded, bool reserveCaret, const GalleryRailStyle& st)
        {
            const RowHotspot row = Row("##grow", rowIndex, st.rowH, w, false, selected);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (selected)
                dl->AddRectFilled(ImVec2(row.min.x + 2.f, row.min.y + 3.f),
                                  ImVec2(row.min.x + 5.f, row.min.y + st.rowH - 3.f), kGold, 1.f);
            const float lx = 12.f + level * st.indentPx;
            if (hasChild)
                Render::DrawGlyph(dl, ImVec2(row.min.x + lx + 5.f, row.min.y + st.rowH * 0.5f), 12.f,
                                  expanded ? Render::Glyph::CaretDown : Render::Glyph::CaretRight,
                                  selected ? kGold : kTextDim);
            float cw = 0.f;
            if (total >= 0)
            {
                char cb[24]; std::snprintf(cb, sizeof(cb), "%d / %d", have, total);
                cw = MeasureWidth(cb, st.badgeFont) + 10.f;
                RowLabel(dl, row, w - cw - 4.f, 6.f, cb, HAlign::Right, VAlign::Middle,
                         selected ? kGold : kTextDim, false, nullptr, st.badgeFont);
            }
            else if (have >= 0)   // single catalog count (no completion ratio) -- comma-grouped, e.g. "1,234"
            {
                std::string cb = std::to_string(have);
                for (int p = (int)cb.size() - 3; p > 0; p -= 3) cb.insert(p, ",");
                cw = MeasureWidth(cb.c_str(), st.badgeFont) + 10.f;
                RowLabel(dl, row, w - cw - 4.f, 6.f, cb.c_str(), HAlign::Right, VAlign::Middle,
                         selected ? kGold : kTextDim, false, nullptr, st.badgeFont);
            }
            RowLabel(dl, row, lx + (reserveCaret ? 18.f : 6.f), cw + 10.f, label, HAlign::Left, VAlign::Middle,
                     selected ? kGold : kTextSelected, false, nullptr, level ? st.labelChild : st.labelTop);
            return row.clicked;
        }

        bool DrawNodes(const std::vector<GalleryRailNode>& nodes, const std::string& parentKey, int level,
                       int& rowIdx, float w, bool reserveCaret, std::string* sel, const GalleryRailStyle& st)
        {
            bool changed = false;
            for (const GalleryRailNode& n : nodes)
            {
                const bool hasChild = !n.children.empty();
                const bool selected = (*sel == n.key);
                const bool expanded = hasChild && SubtreeHasKey(n, *sel);
                ImGui::PushID(n.key.empty() ? "__all" : n.key.c_str());
                if (RailRow(n.label.c_str(), n.have, n.total, selected, rowIdx++, w, level, hasChild, expanded,
                            reserveCaret, st))
                {
                    *sel = (selected && !hasChild && (level > 0 || st.collapseTopLeaf)) ? parentKey : n.key;   // re-click a selected leaf -> collapse to parent (top-level only when the style opts in)
                    changed = true;
                }
                if (expanded)
                    changed |= DrawNodes(n.children, n.key, level + 1, rowIdx, w, reserveCaret, sel, st);
                ImGui::PopID();
            }
            return changed;
        }
    }

    bool GalleryRail(const char* id, float width, float height,
                     const GalleryRailNode& root, std::string* selectedKey, const GalleryRailStyle& style)
    {
        if (!selectedKey) return false;
        // Reserve a caret column when ANY top-level node is expandable, so all labels align (a flat rail packs
        // tight instead). Nesting always implies a top-level parent, so the top-level scan is sufficient.
        bool reserveCaret = false;
        for (const GalleryRailNode& n : root.children)
            if (!n.children.empty()) { reserveCaret = true; break; }

        ImGui::BeginChild(id, ImVec2(width, height), false);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
        const float w = ImGui::GetContentRegionAvail().x;
        int rowIdx = 0;
        const bool changed = DrawNodes(root.children, std::string(), 0, rowIdx, w, reserveCaret, selectedKey, style);
        ImGui::PopStyleVar();
        ImGui::EndChild();
        return changed;
    }
}
