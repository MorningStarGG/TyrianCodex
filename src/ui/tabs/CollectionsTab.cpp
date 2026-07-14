// CollectionsTab: the registered "Collections" tab -- a PURE ORCHESTRATOR. It owns the scope-chip rail and
// dispatches to the per-scope bodies in the sibling *Section.cpp files (Wardrobe / Homestead / Fishing /
// Cosmetics / Recipes); it has no gallery body of its own.
#include "ui/tabs/CollectionsTab.h"

#include "app/App.h"
#include "app/AccountData.h"               // AccountData::Dom* (the per-scope poll mask)
#include "app/CosmeticCatalog.h"           // CosmeticCatalog::Kind (the visual-cosmetic scopes)
#include "ui/Gw2Ui.h"
#include "ui/SettingsWindow.h"                 // OpenSettingsTab + SettingsTabCollections (scope-aware opener)
#include "ui/tabs/WardrobeSection.h"           // DrawWardrobeContent (Wardrobe scope)
#include "ui/tabs/HomesteadSection.h"          // DrawHomesteadContent (Homestead scope)
#include "ui/tabs/FishingSection.h"            // DrawFishingContent (Fishing scope)
#include "ui/tabs/CosmeticsSection.h"          // DrawCosmeticScope (Mounts..Novelties scopes)
#include "ui/tabs/RecipesSection.h"            // DrawRecipesContent (Recipes scope -- a custom, non-cosmetic body)

#include <imgui.h>

namespace
{
    // scope ids. 0-2 host the existing browsers; 3-12 are the P2 visual cosmetics + P3 icon collections (one
    // CosmeticCatalog Kind each), dispatched through the generic DrawCosmeticScope.
    enum Scope { ScWardrobe = 0, ScHomestead, ScFishing,
                 ScMounts, ScGliders, ScJadeBots, ScSkiffs, ScNovelties,
                 ScFinishers, ScMailCarriers, ScMinis, ScMistChampions, ScTitles, ScEmotes, ScRecipes, ScCount };
    int g_collScope = ScWardrobe;

    // catalog Kind for a cosmetic scope (>= ScMounts).
    CosmeticCatalog::Kind KindFor(int scope)
    {
        switch (scope)
        {
            case ScMounts:    return CosmeticCatalog::Kind::Mounts;
            case ScGliders:   return CosmeticCatalog::Kind::Gliders;
            case ScJadeBots:  return CosmeticCatalog::Kind::JadeBots;
            case ScSkiffs:    return CosmeticCatalog::Kind::Skiffs;
            case ScNovelties: return CosmeticCatalog::Kind::Novelties;
            case ScFinishers:     return CosmeticCatalog::Kind::Finishers;
            case ScMailCarriers:  return CosmeticCatalog::Kind::MailCarriers;
            case ScMinis:         return CosmeticCatalog::Kind::Minis;
            case ScMistChampions: return CosmeticCatalog::Kind::MistChampions;
            case ScTitles:        return CosmeticCatalog::Kind::Titles;
            case ScEmotes:        return CosmeticCatalog::Kind::Emotes;
            default:          return CosmeticCatalog::Kind::Count;
        }
    }
}

void DrawCollectionsContent(App& app)
{
    // scope chips: a width-fitted, WRAPPING row so the bar never overflows a narrow body (mirrors the Items tab's
    // scope-chip shell). Gw2Ui::ChipRow fits ~116px-wide chips per row (equal-share within each row) and reflows.
    static const Gw2Ui::ChipItem kScopeChips[] = {
        { "Wardrobe"  }, { "Homestead" }, { "Fishing"   },
        { "Mounts"    }, { "Gliders"   }, { "Jade Bots" }, { "Skiffs" }, { "Novelties" },
        { "Finishers" }, { "Mail Carriers" }, { "Minis" }, { "Mist Champions" },
        { "Titles"    }, { "Emotes" }, { "Recipes" },
    };
    Gw2Ui::ChipRow("##collScope", kScopeChips, ScCount, &g_collScope, 32.f, 18.f, 6.f, 116.f, true, 3, 5);
    Gw2Ui::Divider(0.f);

    if (g_collScope == ScHomestead) { DrawHomesteadContent(app); return; }
    if (g_collScope == ScFishing)   { DrawFishingContent(app);   return; }
    if (g_collScope == ScRecipes)   { DrawRecipesContent(app);   return; }   // custom body (NOT a CosmeticCatalog kind)
    if (g_collScope >= ScMounts)    { DrawCosmeticScope(app, KindFor(g_collScope)); return; }
    DrawWardrobeContent(app);
}

// Open the Collections tab at a scope (scope id 0..ScCount-1; see the Scope enum above; -1 = leave current) --
// mirrors OpenItemsTab; used by the tray menu's Collections submenu.
void OpenCollectionsTab(App& app, int scope)
{
    if (scope >= 0) g_collScope = scope;
    OpenSettingsTab(app, SettingsTabCollections);
}

// AccountData domains the active scope needs (polled only while shown), OR'd into the entry.cpp Tick mask.
// Fishing reads its caught state from FishData (app.fish), not AccountData -> it contributes no domain.
unsigned CollectionsNeededDomains(App&)
{
    switch (g_collScope)
    {
        case ScWardrobe:  return AccountData::DomSkins | AccountData::DomDyes | AccountData::DomOutfits | AccountData::DomLegendaryArmory;   // Wardrobe: skins + dyes + Outfits + Legendary sub-chips
        case ScHomestead: return AccountData::DomHomestead;                      // Homestead: owned status
        case ScFishing:   return 0u;                                            // Fishing: no AccountData domain
        case ScMounts:    return AccountData::DomMounts;
        case ScGliders:   return AccountData::DomGliders;
        case ScJadeBots:  return AccountData::DomJadeBots;
        case ScSkiffs:    return AccountData::DomSkiffs;
        case ScNovelties: return AccountData::DomNovelties;
        case ScFinishers:     return AccountData::DomFinishers;
        case ScMailCarriers:  return AccountData::DomMailCarriers;
        case ScMinis:         return AccountData::DomMinis;
        case ScMistChampions: return AccountData::DomMistChampions;
        case ScTitles:        return AccountData::DomTitles;
        case ScEmotes:        return AccountData::DomEmotes;
        case ScRecipes:       return AccountData::DomRecipes;
        default:          return 0u;
    }
}
