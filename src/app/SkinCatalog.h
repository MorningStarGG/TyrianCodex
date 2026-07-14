#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace Api { class Client; }

// -----------------------------------------------------------------------------------------------------
// SkinCatalog: the whole-game wardrobe skin database (the Items-tab "Wardrobe" scope's data layer). Holds
// every /v2/skins entry's display/search fields, so the gallery browses unlocked-vs-locked skins fully
// offline. Shipped as a builder-generated bundled seed (data/skins.json) and merged into a writable runtime
// cache (cache/skins.json); Warm() diffs the live /v2/skins id list and tops up only NEW skins after a game
// patch -- the same seed + delta + cache model as ItemCatalog. The account's UNLOCKED set is separate + live
// (AccountData DomSkins). Shares the one Api::Client + main-thread Pump, so no locking.
// -----------------------------------------------------------------------------------------------------
namespace SkinCatalog
{
    struct Skin
    {
        int id = 0;
        std::string name, icon, rarity, type, subtype, weight, set;   // type: Armor/Weapon/Back/Gathering; subtype: slot/weapon-type/tool; set: armor set (data/skin_sets.json)
        std::vector<std::string> flags;
        std::string keyLower;   // precomputed lowercased haystack (name + type + subtype + rarity)
        bool wardrobe = false;  // flags contains "ShowInWardrobe" (only these are gallery-listable)
        bool premium  = false;  // data/skin_premium.json: gem-store-only (no in-game route); else free
    };

    // dataDir = <addon>\data (bundled seed), cacheDir = <addon>\cache (writable). Loads the cache if present,
    // else the bundled seed (synchronous; the ~2 MB seed parses fast). Both stale until Warm() tops up.
    void Init(Api::Client* api, const std::string& dataDir, const std::string& cacheDir);
    void Shutdown();
    void Warm();   // login (background callbacks): diff /v2/skins ids, fetch + merge any new ones, save the cache

    bool      IsReady();
    bool      Refreshing();
    int       Count();
    uint64_t  Version();   // bumps whenever the catalog set changes (query-cache token)

    const std::vector<Skin>& Items();   // main-thread only
    const Skin&               ById(int id);

    // A few wardrobe "renders" are animated APNGs the builder (build_skin_anim.py) flattened to a HORIZONTAL sprite
    // sheet (cols = frame count) + recorded in data/skin_anim.json. ImageCache::GetSkinImage returns the SHEET
    // texture; the tooltip draws one frame's UV slice on a timer (FrameAt). AnimFor() is null for the static majority.
    struct Anim { int frames = 0, w = 0, h = 0; std::vector<int> delayMs; int FrameAt(double seconds) const; };
    const Anim* AnimFor(int id);   // nullptr if the skin isn't animated
}
