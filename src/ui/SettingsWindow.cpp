#include "ui/SettingsWindow.h"
#include "app/App.h"
#include "app/ItemCatalog.h"
#include "app/SkinCatalog.h"
#include "app/CraftingEngine.h"
#include "app/CraftKnown.h"
#include "app/MarketData.h"
#include "app/TpEligibility.h"
#include "app/StaticData.h"   // WarmCurrencies (wallet/session currency catalog)
#include "ui/Gw2Ui.h"
#include "util/Textures.h"
#include "ui/tabs/OptionsTab.h"
#include "ui/tabs/JournalTab.h"
#include "ui/tabs/ChecklistTab.h"
#include "ui/tabs/DungeonsTab.h"
#include "ui/tabs/AtlasTab.h"
#include "ui/tabs/ItemsTab.h"
#include "ui/tabs/CollectionsTab.h"
#include "ui/tabs/AboutTab.h"
#include "ui/tabs/HubTab.h"
#include "ui/tabs/TimersTab.h"
#include "ui/tabs/TradingPostTab.h"
#include "ui/tabs/SessionTab.h"
#include "render/glyphs/Glyphs.h"   // Render::Glyph::Guide (Hub) / Clock (Timers) / Coins (Trading Post) tab icons
#include <algorithm>
#include <cmath>
#include <cstdint>

// The GW2-framed tabbed settings window SHELL: the window frame + tab bar + per-tab dispatch, the font toggle,
// and the one-time login cache warm. Each tab's body (and the settings model + serialization) lives in its own
// module under ui/tabs/ - this file just orchestrates them. Diagnostics is a SECTION inside Options, not a tab.

void ApplyFontChoice(App& app)
{
    // Build the Gw2Ui native-size ladder from whatever rungs have loaded so far; off (or nothing loaded yet)
    // reverts to the Nexus font. The text core then draws each run from the nearest rung at scale ~1.0 (crisp).
    if (app.config.useGw2Font)
    {
        Gw2Ui::FontBake b[App::kMenoLadderN]; int n = 0;
        for (const auto& m : app.menoLadder)
            if (m.regular) b[n++] = { m.px, m.regular, m.italic };
        if (n > 0) { Gw2Ui::SetGw2FontLadder(b, n); return; }
    }
    Gw2Ui::SetGw2Font(nullptr, nullptr);
}

static int g_settingsTab = 0;

static const Gw2Ui::Tab kSettingsTabs[] = {   // Hub landing first (default); Options demoted above About
    { 0, "Hub", (int)Render::Glyph::Guide }, { 528724, "Atlas" }, { 157098, "Items" },
    { 0, "Collections", (int)Render::Glyph::Star },
    { 0, "Trading Post", (int)Render::Glyph::Coins }, { 102369, "Journal" },
    { 157353, "Checklist" }, { 156626, "Instances" },
    { 0, "Timers", (int)Render::Glyph::Clock },
    { 0, "Sessions", (int)Render::Glyph::Chart },
    { 156736, "Options" }, { 440023, "About" },
};

void OpenSettingsTab(App& app, SettingsTabId tab)
{
    const int nTabs = (int)(sizeof(kSettingsTabs) / sizeof(kSettingsTabs[0]));
    g_settingsTab = std::clamp((int)tab, 0, nTabs - 1);
    app.state.showSettings = true;
}

void DrawSettingsWindow(App& app)
{
    // Latch the open transition so BeginWindow restores the saved size only when the window (re)opens, not
    // every frame (which would fight a live drag-resize).
    static bool s_wasOpen = false;
    const bool justOpened = app.state.showSettings && !s_wasOpen;
    s_wasOpen = app.state.showSettings;
    if (!app.state.showSettings) return;
    const int nTabs = (int)(sizeof(kSettingsTabs) / sizeof(kSettingsTabs[0]));
    const float prevW = app.config.settingsWindowW, prevH = app.config.settingsWindowH;
    if (Gw2Ui::BeginWindow("Tyrian Codex", &app.state.showSettings, "Tyrian Codex", "Guild Wars 2 companion",
                           kSettingsTabs, nTabs, &g_settingsTab,
                           &app.config.settingsWindowW, &app.config.settingsWindowH, justOpened))
    {
        switch (g_settingsTab)
        {
            case SettingsTabHub:       DrawHubContent(app);       break;
            case SettingsTabAtlas:     DrawAtlasContent(app);     break;
            case SettingsTabItems:     DrawItemsContent(app);     break;
            case SettingsTabCollections: DrawCollectionsContent(app); break;
            case SettingsTabTradingPost: DrawTradingPostContent(app); break;
            case SettingsTabJournal:   DrawJournalContent(app);   break;
            case SettingsTabChecklist: DrawChecklistContent(app); break;
            case SettingsTabDungeons:  DrawDungeonsContent(app);  break;
            case SettingsTabTimers:    DrawTimersContent(app);    break;
            case SettingsTabSessions:  DrawSessionContent(app);   break;
            case SettingsTabOptions:   DrawOptionsContent(app);   break;
            case SettingsTabAbout:     DrawAboutContent();        break;
            default: break;
        }
        Gw2Ui::EndWindow();
    }
    // Persist a user resize of the shell (mirrors the guide viewer's persist-on-drag).
    if (std::fabs(app.config.settingsWindowW - prevW) > 0.5f || std::fabs(app.config.settingsWindowH - prevH) > 0.5f)
        app.settingsDirty = true;
}

// One-time, on-login cache warm. Kicks off the DISK download of every map + icon the addon can show, so the
// tabs are ready before the player navigates to them - replacing the old scattered per-tab prefetch. All
// downloads run on the ImageCache background worker (off the render thread); GPU textures are still created
// lazily on display. Run once when in-game. The per-tab warms live with their tabs (WarmChecklist / WarmJournal).
void WarmCaches(App& app)
{
    // Settings-window chrome + tab icons (bundled, local-first) so they're ready before the window opens.
    for (const Gw2Ui::Tab& t : kSettingsTabs) Gw2Ui::WarmAsset(t.icon);
    for (uint32_t id : { 605026u, 156008u, 1002144u, 1032325u, 156044u, 157098u }) Gw2Ui::WarmAsset(id);
    Tex::GetFileTex("data\\textures\\ui\\TCDXLogo.png");       // Main window title-bar emblem
    Tex::GetFileTex("data\\textures\\ui\\viewer-banner.png");   // Rich guide viewer scenic header
    WarmChecklist(app);   // checklist tree + every zone / region-union map (Zones + Checklist)
    WarmJournal(app);     // Story Journal location maps + tree + rewards + per-character My Story preload
    ItemCatalog::Warm();  // all-items catalog: /v2/build freshness check + delta (full fetch only if no seed/cache)
    SkinCatalog::Warm();  // wardrobe skin catalog: stale seed/cache now, diff /v2/skins + top-up new skins in background
    CraftingEngine::Warm(); // recipe catalog: /v2/build freshness check + delta-fetch new official crafting recipes
    TpEligibility::Warm(); // official TP id list: stale cache now, fresh small id list in the background
    CraftKnown::Warm(); // learned recipes (account + every character): stale cache now, refreshed in the background
    MarketData::Warm();   // datawars2 whole-market snapshot for Flip Finder: stale cache now, fresh in background
    StaticData::WarmCurrencies();   // wallet-currency catalog (names/icons) for the Wallet/Session surfaces
}
