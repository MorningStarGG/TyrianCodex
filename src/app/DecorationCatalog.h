#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// DecorationCatalog: the bundled Homestead collection CATALOGS for the Items-tab "Homestead" scope. Loads
// data/decorations.json (decorations + categories + glyphs + home cats + home nodes, built by
// builder/build_decorations.py) -- the static "everything that exists" lists. The LIVE owned status comes from
// AccountData (the /v2/account/homestead/* + /v2/account/home/* unlock sets); the UI joins the two for
// owned/missing. Per-decoration Handiwork recipe + source are OPTIONAL (filled by the builder's --recipes wiki
// scrape; the gallery ships without them). Owned by App as `app.decorations`; loaded once in AddonLoad.
// -----------------------------------------------------------------------------------------------------
class DecorationCatalog
{
public:
    struct RecipeIngredient { int itemId = 0; std::string name; int count = 0; };
    struct Decoration
    {
        int              id = 0;
        std::string      name, desc, icon;   // icon = a render-service URL (drawn via ImageCache::GetUrl)
        std::vector<int> categories;
        int              max = 0;            // max storable count
        std::string      source;             // acquisition class (Homesteading / Scribe / Vendor / ...) -- optional
        std::string      wiki;               // EXACT wiki page title (homestead "(Handiwork)" page) -- optional; the open target
        std::string      sheet;              // the Homestead Recipe Book that teaches the recipe -- optional
        int              handiwork = 0;      // Handiwork crafting rating -- optional (0 = unknown)
        int              recipeId = 0;       // wiki {{Recipe}} id -- the decoration's recipe chat link source (optional)
        std::vector<RecipeIngredient> recipe;// Handiwork ingredients [{name, count}] -- optional
        bool             premium = false;    // Black-Lion / gem-store decoration (data/decorations.json; build_premium.py BL-page pass)
    };

    // The page the wiki-open should target: the resolved "(Handiwork)" title when known, else the bare name.
    static std::string WikiTitle(const Decoration& d) { return d.wiki.empty() ? d.name : d.wiki; }
    struct Glyph { std::string id, slot; int itemId = 0; };   // id like "alchemy_harvesting"
    // A home-instance cat. The API ships only {id, hint}; the rest is scraped from the "Hungry cat scavenger hunt"
    // wiki table (row id == the API cat id): the cat you get (identity), the lure to feed, where, and the nearest
    // waypoint (resolved to a chat link + coord for click-to-travel). hint = the API food keyword (legacy label).
    struct Cat
    {
        int         id = 0;
        std::string hint, name, identity, lure, location, region, waypoint, wpChatLink, notes;
        float       cx = 0.f, cy = 0.f;
        uint32_t    mapId = 0;
    };
    struct Node  { std::string id, name; };                   // id like "iron_ore_node"

    void Load(const std::string& path);
    bool Empty() const { return decorations_.empty(); }

    const std::vector<Decoration>&    Decorations() const { return decorations_; }
    const std::vector<Glyph>&         Glyphs()      const { return glyphs_; }
    const std::vector<Cat>&           Cats()        const { return cats_; }
    const std::vector<Node>&          Nodes()       const { return nodes_; }
    const std::map<int, std::string>& Categories()  const { return categories_; }
    const Decoration*                 ById(int id)  const;            // null if absent
    std::string                       CategoryName(int id) const;

private:
    std::vector<Decoration>    decorations_;
    std::vector<Glyph>         glyphs_;
    std::vector<Cat>           cats_;
    std::vector<Node>          nodes_;
    std::map<int, std::string> categories_;
    std::map<int, int>         decoIndex_;   // decoration id -> index in decorations_
};
