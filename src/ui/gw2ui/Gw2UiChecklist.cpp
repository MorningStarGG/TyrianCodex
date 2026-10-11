#include "ui/Gw2Ui.h"
#include "render/glyphs/Glyphs.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

bool Gw2Ui::MatchesTextFilter(const std::string& text, const char* search)
{
    if (!search || !search[0]) return true;
    for (size_t offset = 0; offset < text.size(); ++offset)
    {
        size_t index = 0;
        while (search[index] && offset + index < text.size() &&
               std::tolower((unsigned char)search[index]) == std::tolower((unsigned char)text[offset + index])) ++index;
        if (!search[index]) return true;
    }
    return false;
}

void Gw2Ui::VisitChecklistItems(const ChecklistNode& node, const std::function<void(const ChecklistNode&)>& visit)
{
    if (node.Leaf()) visit(node);
    else for (const auto& child : node.Children) VisitChecklistItems(child, visit);
}

void Gw2Ui::UpdateChecklist(std::vector<ChecklistNode>& roots, const ChecklistState& state,
                           const std::function<bool(const std::string&)>& marked)
{
    std::function<void(ChecklistNode&)> update = [&](ChecklistNode& node) {
        if (node.Leaf())
        {
            node.Total = 1;
            node.Done = marked(node.ItemId) ? 1 : 0;
            node.Visible = MatchesTextFilter(node.Label + " " + node.Context + " " + node.SearchText, state.Search) &&
                (state.Filter == ChecklistFilter::All || (state.Filter == ChecklistFilter::Marked ? node.Done != 0 : node.Done == 0));
            return;
        }
        node.Total = node.Done = 0;
        node.Visible = false;
        for (auto& child : node.Children)
        {
            update(child);
            node.Total += child.Total;
            node.Done += child.Done;
            node.Visible = node.Visible || child.Visible;
        }
    };
    for (auto& node : roots) update(node);
}

bool Gw2Ui::ExpandChecklistTo(std::vector<ChecklistNode>& roots, ChecklistState& state, const std::string& id)
{
    std::function<bool(const ChecklistNode&)> find = [&](const ChecklistNode& node) {
        if (node.Id == id) { state.Expanded.insert(node.Id); return true; }
        for (const auto& child : node.Children)
            if (find(child)) { state.Expanded.insert(node.Id); return true; }
        return false;
    };
    for (const auto& node : roots)
        if (find(node)) { state.ScrollTo = id; return true; }
    return false;
}

