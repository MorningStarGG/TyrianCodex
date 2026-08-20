#include "Widgets.h"
#include "app/App.h"
#include "render/glyphs/Glyphs.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"      // TravelToLocation, SetClipboard, ViewerAlert
#include "ui/WaypointFavorites.h"
#include "ui/dashboard/widgets/WidgetUtil.h"   // DashUtil::PlayerContinent / ContinentDist (Nearest sort)
#include "ui/viewer/ViewerComponents.h"   // PaintTypeIconAt (the shared waypoint type icon, as in the result rows)
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    void ClippedLabel(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 color,
                      float fontSize, float weight = -1.f)
    {
        if (!dl || b.x <= a.x || b.y <= a.y) return;
        dl->PushClipRect(a, b, true);
        Gw2Ui::LabelDL(dl, a, b, text, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                       color, false, nullptr, fontSize, 0.f, weight);
        dl->PopClipRect();
    }

    // The DISPLAY order: indices into the stored favorites list. Sorting never touches the stored list, so
    // Manual always returns exactly what the player arranged. Rebuilt only when something it depends on
    // changes -- the widget redraws every frame and re-sorting a list per frame is the sort of churn this
    // codebase does not do.
    struct ViewCache
    {
        uint64_t ver = (uint64_t)-1;
        int sort = -1;
        bool group = false;
        bool hideLocked = false;
        int cont = -2;
        float px = 1e30f, py = 1e30f;   // player position the Nearest sort was built at
        // Unlocked-ness is PER CHARACTER, and the view now orders (and can filter) by it -- so the character
        // and the progress store are part of the key. Without them the list would keep an alt's locked/unlocked
        // layout after a character switch, and reaching a waypoint would not lift it out of the locked block.
        std::string chr = "\x01";       // sentinel: force the first build
        uint64_t progVer = (uint64_t)-1;
        std::vector<int> order;
        int hiddenCount = 0;            // filtered out as locked, for the empty/footer copy
    };
    ViewCache g_view;

    // Drag-reorder state. Index into the STORED list, not the view, so it stays valid regardless of sorting.
    int  g_dragFrom = -1;
    bool g_dragMoved = false;

    const std::vector<int>& BuildView(App& app, const std::vector<WaypointFavorites::Favorite>& favs)
    {
        const int sort = std::clamp(app.config.favWpSort, 0, 3);
        const bool group = app.config.favWpGroupByZone;
        const bool hideLocked = app.config.favWpHideLocked;
        const uint64_t ver = WaypointFavorites::Version();
        const std::string& chr = app.state.currentChar;
        const uint64_t progVer = app.progress.Version();

        float px = 0.f, py = 0.f;
        int cont = -1;
        const bool haveP = DashUtil::PlayerContinent(app.state.zone, px, py);
        if (haveP) cont = app.state.zone.ContinentId;

        // Nearest re-sorts as you travel, but only on a COARSE step -- re-sorting on every centimetre would
        // make the list jitter under the cursor while you walk.
        const bool moved = sort == 3 && (std::fabs(px - g_view.px) > 250.f || std::fabs(py - g_view.py) > 250.f ||
                                         cont != g_view.cont);
        if (g_view.ver == ver && g_view.sort == sort && g_view.group == group && g_view.hideLocked == hideLocked &&
            g_view.chr == chr && g_view.progVer == progVer && !moved)
            return g_view.order;

        g_view.ver = ver; g_view.sort = sort; g_view.group = group; g_view.hideLocked = hideLocked;
        g_view.chr = chr; g_view.progVer = progVer;
        g_view.px = px; g_view.py = py; g_view.cont = cont;

        const auto unlocked = [&](int i) {
            return !favs[i].stepId.empty() && app.progress.IsConfirmed(chr, favs[i].stepId);
        };

        std::vector<int>& out = g_view.order;
        out.clear();
        out.reserve(favs.size());
        g_view.hiddenCount = 0;
        for (int i = 0; i < (int)favs.size(); ++i)
        {
            if (hideLocked && !unlocked(i)) { ++g_view.hiddenCount; continue; }
            out.push_back(i);
        }

        if (sort == 1)   // A-Z
            std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return favs[a].name < favs[b].name; });
        else if (sort == 2)   // Zone, then name inside it
            std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
                if (favs[a].zoneName != favs[b].zoneName) return favs[a].zoneName < favs[b].zoneName;
                return favs[a].name < favs[b].name;
            });
        else if (sort == 3 && haveP)   // Nearest: same-continent entries first, by distance
            std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
                const bool ca = favs[a].continentId == cont, cb = favs[b].continentId == cont;
                if (ca != cb) return ca > cb;
                if (!ca) return false;   // both off-continent -> keep manual order between them
                return DashUtil::ContinentDist(px, py, favs[a].cx, favs[a].cy) <
                       DashUtil::ContinentDist(px, py, favs[b].cx, favs[b].cy);
            });

        // Waypoints this character cannot use sink to the bottom, whatever the sort -- the actionable ones rise
        // to the top while the rest stay visible as a to-unlock list. Applied AFTER the sort and stable, so the
        // chosen order still holds within each block. Manual is left alone: it is the order you arranged, and
        // silently rearranging it would make the arrows and drag lie about what they did.
        if (sort != 0)
            std::stable_partition(out.begin(), out.end(), [&](int i) { return unlocked(i); });

        if (group)
        {
            // Cluster by zone WITHOUT disturbing the chosen sort: groups appear in the order their first
            // member appears, and members keep their relative order inside the group.
            std::vector<std::string> seen;
            std::vector<int> grouped;
            grouped.reserve(out.size());
            for (int idx : out)
                if (std::find(seen.begin(), seen.end(), favs[idx].zoneName) == seen.end())
                    seen.push_back(favs[idx].zoneName);
            for (const std::string& z : seen)
                for (int idx : out)
                    if (favs[idx].zoneName == z) grouped.push_back(idx);
            out.swap(grouped);
        }
        return out;
    }
}

