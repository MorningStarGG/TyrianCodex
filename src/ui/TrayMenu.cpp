#include "ui/TrayMenu.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/QuickMenu.h"       // shared Routing/Toggles submenus + Config picks
#include "ui/GuideViewer.h"     // CurrentTargetOverride / TargetClearLabel / ClearTargetOverride
#include "ui/viewer/ViewerLayout.h"
#include "ui/SettingsWindow.h"  // OpenSettingsTab + SettingsTabId (tab-shortcut launchers)
#include "ui/hud/Hud.h"         // HUD::IsEnabled/SetEnabled (UI Toggles)
#include "ui/infopanel/InfoPanel.h"  // InfoPanel::IsEnabled/SetEnabled (UI Toggles)
#include "ui/tabs/AtlasTab.h"   // OpenAtlasScope (Atlas > Zones)
#include "ui/tabs/ItemsTab.h"   // OpenItemsTab (Items > All / My Inventory / Equipment)
#include "ui/tabs/CollectionsTab.h"   // OpenCollectionsTab (Collections scopes)
#include "ui/tabs/TradingPostTab.h"   // OpenTradingPostTab (Trading Post views)
#include "ui/tabs/SessionTab.h"       // OpenSessionTab (Sessions scopes)
#include "ui/tabs/DungeonsTab.h"      // OpenDungeonsTab (Dungeons/Content scopes)
#include "ui/wiki/WikiReader.h"
#include "ui/search/SearchModel.h"   // Search::Kind (the Zones browse filter)
#include "model/Dataset.h"            // Step (Reset zone progress)
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// Tray right-click menu state: Nexus opens its OWN popup for the QuickAccess context menu, which we can't
// size or skin to match. So the Nexus callback (RenderQuickAccessMenu) just records the anchor + requests
// our own GW2-skinned popup, which Render() opens via DrawTrayMenu / Gw2Ui::ContextMenu (auto-sizes to our
// content, our chrome -- same as the arrow menu).
static bool   g_trayMenuOpen = false;
static ImVec2 g_trayMenuPos  = ImVec2(0.f, 0.f);

// Nexus QUICKACCESS context-menu callback. Nexus has already opened its own (unstyled) popup around our
// tray icon; we capture its anchor and ask Render() to open OUR GW2-skinned popup (DrawTrayMenu, via
// Gw2Ui::ContextMenu -- auto-sized to our content, our chrome, like the arrow menu), then dismiss Nexus's.
void RenderQuickAccessMenu()
{
    g_trayMenuPos  = ImGui::GetWindowPos();   // Nexus popup top-left ~ anchored to the tray icon
    g_trayMenuOpen = true;
    ImGui::CloseCurrentPopup();
}

