#pragma once
#include <cstdint>

struct Texture_t; // from Nexus.h (Width / Height / Resource)

// Texture access, GetTextureFromAssetId(id) fetches these from the community gw2dat CDN at
//     https://assets.gw2dat.com/{id}.png - keyed by raw asset id, NO signature - so we point Nexus's
//     Textures_GetOrCreateFromURL at the CDN.
//     (ArenaNet's own render.guildwars2.com needs a per-file signature; gw2dat does not - that is why
//     we get away with just an id.)
//   - GetBundledTexture(relPath) - for our own bundled art (Footprints.png).
//
// The *Tex variants return the full Texture_t (Width/Height + Resource) so UI code can place a sprite at
// its native pixel size; the void* variants return just the shader-resource-view (ImGui ImTextureID).
// All return null until the texture has finished loading (cached by Nexus - call every frame, draw a
// fallback until ready).
namespace Tex
{
    void *GetTextureFromAssetId(uint32_t assetId);
    void *GetBundledTexture(const char *relPath); // relative to <addons>/TyrianCodex/
    // Arbitrary https image (e.g. a render.guildwars2.com achievement icon): splits the URL into host +
    // endpoint and loads it (cached, async) under `identifier`. Returns null until loaded - call every frame.
    void *GetTextureFromURL(const char *identifier, const char *url);

    const Texture_t *GetAssetTex(uint32_t assetId);   // gw2dat dat-id, with dimensions
    const Texture_t *GetFileTex(const char *relPath); // bundled file, with dimensions

    // True if a file is bundled at <addons>/TyrianCodex/<relPath>. Cheap; cache the result if hot.
    bool BundledFileExists(const char *relPath);

    // Load a UI asset that we ALSO ship as a bundled file. If the bundled file exists, this loads ONLY from
    // disk -- returning null for the frame or two the async GPU load takes, and NEVER falling back to the CDN
    // -- so a bundled icon is never spuriously re-downloaded into cache/assets on the not-ready-yet frames
    // (the bug where ui/dat + ui/ icons leaked into the cache). Only a genuinely ABSENT bundled file falls
    // through to the gw2dat CDN by `assetId`. Existence is checked once per path (render-thread only).
    const Texture_t *GetLocalOrAsset(const char *relPath, uint32_t assetId);
}
