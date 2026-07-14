#include "ui/ZoneRow.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"          // OpenZoneTravelChoice
#include "render/glyphs/Glyphs.h"    // Render::DrawGlyph (central mapped check)
#include <imgui.h>
#include <algorithm>
#include <cstdio>

bool DrawZoneRow(App& app, const Zone* z, const ZoneRowStyle& style)
{
    ImGui::PushID((int)z->MapId);
    // Actual draw width (not Gw2Ui::ContentWidth(), which is card-aware -> returns the OUTER card width and pushes
    // the right-aligned pills past the scrollbar when this row is reused inside a dashboard widget's nested child).
    const float  rw = ImGui::GetContentRegionAvail().x;
    // Grow the row to fit the pills: PillHeight is font-derived, so with a large list font it can exceed a
    // fixed height and the centered pills would spill into the neighbouring rows.
    const float  rh = std::max(style.height, Gw2Ui::PillHeight(style.actionFs - 1.f) + 4.f);
    const bool   active    = (z->MapId == app.state.activeMapId);
    const bool   traveling = (app.travel.DestinationMapId() == z->MapId);
    const Gw2Ui::RowHotspot row = Gw2Ui::Row("##zrow", style.stripe, rh, rw, false, active || traveling);
    const ImVec2 rp = row.min;
    const bool clicked = row.clicked;
    const bool hovered = row.hovered;

    // Right-aligned pills, chained leftward: "Active" when this is the live travel destination, otherwise the
    // [ Lv x-y ][ Recommended ] pair (every row in the "head here next" list is a recommendation). The whole
    // row is the travel affordance -- no separate "Travel here" text.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pillFs = style.actionFs - 1.f;
    const float pillY  = rp.y + (rh - Gw2Ui::PillHeight(pillFs)) * 0.5f;
    float rx = rp.x + rw - 10.f;
    if (traveling)
    {
        const float pw = Gw2Ui::PillWidth("Active", pillFs);
        Gw2Ui::PillAt(dl, ImVec2(rx - pw, pillY), "Active", pillFs,
                      Gw2Ui::kPillGoodBorder, Gw2Ui::kPillGoodText, Gw2Ui::kPillGoodFill);
        rx -= pw + 8.f;
    }
    else
    {
        if (style.mapped)   // story view: a green check marks an already map-complete zone (kept, not hidden)
        {
            const float gsz = Gw2Ui::PillHeight(pillFs);
            Render::DrawGlyph(dl, ImVec2(rx - gsz * 0.5f, rp.y + rh * 0.5f), gsz,
                              Render::Glyph::Check, Gw2Ui::kPillGoodText, Render::GlyphStyle{});
            rx -= gsz + 8.f;
        }
        else if (style.showRecommended && rw >= 330.f)   // drop the redundant "Recommended" pill on a narrow row
        {                                                 // (every row in this list is recommended) -> room for the name
            const float recW = Gw2Ui::PillWidth("Recommended", pillFs);
            Gw2Ui::PillAt(dl, ImVec2(rx - recW, pillY), "Recommended", pillFs,
                          Gw2Ui::kPillGoodBorder, Gw2Ui::kPillGoodText, Gw2Ui::kPillGoodFill);
            rx -= recW + 6.f;
        }
        char lv[24]; std::snprintf(lv, sizeof(lv), "Lv %d-%d", z->MinLevel, z->MaxLevel);
        const float lvW = Gw2Ui::PillWidth(lv, pillFs);
        Gw2Ui::PillAt(dl, ImVec2(rx - lvW, pillY), lv, pillFs);
        rx -= lvW + 6.f;
    }

    // Name fills the space left of the pills. Auto-shrink the font if it would otherwise run under the pills
    // (half-width dashboard cards are narrow), same as the Atlas compact row -- no overlap, no mid-word clip.
    const float nameLeftOff  = 10.f;
    const float nameRightOff = std::max(nameLeftOff + 30.f, rx - rp.x - 4.f);
    const float nameAvail = nameRightOff - nameLeftOff;
    float nameFs = style.nameFs;
    const float measured = Gw2Ui::MeasureWidth(z->Name.c_str(), nameFs);
    if (measured > nameAvail && measured > 1.f) nameFs = std::max(10.f, nameFs * (nameAvail / measured));
    Gw2Ui::RowLabel(dl, row, nameLeftOff, std::max(0.f, rw - nameRightOff), z->Name.c_str(),
                    Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                    IM_COL32(235, 230, 215, 255), false, nullptr, nameFs);

    if (clicked)
    {
        if (style.travelViaCard) OpenZoneTravelChoice(app, z->MapId);   // viewer: Travel/Show target/Not now card
        else                     app.travel.StartTravel(z->MapId);
    }
    ImGui::PopID();
    return hovered;
}
