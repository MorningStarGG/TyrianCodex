#include "ui/wiki/WikiStyleAdapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace Wiki
{
    namespace
    {
        struct Rgb { int r = 0, g = 0, b = 0; };
        struct Hsl { float h = 0.f, s = 0.f, l = 0.f; };

        std::string Lower(std::string s)
        {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        float Clamp01(float v) { return std::max(0.f, std::min(1.f, v)); }

        Hsl ToHsl(Rgb c)
        {
            const float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
            const float mx = std::max({ r, g, b });
            const float mn = std::min({ r, g, b });
            Hsl hsl;
            hsl.l = (mx + mn) * 0.5f;
            if (mx == mn) return hsl;
            const float d = mx - mn;
            hsl.s = hsl.l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
            if (mx == r) hsl.h = (g - b) / d + (g < b ? 6.f : 0.f);
            else if (mx == g) hsl.h = (b - r) / d + 2.f;
            else hsl.h = (r - g) / d + 4.f;
            hsl.h /= 6.f;
            return hsl;
        }

        float HueToRgb(float p, float q, float t)
        {
            if (t < 0.f) t += 1.f;
            if (t > 1.f) t -= 1.f;
            if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
            if (t < 1.f / 2.f) return q;
            if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
            return p;
        }

        Rgb FromHsl(Hsl hsl)
        {
            float r, g, b;
            if (hsl.s == 0.f) r = g = b = hsl.l;
            else
            {
                const float q = hsl.l < 0.5f ? hsl.l * (1.f + hsl.s) : hsl.l + hsl.s - hsl.l * hsl.s;
                const float p = 2.f * hsl.l - q;
                r = HueToRgb(p, q, hsl.h + 1.f / 3.f);
                g = HueToRgb(p, q, hsl.h);
                b = HueToRgb(p, q, hsl.h - 1.f / 3.f);
            }
            return Rgb{ (int)std::round(Clamp01(r) * 255.f), (int)std::round(Clamp01(g) * 255.f), (int)std::round(Clamp01(b) * 255.f) };
        }

        int HexVal(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool ParseHex(const std::string& s, size_t pos, size_t& len, Rgb& rgb)
        {
            if (pos >= s.size() || s[pos] != '#') return false;
            size_t n = 0;
            while (pos + 1 + n < s.size() && HexVal(s[pos + 1 + n]) >= 0 && n < 8) ++n;
            if (n != 3 && n != 6) return false;
            if (n == 3)
            {
                rgb.r = HexVal(s[pos + 1]) * 17;
                rgb.g = HexVal(s[pos + 2]) * 17;
                rgb.b = HexVal(s[pos + 3]) * 17;
            }
            else
            {
                rgb.r = HexVal(s[pos + 1]) * 16 + HexVal(s[pos + 2]);
                rgb.g = HexVal(s[pos + 3]) * 16 + HexVal(s[pos + 4]);
                rgb.b = HexVal(s[pos + 5]) * 16 + HexVal(s[pos + 6]);
            }
            len = n + 1;
            return true;
        }

        std::string Hex(Rgb rgb)
        {
            char b[16];
            std::snprintf(b, sizeof(b), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
            return b;
        }

        enum class Role { Text, Background, Border, Other };

        Rgb AdaptColor(Rgb in, Role role)
        {
            Hsl h = ToHsl(in);
            if (role == Role::Background)
            {
                const float hueBoost = h.s > 0.12f ? 0.04f : 0.f;
                h.s = Clamp01(h.s * 0.75f + 0.05f);
                h.l = Clamp01(0.13f + hueBoost + h.s * 0.08f);
            }
            else if (role == Role::Border)
            {
                h.s = Clamp01(h.s * 0.75f + 0.10f);
                h.l = Clamp01(0.30f + h.s * 0.05f);
            }
            else if (role == Role::Text)
            {
                if (h.l < 0.45f) h.l = 0.68f;
                if (h.s > 0.10f) h.s = Clamp01(h.s * 1.05f + 0.10f);
            }
            return FromHsl(h);
        }

        Role PropertyRole(const std::string& css, size_t colorPos)
        {
            size_t colon = css.rfind(':', colorPos);
            size_t brace = css.rfind('{', colorPos);
            size_t semi = css.rfind(';', colorPos);
            if (colon == std::string::npos || (brace != std::string::npos && colon < brace) ||
                (semi != std::string::npos && colon < semi))
                return Role::Other;
            size_t start = (brace == std::string::npos) ? 0 : brace + 1;
            if (semi != std::string::npos && semi > start) start = semi + 1;
            std::string prop = Lower(css.substr(start, colon - start));
            prop.erase(std::remove_if(prop.begin(), prop.end(), [](char c) { return std::isspace((unsigned char)c); }), prop.end());
            if (prop.find("background") != std::string::npos) return Role::Background;
            if (prop.find("border") != std::string::npos || prop.find("outline") != std::string::npos) return Role::Border;
            if (prop == "color") return Role::Text;
            return Role::Other;
        }

        std::string AdaptCssColors(const std::string& css)
        {
            std::string out;
            out.reserve(css.size());
            for (size_t i = 0; i < css.size();)
            {
                Rgb rgb;
                size_t len = 0;
                if (ParseHex(css, i, len, rgb))
                {
                    const Role role = PropertyRole(css, i);
                    out += Hex(AdaptColor(rgb, role));
                    i += len;
                }
                else
                {
                    out.push_back(css[i++]);
                }
            }
            return out;
        }
    }

    std::string BuildTyrianWikiCss(const std::string& wikiCss)
    {
        std::string css = AdaptCssColors(wikiCss);
        css += R"CSS(
html, body, .mw-parser-output, .tc-wiki-doc {
  color: #E8DDC6;
  background: transparent;
  font-family: Menomonia, Arial, sans-serif;
  font-size: 17px;
  line-height: 1.45;
}
.tc-wiki-doc { padding: 0 8px 28px 0; }
.tc-wiki-doc h1 { display: none; }
a, a:link, a:visited { color: #67AFFF; text-decoration: none; }
a:hover { color: #9CCBFF; text-decoration: underline; }
p { margin: 0 0 0.85em 0; }
h1, h2, h3, h4, h5, h6 {
  color: #FFDD82;
  border-bottom: 1px solid rgba(196, 176, 128, 0.32);
  margin: 1.0em 0 0.45em 0;
  padding-bottom: 0.18em;
  font-weight: bold;
}
ul, ol { margin-top: 0.2em; margin-bottom: 0.7em; }
li { margin: 0.12em 0; }
table, .table, .wikitable {
  border-collapse: collapse;
  background-color: rgba(20, 20, 18, 0.58);
  color: #E8DDC6;
  max-width: 100%;
  box-sizing: border-box;
}
.tc-wiki-doc table { max-width: 100%; box-sizing: border-box; }
td, th {
  border: 1px solid rgba(169, 139, 82, 0.38);
  padding: 5px 7px;
  vertical-align: top;
}
th { font-weight: bold; color: #F5E6B4; }
table, td, th, li, p, small {
  line-height: 1.35;
}
.tc-wiki-doc table,
.tc-wiki-doc td,
.tc-wiki-doc th {
  font-size: 16px;
}
.tc-wiki-doc small,
.tc-wiki-doc .small,
.tc-wiki-doc .navbox,
.tc-wiki-doc .navbox td,
.tc-wiki-doc .navbox th,
.tc-wiki-doc .navbox li {
  font-size: 15px;
  line-height: 1.35;
}
.infobox, .recipe-box, .tc-wiki-rail-fallback {
  width: 100%;
  max-width: 100%;
  box-sizing: border-box;
  background-color: rgba(20, 20, 18, 0.50);
  border: 1px solid rgba(169, 139, 82, 0.42);
}
.tc-wiki-rail-doc {
  width: 100%;
  padding: 0;
  margin: 0;
}
.tc-wiki-rail-doc .infobox,
.tc-wiki-rail-doc .recipe-box,
.tc-wiki-rail-doc .tc-wiki-rail-fallback,
.tc-wiki-rail-doc .floatright,
.tc-wiki-rail-doc .tright {
  float: none;
  clear: none;
  width: 100%;
  min-width: 100%;
  max-width: 100%;
  box-sizing: border-box;
  margin-left: auto;
  margin-right: auto;
}
.tc-wiki-rail-doc table.infobox,
.tc-wiki-rail-doc table.recipe-box {
  width: 100%;
  min-width: 100%;
  max-width: 100%;
  box-sizing: border-box;
}
.tc-wiki-rail-doc .infobox,
.tc-wiki-rail-doc .recipe-box,
.tc-wiki-rail-doc .tc-wiki-rail-fallback {
  margin-top: 0;
  margin-bottom: 10px;
}
.tc-wiki-rail-fallback h2 {
  margin: 0;
  padding: 8px 10px;
  border-bottom: 1px solid rgba(169, 139, 82, 0.34);
  text-align: center;
}
.tc-wiki-rail-banner {
  margin: 0 0 10px 0;
  padding: 0;
}
.tc-wiki-rail-image {
  padding: 8px;
  text-align: center;
}
.tc-wiki-rail-image img {
  display: block;
  max-width: 100%;
  max-height: 120px;
  height: auto;
  margin: 0 auto;
}
.tc-wiki-doc div.infobox .wrapper,
.tc-wiki-doc div.recipe-box .wrapper {
  clear: both;
  padding: 0;
}
.tc-wiki-doc div.infobox dl,
.tc-wiki-doc div.recipe-box dl {
  clear: both;
  margin: 6px 0 6px 88px;
  padding: 0;
  border-left: 2px solid rgba(169, 139, 82, 0.78);
}
.tc-wiki-doc div.ingredients dl {
  clear: both;
  margin: 6px 0 6px 34px;
  padding: 0;
  border-left: 2px solid rgba(169, 139, 82, 0.72);
}
.tc-wiki-doc div.infobox dt,
.tc-wiki-doc div.recipe-box dt {
  float: left;
  clear: left;
  width: 88px;
  box-sizing: border-box;
  margin: 0 0 0 -88px;
  padding: 3px 8px 3px 3px;
  border: 0;
  line-height: 1.45;
  text-align: right;
  font-weight: bold;
}
.tc-wiki-doc div.infobox dd,
.tc-wiki-doc div.recipe-box dd {
  float: none;
  display: block;
  min-height: 22px;
  margin: 0;
  padding: 3px 7px;
  border: 0;
  line-height: 1.45;
}
.tc-wiki-doc div.recipe-box .subheading {
  clear: both;
  margin-top: 7px;
  margin-bottom: 5px;
}
.tc-wiki-doc div.ingredients dt {
  float: left;
  clear: left;
  width: 34px;
  box-sizing: border-box;
  margin: 0 0 0 -34px;
  padding: 2px 6px 2px 2px;
  border: 0;
  line-height: 1.45;
  text-align: right;
  font-weight: normal;
}
.tc-wiki-doc div.ingredients dd {
  float: none;
  display: block;
  min-height: 24px;
  margin: 0;
  padding: 2px 7px;
  border: 0;
  line-height: 1.45;
}
.tc-wiki-doc .tc-chatlink {
  white-space: nowrap;
  cursor: pointer;
}
.infobox img, .recipe-box img, .floatright img, .thumb img, .gallery img {
  max-width: 100%;
  height: auto;
}
.floatright, .tright {
  float: right;
  max-width: 45%;
  margin: 0 0 0.75em 1em;
}
ul.gallery {
  display: block;
  margin: 0.4em 0;
  padding: 0;
}
li.gallerybox {
  display: inline-block;
  vertical-align: top;
  margin: 6px;
  padding: 6px;
  background-color: rgba(12, 12, 12, 0.40);
  border: 1px solid rgba(169, 139, 82, 0.35);
}
div.gallerytext { font-size: 15px; color: #D9CFAF; text-align: center; }
.inline-icon, .item-icon.small, .item-icon.medium { display: inline-block; vertical-align: middle; }
.inline-icon img, .item-icon.small img { width: 20px; height: 20px; }
.item-icon.medium img { width: 35px; height: 35px; }
.tc-unsupported-widget {
  margin: 10px 0;
  padding: 12px;
  border: 1px solid rgba(169, 139, 82, 0.55);
  background-color: rgba(20, 18, 14, 0.72);
  color: #E8DDC6;
}
)CSS";
        return css;
    }
}
