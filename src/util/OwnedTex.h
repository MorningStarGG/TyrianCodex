#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct Texture_t;   // Nexus texture VIEW (Width/Height/Resource=ID3D11ShaderResourceView*); full def in Shared.h

// -----------------------------------------------------------------------------------------------------
// OwnedTex: the addon OWNS its ImGui textures. We decode image bytes (stb_image) and create our own
// ID3D11Texture2D + shader-resource-view on Nexus's shared D3D11 device, hand ImGui a Texture_t whose
// Resource is that SRV, and Release it ourselves. Nexus's texture API has NO eviction -- it orphans
// textures in a session-long cache until the game restarts. Owning the lifecycle lets us free VRAM on
// demand (Diagnostics "Clear" / addon disable) and LRU-evict the UNBOUNDED tile + downloaded-icon caches.
//
// THREADING (critical): GW2's D3D11 device is single-threaded, so EVERY device call (CreateTexture2D /
// CreateShaderResourceView / Release) happens on the RENDER thread. The ImageCache workers only DECODE
// (stb, thread-safe) and queue the pixels via SubmitEncoded(); the GPU texture is created on the next
// render-thread Tick(). Bundled files that are already on the render thread use CreateNow() directly.
// Fonts + the Nexus tray icons stay Nexus-owned (referenced by Nexus by id, not drawn by us).
// -----------------------------------------------------------------------------------------------------
namespace OwnedTex
{
    void Init();         // acquire the D3D11 device from APIDefs->SwapChain (render thread; also lazily in Tick/CreateNow)
    void ReleaseAll();   // release EVERY texture + the device ref (AddonUnload -- after ImageCache::Shutdown, rendering stopped)
    bool Ready();        // device acquired?

    // WORKER/any-thread: decode `bytes` (PNG/JPEG/...) on the calling thread and QUEUE the pixels for GPU upload on
    // the next render-thread Tick(); the texture then appears via Get(). Dedups (no-op if already present/queued).
    // `evictable` marks the unbounded caches (tiles / downloaded icons) Tick() may LRU-evict. No device calls here.
    void SubmitEncoded(const std::string& id, const void* bytes, size_t size, bool evictable);

    // RENDER-thread ONLY: decode + create the texture IMMEDIATELY (a handful of small bundled UI/marker textures).
    // Returns its Texture_t view (or the existing one), or nullptr on failure / device-not-ready.
    const Texture_t* CreateNow(const std::string& id, const void* bytes, size_t size, bool evictable);

    const Texture_t* Get(const std::string& id);   // existing texture (marks it used this frame), else nullptr
    bool             Has(const std::string& id);    // already created (NOT counting queued-but-not-uploaded)
    bool             Wanted(const std::string& id); // created OR in-flight (decoding / pending upload) -- i.e. no
                                                    // need to (re)submit. False after an eviction, so ImageCache re-requests.

    // Live VRAM accounting for diagnostics: total addon-owned textures + their decoded byte footprint, the
    // evictable subset the byte budget bounds, and that budget.
    struct MemInfo { int textures = 0; size_t bytes = 0; size_t evictableBytes = 0; size_t budgetBytes = 0; };
    MemInfo Mem();

    void Evict(const std::string& id);              // release + forget one id (render thread)
    void Tick(int frame);                           // RENDER-thread, once per frame: upload queued decodes (budgeted)
                                                    // then LRU-evict STALE evictable textures once their total VRAM
                                                    // exceeds a byte budget (only ones unused for several frames, so
                                                    // no in-flight draw references them)
}
