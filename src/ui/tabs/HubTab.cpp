#include "ui/tabs/HubTab.h"
#include "ui/SettingsWindow.h"   // OpenSettingsTab + SettingsTabId
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "util/Textures.h"       // Tex::GetAssetTex
#include "render/glyphs/Glyphs.h"// Render::DrawGlyph (Timers card uses the Clock glyph, not an asset icon)
#include "ui/wiki/WikiReader.h"
#include "Version.h"             // TC_VERSION_STRING
#include <imgui.h>
#include <algorithm>

namespace
{
    // icon = gw2dat asset id (0 -> use `glyph`); glyph = a Render::Glyph (-1 -> use the asset icon).
    enum class HubAction { Tab, Wiki };
    struct HubCard { HubAction action; SettingsTabId id; uint32_t icon; const char* name; const char* desc; int glyph = -1; const char* file = nullptr; };  // file (data/ texture) > icon > glyph
    // Icons mirror kSettingsTabs (gw2dat asset ids; Timers uses the Clock glyph).
    const HubCard kCards[] = {
        { HubAction::Tab,  SettingsTabAtlas,     528724, "Atlas",     "Search and travel to every waypoint, point of interest, vista, hero point, heart, and zone." },
        { HubAction::Tab,  SettingsTabItems,     157098, "Items",     "Browse the whole-game item catalog, your inventory, and a character's equipment." },
        { HubAction::Tab,  SettingsTabTradingPost, 0,    "Trading Post", "Live prices, flip margins, and the order book for any item; your delivery box, orders, and profit/loss.", (int)Render::Glyph::Coins },
        { HubAction::Wiki, SettingsTabHub,       0,      "Wiki",      "A native Guild Wars 2 Wiki reader with search, bookmarks, history, tables, links, and cached pages.", -1, "data\\textures\\ui\\gw2.png" },
        { HubAction::Tab,  SettingsTabJournal,   102369, "Journal",   "The Story Journal -- every chapter and season, plus your personal story." },
        { HubAction::Tab,  SettingsTabChecklist, 157353, "Checklist", "The per-zone objective checklist with live completion tracking." },
        { HubAction::Tab,  SettingsTabDungeons,  156626, "Instances", "Dungeons (spot-to-spot guidance), story walkthroughs, fractals, raids (weekly clears), strikes, and home -- browse, track + travel." },
        { HubAction::Tab,  SettingsTabCollections, 0,    "Collections", "Account unlocks -- your Wardrobe (skins + dyes), Homestead decorations, and every fishing collection.", (int)Render::Glyph::Star },
        { HubAction::Tab,  SettingsTabTimers,    0,      "Timers",    "Upcoming world bosses and meta-events across Tyria -- a list and a timeline, with click-to-travel.", (int)Render::Glyph::Clock },
        { HubAction::Tab,  SettingsTabSessions,  0,      "Sessions",  "Track your earnings per play session -- per-currency gains, a history log, and an earnings graph.", (int)Render::Glyph::Chart },
        { HubAction::Tab,  SettingsTabOptions,   156736, "Options",   "Every setting: arrow, panel, routing, trails, markers, dashboard, HUD, info panel, keybinds, API." },
    };
}

void DrawHubContent(App& app)
{
    ImDrawList* dl    = ImGui::GetWindowDrawList();
    const float fullW = ImGui::GetContentRegionAvail().x;

    // --- Welcome header ---
    Gw2Ui::Label("Tyrian Codex", Gw2Ui::kGold, true, nullptr, 30.f);
    Gw2Ui::Label("Your Guild Wars 2 companion  -  v" TC_VERSION_STRING, Gw2Ui::kTextDim, false, nullptr, 16.f);
    ImGui::Spacing();
    {
        const char* intro = "Pick a section to jump in -- everything here is also on the tab rail to the left.";
        const float h = Gw2Ui::MeasureWrappedHeight(intro, 16.f, fullW);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(fullW, h));
        Gw2Ui::LabelIn(p, ImVec2(p.x + fullW, p.y + h), intro, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       IM_COL32(192, 180, 152, 255), false, nullptr, 16.f, fullW);
    }
    ImGui::Spacing(); ImGui::Spacing();

    // --- Clickable section cards (1 or 2 columns, depending on width) ---
    const float gap   = 12.f;
    const int   cols  = (fullW > 560.f) ? 2 : 1;
    const float cardW = (fullW - gap * (cols - 1)) / (float)cols;
    const float cardH = 86.f;
    const int   n     = (int)(sizeof(kCards) / sizeof(kCards[0]));
    for (int i = 0; i < n; ++i)
    {
        if (i % cols != 0) ImGui::SameLine(0.f, gap);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("##hubcard", ImVec2(cardW, cardH));
        const bool hov     = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        const ImVec2 p1(p.x + cardW, p.y + cardH);
        dl->AddRectFilled(p, p1, hov ? IM_COL32(42, 39, 30, 240) : IM_COL32(20, 21, 19, 220), 4.f);
        dl->AddRect(p, p1, hov ? IM_COL32(216, 178, 96, 230) : IM_COL32(136, 118, 82, 140), 4.f, 0, 1.5f);

        const float  isz = 52.f;
        const ImVec2 ip(p.x + 16.f, p.y + (cardH - isz) * 0.5f);
        if (kCards[i].file)
        {
            if (void* tx = Tex::GetBundledTexture(kCards[i].file))
                dl->AddImage((ImTextureID)tx, ip, ImVec2(ip.x + isz, ip.y + isz));
        }
        else if (kCards[i].icon != 0)
        {
            if (void* tx = Tex::GetTextureFromAssetId(kCards[i].icon))
                dl->AddImage((ImTextureID)tx, ip, ImVec2(ip.x + isz, ip.y + isz));
        }
        else if (kCards[i].glyph >= 0)
            Render::DrawGlyph(dl, ImVec2(ip.x + isz * 0.5f, ip.y + isz * 0.5f), isz * 0.72f,
                              (Render::Glyph)kCards[i].glyph, Gw2Ui::kGold);

        const float tx = ip.x + isz + 14.f;
        const float tw = (p.x + cardW - 14.f) - tx;
        Gw2Ui::LabelIn(ImVec2(tx, p.y + 12.f), ImVec2(tx + tw, p.y + 36.f), kCards[i].name,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, 20.f);
        Gw2Ui::LabelIn(ImVec2(tx, p.y + 37.f), ImVec2(tx + tw, p.y + cardH - 6.f), kCards[i].desc,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(206, 196, 172, 255), false, nullptr, 16.f, tw);
        ImGui::PopID();

        if (clicked)
        {
            if (kCards[i].action == HubAction::Wiki) Wiki::OpenReader(app);
            else OpenSettingsTab(app, kCards[i].id);
        }
    }
}
