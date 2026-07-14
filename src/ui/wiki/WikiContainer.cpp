#include "ui/wiki/WikiContainer.h"
#include "Shared.h"
#include "app/wiki/WikiService.h"
#include "ui/Gw2Ui.h"
#include "ui/wiki/WikiHtmlSanitizer.h"
#include "ui/wiki/WikiQuoteFont.h"
#include "util/ImageCache.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace
{
    float Snap(float v) { return std::floor(v + 0.5f); }
    ImVec2 Snap(ImVec2 v) { return ImVec2(Snap(v.x), Snap(v.y)); }

    uint64_t Fnv1a64(const std::string& s)
    {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        return h;
    }

    bool IsAllowedImageUrl(const std::string& url)
    {
        return Wiki::IsAllowedImageHost(url);   // single auditable allowlist (see WikiService.h)
    }

    std::string Upper(std::string s)
    {
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

    std::string Lower(std::string s)
    {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    std::string CapitalizeWords(std::string s)
    {
        bool word = false;
        for (char& c : s)
        {
            const unsigned char ch = (unsigned char)c;
            if (std::isalnum(ch))
            {
                c = word ? (char)std::tolower(ch) : (char)std::toupper(ch);
                word = true;
            }
            else word = false;
        }
        return s;
    }

    bool MatchBytes(const char* s, size_t i, const char* bytes, size_t n)
    {
        for (size_t k = 0; k < n; ++k)
            if ((unsigned char)s[i + k] != (unsigned char)bytes[k]) return false;
        return true;
    }

    size_t SeparatorBytesAt(const char* text, size_t i, size_t len)
    {
        const size_t rem = len - i;
        if (rem >= 2 && MatchBytes(text, i, "\xC2\xB7", 2)) return 2; // U+00B7 middle dot
        if (rem >= 3 && (MatchBytes(text, i, "\xE2\x80\xA2", 3) || // U+2022 bullet
                         MatchBytes(text, i, "\xE2\x80\xA7", 3) || // U+2027 hyphenation point
                         MatchBytes(text, i, "\xE2\x88\x99", 3) || // U+2219 bullet operator
                         MatchBytes(text, i, "\xE2\x8B\x85", 3) || // U+22C5 dot operator
                         MatchBytes(text, i, "\xE2\x97\x8F", 3)))  // U+25CF black circle
            return 3;
        return 0;
    }

    // Menomonia (our UI font) has no glyphs for most "smart" Unicode punctuation, so ImGui draws a '?'
    // fallback for each. Map the common offenders to ASCII before measure/draw so wiki prose reads
    // correctly (curly quotes -> straight, en/em dashes -> -/--, ellipsis -> ..., nbsp -> space, x).
    // Dot/bullet separators (U+2022, U+00B7, ...) are intentionally left untouched -- SeparatorBytesAt
    // turns those into a drawn dot.
    std::string NormalizePunct(const char* text)
    {
        if (!text) return {};
        const size_t len = std::strlen(text);
        std::string out;
        out.reserve(len);
        for (size_t i = 0; i < len;)
        {
            const unsigned char b0 = (unsigned char)text[i];
            if (b0 == 0xC2 && i + 1 < len && (unsigned char)text[i + 1] == 0xA0) { out.push_back(' '); i += 2; continue; }  // U+00A0 nbsp
            if (b0 == 0xC3 && i + 1 < len && (unsigned char)text[i + 1] == 0x97) { out.push_back('x'); i += 2; continue; }  // U+00D7 multiplication sign
            if (b0 == 0xE2 && i + 2 < len)
            {
                const unsigned char b1 = (unsigned char)text[i + 1];
                const unsigned char b2 = (unsigned char)text[i + 2];
                if (b1 == 0x80)
                {
                    switch (b2)
                    {
                        case 0x90: case 0x91: case 0x92: case 0x93: out.push_back('-'); i += 3; continue;   // U+2010..2013 hyphen/figure/en dash
                        case 0x94: case 0x95: out += "--"; i += 3; continue;                                 // U+2014 em dash / U+2015 horizontal bar
                        case 0x98: case 0x99: case 0x9A: case 0x9B: out.push_back('\''); i += 3; continue;   // U+2018..201B single quotes
                        case 0x9C: case 0x9D: case 0x9E: case 0x9F: out.push_back('"');  i += 3; continue;   // U+201C..201F double quotes
                        case 0xA6: out += "..."; i += 3; continue;                                           // U+2026 ellipsis
                        default: break;
                    }
                }
                else if (b1 == 0x88 && b2 == 0x92) { out.push_back('-'); i += 3; continue; }                  // U+2212 minus sign
                else if (b1 == 0x9D && (b2 == 0x9B || b2 == 0x9C)) { out.push_back('\''); i += 3; continue; } // U+275B/275C heavy single quotes
                else if (b1 == 0x9D && (b2 == 0x9D || b2 == 0x9E)) { out.push_back('"');  i += 3; continue; } // U+275D/275E heavy double quotes
            }
            out.push_back(text[i]);
            ++i;
        }
        return out;
    }

    // An oversized "pull-quote" decoration: GW2 wiki blockquotes drop a literal U+201C into a
    // position:absolute div at ~5em. Menomonia renders a big straight quote as two bars ("||"), so a LONE
    // large quote glyph is skipped entirely (the blockquote's left border already signals the quote).
    // Small inline prose quotes are mixed with text + at body size, so they are unaffected (mapped above).
    constexpr float kDecorativeQuotePx = 40.f;
    bool IsLoneQuoteRun(const char* text)
    {
        if (!text) return false;
        bool sawQuote = false;
        for (size_t i = 0; text[i];)
        {
            const unsigned char b = (unsigned char)text[i];
            if (b == ' ' || b == '\t' || b == '\n' || b == '\r') { ++i; continue; }
            if (b == '"' || b == '\'' || b == '`') { sawQuote = true; ++i; continue; }
            if (b == 0xE2 && text[i + 1] && text[i + 2])
            {
                const unsigned char b1 = (unsigned char)text[i + 1], b2 = (unsigned char)text[i + 2];
                if ((b1 == 0x80 && b2 >= 0x98 && b2 <= 0x9F) || (b1 == 0x9D && b2 >= 0x9B && b2 <= 0x9E))
                { sawQuote = true; i += 3; continue; }   // U+2018..201F curly + U+275B..275E heavy quotes
            }
            return false;   // any non-quote, non-space glyph -> not a lone-quote run
        }
        return sawQuote;
    }

}

namespace Wiki
{
    float Container::SeparatorAdvance(const FontRef* font)
    {
        return std::ceil(std::max(4.f, font ? font->size * 0.32f : 5.f));
    }

    float Container::TextWidthWithSeparators(const char* text, const FontRef* font)
    {
        if (!text || !font || !font->font) return 0.f;
        const size_t len = std::strlen(text);
        const char* begin = text;
        size_t segmentStart = 0;
        float width = 0.f;

        auto measureSegment = [&](size_t start, size_t end) {
            if (end <= start) return;
            width += font->font->CalcTextSizeA(font->size, FLT_MAX, 0.f, begin + start, begin + end).x;
        };

        for (size_t i = 0; i < len;)
        {
            const size_t sepBytes = SeparatorBytesAt(text, i, len);
            if (sepBytes)
            {
                measureSegment(segmentStart, i);
                width += SeparatorAdvance(font);
                i += sepBytes;
                segmentStart = i;
            }
            else ++i;
        }
        measureSegment(segmentStart, len);
        return width;
    }

    void Container::DrawTextWithSeparators(ImDrawList* dl, const FontRef* font, ImVec2 pos, ImU32 col,
                                           const char* text, bool bold)
    {
        if (!dl || !text || !font || !font->font) return;
        const size_t len = std::strlen(text);
        const char* begin = text;
        size_t segmentStart = 0;
        float x = pos.x;

        auto drawSegment = [&](size_t start, size_t end) {
            if (end <= start) return;
            const char* s = begin + start;
            const char* e = begin + end;
            const float drawX = Snap(x);
            ImTextureID tex = (font->font->ContainerAtlas ? font->font->ContainerAtlas->TexID : (ImTextureID)nullptr);
            const bool pushed = tex != nullptr;
            if (pushed) dl->PushTextureID(tex);
            dl->AddText(font->font, font->size, ImVec2(drawX, pos.y), col, s, e);
            if (bold) dl->AddText(font->font, font->size, ImVec2(drawX + 1.f, pos.y), col, s, e);
            if (pushed) dl->PopTextureID();
            x += font->font->CalcTextSizeA(font->size, FLT_MAX, 0.f, s, e).x;
        };

        for (size_t i = 0; i < len;)
        {
            const size_t sepBytes = SeparatorBytesAt(text, i, len);
            if (sepBytes)
            {
                drawSegment(segmentStart, i);
                const float advance = SeparatorAdvance(font);
                const float r = std::max(1.25f, font->size * 0.085f);
                const ImVec2 c(Snap(x + advance * 0.5f), Snap(pos.y + font->metrics.ascent * 0.54f));
                dl->AddCircleFilled(c, r, col, 12);
                x += advance;
                i += sepBytes;
                segmentStart = i;
            }
            else ++i;
        }
        drawSegment(segmentStart, len);
    }

    Container::Container() = default;

    void Container::ResetFrame()
    {
        m_clickedUrl.clear();
        m_clickedImageUrl.clear();
        m_tooltip.clear();
        m_clipDepth = 0;
    }

    std::string Container::TakeClickedUrl()
    {
        std::string out = std::move(m_clickedUrl);
        m_clickedUrl.clear();
        return out;
    }

    std::string Container::TakeClickedImageUrl()
    {
        std::string out = std::move(m_clickedImageUrl);
        m_clickedImageUrl.clear();
        return out;
    }

    Container::FontRef* Container::FontFromHandle(litehtml::uint_ptr hFont)
    {
        return reinterpret_cast<FontRef*>(hFont);
    }

    ImVec2 Container::ScreenPos(const litehtml::position& pos) const
    {
        return Snap(ImVec2(m_origin.x + (float)pos.x, m_origin.y + (float)pos.y));
    }

    ImU32 Container::Color(litehtml::web_color c)
    {
        return IM_COL32(c.red, c.green, c.blue, c.alpha);
    }

    litehtml::web_color Container::FirstGradientColor(const std::vector<litehtml::background_layer::color_point>& points)
    {
        if (points.empty()) return litehtml::web_color(0, 0, 0, 0);
        return points.front().color;
    }

    Container::LayerGeometry Container::Geometry(const litehtml::background_layer& layer) const
    {
        LayerGeometry g;
        g.borderMin = ScreenPos(layer.border_box);
        g.borderMax = Snap(ImVec2(g.borderMin.x + (float)layer.border_box.width,
                                  g.borderMin.y + (float)layer.border_box.height));
        g.clipMin = ScreenPos(layer.clip_box);
        g.clipMax = Snap(ImVec2(g.clipMin.x + (float)layer.clip_box.width,
                                g.clipMin.y + (float)layer.clip_box.height));
        g.tl = (float)layer.border_radius.top_left_x;
        g.tr = (float)layer.border_radius.top_right_x;
        g.br = (float)layer.border_radius.bottom_right_x;
        g.bl = (float)layer.border_radius.bottom_left_x;
        return g;
    }

    std::string Container::ResolveResourceUrl(const char* src, const char* baseurl) const
    {
        return ResolveResourceUrl(src ? std::string(src) : std::string(), baseurl ? std::string(baseurl) : std::string());
    }

    std::string Container::ResolveResourceUrl(const std::string& src, const std::string& baseurl) const
    {
        if (src.empty()) return {};
        const std::string base = baseurl.empty() ? m_baseUrl : baseurl;
        return ResolveWikiUrl(src, base);
    }

    std::string Container::ImageTextureId(const std::string& url) const
    {
        std::ostringstream oss;
        oss << "TC_WIKI_IMG_" << std::hex << Fnv1a64(url);
        return oss.str();
    }

    std::string Container::OriginalImageUrl(const std::string& url)
    {
        const std::string marker = "/images/thumb/";
        const size_t p = url.find(marker);
        if (p == std::string::npos) return url;

        const std::string prefix = url.substr(0, p) + "/images/";
        const std::string rest = url.substr(p + marker.size());
        const size_t finalSegment = rest.rfind('/');
        if (finalSegment == std::string::npos || finalSegment == 0) return url;
        return prefix + rest.substr(0, finalSegment);
    }

    std::string Container::FindImageUrl(const litehtml::element::ptr& el) const
    {
        if (!el) return {};
        const char* tag = el->get_tagName();
        if (tag && std::strcmp(tag, "img") == 0)
        {
            if (const char* src = el->get_attr("src"))
            {
                const std::string resolved = ResolveResourceUrl(src, m_baseUrl);
                if (IsAllowedImageUrl(resolved)) return OriginalImageUrl(resolved);
            }
        }
        for (const auto& child : el->children())
        {
            std::string hit = FindImageUrl(child);
            if (!hit.empty()) return hit;
        }
        return {};
    }

    litehtml::uint_ptr Container::create_font(const litehtml::font_description& descr,
                                              const litehtml::document*,
                                              litehtml::font_metrics* fm)
    {
        const bool  italic = descr.style == litehtml::font_style_italic;
        const int   weight = descr.weight <= 0 ? 400 : descr.weight;
        const int   decoration = descr.decoration_line;
        ImFont* face = italic && Gw2Ui::Gw2Italic() ? Gw2Ui::Gw2Italic() : Gw2Ui::UiFontResolved();
        if (!face) face = ImGui::GetFont();
        const float baseSize = face && face->FontSize > 0.f ? face->FontSize : ImGui::GetFontSize();
        const float requestedSize = (float)(descr.size > 0 ? descr.size : get_default_font_size());
        const float reqSize = std::max(std::max(8.f, m_minFontSize), requestedSize);

        // Dedup: litehtml asks for the same font description repeatedly across a relayout. Reuse the
        // cached handle instead of growing m_fonts unbounded (delete_font is a no-op for ImGui fonts).
        for (auto& f : m_fonts)
            if (f->font == face && f->size == reqSize && f->weight == weight &&
                f->italic == italic && f->decorationLine == decoration)
            {
                if (fm) *fm = f->metrics;
                return reinterpret_cast<litehtml::uint_ptr>(f.get());
            }

        auto font = std::make_unique<FontRef>();
        font->italic = italic;
        font->weight = weight;
        font->decorationLine = decoration;
        font->font = face;
        font->size = reqSize;
        const float scale = baseSize > 0.f ? (font->size / baseSize) : 1.f;
        const float ascent = font->font ? std::max(1.f, font->font->Ascent * scale) : font->size * 0.8f;
        const float descent = font->font ? std::max(1.f, -font->font->Descent * scale) : font->size * 0.2f;

        font->metrics.font_size = (litehtml::pixel_t)std::ceil(font->size);
        font->metrics.height = (litehtml::pixel_t)std::ceil(std::max(font->size * 1.18f, ascent + descent + 2.f));
        font->metrics.ascent = (litehtml::pixel_t)std::ceil(ascent);
        font->metrics.descent = (litehtml::pixel_t)std::ceil(descent);
        font->metrics.x_height = (litehtml::pixel_t)std::ceil(ascent * 0.55f);
        font->metrics.ch_width = (litehtml::pixel_t)std::ceil(text_width("0", (litehtml::uint_ptr)font.get()));
        font->metrics.draw_spaces = true;

        if (fm) *fm = font->metrics;
        FontRef* raw = font.get();
        m_fonts.push_back(std::move(font));
        return reinterpret_cast<litehtml::uint_ptr>(raw);
    }

    void Container::delete_font(litehtml::uint_ptr)
    {
        // Font refs live for the container lifetime. litehtml caches handles per document and may ask
        // repeatedly; deleting them individually is not useful with ImGui-owned fonts.
    }

    litehtml::pixel_t Container::text_width(const char* text, litehtml::uint_ptr hFont)
    {
        FontRef* font = FontFromHandle(hFont);
        if (!font || !font->font || !text) return 0;
        const bool decorativeQuote = font->size >= kDecorativeQuotePx && IsLoneQuoteRun(text);
        FontRef quoteFont;
        const FontRef* measureFont = font;
        if (decorativeQuote)
        {
            if (ImFont* face = Wiki::QuoteFont())
            {
                quoteFont = *font;
                quoteFont.font = face;
                measureFont = &quoteFont;
            }
        }
        const std::string norm = decorativeQuote && measureFont != font ? std::string(text) : NormalizePunct(text);
        return (litehtml::pixel_t)std::ceil(TextWidthWithSeparators(norm.c_str(), measureFont));
    }

    void Container::draw_text(litehtml::uint_ptr, const char* text, litehtml::uint_ptr hFont,
                              litehtml::web_color color, const litehtml::position& pos)
    {
        FontRef* font = FontFromHandle(hFont);
        if (!font || !font->font || !text || !text[0] || color.alpha == 0) return;
        const bool decorativeQuote = font->size >= kDecorativeQuotePx && IsLoneQuoteRun(text);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ScreenPos(pos);
        const ImU32 col = Color(color);
        const bool bold = font->weight >= 600;
        FontRef quoteFont;
        const FontRef* drawFont = font;
        if (decorativeQuote)
        {
            if (ImFont* face = Wiki::QuoteFont())
            {
                quoteFont = *font;
                quoteFont.font = face;
                drawFont = &quoteFont;
            }
        }
        const std::string norm = decorativeQuote && drawFont != font ? std::string(text) : NormalizePunct(text);
        DrawTextWithSeparators(dl, drawFont, p, col, norm.c_str(), bold);

        const int underlineMask = litehtml::text_decoration_line_underline;
        if (font->decorationLine & underlineMask)
        {
            const float w = (float)text_width(text, hFont);
            const float y = Snap(p.y + font->metrics.ascent + 1.f);
            dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), col, 1.f);
        }
    }

    litehtml::pixel_t Container::pt_to_px(float pt) const
    {
        return (litehtml::pixel_t)std::ceil(pt * (96.f / 72.f));
    }

    litehtml::pixel_t Container::get_default_font_size() const { return 18; }   // bigger, more readable wiki body (was 16)
    const char* Container::get_default_font_name() const { return "Menomonia"; }

    void Container::draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker& marker)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = Color(marker.color);
        const ImVec2 a = ScreenPos(marker.pos);
        const ImVec2 b = ImVec2(a.x + (float)marker.pos.width, a.y + (float)marker.pos.height);
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float r = std::max(2.f, std::min((b.x - a.x), (b.y - a.y)) * 0.35f);

        switch (marker.marker_type)
        {
            case litehtml::list_style_type_none:
                return;
            case litehtml::list_style_type_circle:
                dl->AddCircle(c, r, col, 16, 1.25f);
                break;
            case litehtml::list_style_type_square:
                dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col);
                break;
            case litehtml::list_style_type_decimal:
            case litehtml::list_style_type_decimal_leading_zero:
            {
                char buf[24]{};
                if (marker.marker_type == litehtml::list_style_type_decimal_leading_zero)
                    std::snprintf(buf, sizeof(buf), "%02d.", marker.index);
                else
                    std::snprintf(buf, sizeof(buf), "%d.", marker.index);
                FontRef* font = FontFromHandle(marker.font);
                if (font && font->font)
                    dl->AddText(font->font, font->size, a, col, buf);
                break;
            }
            default:
                dl->AddCircleFilled(c, r, col, 16);
                break;
        }
    }

    void Container::load_image(const char* src, const char* baseurl, bool)
    {
        const std::string url = ResolveResourceUrl(src, baseurl);
        if (!IsAllowedImageUrl(url)) return;
        const std::string id = ImageTextureId(url);
        ImageCache::PrefetchUrl(id.c_str(), url.c_str());
    }

    void Container::get_image_size(const char* src, const char* baseurl, litehtml::size& sz)
    {
        const std::string url = ResolveResourceUrl(src, baseurl);
        if (!IsAllowedImageUrl(url)) { sz.width = 1; sz.height = 1; return; }

        const std::string id = ImageTextureId(url);
        const Texture_t* tex = ImageCache::GetUrl(id.c_str(), url.c_str());
        if (tex && tex->Width > 0 && tex->Height > 0)
        {
            sz.width = (litehtml::pixel_t)tex->Width;
            sz.height = (litehtml::pixel_t)tex->Height;
        }
        else
        {
            // Initial layout must be deterministic while the async image cache is still filling.
            sz.width = 96;
            sz.height = 72;
        }
    }

    void Container::draw_image(litehtml::uint_ptr, const litehtml::background_layer& layer,
                               const std::string& url, const std::string& base_url)
    {
        const std::string resolved = ResolveResourceUrl(url, base_url);
        if (!IsAllowedImageUrl(resolved)) return;

        const std::string id = ImageTextureId(resolved);
        const Texture_t* tex = ImageCache::GetUrl(id.c_str(), resolved.c_str());
        if (!tex || !tex->Resource) return;

        LayerGeometry g = Geometry(layer);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(g.clipMin, g.clipMax, true);
        const float radius = std::min(std::min(g.tl, g.tr), std::min(g.br, g.bl));
        if (radius > 0.f)
            dl->AddImageRounded((ImTextureID)tex->Resource, g.borderMin, g.borderMax, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32_WHITE, radius);
        else
            dl->AddImage((ImTextureID)tex->Resource, g.borderMin, g.borderMax);
        dl->PopClipRect();
    }

    void Container::draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer& layer,
                                    const litehtml::web_color& color)
    {
        if (color.alpha == 0 || layer.border_box.width <= 0 || layer.border_box.height <= 0) return;
        LayerGeometry g = Geometry(layer);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(g.clipMin, g.clipMax, true);
        const float radius = std::min(std::min(g.tl, g.tr), std::min(g.br, g.bl));
        dl->AddRectFilled(g.borderMin, g.borderMax, Color(color), radius);
        dl->PopClipRect();
    }

    void Container::draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                         const litehtml::background_layer::linear_gradient& gradient)
    {
        draw_solid_fill(hdc, layer, FirstGradientColor(gradient.color_points));
    }

    void Container::draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                         const litehtml::background_layer::radial_gradient& gradient)
    {
        draw_solid_fill(hdc, layer, FirstGradientColor(gradient.color_points));
    }

    void Container::draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                        const litehtml::background_layer::conic_gradient& gradient)
    {
        draw_solid_fill(hdc, layer, FirstGradientColor(gradient.color_points));
    }

    void Container::DrawSideBorder(ImDrawList* dl, const litehtml::border& border, ImVec2 a, ImVec2 b) const
    {
        if (border.width <= 0 || border.style == litehtml::border_style_none || border.style == litehtml::border_style_hidden)
            return;
        dl->AddLine(a, b, Color(border.color), (float)border.width);
    }

    void Container::DrawUniformBorder(ImDrawList* dl, const litehtml::borders& borders, ImVec2 a, ImVec2 b)
    {
        const float w = (float)borders.top.width;
        if (w <= 0.f) return;
        const float half = w * 0.5f;
        const ImVec2 aa(a.x + half, a.y + half);
        const ImVec2 bb(b.x - half, b.y - half);
        const float radius = std::max(0.f, (float)borders.radius.top_left_x - half);
        dl->AddRect(aa, bb, Color(borders.top.color), radius, 0, w);
    }

    void Container::draw_borders(litehtml::uint_ptr, const litehtml::borders& borders,
                                 const litehtml::position& draw_pos, bool)
    {
        if (!borders.is_visible()) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ScreenPos(draw_pos);
        const ImVec2 b = Snap(ImVec2(a.x + (float)draw_pos.width, a.y + (float)draw_pos.height));

        const bool uniform =
            borders.top.width == borders.right.width &&
            borders.top.width == borders.bottom.width &&
            borders.top.width == borders.left.width &&
            borders.top.color == borders.right.color &&
            borders.top.color == borders.bottom.color &&
            borders.top.color == borders.left.color &&
            borders.top.style != litehtml::border_style_none &&
            borders.top.style != litehtml::border_style_hidden;

        if (uniform) { DrawUniformBorder(dl, borders, a, b); return; }

        DrawSideBorder(dl, borders.top, a, ImVec2(b.x, a.y));
        DrawSideBorder(dl, borders.right, ImVec2(b.x, a.y), b);
        DrawSideBorder(dl, borders.bottom, ImVec2(b.x, b.y), ImVec2(a.x, b.y));
        DrawSideBorder(dl, borders.left, ImVec2(a.x, b.y), a);
    }

    void Container::set_caption(const char* caption) { m_caption = caption ? caption : ""; }
    void Container::set_base_url(const char* base_url) { if (base_url && base_url[0]) m_baseUrl = base_url; }
    void Container::link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) {}

    void Container::on_anchor_click(const char* url, const litehtml::element::ptr& el)
    {
        std::string imageUrl = FindImageUrl(el);
        if (!imageUrl.empty())
        {
            m_clickedImageUrl = std::move(imageUrl);
            return;
        }
        m_clickedUrl = ResolveResourceUrl(url ? std::string(url) : std::string(), m_baseUrl);
    }

    void Container::on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event)
    {
        if (event == litehtml::mouse_event_leave || !el)
        {
            m_tooltip.clear();
            return;
        }
        const char* title = el->get_attr("title");
        const char* href = el->get_attr("href");
        const char* alt = el->get_attr("alt");
        if (title && title[0]) m_tooltip = title;
        else if (href && href[0]) m_tooltip = href;
        else if (alt && alt[0]) m_tooltip = alt;
    }

    void Container::set_cursor(const char* cursor)
    {
        if (cursor && std::strcmp(cursor, "pointer") == 0 && ImGui::IsWindowHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    void Container::transform_text(litehtml::string& text, litehtml::text_transform tt)
    {
        switch (tt)
        {
            case litehtml::text_transform_uppercase: text = Upper(text); break;
            case litehtml::text_transform_lowercase: text = Lower(text); break;
            case litehtml::text_transform_capitalize: text = CapitalizeWords(text); break;
            default: break;
        }
    }

    void Container::import_css(litehtml::string& text, const litehtml::string&, litehtml::string&)
    {
        text.clear();
    }

    void Container::set_clip(const litehtml::position& pos, const litehtml::border_radiuses&)
    {
        const ImVec2 a = ScreenPos(pos);
        const ImVec2 b = Snap(ImVec2(a.x + (float)pos.width, a.y + (float)pos.height));
        ImGui::GetWindowDrawList()->PushClipRect(a, b, true);
        ++m_clipDepth;
    }

    void Container::del_clip()
    {
        if (m_clipDepth > 0)
        {
            ImGui::GetWindowDrawList()->PopClipRect();
            --m_clipDepth;
        }
    }

    void Container::get_viewport(litehtml::position& viewport) const
    {
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = (litehtml::pixel_t)std::max(1.f, m_viewport.x);
        viewport.height = (litehtml::pixel_t)std::max(1.f, m_viewport.y);
    }

    litehtml::element::ptr Container::create_element(const char*, const litehtml::string_map&,
                                                     const std::shared_ptr<litehtml::document>&)
    {
        return nullptr;
    }

    void Container::get_media_features(litehtml::media_features& media) const
    {
        media.type = litehtml::media_type_screen;
        media.width = (litehtml::pixel_t)std::max(1.f, m_viewport.x);
        media.height = (litehtml::pixel_t)std::max(1.f, m_viewport.y);
        media.device_width = media.width;
        media.device_height = media.height;
        media.color = 8;
        media.color_index = 0;
        media.monochrome = 0;
        media.resolution = 96;
    }

    void Container::get_language(litehtml::string& language, litehtml::string& culture) const
    {
        language = "en";
        culture = "US";
    }

    litehtml::string Container::resolve_color(const litehtml::string&) const
    {
        return litehtml::string();
    }

    std::shared_ptr<litehtml::document> CreateDocument(const std::string& bodyHtml,
                                                       const std::string& userCss,
                                                       Container& container)
    {
        const std::string html =
            "<!doctype html><html><head><meta charset=\"utf-8\"></head><body><div class=\"tc-wiki-root\">"
            + bodyHtml + "</div></body></html>";
        container.SetBaseUrl("https://wiki.guildwars2.com/wiki/");
        return litehtml::document::createFromString(html.c_str(), &container, litehtml::master_css, userCss);
    }
}
