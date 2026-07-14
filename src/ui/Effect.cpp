#include "ui/Effect.h"
#include "Shared.h"        // APIDefs (AddonAPI_t::SwapChain) + Texture_t (via Nexus.h)
#include "util/Textures.h" // Tex::GetAssetTex -> the mask/overlay SRVs

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <imgui.h> // ImGui::GetFrameCount + IM_ASSERT for the debug ring guard below

// Debug-only ring-overflow guard. Each per-frame payload ring (g_pool*) wraps by index, so submitting MORE than
// its capacity of ONE effect in a SINGLE frame would overwrite a payload before its draw callback runs at
// RenderDrawData. No live path approaches the caps (sized with headroom), but this catches a future regression
// in debug builds -- ImGui's monotonic frame counter auto-detects the frame boundary; compiled out in Release.
#ifndef NDEBUG
#define TC_RING_GUARD(pool, what)                                                           \
    do {                                                                                    \
        static int _rgFrame = -1, _rgCount = 0;                                             \
        const int _rgNow = ImGui::GetFrameCount();                                          \
        if (_rgNow != _rgFrame) { _rgFrame = _rgNow; _rgCount = 0; }                        \
        IM_ASSERT(_rgCount < static_cast<int>(sizeof(pool) / sizeof((pool)[0])) && (what)); \
        ++_rgCount;                                                                         \
    } while (0)
#else
#define TC_RING_GUARD(pool, what) ((void)0)
#endif

namespace
{
    // -- D3D handles, lazily pulled from Nexus's swap chain ----------------------
    ID3D11Device *g_dev = nullptr;
    ID3D11DeviceContext *g_ctx = nullptr;
    ID3D11PixelShader *g_ps = nullptr;
    ID3D11Buffer *g_cb = nullptr;
    ID3D11SamplerState *g_clamp = nullptr; // CLAMP-addressed sampler for the menuitem reveal (see SetupCB)
    bool g_inited = false;
    bool g_failed = false;

    // Second effect: the Zone Display gold sheen sweep (its own PS + cbuffer, lazily compiled).
    ID3D11PixelShader *g_ps2 = nullptr;
    ID3D11Buffer *g_cb2 = nullptr;
    bool g_inited2 = false;
    bool g_failed2 = false;

    // Third effect: the Zone Display per-letter "Dissolve" (samples a glyph's alpha + a value-noise threshold).
    ID3D11PixelShader *g_ps3 = nullptr;
    ID3D11Buffer *g_cb3 = nullptr;
    bool g_inited3 = false;
    bool g_failed3 = false;

    // Fourth effect: the Zone Display whole-banner "Burn". Needs its
    // own Mirror-addressed sampler so the perlin's zoomed octave samples wrap into noise.
    ID3D11PixelShader *g_ps4 = nullptr;
    ID3D11Buffer *g_cb4 = nullptr;
    ID3D11SamplerState *g_mirror = nullptr;
    bool g_inited4 = false;
    bool g_failed4 = false;

    // cbuffer layout (16-byte aligned) mirroring the HLSL `params` at register(b0).
    struct Params
    {
        float Roller;
        float Opacity;
        float Pad[2];
    };
    struct Params2
    {
        float Phase;
        float Intensity;
        float Alpha;
        float Band;
    };
    struct Params3
    {
        float Threshold;
        float NoiseScale;
        float EdgeW;
        float Alpha;
        float Tint[4];
        float Edge[4];
    }; // 48 bytes (16-aligned)
    struct Params4
    {
        float Amount;
        float Opacity;
        float Mode;
        float Glow;
        float GlowColor[4];
        float GlowRange;
        float GlowFalloff;
        float Hot;
        float Pad0;
        float Rect[4];
    }; // 64 bytes (16-aligned). Rect = (minX, minY, 1/w, 1/h) for Mode 1.

    // The sheen pixel shader: a procedural moving gold band (gaussian across uv.x, soft vertical falloff),
    // alpha-blended as a light wash. Ignores the bound texture (any quad triggers it) -- the band is computed
    // entirely from uv + the cbuffer phase. ps_4_0_level_9_1 to match Nexus's feature target.
    const char *kSheenShader = R"hlsl(
cbuffer params : register(b0) { float Phase; float Intensity; float Alpha; float Band; };
float4 main(float4 pos : SV_POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) : SV_TARGET
{
    float d    = uv.x - Phase;
    float band = exp(-(d * d) / max(Band, 0.0005));
    float vy   = 1.0 - smoothstep(0.30, 0.5, abs(uv.y - 0.5));
    float a    = saturate(band) * vy * Intensity * Alpha;
    float3 gold = float3(1.0, 0.93, 0.62);
    return float4(gold, a);
}
)hlsl";

    // The dissolve PS: sample the bound texture's ALPHA as the shape (a font-atlas glyph, or a rendered banner),
    // erase pixels where screen-space value-noise < Threshold, and glow an ember edge just above the front. Used
    // for both the per-letter "Burn (dissolve)" and the whole-banner "Full dissolve". ps_4_0_level_9_1.
    const char *kDissolveShader = R"hlsl(
