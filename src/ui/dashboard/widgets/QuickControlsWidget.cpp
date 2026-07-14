#include "Widgets.h"
#include "app/App.h"
#include "render/glyphs/Glyphs.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>
#include <array>

namespace
{
    struct Tile
    {
        const char *id;
        const char *label;
        const char *tooltip;
        bool *value;
        bool *visible;
        Render::Glyph glyph;
    };

    std::array<Tile, 8> Tiles(App &app)
    {
        return {{
            {"guide", "Guide", "Show or hide the guide panel and guidance UI.", &app.config.showGuide, &app.config.quickControlsTileGuide, Render::Glyph::Guide},
            {"arrow", "Arrow", "Show or hide the floating GPS arrow.", &app.config.showArrow, &app.config.quickControlsTileArrow, Render::Glyph::Navigate},
            {"trail", "Trail", "Show or hide the in-world route trail.", &app.config.showTrail, &app.config.quickControlsTileTrail, Render::Glyph::Trail},
            {"markers", "Markers", "Show or hide in-world objective markers.", &app.config.showMarkers, &app.config.quickControlsTileMarkers, Render::Glyph::Marker},
            {"mappins", "Map Pins", "Show or hide objective pins on the map.", &app.config.showMapPins, &app.config.quickControlsTileMapPins, Render::Glyph::MapPin},
            {"maptrail", "Map Trail", "Show or hide the route line on the map.", &app.config.showMapTrail, &app.config.quickControlsTileMapTrail, Render::Glyph::Map},
            {"minimap", "Minimap Trail", "Show or hide the route line on the minimap.", &app.config.showMiniMapTrail, &app.config.quickControlsTileMinimap, Render::Glyph::MiniMap},
            {"personal", "Personal Marker", "Show or hide the personal target marker.", &app.config.showPersonalMarker, &app.config.quickControlsTilePersonal, Render::Glyph::MapPin},
        }};
    }

    bool AnyTileVisible(const std::array<Tile, 8> &tiles)
    {
        for (const Tile &t : tiles)
            if (t.visible && *t.visible)
                return true;
        return false;
    }

    void EnsureVisibleTiles(App &app, const std::array<Tile, 8> &tiles)
    {
        if (AnyTileVisible(tiles))
            return;
        app.config.quickControlsTileGuide = true;
        app.config.quickControlsTileArrow = true;
        app.config.quickControlsTileTrail = true;
        app.config.quickControlsTileMarkers = true;
        app.settingsDirty = true;
    }

    int ColumnCount(float width)
    {
        if (width < 170.f * Gw2Ui::TextScale())
            return 1;
        if (width < 420.f * Gw2Ui::TextScale())
            return 2;
        return 4;
    }

