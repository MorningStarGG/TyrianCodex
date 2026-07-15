#include "Hud.h"
#include "HudButtons.h"
#include "ui/InfoHudShared.h"            // shared clock/time helpers (HudShared::)
#include "app/App.h"                     // Config (app.config.hud*)
#include "api/core/Json.h"               // Api::Json null-safe readers (button layout load)
#include "Shared.h"                      // IsInCombat / IsMapOpen live helpers
#include "ui/Gw2Ui.h"
#include "ui/SettingsWindow.h"           // OpenSettingsTab + OpenOptionsSection (launcher actions)
#include "ui/tabs/ItemsTab.h"            // OpenItemsTab (items -> All Items, inventory -> My Inventory)
#include "ui/tabs/TradingPostTab.h"      // OpenCraftingCart (cart) + OpenTradingPostTab (Trading Post button)
#include "ui/wiki/WikiReader.h"          // Wiki::OpenReader (Wiki button)
#include "app/CraftCart.h"               // cart item-count badge
#include "ui/tabs/SettingsModel.h"       // SEC_HUD + DrawSettingSection (scalar model rows)
#include "ui/dashboard/Dashboard.h"      // Dashboard::Toggle
#include "ui/profiles/ProfileBar.h"      // Profiles::DrawProfileBar
#include "ui/profiles/ConfigProfiles.h"  // ConfigProfiles::Host / RegisterLayout / Owner
#include "ui/LayoutOrderJson.h"          // OrderedToJson / OrderedFromJson (button layout persistence)
#include "ui/LayoutTypes.h"              // HudBtn (also via Config.h, but explicit)
#include "ui/tabs/SettingsCommon.h"      // SettingsParagraph + SettingsText
#include "util/Draw.h"                   // kHudClockPrimaryNames / kHudClockSecondaryNames
#include "render/glyphs/Glyphs.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// The HUD launcher bar reads/writes app.config directly (config.hud* scalars + config.hudButtons). Per-character
// switching is handled uniformly by the HUD ConfigProfileFamily (ConfigProfiles), which snapshots this slice.
namespace
{
    enum class MenuKind { None, Icon, Clock, Bar };
    MenuKind       g_menuReq = MenuKind::None;
    std::string    g_menuBtn;
    int            g_menuSide = 0;        // side a right-click "Add" lands on (0 Left, 1 Right)
    constexpr int  kAddBase = 1000;       // ContextMenuTree leaf id base for "Add <button>" (id = base + registry index)

    HudBtn MakeBtn(const std::string& key)   // new/re-enabled -> its own default side
    {
        const HudButtons::Button* d = HudButtons::Find(key.c_str());
        HudBtn b; b.key = key; b.side = d ? d->defaultSide : 0; return b;
    }
    std::vector<UiLayout::Ordered<HudBtn>::Reg> BuildReg()
    {
        std::vector<UiLayout::Ordered<HudBtn>::Reg> reg;
        for (const HudButtons::Button& def : HudButtons::All()) reg.push_back({ def.key, def.defaultOn, [k = def.key] { return MakeBtn(k); } });
        return reg;
    }
    // The catalog default layout: each Button carries its own default (defaultOn = shown, defaultSide, defaultOrder).
    UiLayout::Ordered<HudBtn> DefaultButtons()
    {
        UiLayout::Ordered<HudBtn> o;
        std::vector<const HudButtons::Button*> on;
        for (const HudButtons::Button& b : HudButtons::All()) if (b.defaultOn) on.push_back(&b);
        std::sort(on.begin(), on.end(), [](const HudButtons::Button* a, const HudButtons::Button* b) { return a->defaultOrder < b->defaultOrder; });
        for (const HudButtons::Button* b : on) o.items.push_back({ b->key, b->defaultSide });
        return o;
    }

