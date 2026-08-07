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

    // Dock-edge tracking: a USER switching Dock edge re-seats the bar at that edge (otherwise, once dragged,
    // nothing could ever put it back). A PROFILE applying a different edge must NOT re-seat -- that profile
    // carries its own saved position -- so the apply hook sets g_edgeFromProfile to absorb one change.
    bool           g_edgeInit = false;
    int            g_lastEdge = 0;
    bool           g_edgeFromProfile = false;
    // Whole-bar drag: cursor offset from the bar's anchor when the press began (so the bar doesn't jump to
    // centre on grab), plus press/drag state. g_barDragged outlives the release by design -- the icon
    // release-clicks are evaluated after it, and a drag must never fire the button it ended on.
    ImVec2         g_grab(0.f, 0.f);
    bool           g_barDown = false;
    bool           g_barDragged = false;

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
            j["hudPosX"]    = app.config.hudPosX;
            j["hudPosY"]    = app.config.hudPosY;
        },
        // apply: slice json -> config structured fields, then reconcile against the button catalog
        [](App& app, const nlohmann::json& j) {
            if (j.contains("hudButtons"))
                UiLayout::OrderedFromJson(app.config.hudButtons, j["hudButtons"],
                    [](const nlohmann::json& b) { HudBtn c; c.key = Api::Json::Str(b, "key"); c.side = Api::Json::Int(b, "side", 0); return c; });
            app.config.hudPosX = (float)Api::Json::Num(j, "hudPosX", -1.0);
            app.config.hudPosY = (float)Api::Json::Num(j, "hudPosY", -1.0);
            // Older slices stored raw pixel offsets; carry them so Render can migrate them once it knows the
            // screen size (a fraction cannot be derived here -- no display size at apply time).
            app.config.hudOffset = (float)Api::Json::Num(j, "hudOffset", 0.0);
            app.config.hudOffsetY = (float)Api::Json::Num(j, "hudOffsetY", 0.0);
            g_edgeFromProfile = true;   // this profile's dock edge comes with its own position -- don't re-seat
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
    if (cfg.hudHideInWvW && IsInWvW()) return;

    // Picking a different Dock edge re-seats the bar at that edge, so the selector always restores a sane
    // position even after the bar has been dragged. Profile-applied edges are adopted silently (see above).
    if (!g_edgeInit || g_edgeFromProfile) { g_lastEdge = cfg.hudEdge; g_edgeInit = true; g_edgeFromProfile = false; }
    else if (g_lastEdge != cfg.hudEdge)
    {
        g_lastEdge = cfg.hudEdge;
        cfg.hudPosY = -1.f;
        app.settingsDirty = true;
    }

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
    const float winW = totalW + 2.f * m, winH = centerH + 2.f * m;
    // The ANCHOR is the clock centre when there is one, so adding buttons to one side pushes that side outward
    // and the clock never moves; with no clock it is the bar's own centre. hudPosX is that anchor as a fraction
    // of screen width, hudPosY the bar's vertical centre as a fraction of height (-1 = default seat).
    const float anchorMid = (centerW > 0.f) ? (leftPanelW + (leftPanelW > 0.f ? panelGap : 0.f) + centerW * 0.5f)
                                            : (totalW * 0.5f);
    const float defaultY = (cfg.hudEdge == 0) ? 6.f * ui : (H - winH - 6.f * ui);

    // One-time migration of the legacy pixel offsets, now that the screen size is known.
    if (cfg.hudOffset != 0.f || cfg.hudOffsetY != 0.f)
    {
        if (cfg.hudPosX < 0.f) cfg.hudPosX = (W * 0.5f + cfg.hudOffset) / W;
        if (cfg.hudPosY < 0.f)
        {
            const float legacyY = (cfg.hudEdge == 0) ? (6.f * ui + cfg.hudOffsetY) : (H - winH - 6.f * ui - cfg.hudOffsetY);
            cfg.hudPosY = (legacyY + winH * 0.5f) / H;
        }
        cfg.hudOffset = cfg.hudOffsetY = 0.f;
        app.settingsDirty = true;
    }

    // Draw position. Clamped to the screen WITHOUT writing back, so the stored fraction survives a resolution
    // change intact instead of being ratcheted by whatever monitor it was last drawn on.
    float x0 = ((cfg.hudPosX < 0.f) ? (W * 0.5f) : (cfg.hudPosX * W)) - anchorMid;
    x0 = std::min(std::max(x0, 0.f), std::max(0.f, W - totalW));
    float winY = (cfg.hudPosY < 0.f) ? defaultY : (cfg.hudPosY * H - winH * 0.5f);
    winY = std::min(std::max(winY, 0.f), std::max(0.f, H - winH));
    const float winX = x0 - m;
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

        // ---- Drag the WHOLE bar, from anywhere on it ----------------------------------------------------
        // Grabbing any part -- an icon, a panel, the clock, the gaps -- moves the bar, so it behaves the same
        // whether or not the clock is shown. Handled here at window level, BEFORE the icons are submitted, so
        // g_barDragged is already set when their release-clicks are evaluated below and a drag never fires a
        // button. A press that never passes the threshold stays a plain click.
        const float kDragSlop = 4.f * ui;
        if (!cfg.hudLocked)
        {
            const bool overBar = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (overBar && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                g_barDown = true;
                g_barDragged = false;
                g_grab = ImVec2(io.MousePos.x - (x0 + anchorMid), io.MousePos.y - (winY + winH * 0.5f));
            }
            if (g_barDown && io.MouseDown[0])
            {
                const float dx = io.MousePos.x - (x0 + anchorMid) - g_grab.x;
                const float dy = io.MousePos.y - (winY + winH * 0.5f) - g_grab.y;
                if (std::sqrt(dx * dx + dy * dy) > kDragSlop) g_barDragged = true;
                if (g_barDragged)
                {
                    // ABSOLUTE positioning: the bar goes where the cursor is (minus wherever it was grabbed),
                    // never "current += delta". An accumulator drifts -- drag past the edge and it keeps banking
                    // movement the clamp hides, so dragging back does nothing until the slack unwinds. Clamped
                    // HERE, so what gets stored is always a position the bar can actually occupy.
                    const float axMin = anchorMid, axMax = W - (totalW - anchorMid);   // keep the WHOLE bar on screen
                    const float ay = winH * 0.5f;
                    cfg.hudPosX = std::min(std::max(io.MousePos.x - g_grab.x, axMin), std::max(axMin, axMax)) / W;
                    cfg.hudPosY = std::min(std::max(io.MousePos.y - g_grab.y, ay), std::max(ay, H - ay)) / H;
                    app.settingsDirty = true;
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                }
            }
            if (overBar && !g_barDragged) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (!io.MouseDown[0]) g_barDown = false;   // g_barDragged survives the release so the click below is suppressed
        }
        else { g_barDown = false; g_barDragged = false; }
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
            if (clicked && !g_barDragged) RunButton(app, b->key);   // a drag that ended on an icon must not launch it
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
            g_menuSide = (io.MousePos.x < x0 + anchorMid) ? 0 : 1;
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

// Position lives in hudPosX/hudPosY, which are set by DRAGGING the bar rather than by a settings row -- so this
// is the only way back to the default seat once it has been moved. Split out so the Options SEARCH can surface
// it too: it is an ACTION, not a model row, and search only walks the settings model (same reason the live
// Diagnostics readout is special-cased there).
void HUD::DrawResetPosition(App& app)
{
    Config& cfg = app.config;
    const bool moved = (cfg.hudPosX >= 0.f || cfg.hudPosY >= 0.f);
    if (Gw2Ui::ActionButtonPx("Reset position", Gw2Ui::Scaled(150.f), Gw2Ui::Scaled(26.f),
                              Gw2Ui::ActionButtonVariant::Normal,
                              moved ? "Re-centre the HUD bar at its docked edge"
                                    : "The HUD bar is already at its default position") &&
        moved)
    {
        cfg.hudPosX = -1.f;
        cfg.hudPosY = -1.f;
        app.settingsDirty = true;
    }
}

bool HUD::SearchMatchesActions(const char* qLower)
{
    return qLower && *qLower &&
           KeywordsMatch("hud bar reset position move drag place placement recentre recenter default "
                         "buttons layout order side icons add remove",
                         qLower);
}

void HUD::DrawSettings(App& app)
{
    // --- Profiles (top) -- one universal per-character profile family, via ConfigProfiles ---
    Profiles::DrawProfileBar(app, ConfigProfiles::Host(app, ConfigProfiles::Owner::Hud), "hudprof",
        "The HUD is a launcher bar with a center clock. Everything here -- placement, clock, buttons and "
        "sides -- is saved per character in this profile; import one from another character below.");

    // --- Scalar settings: the SEC_HUD model rows (searchable + tooltipped, same look as every other section) ---
    DrawSettingSection(app, SEC_HUD);

    DrawResetPosition(app);
    ImGui::Dummy(ImVec2(0.f, 6.f));
    DrawButtonLayout(app);
}

// --- Button layout (structured; edits config.hudButtons) -----------------------------------------------
// Split out for the same reason as DrawResetPosition: it is an editor, not a settings model row, so the
// Options search can only reach it if it can be drawn on its own.
void HUD::DrawButtonLayout(App& app)
{
    Config& cfg = app.config;
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
