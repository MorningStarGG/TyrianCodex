#include "ui/wiki/WikiHtmlSanitizer.h"

#include "app/wiki/WikiService.h" // Wiki::IsAllowedImageHost + Wiki::DecodeHtmlEntities (shared)

#include <gumbo.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace Wiki
{
    namespace
    {
        std::string Lower(std::string s)
        {
            for (char &c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }

        std::string Trim(std::string s)
        {
            while (!s.empty() && std::isspace((unsigned char)s.front()))
                s.erase(s.begin());
            while (!s.empty() && std::isspace((unsigned char)s.back()))
                s.pop_back();
            return s;
        }

        std::string StripTags(const std::string &s)
        {
            std::string out;
            bool tag = false;
            for (char c : s)
            {
                if (c == '<')
                {
                    tag = true;
                    continue;
                }
                if (c == '>')
                {
                    tag = false;
                    continue;
                }
                if (!tag)
                    out.push_back(c);
            }
            return out;
        }

        std::string PlainHtmlText(const std::string &html)
        {
            return Trim(DecodeHtmlEntities(StripTags(html))); // Wiki::DecodeHtmlEntities (shared, in WikiService)
        }

        bool ContainsClass(const std::string &classes, const char *needle)
        {
            const std::string n = Lower(needle);
            std::istringstream in(classes);
            std::string c;
            while (in >> c)
                if (Lower(c) == n)
                    return true;
            return false;
        }

        std::string Attr(const GumboElement &e, const char *name)
        {
            if (GumboAttribute *a = gumbo_get_attribute(&e.attributes, name))
                return a->value ? a->value : "";
            return {};
        }

        bool HasClass(const GumboElement &e, const char *name)
        {
            return ContainsClass(Attr(e, "class"), name);
        }

        bool HasAnyClass(const GumboElement &e, std::initializer_list<const char *> names)
        {
            for (const char *n : names)
                if (HasClass(e, n))
                    return true;
            return false;
        }

        bool StyleHidden(const GumboElement &e)
        {
            const std::string s = Lower(Attr(e, "style"));
            return s.find("display:none") != std::string::npos ||
                   s.find("display: none") != std::string::npos ||
                   s.find("visibility:hidden") != std::string::npos ||
                   s.find("visibility: hidden") != std::string::npos;
        }

        bool IsUnsafeUrl(const std::string &url)
        {
            const std::string u = Lower(url);
            return u.find("javascript:") == 0 || u.find("data:text/html") == 0 || u.find("vbscript:") == 0;
        }

        bool SafeInlineStyle(const std::string &style)
        {
            const std::string s = Lower(style);
            return s.find("expression(") == std::string::npos &&
                   s.find("javascript:") == std::string::npos &&
                   s.find("behavior:") == std::string::npos &&
                   s.find("-moz-binding") == std::string::npos;
        }

        std::string EscapeText(const std::string &s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
            {
                switch (c)
                {
                case '&':
                    out += "&amp;";
                    break;
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                default:
                    out.push_back(c);
                    break;
                }
            }
            return out;
        }

        std::string EscapeAttr(const std::string &s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
            {
                switch (c)
                {
                case '&':
                    out += "&amp;";
                    break;
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                case '"':
                    out += "&quot;";
                    break;
                default:
                    out.push_back(c);
                    break;
                }
            }
            return out;
        }

        std::string Base64(const unsigned char *data, size_t len)
        {
            static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((len + 2) / 3) * 4);
            for (size_t i = 0; i < len; i += 3)
            {
                const unsigned a = data[i];
                const unsigned b = (i + 1 < len) ? data[i + 1] : 0;
                const unsigned c = (i + 2 < len) ? data[i + 2] : 0;
                const unsigned triple = (a << 16) | (b << 8) | c;
                out.push_back(kTable[(triple >> 18) & 0x3F]);
                out.push_back(kTable[(triple >> 12) & 0x3F]);
                out.push_back(i + 1 < len ? kTable[(triple >> 6) & 0x3F] : '=');
                out.push_back(i + 2 < len ? kTable[triple & 0x3F] : '=');
            }
            return out;
        }

        int ChatTypeCode(const std::string &raw)
        {
            const std::string type = Lower(Trim(raw));
            if (type == "item")
                return 2;
            if (type == "text")
                return 3;
            if (type == "map")
                return 4;
            if (type == "skill")
                return 6;
            if (type == "trait")
                return 7;
            if (type == "recipe")
                return 9;
            if (type == "skin")
                return 10;
            if (type == "outfit")
                return 11;
            return 0;
        }

        std::string BuildChatLink(const std::string &type, int id)
        {
            const int code = ChatTypeCode(type);
            if (code <= 0 || id <= 0)
                return {};

            std::vector<unsigned char> bytes;
            int value = id;
            while (value > 0)
            {
                bytes.push_back((unsigned char)(value & 0xFF));
                value >>= 8;
            }
            while (bytes.size() < 4 || (bytes.size() % 2) != 0)
                bytes.push_back(0);
            if (code == 2)
                bytes.insert(bytes.begin(), 1);
            bytes.insert(bytes.begin(), (unsigned char)code);
            return "[&" + Base64(bytes.data(), bytes.size()) + "]";
        }

        std::string TagName(const GumboNode *node)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return {};
            const char *norm = gumbo_normalized_tagname(node->v.element.tag);
            if (norm && *norm)
                return norm;
            GumboStringPiece p = node->v.element.original_tag;
            gumbo_tag_from_original_text(&p);
            return p.data ? std::string(p.data, p.length) : std::string();
        }

        bool HasDescendantTag(const GumboNode *node, GumboTag tag)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return false;
            if (node->v.element.tag == tag)
                return true;
            const GumboVector *children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
                if (HasDescendantTag((const GumboNode *)children->data[i], tag))
                    return true;
            return false;
        }

        bool IsSkippedElement(const GumboElement &e)
        {
            const std::string id = Lower(Attr(e, "id"));
            if (id == "toc" || id == "mw-toc-heading")
                return true;
            if (e.tag == GUMBO_TAG_SCRIPT || e.tag == GUMBO_TAG_FORM || e.tag == GUMBO_TAG_IFRAME ||
                e.tag == GUMBO_TAG_OBJECT || e.tag == GUMBO_TAG_EMBED || e.tag == GUMBO_TAG_INPUT ||
                e.tag == GUMBO_TAG_BUTTON || e.tag == GUMBO_TAG_TEXTAREA || e.tag == GUMBO_TAG_SELECT ||
                e.tag == GUMBO_TAG_NOSCRIPT)
                return true;

            if (StyleHidden(e))
                return true;
            if (HasAnyClass(e, {"mw-editsection", "mw-empty-elt", "metadata", "noprint", "wikipopup",
                                "reference", "references", "printfooter", "catlinks", "navbox", "sistersitebox",
                                "plainlinksneverexpand", "toc", "toctitle", "mw-toc"}))
                return true;
            return false;
        }

        bool IsInfoRailCandidate(const GumboNode *node)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return false;
            const GumboElement &e = node->v.element;
            return HasAnyClass(e, {"infobox", "recipe-box"});
        }

        bool IsImageRailCandidate(const GumboNode *node)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return false;
            const GumboElement &e = node->v.element;
            if ((HasClass(e, "floatright") || HasClass(e, "tright")) && HasDescendantTag(node, GUMBO_TAG_IMG))
                return true;
            return false;
        }

        const GumboNode *FindFirstElement(const GumboNode *node, GumboTag tag)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return nullptr;
            if (node->v.element.tag == tag)
                return node;
            const GumboVector *children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
            {
                if (const GumboNode *hit = FindFirstElement((const GumboNode *)children->data[i], tag))
                    return hit;
            }
            return nullptr;
        }

        const GumboNode *FindFirstImageLink(const GumboNode *node)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return nullptr;
            if (node->v.element.tag == GUMBO_TAG_A && HasDescendantTag(node, GUMBO_TAG_IMG))
                return node;
            const GumboVector *children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
            {
                if (const GumboNode *hit = FindFirstImageLink((const GumboNode *)children->data[i]))
                    return hit;
            }
            return nullptr;
        }

        std::string BuildImageRailHtml(const GumboNode *node, const std::string &title)
        {
            const GumboNode *imgNode = FindFirstElement(node, GUMBO_TAG_IMG);
            if (!imgNode)
                return "<div class=\"tc-wiki-rail-fallback\"><h2>" + EscapeText(title) + "</h2></div>";

            const GumboElement &img = imgNode->v.element;
            std::string src = Attr(img, "src");
            if (!src.empty())
                src = ResolveWikiUrl(src);
            std::string alt = Attr(img, "alt");

            std::string href;
            if (const GumboNode *linkNode = FindFirstImageLink(node))
            {
                href = Attr(linkNode->v.element, "href");
                if (!href.empty() && !IsUnsafeUrl(href))
                    href = ResolveWikiUrl(href);
            }

            std::string out = "<div class=\"tc-wiki-rail-fallback tc-wiki-rail-banner\"><h2>" + EscapeText(title) + "</h2>";
            if (!src.empty())
            {
                out += "<div class=\"tc-wiki-rail-image\">";
                if (!href.empty())
                    out += "<a href=\"" + EscapeAttr(href) + "\">";
                out += "<img src=\"" + EscapeAttr(src) + "\" alt=\"" + EscapeAttr(alt) + "\">";
                if (!href.empty())
                    out += "</a>";
                out += "</div>";
            }
            out += "</div>";
            return out;
        }

        bool IsUnsupportedWidget(const GumboElement &e)
        {
            return HasAnyClass(e, {"zone-map", "leaflet-container", "interactive-map"}) ||
                   (Attr(e, "id").find("map") != std::string::npos && HasClass(e, "map"));
        }

        bool IsVoidTag(const std::string &tag)
        {
            static const std::unordered_set<std::string> kVoid = {
                "area", "base", "br", "col", "embed", "hr", "img", "input",
                "link", "meta", "param", "source", "track", "wbr"};
            return kVoid.count(tag) != 0;
        }

        bool KeepAttrForTag(const std::string &tag, const std::string &name)
        {
            if (name == "class" || name == "id" || name == "title" || name == "style")
                return true;
            if (name == "href" && tag == "a")
                return true;
            if ((name == "src" || name == "srcset" || name == "alt" || name == "width" || name == "height") && tag == "img")
                return true;
            if (name == "colspan" || name == "rowspan" || name == "align" || name == "valign")
                return true;
            if (name.find("data-file-") == 0)
                return true;
            return false;
        }

        std::string SerializeNode(const GumboNode *node, bool &extractedRail, SanitizedHtml &out);

        std::string SerializeChildren(const GumboNode *node, bool &extractedRail, SanitizedHtml &out)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return {};
            std::string s;
            const GumboVector *children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
                s += SerializeNode((const GumboNode *)children->data[i], extractedRail, out);
            return s;
        }

        std::string SerializeElement(const GumboNode *node, bool &extractedRail, SanitizedHtml &out)
        {
            const GumboElement &e = node->v.element;
            if (IsSkippedElement(e))
                return {};

            if (IsUnsupportedWidget(e))
            {
                out.skippedWidgets.push_back("interactive map");
                return "<div class=\"tc-unsupported-widget\"><b>Interactive map</b><br/>This wiki widget requires browser JavaScript.</div>";
            }

            if (HasClass(e, "gamelink"))
            {
                const std::string link = BuildChatLink(Attr(e, "data-type"), std::atoi(Attr(e, "data-id").c_str()));
                if (link.empty())
                    return {};
                return "<a class=\"tc-chatlink\" href=\"#tc-chatlink:" + EscapeAttr(link) +
                       "\" title=\"Click to copy chat link\">" + EscapeText(link) + "</a>";
            }

            if (!extractedRail && IsInfoRailCandidate(node))
            {
                bool nestedRail = true;
                out.railHtml = SerializeElement(node, nestedRail, out);
                extractedRail = true;
                return {};
            }

            if (!extractedRail && IsImageRailCandidate(node))
            {
                out.railHtml = BuildImageRailHtml(node, out.railTitle);
                extractedRail = true;
                return {};
            }

            std::string tag = TagName(node);
            if (tag.empty())
                return SerializeChildren(node, extractedRail, out);

            if (tag == "html" || tag == "body")
                return SerializeChildren(node, extractedRail, out);
            if (tag == "head" || tag == "style" || tag == "meta" || tag == "link")
                return {};

            std::string s = "<" + tag;
            const GumboVector *attrs = &e.attributes;
            for (unsigned int i = 0; i < attrs->length; ++i)
            {
                const GumboAttribute *a = (const GumboAttribute *)attrs->data[i];
                if (!a || !a->name || !a->value)
                    continue;
                std::string name = Lower(a->name);
                if (name.rfind("on", 0) == 0)
                    continue;
                if (!KeepAttrForTag(tag, name))
                    continue;
                std::string value = a->value;
                if ((name == "href" || name == "src") && IsUnsafeUrl(value))
                    continue;
                if (name == "style" && !SafeInlineStyle(value))
                    continue;
                if ((name == "href" || name == "src") && !value.empty())
                    value = ResolveWikiUrl(value);
                // Defense-in-depth: never emit a foreign-host image into the document. The container's
                // load_image gate would refuse to fetch it anyway; dropping the resolved src here keeps
                // the allowlist a single boundary and avoids a broken-image box. (srcset URLs are
                // relative/self-hosted on the GW2 wiki and remain fetch-gated by the container.)
                if (tag == "img" && name == "src" && !value.empty() && !Wiki::IsAllowedImageHost(value))
                    continue;
                // Collect the resolved, host-allowed image so the page can prefetch them ALL on
                // load (parallel, off-thread) before litehtml lays out + asks per-<img> -- cuts the first paint.
                if (tag == "img" && name == "src" && !value.empty())
                    out.imageUrls.push_back(value);
                s += " " + name + "=\"" + EscapeAttr(value) + "\"";
            }
            s += ">";
            if (!IsVoidTag(tag))
            {
                s += SerializeChildren(node, extractedRail, out);
                s += "</" + tag + ">";
            }
            return s;
        }

        std::string SerializeNode(const GumboNode *node, bool &extractedRail, SanitizedHtml &out)
        {
            if (!node)
                return {};
            switch (node->type)
            {
            case GUMBO_NODE_TEXT:
            case GUMBO_NODE_WHITESPACE:
                return EscapeText(node->v.text.text ? node->v.text.text : "");
            case GUMBO_NODE_ELEMENT:
            case GUMBO_NODE_TEMPLATE:
                return SerializeElement(node, extractedRail, out);
            default:
                return {};
            }
        }

        const GumboNode *FindParserOutput(const GumboNode *node)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT)
                return nullptr;
            if (HasClass(node->v.element, "mw-parser-output"))
                return node;
            const GumboVector *children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
            {
                if (const GumboNode *hit = FindParserOutput((const GumboNode *)children->data[i]))
                    return hit;
            }
            return nullptr;
        }
    }

    std::string ResolveWikiUrl(const std::string &url, const std::string &baseUrl)
    {
        if (url.empty())
            return {};
        if (url.rfind("//", 0) == 0)
            return "https:" + url;
        if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
            return url;
        if (url[0] == '/')
            return std::string("https://wiki.guildwars2.com") + url;
        if (url[0] == '#')
            return url;
        size_t slash = baseUrl.find_last_of('/');
        const std::string base = (slash == std::string::npos) ? baseUrl : baseUrl.substr(0, slash + 1);
        return base + url;
    }

    SanitizedHtml SanitizeArticleHtml(const std::string &html, const std::string &title)
    {
        SanitizedHtml out;
        try
        {
            std::string plainTitle = PlainHtmlText(title);
            if (plainTitle.empty())
                plainTitle = title;
            GumboOutput *gumbo = gumbo_parse_with_options(&kGumboDefaultOptions, html.data(), html.size());
            if (!gumbo)
            {
                out.articleHtml = "<div class=\"tc-wiki-doc\"><p>Could not parse wiki article.</p></div>";
                return out;
            }
            // RAII: gumbo_destroy_output must run even if the recursive serialize below throws.
            struct GumboGuard
            {
                GumboOutput *g;
                ~GumboGuard()
                {
                    if (g)
                        gumbo_destroy_output(&kGumboDefaultOptions, g);
                }
            } guard{gumbo};

            const GumboNode *root = FindParserOutput(gumbo->root);
            if (!root)
                root = gumbo->root;
            out.railTitle = plainTitle;
            bool extractedRail = false;
            out.articleHtml = "<div class=\"tc-wiki-doc\"><h1>" + EscapeText(plainTitle) + "</h1>" + SerializeChildren(root, extractedRail, out) + "</div>";
            if (out.railHtml.empty())
                out.railHtml = "<div class=\"tc-wiki-rail-fallback\"><h2>" + EscapeText(plainTitle) + "</h2></div>";
        }
        catch (...)
        {
            // Never let a parse/serialize failure escape into the render path; return a safe article.
            out = SanitizedHtml{};
            out.articleHtml = "<div class=\"tc-wiki-doc\"><p>Could not display this wiki article.</p></div>";
        }
        return out;
    }
}