// Our GW2-skinned tray menu, opened from Render() when the Nexus callback requested it. Grouped via
// ContextMenuTree: Routing + In-world toggles come from the shared QuickMenu (so they match the arrow menu);
// the rest are the global app actions. Labels rebuild each frame so they reflect live state while open.
void DrawTrayMenu(App& app)
{
    // The anchor (g_trayMenuPos) is the Nexus popup's top-left, level with the tray-icon row, so the menu's top
    // edge lands under neighbouring QuickAccess icons. Nudge it DOWN past the icon row and slightly RIGHT. The
    // popup is opened below via ContextMenuTree's openNow; SetNextWindowPos must precede it.
    const ImVec2 kTrayMenuOffset(22.f, 30.f);
    const bool openNow = g_trayMenuOpen;
    if (openNow)
    {
        ImGui::SetNextWindowPos(ImVec2(g_trayMenuPos.x + kTrayMenuOffset.x, g_trayMenuPos.y + kTrayMenuOffset.y));
        g_trayMenuOpen = false;
    }

    const TargetOverrideKind overrideKind = CurrentTargetOverride(app);
    // Tray-only action ids -- kept >= 20000 so they never collide with QuickMenu's shared ids (1..8) or its
    // routing/toggle ranges (10000+/11000+). Everything shared routes through QuickMenu::ArrowDispatch.
    enum {
        TR_ViewExpanded = 20001, TR_ViewNormal, TR_ViewNone,
        TR_HUD, TR_Dashboard, TR_InfoPanel, TR_Arrow,
        TR_PanelLock, TR_PanelReset, TR_ResetProgress,
        TR_Journal, TR_Wiki, TR_Checklist, TR_Timers,   // leaf tabs (no scopes)
    };
    // Scoped-tab submenu ids = base + scopeIndex (each tab has < 100 scopes). Clicking the PARENT row uses `base`
    // (scope 0 = the tab's main view); each child = base + its scope index. Decoded by range in the dispatch.
    enum {
        TR_ATLAS_BASE = 22100, TR_COLL_BASE = 22200, TR_DGN_BASE = 22300,
        TR_ITEMS_BASE = 22400, TR_SESS_BASE = 22500, TR_TP_BASE  = 22600,
    };

    std::vector<Gw2Ui::MenuNode> nodes;
    auto leaf     = [&](const char* label, int id) { Gw2Ui::MenuNode n; n.label = label; n.id = id; nodes.push_back(std::move(n)); };
    auto sep      = [&]() { Gw2Ui::MenuNode s; s.separator = true; nodes.push_back(std::move(s)); };
    auto child    = [](Gw2Ui::MenuNode& parent, const char* label, int id) { Gw2Ui::MenuNode c; c.label = label; c.id = id; parent.children.push_back(std::move(c)); };
    auto childSel = [](Gw2Ui::MenuNode& parent, const char* label, int id, bool sel) { Gw2Ui::MenuNode c; c.label = label; c.id = id; c.selected = sel; parent.children.push_back(std::move(c)); };

    QuickMenu::RoutingNodes(app.config, nodes);                                  // "Routing >" (pick a mode, current marked)
    if (overrideKind != TargetOverrideKind::None) leaf(TargetClearLabel(overrideKind), QuickMenu::IdClear);
    sep();
    { Gw2Ui::MenuNode vw; vw.label = "Viewer"; vw.id = -1;                        // guide-panel style (current = green check)
      childSel(vw, "Expanded", TR_ViewExpanded, app.config.viewerStyle == ViewerStyleRich);
      childSel(vw, "Normal",   TR_ViewNormal,   app.config.viewerStyle == ViewerStyleCompact);
      childSel(vw, "None",     TR_ViewNone,     app.config.viewerStyle == ViewerStyleNone);
      nodes.push_back(std::move(vw)); }
    { Gw2Ui::MenuNode ut; ut.label = "UI Toggles"; ut.id = -1;                    // whole-surface on/off (checked = on)
      childSel(ut, "HUD",        TR_HUD,       HUD::IsEnabled(app));
      childSel(ut, "Dashboard",  TR_Dashboard, app.config.dashEnabled);
      childSel(ut, "Info Panel", TR_InfoPanel, InfoPanel::IsEnabled(app));
      childSel(ut, "Arrow",      TR_Arrow,     app.config.showArrow);
      nodes.push_back(std::move(ut)); }
    QuickMenu::TogglesNodes(app.config, nodes);                                  // "In-world toggles >" (render layers)
    sep();
    if (FarmingRunActive(app))                                                   // a gathering run -> its own actions
    {
        leaf("Reset gathering run", QuickMenu::IdFarmReset);                     // re-show passed nodes (session cross-off)
        leaf("Stop gathering run",  QuickMenu::IdFarmStop);
    }
    else
        QuickMenu::CurrentStepNodes(nodes);                                      // "Current step >": Mark done / Back / Copy (shared with the arrows)
    { Gw2Ui::MenuNode wn; wn.label = "Windows"; wn.id = -1;
      child(wn, app.config.arrowLocked ? "Unlock GPS arrow" : "Lock GPS arrow", QuickMenu::IdLockArrow);
      child(wn, "Reset arrow position", QuickMenu::IdResetArrow);
      child(wn, app.config.viewerLocked ? "Unlock guide panel" : "Lock guide panel", TR_PanelLock);
      child(wn, "Reset guide panel position", TR_PanelReset);
      nodes.push_back(std::move(wn)); }
    sep();
    // Tab launchers, ALPHABETICAL. A scoped tab is a clickable PARENT (click the row -> scope 0 = its main view)
    // whose children open each in-tab scope (id = base + scope index; the dispatch decodes the range). Leaf tabs
    // (no scopes) open directly. Keep the kXxxScopes label arrays in sync with each tab's scope-chip rail.
    static const char* const kAtlasScopes[] = { "All", "Open World", "Zones", "Cities", "Lounges", "WvW",
        "Dungeons", "Fractals", "Raids", "Strikes", "Story", "Festival", "Home", "Guild Halls", "Tutorials",
        "PvP", "Farming" };
    static const char* const kCollScopes[]  = { "Wardrobe", "Homestead", "Fishing", "Mounts", "Gliders",
        "Jade Bots", "Skiffs", "Novelties", "Finishers", "Mail Carriers", "Minis", "Mist Champions", "Titles",
        "Emotes", "Recipes" };
    static const char* const kDgnScopes[]   = { "Dungeons", "Story", "Fractals", "Raids", "Strikes", "Home" };
    static const char* const kItemScopes[]  = { "All Items", "My Inventory", "Equipment" };
    static const char* const kSessScopes[]  = { "Session", "Currencies", "Levels", "Objectives" };
    static const char* const kTpScopes[]    = { "My Trading Post", "Flip Finder", "Crafting", "Charts" };
    auto scopedTab = [&](const char* label, int base, const char* const* scopes, int n) {
        Gw2Ui::MenuNode t; t.label = label; t.id = base;   // click the parent row -> scope 0 (the tab's main view)
        for (int i = 0; i < n; ++i) child(t, scopes[i], base + i);
        nodes.push_back(std::move(t));
    };
    scopedTab("Atlas",        TR_ATLAS_BASE, kAtlasScopes, (int)(sizeof(kAtlasScopes) / sizeof(kAtlasScopes[0])));
    leaf("Checklist", TR_Checklist);
    scopedTab("Collections",  TR_COLL_BASE,  kCollScopes,  (int)(sizeof(kCollScopes)  / sizeof(kCollScopes[0])));
    scopedTab("Dungeons",     TR_DGN_BASE,   kDgnScopes,   (int)(sizeof(kDgnScopes)   / sizeof(kDgnScopes[0])));
    scopedTab("Items",        TR_ITEMS_BASE, kItemScopes,  (int)(sizeof(kItemScopes)  / sizeof(kItemScopes[0])));
    leaf("Journal",   TR_Journal);
    scopedTab("Sessions",     TR_SESS_BASE,  kSessScopes,  (int)(sizeof(kSessScopes)  / sizeof(kSessScopes[0])));
    leaf("Timers",    TR_Timers);
    scopedTab("Trading Post", TR_TP_BASE,    kTpScopes,    (int)(sizeof(kTpScopes)    / sizeof(kTpScopes[0])));
    leaf("Wiki",      TR_Wiki);
    sep();
    if (!FarmingRunActive(app)) leaf("Reset zone progress", TR_ResetProgress);   // farming has its own reset above
    leaf("Open Hub",       QuickMenu::IdHub);
    leaf("Settings window", QuickMenu::IdSettings);

    const int picked = Gw2Ui::ContextMenuTree("##tctray", nodes, openNow);
    if (picked < 0) return;
    if (QuickMenu::ArrowDispatch(app, picked)) return;   // shared: routing / toggles / current step / clear / lock+reset arrow / Hub / settings
    // Scoped tabs: decode `base + scopeIndex` -> the tab's scope opener (a parent click = scope 0 = main view).
    if (picked >= TR_ATLAS_BASE && picked < TR_ATLAS_BASE + 17) { OpenAtlasScope(app,    picked - TR_ATLAS_BASE); return; }
    if (picked >= TR_COLL_BASE  && picked < TR_COLL_BASE  + 15) { OpenCollectionsTab(app, picked - TR_COLL_BASE);  return; }
    if (picked >= TR_DGN_BASE   && picked < TR_DGN_BASE   +  6) { OpenDungeonsTab(app,    picked - TR_DGN_BASE);   return; }
    if (picked >= TR_ITEMS_BASE && picked < TR_ITEMS_BASE +  3) { OpenItemsTab(app,       picked - TR_ITEMS_BASE); return; }
    if (picked >= TR_SESS_BASE  && picked < TR_SESS_BASE  +  4) { OpenSessionTab(app,     picked - TR_SESS_BASE);  return; }
    if (picked >= TR_TP_BASE    && picked < TR_TP_BASE    +  4) { OpenTradingPostTab(app, picked - TR_TP_BASE);    return; }
    switch (picked)
    {
        case TR_ViewExpanded: app.config.viewerStyle = ViewerStyleRich;    app.settingsDirty = true; break;
        case TR_ViewNormal:   app.config.viewerStyle = ViewerStyleCompact; app.settingsDirty = true; break;
        case TR_ViewNone:
            if (app.config.viewerStyle != ViewerStyleNone) app.config.viewerStylePrev = app.config.viewerStyle;
            app.config.viewerStyle = ViewerStyleNone; app.settingsDirty = true; break;
        case TR_HUD:        HUD::SetEnabled(app, !HUD::IsEnabled(app)); break;
        case TR_Dashboard:  app.config.dashEnabled = !app.config.dashEnabled; app.settingsDirty = true; break;
        case TR_InfoPanel:  InfoPanel::SetEnabled(app, !InfoPanel::IsEnabled(app)); break;
        case TR_Arrow:      app.config.showArrow = !app.config.showArrow; app.settingsDirty = true; break;
        case TR_PanelLock: app.config.viewerLocked = !app.config.viewerLocked; app.settingsDirty = true; break;
        case TR_PanelReset: ImGui::SetWindowPos("##tcguide", ImVec2(60.f, 60.f));
                 {
                     const ImVec2 resetSize = ResetGuidePanelSize(app);
                     app.config.viewerW = resetSize.x; app.config.viewerH = resetSize.y;
                     ImGui::SetWindowSize("##tcguide", resetSize);
                 }
                 app.settingsDirty = true; break;   // reset the guide panel's position AND size
        case TR_ResetProgress:
            if (app.state.zone.Loaded)
            {
                std::vector<std::string> ids; ids.reserve(app.state.zone.Steps.size());
                for (const Step& s : app.state.zone.Steps) ids.push_back(s.StepId);
                app.progress.ResetMany(app.state.currentChar, ids);
                app.state.manualTarget.clear(); app.state.undoStack.clear(); app.follower.Reset(); app.gpsArrow.ResetEase();
            }
            break;
        case TR_Journal:    OpenSettingsTab(app, SettingsTabJournal);   break;
        case TR_Wiki:       Wiki::OpenReader(app);                      break;
        case TR_Checklist:  OpenSettingsTab(app, SettingsTabChecklist); break;
        case TR_Timers:     OpenSettingsTab(app, SettingsTabTimers);    break;
    }
}