    bool DrawTile(const Tile &t, float width, float height)
    {
        const bool on = t.value && *t.value;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(t.id, ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const ImVec2 b(p.x + width, p.y + height);

        const float sc = Gw2Ui::TextScale();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float rounding = 3.f * sc;

        const ImU32 bg = on
                             ? (held ? IM_COL32(88, 60, 20, 238) : (hovered ? IM_COL32(68, 49, 20, 232) : IM_COL32(36, 27, 12, 222)))
                             : (held ? IM_COL32(24, 23, 20, 218) : (hovered ? IM_COL32(20, 20, 18, 205) : IM_COL32(6, 7, 7, 190)));
        const ImU32 border = on
                                 ? (hovered ? IM_COL32(255, 205, 98, 255) : IM_COL32(206, 160, 66, 230))
                                 : (hovered ? IM_COL32(128, 118, 94, 170) : IM_COL32(74, 70, 60, 120));
        const ImU32 inner = on
                                ? (hovered ? IM_COL32(255, 230, 150, 34) : IM_COL32(255, 218, 128, 24))
                                : (hovered ? IM_COL32(210, 196, 160, 12) : IM_COL32(210, 196, 160, 5));
        const ImU32 iconCol = on
                                  ? (hovered ? IM_COL32(255, 232, 150, 255) : IM_COL32(250, 214, 118, 255))
                                  : (hovered ? IM_COL32(178, 170, 146, 238) : IM_COL32(104, 101, 91, 218));
        const ImU32 textCol = on
                                  ? IM_COL32(255, 236, 170, 255)
                                  : (hovered ? IM_COL32(186, 178, 154, 245) : IM_COL32(118, 113, 100, 225));

        dl->AddRectFilled(p, b, bg, rounding);
        if (on)
        {
            const float topH = std::max(10.f * sc, height * 0.52f);
            dl->AddRectFilledMultiColor(p, ImVec2(b.x, p.y + topH),
                                        IM_COL32(255, 214, 102, hovered ? 40 : 30),
                                        IM_COL32(255, 214, 102, hovered ? 40 : 30),
                                        IM_COL32(255, 214, 102, 0),
                                        IM_COL32(255, 214, 102, 0));
        }
        dl->AddRect(p, b, border, rounding, 0, on ? (hovered ? 1.8f * sc : 1.45f * sc) : 1.0f * sc);
        dl->AddRect(ImVec2(p.x + 1.f * sc, p.y + 1.f * sc), ImVec2(b.x - 1.f * sc, b.y - 1.f * sc),
                    inner, std::max(0.f, rounding - 1.f * sc), 0, 1.f * sc);

        if (hovered && t.tooltip && *t.tooltip)
            Gw2Ui::Tooltip(t.tooltip);

        const float iconSize = std::min(24.f * sc, height * 0.42f);
        const float labelFs = (width < 76.f * sc) ? 12.f : 13.f;
        Render::DrawGlyph(dl, ImVec2((p.x + b.x) * 0.5f, p.y + height * 0.36f),
                          iconSize, t.glyph, iconCol, Render::GlyphStyle{});
        Gw2Ui::LabelDL(dl, ImVec2(p.x + 4.f * sc, p.y + height * 0.58f),
                       ImVec2(b.x - 4.f * sc, b.y - 3.f * sc),
                       t.label, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       textCol, true, nullptr, labelFs, 0.f, on ? 1.0f : 0.8f);
        return clicked;
    }

    bool DrawConfigButton(float width)
    {
        const float sc = Gw2Ui::TextScale();
        const float sz = 20.f * sc;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 at(p.x + width - sz, p.y);
        ImGui::SetCursorScreenPos(at);
        const bool clicked = ImGui::InvisibleButton("##qcconfigbtn", ImVec2(sz, sz));
        const bool hov = ImGui::IsItemHovered();
        Render::DrawGlyph(ImGui::GetWindowDrawList(), ImVec2(at.x + sz * 0.5f, at.y + sz * 0.5f),
                          17.f * sc, Render::Glyph::Cog, hov ? Gw2Ui::kGold : Gw2Ui::kTextDim,
                          Render::GlyphStyle{});
        if (hov)
            Gw2Ui::Tooltip("Choose Quick Controls tiles");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + sz + 3.f * sc));
        return clicked;
    }

    bool DrawVisibilityRow(const Tile &t, float width)
    {
        const float sc = Gw2Ui::TextScale();
        const float h = 26.f * sc;
        const Gw2Ui::RowHotspot row = Gw2Ui::Row(t.id, -1, h, width);
        const ImVec2 p = row.min;
        const bool clicked = row.clicked;
        const bool hov = row.hovered;
        ImDrawList *dl = ImGui::GetWindowDrawList();

        const float box = 14.f * sc;
        const ImVec2 a(p.x + 6.f * sc, p.y + (h - box) * 0.5f);
        const ImVec2 b(a.x + box, a.y + box);
        const bool checked = t.visible && *t.visible;
        const ImU32 col = checked ? (hov ? IM_COL32(255, 226, 142, 255) : Gw2Ui::kGold)
                                  : (hov ? IM_COL32(170, 158, 130, 235) : IM_COL32(112, 106, 92, 210));
        dl->AddRect(a, b, col, 2.f * sc, 0, 1.2f * sc);
        if (checked)
        {
            dl->AddLine(ImVec2(a.x + 3.f * sc, a.y + 7.f * sc), ImVec2(a.x + 6.f * sc, a.y + 10.f * sc), col, 1.6f * sc);
            dl->AddLine(ImVec2(a.x + 6.f * sc, a.y + 10.f * sc), ImVec2(a.x + 11.f * sc, a.y + 4.f * sc), col, 1.6f * sc);
        }
        Gw2Ui::RowLabel(dl, row, (b.x - p.x) + 8.f * sc, 6.f * sc, t.label, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        IM_COL32(218, 210, 190, 255), false, nullptr, 14.f);
        return clicked;
    }
}

void DashW::QuickControls(App &app, float w)
{
    std::array<Tile, 8> tiles = Tiles(app);
    EnsureVisibleTiles(app, tiles);

    if (DrawConfigButton(w))
        ImGui::OpenPopup("##quickControlsConfig");

    if (ImGui::BeginPopup("##quickControlsConfig"))
    {
        Gw2Ui::Label("Show tiles", Gw2Ui::kGold, false, nullptr, 16.f, 1.0f);
        ImGui::Dummy(ImVec2(0.f, 4.f * Gw2Ui::TextScale()));
        const float rowW = std::max(180.f, ImGui::GetContentRegionAvail().x);
        bool changed = false;
        for (const Tile &t : tiles)
        {
            if (DrawVisibilityRow(t, rowW) && t.visible)
            {
                *t.visible = !*t.visible;
                changed = true;
            }
        }
        if (changed)
        {
            EnsureVisibleTiles(app, tiles);
            app.settingsDirty = true;
        }
        ImGui::EndPopup();
    }

    tiles = Tiles(app);
    EnsureVisibleTiles(app, tiles);

    const float sc = Gw2Ui::TextScale();
    const float gap = 6.f * sc;
    const int cols = ColumnCount(w);
    const float tileW = std::max(42.f * sc, (w - gap * (float)(cols - 1)) / (float)cols);
    const float tileH = 58.f * sc;

    int visibleIndex = 0;
    for (const Tile &t : tiles)
    {
        if (!t.visible || !*t.visible)
            continue;
        ImGui::PushID(t.id);
        if (DrawTile(t, tileW, tileH) && t.value)
        {
            *t.value = !*t.value;
            app.settingsDirty = true;
        }
        ImGui::PopID();

        ++visibleIndex;
        if ((visibleIndex % cols) != 0)
            ImGui::SameLine(0.f, gap);
        else
            ImGui::Dummy(ImVec2(0.f, 2.f * sc));
    }
}
