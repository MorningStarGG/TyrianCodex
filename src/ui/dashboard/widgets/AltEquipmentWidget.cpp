#include "Widgets.h"
#include "ApiWidgetUtil.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/items/ItemRender.h"      // ItemUI::DrawItemCell (hoverable cell + game tooltip)
#include "ui/tabs/InventorySection.h"     // SetEquipmentChar
#include "ui/tabs/ItemsTab.h"         // OpenItemsTab
#include <imgui.h>
#include <algorithm>
#include <string>

// Any character's equipped gear without logging in (/v2/characters/:id/equipment): a compact grid of hoverable
// item cells (full game-style tooltip + right-click copy link), plus a link to the full Equipment hero panel.
namespace
{
    std::string g_sel;
    std::string ItemName(const AccountData::Model& m, int id)
    { auto it = m.itemMeta.find(id); if (it != m.itemMeta.end() && !it->second.name.empty()) return it->second.name; char b[32]; std::snprintf(b, sizeof(b), "Item %d", id); return b; }
    const AccountData::ItemMeta* Meta(const AccountData::Model& m, int id)
    { auto it = m.itemMeta.find(id); return it == m.itemMeta.end() ? nullptr : &it->second; }
    ItemUI::NameFn NameFnFor(const AccountData::Model& m) { return [&m](int id) { return ItemName(m, id); }; }
}

void DashW::AltEquipment(App& app, float w)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Characters, "characters")) return;
    const std::string name = DashApi::CharacterPicker("##altequip", g_sel, w);
    if (name.empty()) return;
    ImGui::Spacing();

    const AccountData::Model& m = AccountData::Get();
    auto it = m.equip.find(name);
    if (it == m.equip.end() || !it->second.have)
    { Gw2Ui::Label("Loading equipment...", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    const float ui = Gw2Ui::GlobalScale();
    const float cell = 34.f * ui, gap = 4.f * ui;
    const int cols = std::max(1, (int)((w + gap) / (cell + gap)));
    int n = 0;
    for (const Api::V2::ItemSlot& s : it->second.pieces)
    {
        if (s.id <= 0) continue;
        ImGui::PushID(n);
        if (n % cols != 0) ImGui::SameLine(0.f, gap);
        ItemUI::DrawItemCell(s.id, Meta(m, s.id), ItemUI::Instance{ 0, s.slot, &s, nullptr }, NameFnFor(m), cell);
        ImGui::PopID();
        ++n;
    }
    if (n == 0) { Gw2Ui::Label("No equipped items.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

    ImGui::Spacing();
    if (Gw2Ui::ActionButtonPx("Open Equipment", w, Gw2Ui::Scaled(24.f), Gw2Ui::ActionButtonVariant::Primary, "Open this character's equipment in the Items tab"))
    { SetEquipmentChar(name.c_str()); OpenItemsTab(app, 2); }
}