cbuffer params : register(b0) { float Threshold; float NoiseScale; float EdgeW; float Alpha; float4 Tint; float4 Edge; };
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
float h21(float2 q){ return frac(sin(dot(q, float2(127.1, 311.7))) * 43758.5453); }
float vnoise(float2 p){
    float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
    float a = h21(i), b = h21(i + float2(1,0)), c = h21(i + float2(0,1)), d = h21(i + float2(1,1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float4 main(float4 pos : SV_POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) : SV_TARGET
{
    float  cov  = tex.Sample(samp, uv).a;             // coverage (the font-atlas glyph)
    float  nz   = vnoise(pos.xy * NoiseScale);
    float  body = step(Threshold, nz);                // erase where the noise is below the rising threshold
    float  edge = smoothstep(Threshold, Threshold + EdgeW, nz) * (1.0 - smoothstep(Threshold + EdgeW, Threshold + 2.0 * EdgeW, nz));
    float3 c = lerp(Edge.rgb, Tint.rgb, saturate((nz - Threshold) / 0.30));   // ember-hot just above the front
    float  a = cov * (body * Tint.a + edge * Edge.a);
    return float4(c, a * Alpha);
}
)hlsl";

    // Dissolve applied PER GLYPH each char as its own atlas sprite, so the shader gets
    // the glyph's atlas UV. dissolve_value selects the source of the diagonal/noise: Mode 0 = the glyph's atlas
    // UV (x+y in atlas space -> the "decode" scramble, glyphs resolve by atlas position); Mode 1 = a spatial
    // diagonal across the whole banner via Rect (the Slide/Twist wipe); Mode 2 = GeneratePerlinNoise (the burn).
    // Straight-alpha: coverage = atlas .a, colour = the AddImage vertex tint.
    const char *kBurnShader = R"hlsl(
cbuffer params : register(b0) { float Amount; float Opacity; float Mode; float Glow; float4 GlowColor; float GlowRange; float GlowFalloff; float Hot; float Pad0; float4 Rect; };
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
float h21(float2 q) { return frac(sin(dot(q, float2(127.1, 311.7))) * 43758.5453); }
float vnoise(float2 p)   // smooth value noise; varies finely WITHIN each glyph and is continuous across the banner
{
    float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
    float a = h21(i), b = h21(i + float2(1, 0)), c = h21(i + float2(0, 1)), d = h21(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float4 main(float4 pos : SV_POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) : SV_TARGET
{
    if (Amount >= 1.0) discard;
    float cov = tex.Sample(samp, uv).a;                // glyph coverage (straight alpha from the font atlas)
    if (cov < 0.003) discard;
    float3 base = col.rgb;                             // text colour from the AddImage tint
    float  outA = cov * col.a * Opacity;
    if (Amount <= 0.0) return float4(base, outA);
    float dissolve_value;
    if (Mode < 0.5)        dissolve_value = (uv.x + uv.y) - Amount;                                   // 0: atlas slide (decode)
    else if (Mode < 1.5) { float2 np = (pos.xy - Rect.xy) * Rect.zw; dissolve_value = (np.x + np.y) - Amount; }  // 1: spatial slide
    else
    {
        // 2: 5-octave perlin "Burn". Samples the SOURCE text; our atlas rgb is flat white and large
        // (a glyph spans a tiny UV), so texture-sampled octaves are near-constant -> a flat flash. So octave 1 = the
        // glyph COVERAGE (erodes the strokes from their edges -> traces the letter) and the higher octaves are value
        // noise (fragments THROUGH the body, not just the outline). (noise + 2.5) / 5 remap.
        float noise = cov
                    + 0.5    * vnoise(pos.xy * 0.045)
                    + 0.25   * vnoise(pos.xy * 0.090)
                    + 0.125  * vnoise(pos.xy * 0.180)
                    + 0.0625 * vnoise(pos.xy * 0.360);
        dissolve_value = (noise + 2.5) / 5.0;
    }
    float isVisible = dissolve_value - Amount;
    clip(isVisible);
    if (Glow > 0.5)   // An orange ember band at the dissolve front, ADDED to the text colour
    {
        float isGlowing = smoothstep(GlowRange + GlowFalloff, GlowRange, isVisible);
        base += isGlowing * GlowColor.rgb;
    }
    return float4(base, outA);
}
)hlsl";

    // ImGui binds the AddImage texture (our MASK) to t0; we
    // bind the OVERLAY to t1. alpha_comb = lerp(mask.b - 1 - mask.r, 1, Roller): where it is > 0 the
    // pixel shows the gold overlay, else it stays transparent -- the organic left->right wipe. Written
    // branchless (step) so it compiles cleanly at the ps_4_0_level_9_1 feature target.
    const char *kPixelShader = R"hlsl(
cbuffer params : register(b0) { float Roller; float Opacity; float2 Pad; };
Texture2D    maskTex    : register(t0);
Texture2D    overlayTex : register(t1);
SamplerState samp       : register(s0);
float4 main(float4 pos : SV_POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) : SV_TARGET
{
    float4 mask    = maskTex.Sample(samp, uv);
    float4 overlay = overlayTex.Sample(samp, uv);
    float  alpha_comb = lerp(mask.b - 1.0 - mask.r, 1.0, Roller);
    float  keep = step(0.00001, alpha_comb);
    return overlay * Opacity * col.a * keep;
}
)hlsl";

    // Lazily pull the device + immediate context from Nexus's swap chain (shared by both effects). Idempotent.
    bool EnsureDevice()
    {
        if (g_dev && g_ctx)
            return true;
        if (!APIDefs || !APIDefs->SwapChain)
            return false;
        IDXGISwapChain *sc = static_cast<IDXGISwapChain *>(APIDefs->SwapChain);
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&g_dev))) || !g_dev)
        {
            g_dev = nullptr;
            return false;
        }
        g_dev->GetImmediateContext(&g_ctx);
        return g_ctx != nullptr;
    }

    void Init()
    {
        g_inited = true;
        if (!EnsureDevice())
        {
            g_failed = true;
            return;
        }

        ID3DBlob *code = nullptr;
        ID3DBlob *err = nullptr;
        HRESULT hr = D3DCompile(kPixelShader, std::strlen(kPixelShader), "menuitem",
                                nullptr, nullptr, "main", "ps_4_0_level_9_1", 0, 0, &code, &err);
        if (err)
            err->Release();
        if (FAILED(hr) || !code)
        {
            g_failed = true;
            return;
        }

        hr = g_dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps);
        code->Release();
        if (FAILED(hr))
        {
            g_failed = true;
            return;
        }

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Params);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_cb)))
        {
            g_failed = true;
            return;
        }

        // CLAMP sampler: the overlay (156071) is bright gold on its LEFT edge fading out to the right. ImGui's
        // default sampler WRAPS, so bilinear filtering at the row's right edge (uv.x ~= 1) bleeds the bright left
        // texel back in -> a bright vertical line where the reveal finishes. Clamp kills the wrap-around. Non-fatal:
        // if it fails we fall back to ImGui's sampler (SetupCB only binds it when present).
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        g_dev->CreateSamplerState(&sd, &g_clamp);
    }

    // The two effect textures, loaded LOCAL. We bundle 156071/156072 under
    // data/textures/ui so they resolve from disk in a frame or two and are never re-downloaded per
    // session -- the gw2dat CDN id is only a fallback if the bundled file is somehow missing. (Loading
    // these from the CDN was why the highlight "popped in" a few seconds after opening the window and
    // did it again on every relaunch.)
    const Texture_t *EffectTex(const char *file, uint32_t id)
    {
        // Bundled -> disk only (never re-download on the not-ready-yet frames -- that leaked 156071/156072
        // into cache/assets); only a missing bundled file falls through to the gw2dat CDN.
        return Tex::GetLocalOrAsset(file, id);
    }
    constexpr const char *kMaskFile = "data\\textures\\ui\\156072.png";
    constexpr const char *kOverlayFile = "data\\textures\\ui\\156071.png";

    // Per-draw payload, ring-buffered so it stays alive until ImGui renders the draw data this frame.
    struct Payload
    {
        float roller;
        ID3D11ShaderResourceView *overlay;
    };
    Payload g_pool[128];
    int g_poolIdx = 0;

    // ImGui callback: runs during RenderDrawData, right before the mask quad draws. Installs our PS,
    // pushes the roller into the cbuffer, and binds the overlay to t1 (the mask lands on t0 via the
    // AddImage that follows). A trailing ImDrawCallback_ResetRenderState restores ImGui's own state.
    void SetupCB(const ImDrawList *, const ImDrawCmd *cmd)
    {
        const Payload *p = static_cast<const Payload *>(cmd->UserCallbackData);
        if (!g_ctx || !g_ps || !g_cb || !p)
            return;

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            Params pr;
            pr.Roller = p->roller;
            pr.Opacity = 1.0f;
            pr.Pad[0] = pr.Pad[1] = 0.0f;
            std::memcpy(m.pData, &pr, sizeof(pr));
            g_ctx->Unmap(g_cb, 0);
        }
        g_ctx->PSSetShader(g_ps, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
        ID3D11ShaderResourceView *ov = p->overlay;
        g_ctx->PSSetShaderResources(1, 1, &ov); // t1 = overlay; t0 = mask (bound by the AddImage)
        if (g_clamp)
            g_ctx->PSSetSamplers(0, 1, &g_clamp); // ImDrawCallback_ResetRenderState restores ImGui's after
    }

    // -- Sheen effect (Zone Display glint) ---------------------------------------
    void Init2()
    {
        g_inited2 = true;
        if (!EnsureDevice())
        {
            g_failed2 = true;
            return;
        }

        ID3DBlob *code = nullptr;
        ID3DBlob *err = nullptr;
        HRESULT hr = D3DCompile(kSheenShader, std::strlen(kSheenShader), "zonesheen",
                                nullptr, nullptr, "main", "ps_4_0_level_9_1", 0, 0, &code, &err);
        if (err)
            err->Release();
        if (FAILED(hr) || !code)
        {
            g_failed2 = true;
            return;
        }

        hr = g_dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps2);
        code->Release();
        if (FAILED(hr))
        {
            g_failed2 = true;
            return;
        }

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Params2);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_cb2)))
        {
            g_failed2 = true;
            return;
        }
    }

    struct Payload2
    {
        float phase;
        float intensity;
        float alpha;
        float band;
    };
    Payload2 g_pool2[64];
    int g_pool2Idx = 0;

    void SetupCB2(const ImDrawList *, const ImDrawCmd *cmd)
    {
        const Payload2 *p = static_cast<const Payload2 *>(cmd->UserCallbackData);
        if (!g_ctx || !g_ps2 || !g_cb2 || !p)
            return;

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_cb2, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            Params2 pr;
            pr.Phase = p->phase;
            pr.Intensity = p->intensity;
            pr.Alpha = p->alpha;
            pr.Band = p->band;
            std::memcpy(m.pData, &pr, sizeof(pr));
            g_ctx->Unmap(g_cb2, 0);
        }
        g_ctx->PSSetShader(g_ps2, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb2);
    }

    // -- Dissolve effect (Zone Display burn) -------------------------------------
    void Init3()
    {
        g_inited3 = true;
        if (!EnsureDevice())
        {
            g_failed3 = true;
            return;
        }

        ID3DBlob *code = nullptr;
        ID3DBlob *err = nullptr;
        // ps_4_0 (NOT level_9_1): the value-noise dissolve exceeds 9_1's tiny instruction limit, so 9_1 silently
        // fails to compile -> the effect would fall back to a plain fade. Nexus's device is feature-level 11.
        HRESULT hr = D3DCompile(kDissolveShader, std::strlen(kDissolveShader), "zonedissolve",
                                nullptr, nullptr, "main", "ps_4_0", 0, 0, &code, &err);
        if (err)
            err->Release();
        if (FAILED(hr) || !code)
        {
            g_failed3 = true;
            return;
        }

        hr = g_dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps3);
        code->Release();
        if (FAILED(hr))
        {
            g_failed3 = true;
            return;
        }

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Params3);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_cb3)))
        {
            g_failed3 = true;
            return;
        }
    }

    Params3 g_pool3[128];
    int g_pool3Idx = 0;

    void SetupCB3(const ImDrawList *, const ImDrawCmd *cmd)
    {
        const Params3 *p = static_cast<const Params3 *>(cmd->UserCallbackData);
        if (!g_ctx || !g_ps3 || !g_cb3 || !p)
            return;
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_cb3, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            std::memcpy(m.pData, p, sizeof(Params3));
            g_ctx->Unmap(g_cb3, 0);
        }
        g_ctx->PSSetShader(g_ps3, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb3);
    }

    // -- Burn effect (Zone Display whole-banner) ----------
    void Init4()
    {
        g_inited4 = true;
        if (!EnsureDevice())
        {
            g_failed4 = true;
            return;
        }

        ID3DBlob *code = nullptr;
        ID3DBlob *err = nullptr;
        HRESULT hr = D3DCompile(kBurnShader, std::strlen(kBurnShader), "zoneburn",
                                nullptr, nullptr, "main", "ps_4_0", 0, 0, &code, &err);
        if (err)
            err->Release();
        if (FAILED(hr) || !code)
        {
            g_failed4 = true;
            return;
        }

        hr = g_dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g_ps4);
        code->Release();
        if (FAILED(hr))
        {
            g_failed4 = true;
            return;
        }

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Params4);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_cb4)))
        {
            g_failed4 = true;
            return;
        }

        // Mirror sampler so the perlin's zoomed (uv * up to 16) octave samples wrap into noise.
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
        if (FAILED(g_dev->CreateSamplerState(&sd, &g_mirror)))
        {
            g_failed4 = true;
            return;
        }
    }

    Params4 g_pool4[1024]; // large: the per-glyph dissolve issues one Params per glyph (hundreds per frame)
    int g_pool4Idx = 0;

    void SetupCB4(const ImDrawList *, const ImDrawCmd *cmd)
    {
        const Params4 *p = static_cast<const Params4 *>(cmd->UserCallbackData);
        if (!g_ctx || !g_ps4 || !g_cb4 || !p)
            return;
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(g_cb4, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            std::memcpy(m.pData, p, sizeof(Params4));
            g_ctx->Unmap(g_cb4, 0);
        }
        g_ctx->PSSetShader(g_ps4, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb4);
        if (g_mirror)
            g_ctx->PSSetSamplers(0, 1, &g_mirror); // ImDrawCallback_ResetRenderState restores ImGui's after
    }

    // -- 3D trail render pass (the in-world route ribbon) ------------------------
    // Our OWN vertex + pixel shader: the VS transforms WORLD geometry with the camera matrix, so the GPU does
    // projection, near-plane + frustum CLIPPING (free + correct -- no snap, no off-screen stretch) and
    // PERSPECTIVE-CORRECT texturing (no foreshorten "swim"). Own input layout + dynamic vertex buffer + cbuffer
    // (the view-proj as 4 explicit columns, multiplied exactly like our CPU Mat4::Transform) + sampler.
    ID3D11VertexShader *g_tvs = nullptr;
    ID3D11PixelShader *g_tps = nullptr;
    ID3D11InputLayout *g_til = nullptr;
    ID3D11Buffer *g_tvb = nullptr;         // dynamic vertex buffer (refilled each frame)
    ID3D11Buffer *g_tcb = nullptr;         // cbuffer b0: the view-proj as 4 columns
    ID3D11SamplerState *g_tsamp = nullptr; // clamp-U (0..1 across width), wrap-V (tiles along length)
    int g_tvbCap = 0;
    bool g_initedT = false;
    bool g_failedT = false;

    // 80 bytes: the 4 view-proj columns (VS) + the FadeCenter params (PS).
    struct TrailCB
    {
        float C0[4], C1[4], C2[4], C3[4];
        float PlayerScreen[2];
        float FadeRadius;
        float FadeOn;
    };

    const char *kTrailVS = R"hlsl(
