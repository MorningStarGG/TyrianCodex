#pragma once
class App;

// Collections > Wardrobe "Legendary" sub-chip: the account-wide legendary-armory catalog (/v2/legendaryarmory)
// drawn as an icon grid with an owned-vs-max badge per item. Owned counts come from the AccountData
// DomLegendaryArmory refresh (/v2/account/legendaryarmory); item art/names reuse the ItemCatalog + the shared
// ItemUI cell renderer. Dispatched from DrawWardrobeContent.
void DrawLegendaryArmoryContent(App& app);