Gw2Ui::ChecklistItemResult Gw2Ui::ChecklistItem(const ChecklistItemSpec& spec, int depth, int rowIndex)
{
    const float scale = TextScale(), checkbox = 30.f * scale, icon = spec.DrawIcon ? 28.f * scale : 0.f;
    const float width = std::min(ContentWidth(), ImGui::GetContentRegionAvail().x);
    const float left = (14.f + depth * 14.f) * scale + checkbox + 8.f * scale;
    const float bodyWidth = std::max(1.f, width - left - (icon ? icon + 8.f * scale : 0.f) - spec.TrailingWidth - 12.f * scale);
    const bool pill = spec.Detail && spec.Detail[0] && spec.DetailStyle == ChecklistDetailStyle::Pill;
    // A badge must survive narrow layouts. Shorten only text too long to fit the entire row;
    // otherwise let the label use the full body width and put the badge on its own line.
    const auto detail = pill ? Ellipsize(spec.Detail, spec.DetailFontSize,
        std::max(0.f, width - spec.TrailingWidth - 40.f * scale)) : std::string();
    const float detailWidth = pill ? PillWidth(detail.c_str(), spec.DetailFontSize) :
        (spec.Detail && spec.Detail[0] && width > 360.f * scale ? 100.f * scale : 0.f);
    const bool detailBelow = pill && bodyWidth < detailWidth + 8.f * scale + 80.f * scale;
    const float textWidth = std::max(1.f, bodyWidth - (detailBelow ? 0.f : detailWidth + (pill ? 8.f * scale : 0.f)));
    const float titleHeight = MeasureWrappedHeight(spec.Label, spec.FontSize, textWidth);
    const float subtitleHeight = spec.Subtitle && spec.Subtitle[0] ? MeasureWrappedHeight(spec.Subtitle, spec.SubtitleFontSize, textWidth) + 2.f * scale : 0.f;
    const float detailHeight = detailWidth > 0.f ? (pill ? PillHeight(spec.DetailFontSize) :
        MeasureWrappedHeight(spec.Detail, spec.DetailFontSize, detailWidth)) : 0.f;
    const float bodyHeight = std::max(30.f * scale, titleHeight + subtitleHeight);
    const float height = (detailBelow ? bodyHeight + 4.f * scale + detailHeight : std::max(bodyHeight, detailHeight)) + 8.f * scale;
    ChecklistItemResult result;
    if (!ImGui::IsRectVisible(ImVec2(width, height)))
    {
        ImGui::Dummy(ImVec2(width, height));
        return result;
    }
    result.Row = TreeRow(spec.Id, depth, height, -1, spec.Selected, rowIndex);
    const auto& row = result.Row;
    const float bodyCenterY = detailBelow ? row.rowMin.y + 4.f * scale + bodyHeight * 0.5f : row.rowMin.y + height * 0.5f;
    const auto mouse = ImGui::GetIO().MousePos;
    const bool overCheckbox = mouse.x >= row.contentMin.x && mouse.x < row.contentMin.x + checkbox &&
        mouse.y >= bodyCenterY - checkbox * 0.5f && mouse.y < bodyCenterY + checkbox * 0.5f;
    PaintCheckbox(ImVec2(row.contentMin.x, bodyCenterY - checkbox * 0.5f), checkbox,
                  spec.Marked ? 1 : 0, row.hovered && overCheckbox && spec.CanMark);
    const bool clickedCheckbox = row.clicked && overCheckbox;
    result.CheckboxClicked = clickedCheckbox && spec.CanMark;
    result.BodyClicked = row.clicked && !clickedCheckbox;
    if (row.hovered && overCheckbox && !spec.CanMark) Tooltip(spec.DisabledTip);
    float x = row.contentMin.x + checkbox + 8.f * scale;
    if (spec.DrawIcon)
    {
        spec.DrawIcon(ImGui::GetWindowDrawList(), ImVec2(x + icon * 0.5f, bodyCenterY), icon);
        x += icon + 8.f * scale;
    }
    const float textTop = bodyCenterY - (titleHeight + subtitleHeight) * 0.5f;
    LabelIn(ImVec2(x, textTop), ImVec2(x + textWidth, textTop + titleHeight), spec.Label, HAlign::Left, VAlign::Top,
            spec.Marked ? kTextDim : spec.Color, true, nullptr, spec.FontSize, textWidth);
    if (subtitleHeight > 0.f)
        LabelIn(ImVec2(x, textTop + titleHeight + 2.f * scale), ImVec2(x + textWidth, textTop + titleHeight + subtitleHeight), spec.Subtitle,
                HAlign::Left, VAlign::Top, spec.Marked ? kTextDim : kTextSub, false, nullptr, spec.SubtitleFontSize, textWidth);
    if (pill && detailWidth > 0.f)
    {
        const float y = detailBelow ? row.rowMax.y - 4.f * scale - detailHeight : row.rowMin.y + (height - detailHeight) * 0.5f;
        PillAt(ImGui::GetWindowDrawList(), ImVec2(row.rowMax.x - spec.TrailingWidth - detailWidth - 8.f * scale, y),
               detail.c_str(), spec.DetailFontSize, spec.Marked ? Alpha(kTextDim, 100) : spec.DetailBorder,
               spec.Marked ? kTextDim : kPillText);
    }
    else if (detailWidth > 0.f)
        LabelIn(ImVec2(row.rowMax.x - spec.TrailingWidth - detailWidth - 8.f * scale, row.rowMin.y),
                ImVec2(row.rowMax.x - spec.TrailingWidth - 8.f * scale, row.rowMax.y), spec.Detail,
                HAlign::Right, VAlign::Middle, kTextSub, false, nullptr, spec.DetailFontSize, detailWidth);
    return result;
}

