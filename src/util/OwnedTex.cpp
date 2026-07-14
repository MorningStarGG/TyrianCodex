#include "util/OwnedTex.h"
#include "Shared.h"       // APIDefs (AddonAPI_t*) + Texture_t

#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO             // decode from memory only (no fopen)
#define STBI_NO_FAILURE_STRINGS   // we don't surface stb error text
#include "stb_image.h"    // vendored: third_party/stb (include dir added in CMakeLists). All common formats
                          // (PNG/JPEG/GIF/BMP/TGA/...) so we decode whatever Nexus/WIC used to.

namespace OwnedTex
{
    namespace
    {
        struct Owned
        {
            ID3D11Texture2D*          tex = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            Texture_t                 view{};      // {Width, Height, Resource=srv} handed to ImGui
            size_t                    bytes = 0;   // decoded VRAM footprint (w*h*4) -- the eviction budget unit
            bool                      evictable = false;
            int                       lastUsed = 0;
        };
        struct Pending   // a worker-decoded image awaiting render-thread GPU upload
        {
            std::string          id;
            std::vector<uint8_t> rgba;
            int                  w = 0, h = 0;
            bool                 evictable = false;
        };

        std::mutex                             s_mtx;      // guards ALL of the below
        std::unordered_map<std::string, Owned> s_tex;      // created textures (render-thread only touch the D3D objects)
        std::vector<Pending>                   s_pending;  // decoded, awaiting upload (worker push / render drain)
        std::unordered_set<std::string>        s_queued;   // ids in-flight (decoding or pending) -> dedup Submit
        ID3D11Device*                          s_dev = nullptr;
        int                                    s_frame = 0;

        // VRAM ceiling for evictable (tile / downloaded-icon / wiki) textures. Budgeting by BYTES, not count, bounds
        // the ACTUAL video memory regardless of the mix (a 64x64 icon is 16 KB; a 256x256 tile is 256 KB). ~128 MB
        // holds a large working set; the LRU pass keeps whatever's on screen / recent resident.
        constexpr size_t kEvictableVramBudget = 128ull * 1024 * 1024;
        constexpr int kSafetyFrames  = 4;      // never evict anything drawn within the last N frames (in-flight GPU)
        constexpr int kUploadsPerTick = 48;    // cap GPU creates per frame so a burst (Items tab open) can't hitch

