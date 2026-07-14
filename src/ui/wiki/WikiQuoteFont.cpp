#include "ui/wiki/WikiQuoteFont.h"
#include "Shared.h"

#include <imgui.h>
#include <d3d11.h>
#include <cstdio>
#include <string>

namespace
{
    ImFontAtlas* g_atlas = nullptr;
    ImFont* g_font = nullptr;
    ID3D11ShaderResourceView* g_srv = nullptr;
    bool g_failed = false;

    static const ImWchar kGlyphRanges[] = {
        0x0020, 0x00FF,   // Basic Latin + Latin Supplement
        0x2010, 0x2026,   // General punctuation: dashes, curly quotes, ellipsis
        0
    };

    std::string BundledQuoteFontPath()
    {
        const char* root = AddonDir();
        if (root && *root)
            return std::string(root) + "\\data\\fonts\\TimesNewerRoman-Bold.otf";
        return "data\\fonts\\TimesNewerRoman-Bold.otf";
    }

    void ReleaseAtlas()
    {
        g_font = nullptr;
        if (g_srv)
        {
            g_srv->Release();
            g_srv = nullptr;
        }
        if (g_atlas)
        {
            IM_DELETE(g_atlas);
            g_atlas = nullptr;
        }
    }

    bool BuildAtlas()
    {
        if (g_font) return true;
        if (g_failed || !APIDefs || !APIDefs->SwapChain) return false;

        IDXGISwapChain* swap = static_cast<IDXGISwapChain*>(APIDefs->SwapChain);
        ID3D11Device* device = nullptr;
        if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device)
        {
            g_failed = true;
            if (APIDefs && APIDefs->Log) APIDefs->Log(LOGL_WARNING, "Tyrian Codex", "Wiki quote font: failed to get D3D11 device.");
            return false;
        }

        const std::string fontPath = BundledQuoteFontPath();
        if (GetFileAttributesA(fontPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            device->Release();
            g_failed = true;
            if (APIDefs && APIDefs->Log) APIDefs->Log(LOGL_WARNING, "Tyrian Codex", "Wiki quote font: bundled TimesNewerRoman-Bold.otf was not found.");
            return false;
        }

        ImFontAtlas* atlas = IM_NEW(ImFontAtlas)();
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        std::snprintf(cfg.Name, IM_ARRAYSIZE(cfg.Name), "TC Wiki Times Newer Roman Bold");

        ImFont* font = atlas->AddFontFromFileTTF(fontPath.c_str(), 96.f, &cfg, kGlyphRanges);
        if (!font)
        {
            IM_DELETE(atlas);
            device->Release();
            g_failed = true;
            if (APIDefs && APIDefs->Log) APIDefs->Log(LOGL_WARNING, "Tyrian Codex", "Wiki quote font: failed to load Times Newer Roman Bold.");
            return false;
        }

        unsigned char* pixels = nullptr;
        int width = 0, height = 0, bytesPerPixel = 0;
        atlas->GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);
        if (!pixels || width <= 0 || height <= 0 || bytesPerPixel != 4)
        {
            IM_DELETE(atlas);
            device->Release();
            g_failed = true;
            if (APIDefs && APIDefs->Log) APIDefs->Log(LOGL_WARNING, "Tyrian Codex", "Wiki quote font: failed to rasterize Times atlas.");
            return false;
        }
        if (!font->FindGlyphNoFallback((ImWchar)0x201C))
        {
            IM_DELETE(atlas);
            device->Release();
            g_failed = true;
            if (APIDefs && APIDefs->Log) APIDefs->Log(LOGL_WARNING, "Tyrian Codex", "Wiki quote font: Times atlas does not contain U+201C.");
            return false;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels;
        sub.SysMemPitch = static_cast<UINT>(width * 4);

        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = device->CreateTexture2D(&desc, &sub, &texture);
        if (FAILED(hr) || !texture)
        {
            IM_DELETE(atlas);
            device->Release();
            g_failed = true;
            if (APIDefs && APIDefs->Log) APIDefs->Log(LOGL_WARNING, "Tyrian Codex", "Wiki quote font: failed to create D3D texture.");
            return false;
        }

        ID3D11ShaderResourceView* srv = nullptr;
        hr = device->CreateShaderResourceView(texture, nullptr, &srv);
        texture->Release();
        device->Release();
        if (FAILED(hr) || !srv)
        {
            IM_DELETE(atlas);
            g_failed = true;
            if (APIDefs && APIDefs->Log) APIDefs->Log(LOGL_WARNING, "Tyrian Codex", "Wiki quote font: failed to create D3D shader resource view.");
            return false;
        }

        atlas->TexID = static_cast<ImTextureID>(srv);
        g_atlas = atlas;
        g_font = font;
        g_srv = srv;
        return true;
    }
}

ImFont* Wiki::QuoteFont()
{
    return BuildAtlas() ? g_font : nullptr;
}

void Wiki::ShutdownQuoteFont()
{
    ReleaseAtlas();
    g_failed = false;
}