cbuffer cam : register(b0) { float4 C0; float4 C1; float4 C2; float4 C3; float2 PlayerScreen; float FadeRadius; float FadeOn; }
struct VIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VOut main(VIn i)
{
    VOut o;
    o.pos = C0 * i.pos.x + C1 * i.pos.y + C2 * i.pos.z + C3;   // == our CPU Mat4::Transform (column-major)
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}
)hlsl";

    const char *kTrailPS = R"hlsl(
cbuffer cam : register(b0) { float4 C0; float4 C1; float4 C2; float4 C3; float2 PlayerScreen; float FadeRadius; float FadeOn; }
Texture2D    tex  : register(t0);
SamplerState samp : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0, float4 col : COLOR0) : SV_TARGET
{
    float4 o = tex.Sample(samp, uv) * col;   // straight-alpha tint; ImGui's alpha blend is already bound
    // "Fade Trails Around Character" (FadeCenter): a hole around the player ON SCREEN -- front AND back --
    // so the ribbon never paints over your character. pos.xy is this pixel's screen position. 
    // Falloff is the inner ~63% of the radius is pinned to the 0.075 floor (a SOLID near-transparent hole), 
    // and only the outer 37% ramps back to full. (Not a gentle smoothstep, which only ever dims faintly.)
    if (FadeOn > 0.5 && FadeRadius > 1.0)
    {
        float d = distance(pos.xy, PlayerScreen);
        if (d < FadeRadius) o.a *= clamp(lerp(-1.5, 1.0, d / FadeRadius), 0.075, 1.0);
    }
    return o;
}
)hlsl";

    void InitT()
    {
        g_initedT = true;
        if (!EnsureDevice())
        {
            g_failedT = true;
            return;
        }

        ID3DBlob *vsb = nullptr;
        ID3DBlob *err = nullptr;
        if (FAILED(D3DCompile(kTrailVS, std::strlen(kTrailVS), "trailvs", nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsb, &err)) || !vsb)
        {
            if (err)
                err->Release();
            g_failedT = true;
            return;
        }
        if (err)
        {
            err->Release();
            err = nullptr;
        }
        if (FAILED(g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_tvs)))
        {
            vsb->Release();
            g_failedT = true;
            return;
        }

        const D3D11_INPUT_ELEMENT_DESC il[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        HRESULT hr = g_dev->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_til);
        vsb->Release();
        if (FAILED(hr))
        {
            g_failedT = true;
            return;
        }

        ID3DBlob *psb = nullptr;
        if (FAILED(D3DCompile(kTrailPS, std::strlen(kTrailPS), "trailps", nullptr, nullptr, "main", "ps_4_0", 0, 0, &psb, &err)) || !psb)
        {
            if (err)
                err->Release();
            g_failedT = true;
            return;
        }
        if (err)
        {
            err->Release();
        }
        if (FAILED(g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_tps)))
        {
            psb->Release();
            g_failedT = true;
            return;
        }
        psb->Release();

        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth = sizeof(TrailCB);
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&cbd, nullptr, &g_tcb)))
        {
            g_failedT = true;
            return;
        }

        g_tvbCap = 24000; // ~4000 ribbon segments * 6 verts
        D3D11_BUFFER_DESC vbd = {};
        vbd.ByteWidth = (UINT)(g_tvbCap * (int)sizeof(Fx::TrailVert));
        vbd.Usage = D3D11_USAGE_DYNAMIC;
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_dev->CreateBuffer(&vbd, nullptr, &g_tvb)))
        {
            g_failedT = true;
            return;
        }

        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        g_dev->CreateSamplerState(&sd, &g_tsamp);
    }

    // One trail draw per frame; the VB/CB hold this frame's data when the callback runs during RenderDrawData.
    struct TrailDrawData
    {
        UINT vtxCount;
        ID3D11ShaderResourceView *tex;
    };
    TrailDrawData g_trailDraw{};

    void SetupTrailDraw(const ImDrawList *, const ImDrawCmd *cmd)
    {
        const TrailDrawData *p = static_cast<const TrailDrawData *>(cmd->UserCallbackData);
        if (!g_ctx || !g_tvs || !g_tps || !g_til || !g_tvb || !g_tcb || !p || p->vtxCount < 3)
            return;
        // ImGui sets the scissor per NORMAL draw command but NOT before a callback, so our custom Draw would
        // inherit whatever clip rect the previous UI element / marker left behind -- and that set + its order
        // changes as you pan the camera, cutting the trail to a stale sub-rectangle. Reset to the full viewport.
        const ImGuiIO &io = ImGui::GetIO();
        const D3D11_RECT full = {0, 0, (LONG)(io.DisplaySize.x + 0.5f), (LONG)(io.DisplaySize.y + 0.5f)};
        g_ctx->RSSetScissorRects(1, &full);
        UINT stride = (UINT)sizeof(Fx::TrailVert), offset = 0;
        g_ctx->IASetInputLayout(g_til);
        g_ctx->IASetVertexBuffers(0, 1, &g_tvb, &stride, &offset);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->VSSetShader(g_tvs, nullptr, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &g_tcb);
        g_ctx->PSSetShader(g_tps, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_tcb); // same cbuffer carries the FadeCenter params for the PS
        ID3D11ShaderResourceView *srv = p->tex;
        g_ctx->PSSetShaderResources(0, 1, &srv);
        if (g_tsamp)
            g_ctx->PSSetSamplers(0, 1, &g_tsamp);
        g_ctx->Draw(p->vtxCount, 0);
        // ImDrawCallback_ResetRenderState (queued right after) restores ImGui's shaders/IA/blend.
    }
}