    void RunButton(App& app, const std::string& key)
    {
        if      (key == "settings")  OpenSettingsTab(app, SettingsTabOptions);
        else if (key == "atlas")     OpenSettingsTab(app, SettingsTabAtlas);
        else if (key == "items")     OpenItemsTab(app, 0);   // catalogue (All Items)
        else if (key == "inventory") OpenItemsTab(app, 1);   // owned (My Inventory)
        else if (key == "collections") OpenSettingsTab(app, SettingsTabCollections);
        else if (key == "tradingpost") OpenTradingPostTab(app);
        else if (key == "journal")   OpenSettingsTab(app, SettingsTabJournal);
        else if (key == "wiki")      Wiki::OpenReader(app);
        else if (key == "checklist") OpenSettingsTab(app, SettingsTabChecklist);
        else if (key == "dungeons")  OpenSettingsTab(app, SettingsTabDungeons);
        else if (key == "timers")    OpenSettingsTab(app, SettingsTabTimers);
        else if (key == "sessions")  OpenSettingsTab(app, SettingsTabSessions);
        else if (key == "cart")      OpenCraftingCart(app);
        else if (key == "dashboard") Dashboard::Toggle();
        else if (key == "guide")     app.config.showGuide = !app.config.showGuide;
    }

    int FindBtnIdx(App& app, const std::string& key)
    {
        auto& items = app.config.hudButtons.items;
        for (int i = 0; i < (int)items.size(); ++i) if (items[i].key == key) return i;
        return -1;
    }
    // Move a button earlier/later among the buttons on its OWN side (swap with the nearest same-side neighbour).
    void MoveWithinSide(App& app, int idx, int dir) { app.config.hudButtons.Move(idx, dir, [](const HudBtn& b) { return b.side; }); }
}

void HUD::RegisterConfig()
{
    ConfigProfiles::RegisterLayout(ConfigProfiles::Owner::Hud, {
        // capture: config structured fields -> the slice json
        [](App& app, nlohmann::json& j) {
            j["hudButtons"] = UiLayout::OrderedToJson(app.config.hudButtons, [](const HudBtn& b, nlohmann::json& e) { e["side"] = b.side; });
            j["hudOffset"]  = app.config.hudOffset;
        },
        // apply: slice json -> config structured fields, then reconcile against the button catalog
        [](App& app, const nlohmann::json& j) {
            if (j.contains("hudButtons"))
                UiLayout::OrderedFromJson(app.config.hudButtons, j["hudButtons"],
                    [](const nlohmann::json& b) { HudBtn c; c.key = Api::Json::Str(b, "key"); c.side = Api::Json::Int(b, "side", 0); return c; });
            app.config.hudOffset = (float)Api::Json::Num(j, "hudOffset", app.config.hudOffset);
            app.config.hudButtons.Reconcile(BuildReg());
        },
        // seedDefault: config structured fields <- catalog default (feeds the baked m_default)
        [](App& app) { app.config.hudButtons = DefaultButtons(); }
    });
}

bool HUD::IsEnabled(App& app)            { return app.config.hudEnabled; }
void HUD::SetEnabled(App& app, bool on)  { if (app.config.hudEnabled != on) { app.config.hudEnabled = on; app.settingsDirty = true; } }

