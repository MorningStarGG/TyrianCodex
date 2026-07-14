#pragma once
#include "api/core/Permissions.h"

// The ONE catalog of GW2 API scopes the guide uses + a plain-language note on what each unlocks. Shared by the
// Account/API settings section (full per-scope cards, ui/tabs/OptionsTab.cpp) and the first-run Welcome window
// (a compact list). Add a scope here once and both surfaces pick it up.
namespace ApiScopes
{
    struct Scope { Api::TokenPermission p; const char* name; const char* desc; };

    inline constexpr Scope kScopes[] = {
        { Api::TokenPermission::Account,     "account",
          "Account basics (total playtime and home world) on the Dashboard, plus the Wizard's Vault daily / "
          "weekly objectives. Also half of Story auto-completion (paired with 'progression')." },
        { Api::TokenPermission::Wvw,         "wvw",
          "World vs. World: your assigned WvW team -- powers the live match panel (score, skirmish, PPT, K/D and "
          "map objective control) across the Info Panel and dashboard widgets." },
        { Api::TokenPermission::Characters,  "characters",
          "Your characters and their levels -- this is what drives level-appropriate zone recommendations -- "
          "plus each character's bag space, equipped gear, and active-character detection." },
        { Api::TokenPermission::Progression, "progression",
          "Completion tracking: the Story Journal auto-check, achievement points, masteries, luck, raids / "
          "dungeons cleared, and daily completions (world bosses / map chests / daily crafting)." },
        { Api::TokenPermission::Inventories, "inventories",
          "The Items tab's account-storage browser: your bank, material storage, shared slots, and character "
          "bags (character bags also need 'characters')." },
        { Api::TokenPermission::Wallet,      "wallet",
          "Your currencies -- gold, karma, laurels, spirit shards and the rest -- on the Info Panel, the Wallet "
          "widget, and the Session Tracker." },
        { Api::TokenPermission::Tradingpost, "tradingpost",
          "Trading Post 'to collect': gold and items waiting for you, plus your open buy / sell order counts." },
        { Api::TokenPermission::Unlocks,     "unlocks",
          "Account unlocks used to mark what you already own: dyes, skins, minis, recipes, the full Collections "
          "set (mounts, outfits, gliders, titles and more), and Homestead decorations / glyphs / cats." },
    };
    inline constexpr int kScopeCount = (int)(sizeof(kScopes) / sizeof(kScopes[0]));
}
