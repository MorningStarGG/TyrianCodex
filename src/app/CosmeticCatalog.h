#pragma once
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// CosmeticCatalog: the bundled whole-game catalogs for the visual-cosmetic Collections scopes -- Mounts,
// Outfits, Gliders, Jade Bots, Skiffs, Novelties. Each /v2/<cat> entry's display fields (id/name/icon + a
// per-category secondary `sub`), so the grids browse unlocked-vs-locked fully offline. Shipped as builder-
// generated bundled seeds (data/<cat>.json, by build_cosmetics.py); these sets are small + stable so there is
// NO runtime Warm()/delta (unlike SkinCatalog/ItemCatalog). The account's UNLOCKED set is separate + live
// (AccountData Dom<Cat>). Loaded synchronously at startup; main-thread only.
// -----------------------------------------------------------------------------------------------------
namespace CosmeticCatalog
{
    enum class Kind { Mounts, Outfits, Gliders, JadeBots, Skiffs, Novelties,
                      Finishers, MailCarriers, Minis, MistChampions, Titles, Emotes, Count };

    struct Cosmetic
    {
        int id = 0;
        int unlockItem = 0;            // the cosmetic's unlock-item id (0 = none); enables the chat-link copy
        bool isDefault = false;        // emotes: an always-available DEFAULT emote (shown "Default", not Locked)
        std::string name, icon, sub;   // sub: mounts -> mount type; novelties -> slot kind; mistchampions -> hero name; emotes -> command
        std::string keyLower;          // precomputed lowercased haystack (name + sub)
        bool premium = false;          // gem-store / Black-Lion sourced, paid-only (build_cosmetic_premium.py); drives the Free/Premium bars
    };

    // dataDir = <addon>\data (bundled seeds). Loads each <cat>.json by kind.
    void Init(const std::string& dataDir);
    void Shutdown();

    bool      IsReady();
    int       Count(Kind k);
    uint64_t  Version();   // bumps on (re)load (query-cache token)

    const std::vector<Cosmetic>& Items(Kind k);   // main-thread only
    const Cosmetic&               ById(Kind k, int id);

    // A few multi-outcome cosmetics (the random-transformation tonics) have NO single appearance -- each outcome is a
    // separate FRAME in cosmetic_renders.pack ("<kind>_<id>" = frame 0, "<kind>_<id>@N" = the rest) + recorded in
    // data/cosmetic_anim.json (build_cosmetic_anim.py). AnimFor() is null for the static majority; when present, the
    // tooltip cycles ImageCache::GetCosmeticRenderFrame(kind, id, FrameAt(time)) -- separate textures, own aspect.
    struct Anim { int frames = 0; std::vector<int> delayMs; int FrameAt(double seconds) const; };
    const Anim* AnimFor(Kind k, int id);
}
