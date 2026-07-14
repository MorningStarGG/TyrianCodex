#pragma once
class App;

// The Collections "Recipes" scope (P4, the last + biggest piece): a Discipline -> output-Rarity left-rail browser
// over the WHOLE crafting-recipe catalog (CraftingEngine::All -- official crafting + Mystic Forge). Each row is the
// recipe's OUTPUT item (icon + name in its rarity color) with the discipline + min crafting rating and a learned/
// unknown status (AccountData /v2/account/recipes); the tooltip lists the full ingredients, the right-click menu
// copies the recipe + item chat links. A CUSTOM Collections body (NOT a CosmeticCatalog kind).
void DrawRecipesContent(App& app);
