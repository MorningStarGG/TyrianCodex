#include "Widgets.h"
#include "app/App.h"
#include "render/glyphs/Glyphs.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"      // TravelToLocation, SetClipboard, ViewerAlert
#include "ui/WaypointFavorites.h"
#include "ui/viewer/ViewerComponents.h"   // PaintTypeIconAt (the shared waypoint type icon, as in the result rows)
#include <imgui.h>
#include <algorithm>
#include <string>

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
}

void DashW::FavoriteWaypoints(App& app, float w)
{
    const std::vector<WaypointFavorites::Favorite>& favs = WaypointFavorites::List(app.state.currentChar);
    if (favs.empty())
    {
        const char* title = "No favorite waypoints for this character.";
        const char* hint = "Use waypoint stars in Atlas or Search.";
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
        return;
    }

    const float sc = Gw2Ui::TextScale();
    const float rowH = 34.f * sc;
    int removeIdx = -1;
    for (int i = 0; i < (int)favs.size(); ++i)
    {
        const WaypointFavorites::Favorite& f = favs[i];
        const bool unlocked = !f.stepId.empty() && app.progress.IsConfirmed(app.state.currentChar, f.stepId);
        ImGui::PushID(i);
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##favrow", i, rowH, w, true);
        const ImVec2 p = row.min;
        const bool rowClick = row.clicked;
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

    if (removeIdx >= 0)
    {
        const std::string id = favs[removeIdx].stepId;   // copy first: Remove mutates the favorites vector `favs` aliases (remove_if), so don't pass a reference into it
        WaypointFavorites::Remove(app.state.currentChar, id);
        ViewerAlert("Removed favorite waypoint.");
    }
}