void DashW::FavoriteWaypoints(App& app, float w)
{
    const std::vector<WaypointFavorites::Favorite>& favs = WaypointFavorites::List();

    // Two-line note, shared by the "none at all" and "all of them filtered out" states. The second case MUST
    // explain itself: with hide-locked on, a fresh character filters everything and a bare empty list reads as
    // "my favorites are gone" rather than "you have not unlocked these yet".
    const auto drawNote = [&](const char* title, const char* hint) {
        const float wrapW = std::max(48.f, w);
        const float gap = 3.f * Gw2Ui::TextScale();
        const float titleH = std::max(16.f * Gw2Ui::TextScale(), Gw2Ui::MeasureWrappedHeight(title, 14.f, wrapW));
        const float hintH = std::max(15.f * Gw2Ui::TextScale(), Gw2Ui::MeasureWrappedHeight(hint, 14.f, wrapW));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, titleH + gap + hintH));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Gw2Ui::LabelDL(dl, p, ImVec2(p.x + w, p.y + titleH), title,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextDim,
                       false, nullptr, 14.f, wrapW, 1.0f);
        Gw2Ui::LabelDL(dl, ImVec2(p.x, p.y + titleH + gap), ImVec2(p.x + w, p.y + titleH + gap + hintH), hint,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(140, 132, 116, 255),
                       false, nullptr, 14.f, wrapW, 1.0f);
    };

    if (favs.empty())
    {
        drawNote("No favorite waypoints yet.",
                 "Use waypoint stars in Atlas or Search. They are shared by every character.");
        return;
    }

    const float sc = Gw2Ui::TextScale();
    const float rowH = 34.f * sc;
    int removeIdx = -1;

    const std::vector<int>& view = BuildView(app, favs);
    if (view.empty())
    {
        char hint[160];
        std::snprintf(hint, sizeof(hint),
                      "%d hidden because this character has not reached them. Turn off Options > Widgets > "
                      "Favorite Waypoints > Hide locked to see them.",
                      g_view.hiddenCount);
        drawNote("Nothing unlocked here yet.", hint);
        return;
    }
    // Dragging only means something when the list is showing the order it would change. Under a sort or a
    // grouping the visible order is a view, so "drop it here" has no stored position to map onto -- the
    // settings list keeps its arrows working in those modes.
    const bool canDrag = app.config.favWpSort == 0 && !app.config.favWpGroupByZone;
    if (!canDrag) { g_dragFrom = -1; g_dragMoved = false; }
    DashUtil::SetContentDrag(g_dragFrom >= 0);   // let the dashboard body auto-scroll for THIS drag too

    std::vector<ImVec2> rowSpan;      // (y0, y1) per drawn row, for the drop indicator
    rowSpan.reserve(view.size());
    std::string lastZone;

    for (int vi = 0; vi < (int)view.size(); ++vi)
    {
        const int i = view[vi];
        const WaypointFavorites::Favorite& f = favs[i];

        if (app.config.favWpGroupByZone && f.zoneName != lastZone)
        {
            lastZone = f.zoneName;
            Gw2Ui::Label(f.zoneName.empty() ? "Elsewhere" : f.zoneName.c_str(),
                         IM_COL32(186, 172, 138, 255), false, nullptr, 14.f * sc);
        }

        const bool unlocked = !f.stepId.empty() && app.progress.IsConfirmed(app.state.currentChar, f.stepId);
        ImGui::PushID(i);   // keyed on the STORED index so ids stay put when the view re-sorts
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##favrow", vi, rowH, w, true);
        if (canDrag && ImGui::IsItemActivated()) { g_dragFrom = i; g_dragMoved = false; }
        const ImVec2 p = row.min;
        rowSpan.push_back(ImVec2(p.y, p.y + rowH));
        const bool dragging = g_dragFrom >= 0 && g_dragMoved;
        const bool rowClick = row.clicked && !dragging;   // a drag must not also travel
        const bool rowHover = row.hovered;

        ImDrawList* dl = ImGui::GetWindowDrawList();

        const float starSize = 18.f * sc;
        const float copyW = 24.f * sc;
        const float rightPad = 4.f * sc;
        const float textPad = 3.f * sc;
        const float actionGap = 6.f * sc;
        const float starX = Gw2Ui::RowRight(row, starSize, starSize, rightPad).x;
        const float copyX = starX - actionGap - copyW;
        // Waypoint type icon on the left (same icon the Zone Waypoints / Search rows draw), then the text after it.
        const float isz = 18.f * sc;
        PaintTypeIconAt(dl, "waypoint", ImVec2(p.x + textPad, p.y + (rowH - isz) * 0.5f), isz, false);
        const float textL = p.x + textPad + isz + 6.f * sc;
        const float textR = std::max(textL, copyX - 8.f * sc);
        const float textW = std::max(0.f, textR - textL);
        const ImU32 nameCol = unlocked ? IM_COL32(232, 226, 206, 255) : Gw2Ui::kTextDim;
        const std::string rawName = f.name.empty() ? std::string("(waypoint)") : f.name;
        const std::string shownName = Gw2Ui::Ellipsize(rawName, 14.f, textW);
        const bool nameShortened = shownName != rawName;

        ClippedLabel(dl, ImVec2(textL, p.y + 1.f * sc), ImVec2(textR, p.y + 18.f * sc),
                     shownName.c_str(), nameCol, 14.f, 1.0f);
        ClippedLabel(dl, ImVec2(textL, p.y + 17.f * sc), ImVec2(textR, p.y + rowH),
                     unlocked ? f.zoneName.c_str() : "Locked for this character",
                     unlocked ? IM_COL32(165, 156, 132, 255) : IM_COL32(190, 126, 96, 255),
                     12.f, 1.0f);

        bool copyClick = false;
        bool copyHover = false;
        if (unlocked && !f.chatLink.empty())
        {
            const float hitH = std::min(rowH, 24.f * sc);
            ImGui::SetCursorScreenPos(ImVec2(copyX, p.y + (rowH - hitH) * 0.5f));
            copyClick = ImGui::InvisibleButton("##copy", ImVec2(copyW, hitH));
            copyHover = ImGui::IsItemHovered();
            Render::DrawGlyph(dl, ImVec2(copyX + copyW * 0.5f, p.y + rowH * 0.5f), 18.f * sc,
                              Render::Glyph::Copy, copyHover ? Gw2Ui::kGold : Gw2Ui::kTextDim,
                              Render::GlyphStyle{});
            if (copyHover) Gw2Ui::Tooltip("Copy waypoint link");
        }

        ImGui::SetCursorScreenPos(ImVec2(starX, p.y + (rowH - starSize) * 0.5f));
        const bool starClick = ImGui::InvisibleButton("##remove", ImVec2(starSize, starSize));
        const bool starHover = ImGui::IsItemHovered();
        Render::GlyphStyle starStyle;
        starStyle.filled = true;
        starStyle.shadow = false;
        Render::DrawGlyph(dl, ImVec2(starX + starSize * 0.5f, p.y + rowH * 0.5f), starSize,
                          Render::Glyph::Star, starHover ? Gw2Ui::kGold : IM_COL32(224, 184, 86, 245),
                          starStyle);
        if (starHover) Gw2Ui::Tooltip("Remove favorite");
        if (starClick) removeIdx = i;
        if (rowHover && nameShortened && !copyHover && !starHover)
            Gw2Ui::Tooltip(rawName.c_str());

        if (copyClick)
        {
            SetClipboard(f.chatLink);
            ViewerAlert("Copied waypoint link - paste/click to teleport.");
        }
        else if (starClick)
        {
            // Consumes the row click; removal happens after the loop so the current iteration remains valid.
        }
        else if (rowClick && unlocked)
        {
            TravelToLocation(app, "waypoint", f.mapId, f.cx, f.cy, f.name, f.chatLink);
        }
        else if (rowClick && !unlocked)
        {
            ViewerAlert("Waypoint is not unlocked for this character.");
        }

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
        ImGui::PopID();
    }

    // ---- drag-reorder: gold insertion line at the drop point, applied on release ----------------------
    if (g_dragFrom >= 0)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (!g_dragMoved && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.f * sc)) g_dragMoved = true;

        // Which slot is the cursor over? Above every row -> the top, below them all -> the end.
        int dropView = (int)view.size();
        for (int vi = 0; vi < (int)rowSpan.size(); ++vi)
        {
            const float mid = (rowSpan[vi].x + rowSpan[vi].y) * 0.5f;
            if (io.MousePos.y < mid) { dropView = vi; break; }
        }

        if (g_dragMoved && !rowSpan.empty())
        {
            const float lineY = dropView < (int)rowSpan.size() ? rowSpan[dropView].x : rowSpan.back().y;
            const ImVec2 a = ImGui::GetWindowPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(a.x, lineY - 1.5f), ImVec2(a.x + w, lineY + 1.5f),
                              Gw2Ui::Alpha(Gw2Ui::kGold, 240));
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (g_dragMoved)
            {
                // dropView counts slots BEFORE the removal, so a downward move overshoots by one once the
                // dragged entry is pulled out of the list.
                int to = dropView >= (int)view.size() ? (int)favs.size() - 1
                                                      : view[std::min(dropView, (int)view.size() - 1)];
                if (to > g_dragFrom) --to;
                WaypointFavorites::Move(g_dragFrom, std::max(0, to));
            }
            g_dragFrom = -1;
            g_dragMoved = false;
        }
    }

    // Say how many are being withheld. A filter that silently shortens the list is how "my favorites vanished"
    // reports start.
    if (g_view.hiddenCount > 0)
    {
        char note[96];
        std::snprintf(note, sizeof(note), "%d more not unlocked on this character", g_view.hiddenCount);
        Gw2Ui::Label(note, IM_COL32(140, 132, 116, 255), false, nullptr, 12.f * sc);
    }

    if (removeIdx >= 0)
    {
        const std::string id = favs[removeIdx].stepId;   // copy first: Remove mutates the favorites vector `favs` aliases (remove_if), so don't pass a reference into it
        WaypointFavorites::Remove(id);
        ViewerAlert("Removed favorite waypoint.");
    }
}