int Gw2Ui::DrawChecklist(const std::vector<ChecklistNode>& roots, ChecklistState& state, const ChecklistCallbacks& callbacks)
{
    int rowIndex = 0;
    std::function<void(const ChecklistNode&, int)> draw = [&](const ChecklistNode& node, int depth) {
        if (!node.Visible) return;
        if (node.Leaf())
        {
            callbacks.DrawLeaf(node, depth, rowIndex++);
            if (state.ScrollTo == node.Id) { ImGui::SetScrollHereY(0.2f); state.ScrollTo.clear(); }
            return;
        }
        const bool open = state.Expanded.count(node.Id) != 0;
        const float scale = TextScale(), checkbox = 30.f * scale;
        char counts[40]; std::snprintf(counts, sizeof(counts), "(%d/%d)", node.Done, node.Total);
        const float countWidth = MeasureWidth(counts, 20.f) + 8.f * scale;
        const float width = std::min(ContentWidth(), ImGui::GetContentRegionAvail().x);
        const float textWidth = std::max(30.f * scale, width - (28.f + depth * 14.f) * scale - checkbox - 24.f * scale - countWidth);
        const float fontSize = node.Bold ? 24.f : 20.f;
        const float height = std::max(node.Height * scale, MeasureWrappedHeight(node.Label.c_str(), fontSize, textWidth) + 8.f * scale);
        if (!ImGui::IsRectVisible(ImVec2(width, height))) { ImGui::Dummy(ImVec2(width, height)); ++rowIndex; }
        else
        {
            const int checked = node.Total && node.Done == node.Total ? 1 : node.Done ? 2 : 0;
            const auto row = TreeRow(node.Id.c_str(), depth, height, open ? 1 : 0, false, rowIndex++);
            PaintCheckbox(ImVec2(row.contentMin.x, row.rowMin.y + (height - checkbox) * 0.5f), checkbox, checked);
            LabelIn(ImVec2(row.contentMin.x + checkbox + 8.f * scale, row.rowMin.y),
                    ImVec2(row.rowMax.x - countWidth - 8.f * scale, row.rowMax.y), node.Label.c_str(),
                    HAlign::Left, VAlign::Middle, IM_COL32_WHITE, true, nullptr, fontSize, textWidth);
            LabelIn(ImVec2(row.rowMax.x - countWidth - 8.f * scale, row.rowMin.y), ImVec2(row.rowMax.x - 8.f * scale, row.rowMax.y),
                    counts, HAlign::Right, VAlign::Middle, kTextSub, false, nullptr, 20.f);
            if (row.clicked)
            {
                const float cbX = row.contentMin.x - row.rowMin.x;
                if (row.clickX >= cbX && row.clickX < cbX + checkbox)
                { if (callbacks.CanMark && callbacks.MarkGroup) callbacks.MarkGroup(node, checked != 1); }
                else if (open) state.Expanded.erase(node.Id);
                else state.Expanded.insert(node.Id);
            }
            if (row.hovered && callbacks.HoverGroup) callbacks.HoverGroup(node);
        }
        if (state.ScrollTo == node.Id) { ImGui::SetScrollHereY(0.2f); state.ScrollTo.clear(); }
        if (open) for (const auto& child : node.Children) draw(child, depth + 1);
    };
    if (state.Search[0])
    {
        for (const auto& root : roots) VisitChecklistItems(root, [&](const auto& leaf) {
            if (leaf.Visible) callbacks.DrawLeaf(leaf, 0, rowIndex++);
        });
    }
    else for (const auto& root : roots) draw(root, 0);
    return rowIndex;
}

bool Gw2Ui::ShowCompletedChip(const char* id, bool& show, ImVec2 size, const char* tooltip)
{
    const auto icon = [show](ImDrawList* list, ImVec2 center, float size) {
        Render::DrawGlyph(list, center, size * 0.9f, Render::Glyph::Check, show ? kPillGoodText : kTextDim);
    };
    if (!SelectableChip(id, show, size, nullptr, 16.f, tooltip, icon)) return false;
    show = !show;
    return true;
}