void Fx::Shutdown()
{
    // Release in reverse acquisition order: our created objects first, then the AddRef'd context + device.
    if (g_tsamp)
    {
        g_tsamp->Release();
        g_tsamp = nullptr;
    }
    if (g_tvb)
    {
        g_tvb->Release();
        g_tvb = nullptr;
    }
    if (g_tcb)
    {
        g_tcb->Release();
        g_tcb = nullptr;
    }
    if (g_til)
    {
        g_til->Release();
        g_til = nullptr;
    }
    if (g_tps)
    {
        g_tps->Release();
        g_tps = nullptr;
    }
    if (g_tvs)
    {
        g_tvs->Release();
        g_tvs = nullptr;
    }
    if (g_mirror)
    {
        g_mirror->Release();
        g_mirror = nullptr;
    }
    if (g_cb4)
    {
        g_cb4->Release();
        g_cb4 = nullptr;
    }
    if (g_ps4)
    {
        g_ps4->Release();
        g_ps4 = nullptr;
    }
    if (g_cb3)
    {
        g_cb3->Release();
        g_cb3 = nullptr;
    }
    if (g_ps3)
    {
        g_ps3->Release();
        g_ps3 = nullptr;
    }
    if (g_cb2)
    {
        g_cb2->Release();
        g_cb2 = nullptr;
    }
    if (g_ps2)
    {
        g_ps2->Release();
        g_ps2 = nullptr;
    }
    if (g_clamp)
    {
        g_clamp->Release();
        g_clamp = nullptr;
    }
    if (g_cb)
    {
        g_cb->Release();
        g_cb = nullptr;
    }
    if (g_ps)
    {
        g_ps->Release();
        g_ps = nullptr;
    }
    if (g_ctx)
    {
        g_ctx->Release();
        g_ctx = nullptr;
    }
    if (g_dev)
    {
        g_dev->Release();
        g_dev = nullptr;
    }
    g_inited = false;
    g_failed = false; // allow a fresh Init() on re-enable without a full restart
    g_inited2 = false;
    g_failed2 = false;
    g_inited3 = false;
    g_failed3 = false;
    g_inited4 = false;
    g_failed4 = false;
    g_initedT = false;
    g_failedT = false;
}

