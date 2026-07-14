#include "util/Textures.h"
#include "Shared.h"
#include "util/ImageCache.h"
#include "util/OwnedTex.h" // addon-owned D3D11 textures (replaces Nexus's texture API for bundled files)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace
{
    std::string ReadAllBytes(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return {};
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
}

// A GW2 dat asset id maps to https://assets.gw2dat.com/<id>.png on the community gw2dat CDN,
// addressed by the raw id alone (no per-file signature).
// We route it through ImageCache::GetUrl so the icon persists to DISK (not just
// Nexus's session-only URL cache) - same CDN, same ids, same images, now cached across launches.
const Texture_t *Tex::GetAssetTex(uint32_t assetId)
{
    // Prefer a bundled copy at data/textures/ui/dat/<id>.png (instant + offline, no cache duplication). This
    // lives HERE -- not just in Gw2Ui::Asset -- so EVERY caller of a dat id (dungeon / journal / checklist /
    // map-thumbnail icons that call GetAssetTex directly) gets the bundled file and none leaks a byte-identical
    // duplicate into cache/assets on the async-not-ready frames. Existence is cached per id (render thread).
    char rel[64];
    std::snprintf(rel, sizeof(rel), "data\\textures\\ui\\dat\\%u.png", assetId);
    static std::unordered_map<uint32_t, bool> s_bundled;
    auto it = s_bundled.find(assetId);
    const bool bundled = (it != s_bundled.end()) ? it->second : (s_bundled[assetId] = BundledFileExists(rel));
    if (bundled)
        return GetFileTex(rel);

    char texId[32];
    std::snprintf(texId, sizeof(texId), "TC_DAT_%u", assetId);
    char url[64];
    std::snprintf(url, sizeof(url), "https://assets.gw2dat.com/%u.png", assetId);
    return ImageCache::GetUrl(texId, url);
}

const Texture_t *Tex::GetFileTex(const char *relPath)
{
    if (!APIDefs || !relPath)
        return nullptr;

    const std::string id = std::string("TC_FILE_") + relPath;
    if (const Texture_t *t = OwnedTex::Get(id))
        return t; // already decoded + created (fast path)

    const char *dir = AddonDir();
    if (!dir)
        return nullptr;
    // A handful of small, persistent bundled UI/marker textures: read + decode + create ONCE on the render thread
    // (cached thereafter via OwnedTex::Get). Not evictable. A missing/undecodable file returns null (callers
    // null-check; retried next call, matching the old Nexus behaviour).
    const std::string bytes = ReadAllBytes(std::string(dir) + "\\" + relPath);
    if (bytes.empty())
        return nullptr;
    return OwnedTex::CreateNow(id, bytes.data(), bytes.size(), /*evictable*/ false); // render thread: decode + create now
}

bool Tex::BundledFileExists(const char *relPath)
{
    if (!APIDefs || !relPath)
        return false;
    const char *dir = AddonDir();
    if (!dir)
        return false;
    std::error_code ec;
    return std::filesystem::exists(std::string(dir) + "\\" + relPath, ec);
}

const Texture_t *Tex::GetLocalOrAsset(const char *relPath, uint32_t assetId)
{
    if (!relPath)
        return GetAssetTex(assetId);
    // Existence is checked once per path: the bundle is fixed at runtime, and this only runs on the render
    // thread (both callers draw), so a plain static cache needs no lock.
    static std::unordered_map<std::string, bool> s_exists;
    auto it = s_exists.find(relPath);
    const bool bundled = (it != s_exists.end()) ? it->second : (s_exists[relPath] = BundledFileExists(relPath));
    if (bundled)
        return GetFileTex(relPath); // disk only; null-this-frame is fine, and NEVER the CDN
    return GetAssetTex(assetId);    // genuinely not bundled -> CDN fallback
}

void *Tex::GetTextureFromAssetId(uint32_t assetId)
{
    const Texture_t *t = GetAssetTex(assetId);
    return t ? t->Resource : nullptr;
}

void *Tex::GetBundledTexture(const char *relPath)
{
    const Texture_t *t = GetFileTex(relPath);
    return t ? t->Resource : nullptr;
}

void *Tex::GetTextureFromURL(const char *identifier, const char *url)
{
    const Texture_t *t = ImageCache::GetUrl(identifier, url); // disk-cached (filename = hash of the URL)
    return t ? t->Resource : nullptr;
}