void HUD::Render(App& app)
{
    Config& cfg = app.config;
    if (!cfg.hudEnabled) return;
    if (cfg.hudHideInCombat && IsInCombat()) return;
    if (cfg.hudHideOnMap && IsMapOpen()) return;

    std::vector<const HudBtn*> left, right;
    for (const HudBtn& b : cfg.hudButtons.items)
        (b.side == 1 ? right : left).push_back(&b);

    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    if (W < 2.f || H < 2.f) return;

    const float ui = Gw2Ui::GlobalScale();
    const bool labels = cfg.hudLabels;
    const float iconBox = 34.f * ui, iconSz = 22.f * ui, btnGap = 2.f * ui, sidePad = 8.f * ui, panelGap = 6.f * ui, clockPad = 16.f * ui, vpad = 6.f * ui;
    const float labelFs = 14.f, labelH = labels ? ((labelFs + 2.f) * ui) : 0.f;
    const float cellH = iconBox + labelH;
    const float sideH = cellH + vpad * 2.f;
    const float centerH = std::max(sideH, 46.f * ui) + 14.f * ui;   // clock box is taller than the side boxes

    // per-button cell width (wide enough for the label when labels are on)
    auto cellW = [&](const HudBtn* b) {
        const HudButtons::Button* def = HudButtons::Find(b->key.c_str());
        if (!labels || !def) return iconBox;
        return std::max(iconBox, Gw2Ui::MeasureWidth(def->label, labelFs) + 6.f * ui);
    };
    auto groupW = [&](const std::vector<const HudBtn*>& g) {
        if (g.empty()) return 0.f;
        float w = sidePad * 2.f + (float)(g.size() - 1) * btnGap;
        for (const HudBtn* b : g) w += cellW(b);
        return w;
    };
    const float leftPanelW = groupW(left), rightPanelW = groupW(right);

    // clock cell
    const std::string primStr = HudShared::ClockByType(cfg.hudClockPrimary, cfg.hudClock24h, cfg.hudClockAmpm);
    const bool  hasSec = cfg.hudClockSecondary > 0;
    const std::string secStr = hasSec ? HudShared::ClockByType(cfg.hudClockSecondary - 1, cfg.hudClock24h, cfg.hudClockAmpm) : std::string();
    const float primFs = 26.f, secFs = 14.f;
    float clockTextW = Gw2Ui::MeasureWidth(primStr.c_str(), primFs);
    if (hasSec) clockTextW = std::max(clockTextW, Gw2Ui::MeasureWidth(secStr.c_str(), secFs));
    const float centerW = cfg.hudClockShow ? std::max(92.f * ui, clockTextW + clockPad * 2.f) : 0.f;

    if (left.empty() && right.empty() && !cfg.hudClockShow) return;   // nothing to draw

    const float totalW = leftPanelW + (leftPanelW > 0.f ? panelGap : 0.f) + centerW
                       + (rightPanelW > 0.f && centerW > 0.f ? panelGap : 0.f) + rightPanelW;

    const float m = 2.f * ui;                                  // window margin so panel borders aren't clipped at the edge
    // The CLOCK stays on the true screen centre (W/2 + manual offset); the left/right button groups extend
    // outward from it -- so more buttons on a side just push that side further out, the clock never moves.
    // (When the clock is hidden there's no anchor, so centre the whole bar instead.)
    const float clockMid = leftPanelW + (leftPanelW > 0.f ? panelGap : 0.f) + centerW * 0.5f;   // clock centre, from x0
    float x0;
    if (centerW > 0.f)
    {
        float clockCenter = W * 0.5f + cfg.hudOffset;                              // screen centre + manual nudge
        const float ccMin = centerW * 0.5f + 6.f * ui, ccMax = W - centerW * 0.5f - 6.f * ui;
        if (ccMax > ccMin) clockCenter = std::min(std::max(clockCenter, ccMin), ccMax);   // keep the clock itself on-screen
        x0 = clockCenter - clockMid;                                               // groups extend outward from the clock
    }
    else x0 = (W - totalW) * 0.5f + cfg.hudOffset;                                 // no clock -> centre the whole bar
    const float winW = totalW + 2.f * m, winH = centerH + 2.f * m;
    const float winX = x0 - m;
    const float winY = (cfg.hudEdge == 0) ? 6.f * ui : (H - winH - 6.f * ui);
    const float y0 = winY + m;
    const float sideTop = y0 + (centerH - sideH) * 0.5f;
    const float midY = y0 + centerH * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(winX, winY));
    ImGui::SetNextWindowSize(ImVec2(winW, winH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##tcHud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 fill = IM_COL32(24, 20, 13, 235), border = IM_COL32(150, 124, 70, 220);
        auto panel = [&](float px, float pw, float top, float h, ImU32 brd) {
            dl->AddRectFilled(ImVec2(px, top), ImVec2(px + pw, top + h), fill, 7.f * ui);
            dl->AddRect(ImVec2(px, top), ImVec2(px + pw, top + h), brd, 7.f * ui, 0, 1.4f * ui);
        };

        // ONE InvisibleButton per icon (no overlapping full-bar button -> every click registers). The clock
        // cell is the only other item, sitting between the groups, so it never overlaps an icon.
        auto drawIcon = [&](const HudBtn* b, float cx, float cw)
        {
            const HudButtons::Button* def = HudButtons::Find(b->key.c_str());
            if (!def) return;
            const float iconTop = sideTop + vpad;
            ImGui::SetCursorScreenPos(ImVec2(cx, iconTop));
            ImGui::PushID(b->key.c_str());
            const bool clicked = ImGui::InvisibleButton("##i", ImVec2(cw, cellH));
            const bool hov = ImGui::IsItemHovered();
            const bool rclick = hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            ImGui::PopID();
            if (hov)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                dl->AddRectFilled(ImVec2(cx, iconTop), ImVec2(cx + cw, iconTop + cellH), IM_COL32(255, 220, 140, 40), 5.f * ui);
            }
            const ImU32 col = hov ? Gw2Ui::kGold : IM_COL32(206, 190, 150, 235);
            Render::DrawGlyph(dl, ImVec2(cx + cw * 0.5f, iconTop + iconBox * 0.5f), iconSz, def->glyph, col);
            if (b->key == "cart")   // item-count badge on the cart button
            {
                const int n = CraftCart::Count(CraftCart::Active());
                if (n > 0)
                {
                    char nb[8]; std::snprintf(nb, sizeof(nb), "%d", n > 99 ? 99 : n);
                    const float br = 7.f * ui;
                    const ImVec2 bc(cx + cw * 0.5f + iconBox * 0.5f - br + 2.f * ui, iconTop + iconBox * 0.5f - br - 1.f * ui);
                    dl->AddCircleFilled(bc, br, IM_COL32(150, 40, 36, 240), 12);
                    dl->AddCircle(bc, br, Gw2Ui::Alpha(Gw2Ui::kGold, 200), 12, 1.2f * ui);
                    Gw2Ui::LabelDL(dl, ImVec2(bc.x - br, bc.y - br), ImVec2(bc.x + br, bc.y + br), nb,
                                   Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(255, 240, 220, 255), false, nullptr, 14.f);
                }
            }
            if (labels)
                Gw2Ui::LabelDL(dl, ImVec2(cx, iconTop + iconBox), ImVec2(cx + cw, iconTop + iconBox + labelH), def->label,
                               Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, col, false, nullptr, labelFs);
            if (hov && !labels) Gw2Ui::Tooltip(def->label);
            if (clicked) RunButton(app, b->key);
            if (rclick) { g_menuReq = MenuKind::Icon; g_menuBtn = b->key; }
        };

        float x = x0;
        if (leftPanelW > 0.f)
        {
            panel(x, leftPanelW, sideTop, sideH, border);
            float cx = x + sidePad;
            for (const HudBtn* b : left) { const float w = cellW(b); drawIcon(b, cx, w); cx += w + btnGap; }
            x += leftPanelW + panelGap;
        }

        if (centerW > 0.f)
        {
            const ImVec2 cMin(x, y0), cMax(x + centerW, y0 + centerH);
            panel(x, centerW, y0, centerH, IM_COL32(198, 166, 94, 235));
            if (hasSec)
            {
                Gw2Ui::LabelDL(dl, ImVec2(cMin.x, midY - 18.f * ui), ImVec2(cMax.x, midY + 6.f * ui), primStr.c_str(),
                               Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(255, 236, 188, 255), false, nullptr, primFs);
                Gw2Ui::LabelDL(dl, ImVec2(cMin.x, midY + 6.f * ui), ImVec2(cMax.x, midY + 22.f * ui), secStr.c_str(),
                               Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(184, 200, 224, 235), false, nullptr, secFs);
            }
            else
            {
                Gw2Ui::LabelDL(dl, cMin, cMax, primStr.c_str(),
                               Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(255, 236, 188, 255), false, nullptr, primFs);
            }
            ImGui::SetCursorScreenPos(cMin);
            ImGui::InvisibleButton("##clock", ImVec2(centerW, centerH));
            const bool clockHov = ImGui::IsItemHovered();
            if (clockHov && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) g_menuReq = MenuKind::Clock;
            if (!cfg.hudLocked)
            {
                if (clockHov || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                if (ImGui::IsItemActive() && io.MouseDelta.x != 0.f) { cfg.hudOffset += io.MouseDelta.x; app.settingsDirty = true; }
            }
            if (clockHov && cfg.hudClockTooltip && Gw2Ui::TooltipBegin())
            {
                Gw2Ui::TooltipTitle("Time");
                Gw2Ui::TooltipText((std::string("Local:   ") + HudShared::ClockByType(0, cfg.hudClock24h, cfg.hudClockAmpm)).c_str());
                Gw2Ui::TooltipText((std::string("Server:  ") + HudShared::ClockByType(1, cfg.hudClock24h, cfg.hudClockAmpm)).c_str());
                Gw2Ui::TooltipText((std::string("Tyrian:  ") + HudShared::ClockByType(2)).c_str());
                if (!cfg.hudLocked) Gw2Ui::TooltipMuted("Drag to move");
                Gw2Ui::TooltipEnd();
            }
            x += centerW + (rightPanelW > 0.f ? panelGap : 0.f);
        }

        if (rightPanelW > 0.f)
        {
            panel(x, rightPanelW, sideTop, sideH, border);
            float cx = x + sidePad;
            for (const HudBtn* b : right) { const float w = cellW(b); drawIcon(b, cx, w); cx += w + btnGap; }
        }

        // Right-click empty bar background (not over an icon or the clock) -> Add a button to the side you
        // clicked. The split is the clock centre (or the bar's own midpoint when the clock is hidden), so the
        // half you click matches the half it lands on. !IsAnyItemHovered lets the icon/clock right-clicks win.
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            const float anchorX = (centerW > 0.f) ? (x0 + clockMid) : (x0 + totalW * 0.5f);
            g_menuSide = (io.MousePos.x < anchorX) ? 0 : 1;
            g_menuReq = MenuKind::Bar;
        }

        const bool wasIcon = (g_menuReq == MenuKind::Icon), wasBar = (g_menuReq == MenuKind::Bar);
        if (g_menuReq == MenuKind::Clock) ImGui::OpenPopup("##clClockMenu");
        g_menuReq = MenuKind::None;

        // The "Add <button>" leaves shared by the icon + bar menus: every registry button not already shown,
        // keyed by its STABLE registry index (id = kAddBase + index) so dispatch maps back to the right key.
        std::vector<Gw2Ui::MenuNode> addLeaves;
        {
            const auto& all = HudButtons::All();
            for (int i = 0; i < (int)all.size(); ++i)
                if (!cfg.hudButtons.Shown(all[i].key)) { Gw2Ui::MenuNode lf; lf.label = all[i].label; lf.id = kAddBase + i; addLeaves.push_back(lf); }
        }
        auto addByMenuId = [&](int picked, int side) {
            const int idx = picked - kAddBase;
            const auto& all = HudButtons::All();
            if (idx < 0 || idx >= (int)all.size()) return;
            const std::string key = all[idx].key;
            if (HudBtn* s = cfg.hudButtons.Enable(key, [key] { return MakeBtn(key); })) s->side = side;
            app.settingsDirty = true;
        };

        // --- context menus ---
        // Right-click an ICON for that button's options (+ Add, landing on that icon's side). Right-click empty
        // bar background to Add to the clicked side. Right-click the CLOCK for clock + whole-HUD controls.
        {
            const int bi = FindBtnIdx(app, g_menuBtn);
            const bool isRight = bi >= 0 && cfg.hudButtons.items[bi].side == 1;
            auto leaf = [](const char* l, int id) { Gw2Ui::MenuNode n; n.label = l; n.id = id; return n; };
            std::vector<Gw2Ui::MenuNode> nodes;
            nodes.push_back(leaf(isRight ? "Move to left side" : "Move to right side", 0));
            nodes.push_back(leaf("Move earlier", 1));
            nodes.push_back(leaf("Move later", 2));
            nodes.push_back(leaf("Disable this button", 3));
            if (!addLeaves.empty())
            {
                Gw2Ui::MenuNode sep; sep.separator = true; nodes.push_back(sep);
                Gw2Ui::MenuNode add; add.label = "Add button"; add.children = addLeaves; nodes.push_back(add);
            }
            const int picked = Gw2Ui::ContextMenuTree("##clIconMenu", nodes, wasIcon);
            if (picked >= 0 && bi >= 0)
            {
                if      (picked == 0) { cfg.hudButtons.items[bi].side ^= 1; app.settingsDirty = true; }
                else if (picked == 1) { MoveWithinSide(app, bi, -1); app.settingsDirty = true; }
                else if (picked == 2) { MoveWithinSide(app, bi, +1); app.settingsDirty = true; }
                else if (picked == 3) { cfg.hudButtons.Remove(cfg.hudButtons.items[bi].key); app.settingsDirty = true; }
                else if (picked >= kAddBase) addByMenuId(picked, cfg.hudButtons.items[bi].side);
            }
        }
        {
            std::vector<Gw2Ui::MenuNode> nodes;
            Gw2Ui::MenuNode add; add.label = (g_menuSide == 0) ? "Add to left side" : "Add to right side";
            if (addLeaves.empty()) { Gw2Ui::MenuNode none; none.label = "All buttons shown"; none.id = -1; add.children.push_back(none); }
            else add.children = addLeaves;
            nodes.push_back(add);
            const int picked = Gw2Ui::ContextMenuTree("##clBarMenu", nodes, wasBar);
            if (picked >= kAddBase) addByMenuId(picked, g_menuSide);
        }
        {
            std::vector<const char*> it;
            it.push_back(cfg.hudClock24h ? "Switch to 12-hour" : "Switch to 24-hour");
            it.push_back(cfg.hudClockAmpm ? "Hide AM/PM" : "Show AM/PM");
            it.push_back("Cycle main time");
            it.push_back("Cycle second line");
            it.push_back(cfg.hudClockTooltip ? "Hover times: off" : "Hover times: on");
            it.push_back("Hide clock");
            it.push_back(cfg.hudLocked ? "Unlock HUD (drag to move)" : "Lock HUD position");
            it.push_back(cfg.hudEdge == 0 ? "Move HUD to bottom" : "Move HUD to top");
            it.push_back("HUD settings...");
            const int picked = Gw2Ui::ContextMenu("##clClockMenu", it.data(), (int)it.size());
            if      (picked == 0) { cfg.hudClock24h = !cfg.hudClock24h; app.settingsDirty = true; }
            else if (picked == 1) { cfg.hudClockAmpm = !cfg.hudClockAmpm; app.settingsDirty = true; }
            else if (picked == 2) { cfg.hudClockPrimary = (cfg.hudClockPrimary + 1) % 3; app.settingsDirty = true; }
            else if (picked == 3) { cfg.hudClockSecondary = (cfg.hudClockSecondary + 1) % 4; app.settingsDirty = true; }
            else if (picked == 4) { cfg.hudClockTooltip = !cfg.hudClockTooltip; app.settingsDirty = true; }
            else if (picked == 5) { cfg.hudClockShow = false; app.settingsDirty = true; }
            else if (picked == 6) { cfg.hudLocked = !cfg.hudLocked; app.settingsDirty = true; }
            else if (picked == 7) { cfg.hudEdge = (cfg.hudEdge == 0) ? 1 : 0; app.settingsDirty = true; }
            else if (picked == 8) { OpenSettingsTab(app, SettingsTabOptions); OpenOptionsSection(SEC_HUD); }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void HUD::DrawSettings(App& app)
{
    Config& cfg = app.config;

    // --- Profiles (top) -- one universal per-character profile family, via ConfigProfiles ---
    Profiles::DrawProfileBar(app, ConfigProfiles::Host(app, ConfigProfiles::Owner::Hud), "hudprof",
        "The HUD is a launcher bar with a center clock. Everything here -- placement, clock, buttons and "
        "sides -- is saved per character in this profile; import one from another character below.");

    // --- Scalar settings: the SEC_HUD model rows (searchable + tooltipped, same look as every other section) ---
    DrawSettingSection(app, SEC_HUD);
    ImGui::Dummy(ImVec2(0.f, 6.f));

    // --- Button layout (structured; edits config.hudButtons) ---
    Gw2Ui::Label("Buttons", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    int moveA = -1, moveB = -1;        // swap these two button indices (reorder within a side)
    int sideIdx = -1, sideTo = -1;     // send button index sideIdx to side sideTo (appended to its end)
    std::string hideKey, addKey;       // (declared before the card so the post-card applies can read them)
    static const char* kSideNames[2] = { "Left of clock", "Right of clock" };
    Gw2Ui::BeginCard("hud-buttons");
    SettingsParagraph("Buttons are grouped by side of the clock (Left / Right). Reorder within a side with the "
                      "up/down arrows; click L/R to send a button to the other side (it lands at that side's end); "
                      "the minus disables it. Disabled buttons can be re-enabled below -- or right-click the bar "
                      "itself (a button, or empty space on a side) to add one to that side.", IM_COL32(168, 158, 136, 255));
    ImGui::Spacing();

    const float availW = Gw2Ui::CardInnerWidth();   // full inner width (rows span the card, symmetric padding)
    const float startX = ImGui::GetCursorScreenPos().x;
    const float rh = 28.f;

    for (int sd = 0; sd < 2; ++sd)
    {
        // buttons on this side, in vector order (their order here == their order on the bar)
        std::vector<int> rows;
        for (int i = 0; i < (int)cfg.hudButtons.items.size(); ++i)
            if (cfg.hudButtons.items[i].side == sd && HudButtons::Find(cfg.hudButtons.items[i].key.c_str()))
                rows.push_back(i);

        ImGui::Spacing();
        Gw2Ui::Label(kSideNames[sd], Gw2Ui::kTextSub, false, nullptr, SettingsText::Hint);
        if (rows.empty()) { Gw2Ui::Label("   (empty)", Gw2Ui::kTextDim, false, nullptr, 14.f); continue; }

        static const char* kSideLetters[2] = { "L", "R" };
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const int i = rows[r];
            const HudBtn& b = cfg.hudButtons.items[i];
            const HudButtons::Button* def = HudButtons::Find(b.key.c_str());
            Gw2Ui::ReorderRowDesc rd;
            rd.label = def->label; rd.glyph = (int)def->glyph;
            rd.group = b.side; rd.letters = kSideLetters; rd.groupCount = 2;
            rd.canUp = (r > 0); rd.canDown = (r + 1 < rows.size()); rd.fontSize = SettingsText::Hint;
            char rid[24]; std::snprintf(rid, sizeof(rid), "hb%d", i);
            const Gw2Ui::ReorderResult rr = Gw2Ui::ReorderRow(rid, startX, availW, rh, rd);
            if      (rr.act == Gw2Ui::ReorderResult::Disable)  hideKey = b.key;
            else if (rr.act == Gw2Ui::ReorderResult::Down)     { moveA = i; moveB = rows[r + 1]; }
            else if (rr.act == Gw2Ui::ReorderResult::Up)       { moveA = i; moveB = rows[r - 1]; }
            else if (rr.act == Gw2Ui::ReorderResult::SetGroup) { sideIdx = i; sideTo = rr.toGroup; }
        }
    }
    ImGui::Dummy(ImVec2(availW, 2.f));   // pad the card height so it covers the last row
    Gw2Ui::EndCard();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    // Apply ONE mutation per frame (a click triggers only one; indices shift, so never both).
    if (moveA >= 0 && moveB >= 0) { std::swap(cfg.hudButtons.items[moveA], cfg.hudButtons.items[moveB]); app.settingsDirty = true; }
    else if (sideIdx >= 0)
    {
        auto& items = cfg.hudButtons.items;
        HudBtn moved = items[sideIdx]; moved.side = sideTo;
        items.erase(items.begin() + sideIdx);
        int insertAt = (int)items.size();   // append after the last button already on the target side
        for (int j = (int)items.size() - 1; j >= 0; --j)
            if (items[j].side == sideTo) { insertAt = j + 1; break; }
        items.insert(items.begin() + insertAt, moved);
        app.settingsDirty = true;
    }
    if (!hideKey.empty()) { cfg.hudButtons.Remove(hideKey); app.settingsDirty = true; }

    // disabled buttons (registry buttons not on the bar) -> a "+ enable" list
    bool anyHidden = false;
    for (const HudButtons::Button& def : HudButtons::All()) if (!cfg.hudButtons.Shown(def.key)) { anyHidden = true; break; }
    if (anyHidden)
    {
        Gw2Ui::Label("Disabled buttons", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("hud-disabled");
        const float dAvail = Gw2Ui::CardInnerWidth();
        const float dStartX = ImGui::GetCursorScreenPos().x;
        for (const HudButtons::Button& def : HudButtons::All())
        {
            if (cfg.hudButtons.Shown(def.key)) continue;
            char rid[40]; std::snprintf(rid, sizeof(rid), "hd_%s", def.key);
            if (Gw2Ui::EnableRow(rid, dStartX, dAvail, 26.f, def.label, (int)def.glyph, SettingsText::Hint)) addKey = def.key;
        }
        ImGui::Dummy(ImVec2(dAvail, 2.f));
        Gw2Ui::EndCard();
    }
    if (!addKey.empty()) { cfg.hudButtons.Enable(addKey, [addKey] { return MakeBtn(addKey); }); app.settingsDirty = true; }
}