void Fx::Warm()
{
    // Kick off the bundled-texture load at addon start so the highlight is ready before the window is
    // ever opened (no first-open pop-in). Safe to call before any render.
    EffectTex(kMaskFile, 156072);
    EffectTex(kOverlayFile, 156071);
}

void Fx::DrawTrail3D(ImDrawList *dl, const Fx::TrailVert *verts, int count, const float vpColMajor[16], void *texSrv,
                     float playerSX, float playerSY, float fadeRadiusPx, bool fadeOn)
{
    if (!dl || !verts || count < 3 || !texSrv)
        return;
    if (!g_initedT)
        InitT();
    if (g_failedT)
        return;
    if (count > g_tvbCap)
        count = g_tvbCap - (g_tvbCap % 3); // clamp to capacity (whole triangles)

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->Map(g_tvb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    std::memcpy(m.pData, verts, (size_t)count * sizeof(Fx::TrailVert));
    g_ctx->Unmap(g_tvb, 0);

    if (FAILED(g_ctx->Map(g_tcb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        return;
    {
        TrailCB cb; // vpColMajor is Math::Mat4::m in m[col][row] order -> [0..3]=col0, [4..7]=col1, ...
        for (int i = 0; i < 4; ++i)
        {
            cb.C0[i] = vpColMajor[0 + i];
            cb.C1[i] = vpColMajor[4 + i];
            cb.C2[i] = vpColMajor[8 + i];
            cb.C3[i] = vpColMajor[12 + i];
        }
        cb.PlayerScreen[0] = playerSX;
        cb.PlayerScreen[1] = playerSY;
        cb.FadeRadius = fadeRadiusPx;
        cb.FadeOn = fadeOn ? 1.f : 0.f;
        std::memcpy(m.pData, &cb, sizeof(cb));
    }
    g_ctx->Unmap(g_tcb, 0);

    g_trailDraw.vtxCount = (UINT)count;
    g_trailDraw.tex = static_cast<ID3D11ShaderResourceView *>(texSrv);
    dl->AddCallback(SetupTrailDraw, &g_trailDraw);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void Fx::ScrollingHighlight(ImDrawList *dl, ImVec2 a, ImVec2 b, float roller)
{
    if (roller <= 0.002f)
        return;
    if (!g_inited)
        Init();
    if (g_failed)
        return;

    const Texture_t *mask = EffectTex(kMaskFile, 156072);
    const Texture_t *overlay = EffectTex(kOverlayFile, 156071);
    if (!mask || !mask->Resource || !overlay || !overlay->Resource)
        return;

    TC_RING_GUARD(g_pool, "Effect: too many scrolling-highlight payloads in one frame (ring overwrite)");
    Payload *p = &g_pool[g_poolIdx];
    g_poolIdx = (g_poolIdx + 1) % static_cast<int>(sizeof(g_pool) / sizeof(g_pool[0]));
    p->roller = roller;
    p->overlay = static_cast<ID3D11ShaderResourceView *>(overlay->Resource);

    dl->AddCallback(SetupCB, p);
    dl->AddImage(static_cast<ImTextureID>(mask->Resource), a, b, ImVec2(0, 0), ImVec2(1, 1));
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

bool Fx::ZoneSheenReady()
{
    if (!g_inited2)
        Init2();
    return !g_failed2;
}

void Fx::ZoneSheen(ImDrawList *dl, ImVec2 a, ImVec2 b, float phase, float intensity, float alpha, float band)
{
    if (intensity <= 0.002f || alpha <= 0.002f)
        return;
    if (!g_inited2)
        Init2();
    if (g_failed2)
        return;

    // Any textured quad triggers the PS; the shader ignores the texel and computes the band from uv. Reuse
    // ImGui's font atlas (always present) so we need no extra asset.
    ImGuiIO &io = ImGui::GetIO();
    if (!io.Fonts || !io.Fonts->TexID)
        return;

    TC_RING_GUARD(g_pool2, "Effect: too many payload2 in one frame (ring overwrite)");
    Payload2 *p = &g_pool2[g_pool2Idx];
    g_pool2Idx = (g_pool2Idx + 1) % static_cast<int>(sizeof(g_pool2) / sizeof(g_pool2[0]));
    p->phase = phase;
    p->intensity = intensity;
    p->alpha = alpha;
    p->band = band;

    dl->AddCallback(SetupCB2, p);
    dl->AddImage(io.Fonts->TexID, a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

bool Fx::DissolveReady()
{
    if (!g_inited3)
        Init3();
    return !g_failed3;
}

void Fx::Dissolve(ImDrawList *dl, ImTextureID tex, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1,
                  float threshold, ImU32 tint, ImU32 edge, float noiseScale, float alpha)
{
    if (!tex || alpha <= 0.002f || threshold >= 1.f)
        return;
    if (!g_inited3)
        Init3();
    if (g_failed3)
        return;

    TC_RING_GUARD(g_pool3, "Effect: too many params3 in one frame (ring overwrite)");
    Params3 *p = &g_pool3[g_pool3Idx];
    g_pool3Idx = (g_pool3Idx + 1) % static_cast<int>(sizeof(g_pool3) / sizeof(g_pool3[0]));
    p->Threshold = threshold;
    p->NoiseScale = noiseScale;
    p->EdgeW = 0.06f;
    p->Alpha = alpha;
    auto unpack = [](ImU32 c, float out[4])
    {
        out[0] = (c & 0xFF) / 255.f;
        out[1] = ((c >> 8) & 0xFF) / 255.f;
        out[2] = ((c >> 16) & 0xFF) / 255.f;
        out[3] = ((c >> 24) & 0xFF) / 255.f;
    };
    unpack(tint, p->Tint);
    unpack(edge, p->Edge);

    dl->AddCallback(SetupCB3, p);
    dl->AddImage(tex, a, b, uv0, uv1, IM_COL32_WHITE);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

bool Fx::BurnReady()
{
    if (!g_inited4)
        Init4();
    return !g_failed4;
}

void Fx::DissolveGlyph(ImDrawList *dl, ImTextureID tex, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1,
                       ImU32 color, float amount, int mode, int glowKind, ImVec4 rect)
{
    if (!tex || amount >= 1.f || ((color >> 24) & 0xFF) == 0)
        return;
    if (!g_inited4)
        Init4();
    if (g_failed4)
        return;

    TC_RING_GUARD(g_pool4, "Effect: too many per-glyph dissolve params in one frame (ring overwrite)");
    Params4 *p = &g_pool4[g_pool4Idx];
    g_pool4Idx = (g_pool4Idx + 1) % static_cast<int>(sizeof(g_pool4) / sizeof(g_pool4[0]));
    p->Amount = amount;
    p->Opacity = 1.f; // the line opacity rides in the colour's alpha (cov * col.a * Opacity)
    p->Mode = (float)mode;
    p->Glow = (glowKind > 0) ? 1.f : 0.f;
    p->GlowColor[0] = 1.f;
    p->GlowColor[1] = 0.5f;
    p->GlowColor[2] = 0.f;
    p->GlowColor[3] = 1.f; // Orange (1, 0.5, 0)
    // Burn: a broader orange flame band (the gentle coverage+noise perlin spreads it across the burning letter)
    if (glowKind == 2)
    {
        p->GlowRange = 0.0f;
        p->GlowFalloff = 0.14f;
    }
    else
    {
        p->GlowRange = 0.04f;
        p->GlowFalloff = 0.05f;
    }
    p->Hot = 0.f;
    p->Pad0 = 0.f;
    p->Rect[0] = rect.x;
    p->Rect[1] = rect.y;
    p->Rect[2] = rect.z;
    p->Rect[3] = rect.w;

    dl->AddCallback(SetupCB4, p);
    dl->AddImage(tex, a, b, uv0, uv1, color); // tint = the text colour
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