        // Caller holds s_mtx. Acquire the shared device from Nexus's swap chain (idempotent). RENDER THREAD ONLY
        // (called from Tick/CreateNow) -- GW2's device is single-threaded, so all device use stays on one thread.
        bool EnsureDevLocked()
        {
            if (s_dev) return true;
            if (!APIDefs || !APIDefs->SwapChain) return false;
            IDXGISwapChain* sc = static_cast<IDXGISwapChain*>(APIDefs->SwapChain);
            if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_dev))) || !s_dev)
            { s_dev = nullptr; return false; }
            return true;
        }

        // Caller holds s_mtx AND is on the render thread. Create the immutable RGBA texture + SRV, register it.
        const Texture_t* UploadLocked(const std::string& id, const uint8_t* rgba, int w, int h, bool evictable)
        {
            if (!EnsureDevLocked() || !rgba || w <= 0 || h <= 0) return nullptr;
            D3D11_TEXTURE2D_DESC td{};
            td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = rgba; sd.SysMemPitch = (UINT)w * 4u;
            ID3D11Texture2D* tex = nullptr;
            if (FAILED(s_dev->CreateTexture2D(&td, &sd, &tex)) || !tex) return nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
            vd.Format = td.Format; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1;
            ID3D11ShaderResourceView* srv = nullptr;
            if (FAILED(s_dev->CreateShaderResourceView(tex, &vd, &srv)) || !srv) { tex->Release(); return nullptr; }
            Owned o; o.tex = tex; o.srv = srv; o.evictable = evictable; o.lastUsed = s_frame;
            o.bytes = (size_t)w * (size_t)h * 4u;
            o.view.Width = (uint32_t)w; o.view.Height = (uint32_t)h; o.view.Resource = srv;
            return &s_tex.emplace(id, o).first->second.view;
        }

        void ReleaseOwned(Owned& o)
        {
            if (o.srv) { o.srv->Release(); o.srv = nullptr; }
            if (o.tex) { o.tex->Release(); o.tex = nullptr; }
        }
    }

    void Init() { std::lock_guard<std::mutex> lk(s_mtx); EnsureDevLocked(); }
    bool Ready() { std::lock_guard<std::mutex> lk(s_mtx); return s_dev != nullptr; }

    void ReleaseAll()
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        for (auto& kv : s_tex) ReleaseOwned(kv.second);
        s_tex.clear(); s_pending.clear(); s_queued.clear();
        if (s_dev) { s_dev->Release(); s_dev = nullptr; }
    }

    void SubmitEncoded(const std::string& id, const void* bytes, size_t size, bool evictable)
    {
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            if (s_tex.count(id) || s_queued.count(id)) return;   // already created or in-flight
            if (!bytes || !size) return;
            s_queued.insert(id);
        }
        // Decode OUTSIDE the lock (stb is reentrant) -- this runs on an ImageCache worker, off the render thread.
        int w = 0, h = 0, comp = 0;
        stbi_uc* px = stbi_load_from_memory((const stbi_uc*)bytes, (int)size, &w, &h, &comp, 4);
        std::lock_guard<std::mutex> lk(s_mtx);
        // Leave a failed decode in s_queued as a tombstone (Wanted stays true) so a corrupt image is never retried
        // in a loop; it simply won't display. Cleared on ReleaseAll.
        if (!px || w <= 0 || h <= 0) { if (px) stbi_image_free(px); return; }
        Pending p; p.id = id; p.w = w; p.h = h; p.evictable = evictable;
        p.rgba.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        s_pending.push_back(std::move(p));
    }

    const Texture_t* CreateNow(const std::string& id, const void* bytes, size_t size, bool evictable)
    {
        {
            std::lock_guard<std::mutex> lk(s_mtx);
            auto it = s_tex.find(id);
            if (it != s_tex.end()) { it->second.lastUsed = s_frame; return &it->second.view; }
        }
        int w = 0, h = 0, comp = 0;
        stbi_uc* px = (bytes && size) ? stbi_load_from_memory((const stbi_uc*)bytes, (int)size, &w, &h, &comp, 4) : nullptr;
        if (!px) return nullptr;
        std::lock_guard<std::mutex> lk(s_mtx);
        const Texture_t* v = nullptr;
        auto it = s_tex.find(id);
        if (it != s_tex.end()) { it->second.lastUsed = s_frame; v = &it->second.view; }   // raced (another path made it)
        else v = UploadLocked(id, px, w, h, evictable);
        stbi_image_free(px);
        return v;
    }

    const Texture_t* Get(const std::string& id)
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        auto it = s_tex.find(id);
        if (it == s_tex.end()) return nullptr;
        it->second.lastUsed = s_frame;
        return &it->second.view;
    }

    bool Has(const std::string& id) { std::lock_guard<std::mutex> lk(s_mtx); return s_tex.count(id) != 0; }
    bool Wanted(const std::string& id) { std::lock_guard<std::mutex> lk(s_mtx); return s_tex.count(id) || s_queued.count(id); }

    MemInfo Mem()
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        MemInfo m; m.textures = (int)s_tex.size(); m.budgetBytes = kEvictableVramBudget;
        for (const auto& kv : s_tex) { m.bytes += kv.second.bytes; if (kv.second.evictable) m.evictableBytes += kv.second.bytes; }
        return m;
    }

    void Evict(const std::string& id)
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        auto it = s_tex.find(id);
        if (it == s_tex.end()) return;
        ReleaseOwned(it->second);
        s_tex.erase(it);
    }

    void Tick(int frame)
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        s_frame = frame;

        // 1) Upload worker-decoded images on THIS (render) thread -- budgeted so a big burst spreads over frames.
        if (!s_pending.empty())
        {
            int uploaded = 0;
            size_t i = 0;
            for (; i < s_pending.size() && uploaded < kUploadsPerTick; ++i)
            {
                Pending& p = s_pending[i];
                if (!s_tex.count(p.id)) UploadLocked(p.id, p.rgba.data(), p.w, p.h, p.evictable);
                s_queued.erase(p.id);
                ++uploaded;
            }
            s_pending.erase(s_pending.begin(), s_pending.begin() + i);   // drop the ones we processed this frame
        }

        // 2) LRU-evict evictable textures once their total VRAM exceeds the byte budget -- and only ones safely
        //    STALE (not drawn in the last few frames, so the GPU has finished the draws referencing their SRV).
        //    Budgeting by BYTES (not count) bounds the ACTUAL VRAM regardless of the tiny-icon vs big-tile mix.
        //    Bundled/pack art is not evictable and never counts here.
        size_t evictableBytes = 0;
        for (const auto& kv : s_tex) if (kv.second.evictable) evictableBytes += kv.second.bytes;
        if (evictableBytes <= kEvictableVramBudget) return;
        std::vector<std::unordered_map<std::string, Owned>::iterator> cand;
        for (auto it = s_tex.begin(); it != s_tex.end(); ++it)
            if (it->second.evictable && (frame - it->second.lastUsed) > kSafetyFrames) cand.push_back(it);
        std::sort(cand.begin(), cand.end(),
                  [](const auto& a, const auto& b) { return a->second.lastUsed < b->second.lastUsed; });
        for (auto it : cand)   // drop the least-recently-used stale textures until we're back under budget
        {
            if (evictableBytes <= kEvictableVramBudget) break;
            evictableBytes -= it->second.bytes;
            ReleaseOwned(it->second);
            s_tex.erase(it);
        }
    }
}
