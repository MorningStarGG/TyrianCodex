#pragma once
class App;

// Collections-tab "Wardrobe" scope: a sub-view switcher over Skins (SkinCatalog), Dyes (app.dyes / colors.json),
// Outfits, and Legendary armory -- each an unlocked-vs-locked gallery with search, type filters, and unlock
// counts. Icons + color swatches only -- no 3D armor/character preview (no API for it). Dispatched from
// DrawCollectionsContent.
void DrawWardrobeContent(App& app);
