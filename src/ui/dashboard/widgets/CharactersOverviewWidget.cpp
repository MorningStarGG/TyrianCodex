#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "WidgetUtil.h"        // DashUtil::Narrow (compact-layout breakpoint)
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <algorithm>
#include <string>

// All characters at a glance: level + profession + race (/v2/characters?ids=all).
void DashW::CharactersOverview(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Characters, "characters")) return;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveChars || m.characters.empty()) { Gw2Ui::Label("Loading characters...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    const ImU32 nameCol = IM_COL32(232, 226, 206, 255), metaCol = IM_COL32(180, 172, 150, 255);
    const float sc = Gw2Ui::TextScale();
    for (const AccountData::AcctChar& c : m.characters)
    {
        char meta[64]; std::snprintf(meta, sizeof(meta), "Lv %d  %s", c.level, c.profession.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (DashUtil::Narrow(w))
        {
            // compact (half-width / narrow column): name over meta, so neither is crushed at ~136 px
            ImGui::Dummy(ImVec2(w, 31.f * sc));
            Gw2Ui::LabelDL(dl, ImVec2(p.x, p.y),             ImVec2(p.x + w, p.y + 17.f * sc), c.name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, nameCol, false, nullptr, 14.f);
            Gw2Ui::LabelDL(dl, ImVec2(p.x, p.y + 16.f * sc), ImVec2(p.x + w, p.y + 31.f * sc), meta,           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, metaCol, false, nullptr, 14.f);
        }
        else
        {
            // wide: name + right-aligned meta on one line; clip the name to the MEASURED meta column (not a fixed 100)
            const float metaW = Gw2Ui::MeasureWidth(meta, 14.f) + 10.f * sc;
            ImGui::Dummy(ImVec2(w, 19.f * sc));
            const Gw2Ui::RowHotspot row{ p, w, 19.f * sc, false, false };
            Gw2Ui::RowLabel(dl, row, 0.f, metaW, c.name.c_str(), Gw2Ui::HAlign::Left,  Gw2Ui::VAlign::Middle, nameCol, false, nullptr, 14.f);
            Gw2Ui::RowLabel(dl, row, 0.f, 0.f, meta, Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, metaCol, false, nullptr, 14.f);
        }
    }
    DashApi::Freshness(m.charsAt);
}
