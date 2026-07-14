#include "HudButtons.h"
#include <string>

const std::vector<HudButtons::Button>& HudButtons::All()
{
    // Each button carries its own default: defaultOn (shown), defaultSide (0 Left / 1 Right), defaultOrder (bar order).
    static const std::vector<Button> r = {
        //  key            label           glyph                     on     side  order
        { "settings",  "Settings",     Render::Glyph::Cog,       true,  1,    1 },
        { "atlas",     "Atlas",        Render::Glyph::Map,       true,  0,    2 },
        { "items",     "Items",        Render::Glyph::Items,     true,  0,    4 },
        { "inventory", "Inventory",    Render::Glyph::Bag,       true,  1,    9 },
        { "collections","Collections", Render::Glyph::Star,      true,  1,    6 },
        { "tradingpost","Trading Post",Render::Glyph::Coins,     false, 0,    0 },
        { "journal",   "Journal",      Render::Glyph::Guide,     true,  0,    7 },
        { "wiki",      "Wiki",         Render::Glyph::Globe,      true,  0,   10 },
        { "checklist", "Checklist",    Render::Glyph::Checklist, false, 0,    0 },
        { "dungeons",  "Dungeons",     Render::Glyph::Dungeon,   true,  0,    8 },
        { "timers",    "Timers",       Render::Glyph::Clock,     true,  1,    3 },
        { "sessions",  "Sessions",     Render::Glyph::Chart,     true,  1,    5 },
        { "cart",      "Crafting Cart",Render::Glyph::Cart,      false, 0,    0 },
        { "dashboard", "Dashboard",    Render::Glyph::Grid,      false, 0,    0 },
        { "guide",     "Toggle guide", Render::Glyph::Navigate,  false, 0,    0 },
    };
    return r;
}

const HudButtons::Button* HudButtons::Find(const char* key)
{
    if (!key) return nullptr;
    for (const Button& b : All()) if (std::string(b.key) == key) return &b;
    return nullptr;
}
