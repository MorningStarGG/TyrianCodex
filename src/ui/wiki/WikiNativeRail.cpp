#include "ui/wiki/WikiNativeRail.h"

#include "app/wiki/WikiService.h"
#include "ui/wiki/WikiHtmlSanitizer.h"

#include <gumbo.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace Wiki
{
    namespace
    {
        std::string Lower(std::string s)
        {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        std::string Trim(std::string s)
        {
            while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
            return s;
        }

        std::string CollapseSpace(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            bool pendingSpace = false;
            for (unsigned char ch : s)
            {
                if (std::isspace(ch))
                {
                    pendingSpace = !out.empty();
                    continue;
                }
                if (pendingSpace) { out.push_back(' '); pendingSpace = false; }
                out.push_back((char)ch);
            }
            return Trim(out);
        }

        void ReplaceAll(std::string& s, const char* from, const char* to)
        {
            const size_t n = std::strlen(from);
            if (n == 0) return;
            size_t p = 0;
            while ((p = s.find(from, p)) != std::string::npos)
            {
                s.replace(p, n, to);
                p += std::strlen(to);
            }
        }

        // Fold the Unicode punctuation the wiki uses into ASCII, so it renders in the Menomonia atlas (which has
        // no glyphs for U+2013 etc. and would draw ImGui's '?' fallback -- e.g. "1-25" came out as "1?25").
        void TransliteratePunct(std::string& s)
        {
            ReplaceAll(s, "\xE2\x80\x93", "-");    // en dash
            ReplaceAll(s, "\xE2\x80\x94", "-");    // em dash
            ReplaceAll(s, "\xE2\x80\x98", "'");    // left single quote
            ReplaceAll(s, "\xE2\x80\x99", "'");    // right single quote
            ReplaceAll(s, "\xE2\x80\x9C", "\"");   // left double quote
            ReplaceAll(s, "\xE2\x80\x9D", "\"");   // right double quote
            ReplaceAll(s, "\xE2\x80\xA6", "...");  // horizontal ellipsis
            ReplaceAll(s, "\xE2\x80\xA2", "*");    // bullet
            ReplaceAll(s, "\xC3\x97", "x");        // multiplication sign
            ReplaceAll(s, "\xE2\x80\xAF", " ");    // narrow no-break space
            ReplaceAll(s, "\xC2\xA0", " ");        // non-breaking space
        }

        std::string CleanText(std::string s)
        {
            s = DecodeHtmlEntities(s);
            ReplaceAll(s, "\xE2\x86\x97", ""); // external-link arrow icon text
            TransliteratePunct(s);
            return CollapseSpace(s);
        }

        std::string Attr(const GumboElement& e, const char* name)
        {
            if (GumboAttribute* a = gumbo_get_attribute(&e.attributes, name))
                return a->value ? a->value : "";
            return {};
        }

        bool ContainsClass(const std::string& classes, const char* needle)
        {
            const std::string want = Lower(needle);
            std::istringstream in(classes);
            std::string c;
            while (in >> c)
                if (Lower(c) == want) return true;
            return false;
        }

        bool HasClass(const GumboElement& e, const char* name)
        {
            return ContainsClass(Attr(e, "class"), name);
        }

        bool IsElement(const GumboNode* node, GumboTag tag)
        {
            return node && node->type == GUMBO_NODE_ELEMENT && node->v.element.tag == tag;
        }

        bool IsBlockTag(GumboTag tag)
        {
            return tag == GUMBO_TAG_DIV || tag == GUMBO_TAG_P || tag == GUMBO_TAG_LI ||
                   tag == GUMBO_TAG_UL || tag == GUMBO_TAG_OL;
        }

        void TextFromNode(const GumboNode* node, std::string& out)
        {
            if (!node) return;
            if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE)
            {
                if (node->v.text.text) out += node->v.text.text;
                return;
            }
            if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE) return;
            if (node->v.element.tag == GUMBO_TAG_IMG)
            {
                return;
            }
            if (node->v.element.tag == GUMBO_TAG_BR)
            {
                out.push_back(' ');
                return;
            }
            const GumboVector* children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
                TextFromNode((const GumboNode*)children->data[i], out);
            if (IsBlockTag(node->v.element.tag)) out.push_back(' ');
        }

        std::string NodeText(const GumboNode* node)
        {
            std::string s;
            TextFromNode(node, s);
            return CleanText(s);
        }

        const GumboNode* FindFirst(const GumboNode* node, GumboTag tag)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return nullptr;
            if (node->v.element.tag == tag) return node;
            const GumboVector* children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
                if (const GumboNode* hit = FindFirst((const GumboNode*)children->data[i], tag)) return hit;
            return nullptr;
        }

        const GumboNode* FindFirstWithClass(const GumboNode* node, const char* className)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return nullptr;
            if (HasClass(node->v.element, className)) return node;
            const GumboVector* children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
                if (const GumboNode* hit = FindFirstWithClass((const GumboNode*)children->data[i], className)) return hit;
            return nullptr;
        }

        const GumboNode* FindRailRoot(const GumboNode* node)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return nullptr;
            const GumboElement& e = node->v.element;
            if (HasClass(e, "infobox") || HasClass(e, "recipe-box") || HasClass(e, "tc-wiki-rail-fallback"))
                return node;
            const GumboVector* children = &e.children;
            for (unsigned int i = 0; i < children->length; ++i)
                if (const GumboNode* hit = FindRailRoot((const GumboNode*)children->data[i])) return hit;
            return nullptr;
        }

        const GumboNode* FindRailTable(const GumboNode* node)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return nullptr;
            const GumboElement& e = node->v.element;
            if (e.tag == GUMBO_TAG_TABLE && (HasClass(e, "infobox") || HasClass(e, "recipe-box"))) return node;
            const GumboVector* children = &e.children;
            for (unsigned int i = 0; i < children->length; ++i)
                if (const GumboNode* hit = FindRailTable((const GumboNode*)children->data[i])) return hit;
            return nullptr;
        }

        void FindFirstImage(const GumboNode* node, std::string& imageUrl, std::string& imageHref)
        {
            if (!node || !imageUrl.empty()) return;
            if (node->type != GUMBO_NODE_ELEMENT) return;

            const GumboElement& e = node->v.element;
            if (e.tag == GUMBO_TAG_A)
            {
                std::string priorHref = imageHref;
                std::string href = Attr(e, "href");
                if (!href.empty()) imageHref = ResolveWikiUrl(href);
                const GumboVector* children = &e.children;
                for (unsigned int i = 0; i < children->length; ++i)
                    FindFirstImage((const GumboNode*)children->data[i], imageUrl, imageHref);
                if (imageUrl.empty()) imageHref = std::move(priorHref);
                return;
            }

            if (e.tag == GUMBO_TAG_IMG)
            {
                std::string src = Attr(e, "src");
                if (!src.empty()) imageUrl = ResolveWikiUrl(src);
                return;
            }

            const GumboVector* children = &e.children;
            for (unsigned int i = 0; i < children->length; ++i)
                FindFirstImage((const GumboNode*)children->data[i], imageUrl, imageHref);
        }

        void FlushValue(std::string& text, std::vector<NativeRailItem>& out)
        {
            std::string clean = CleanText(text);
            text.clear();
            if (!clean.empty()) out.push_back({ clean, {} });
        }

        void CollectCellItems(const GumboNode* node, std::string& pending, std::vector<NativeRailItem>& out)
        {
            if (!node) return;
            if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE)
            {
                if (node->v.text.text) pending += node->v.text.text;
                return;
            }
            if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE) return;

            const GumboElement& e = node->v.element;
            if (e.tag == GUMBO_TAG_BR)
            {
                FlushValue(pending, out);
                return;
            }
            if (e.tag == GUMBO_TAG_IMG)
            {
                return;
            }
            if (e.tag == GUMBO_TAG_A)
            {
                FlushValue(pending, out);
                const std::string text = NodeText(node);
                std::string href = Attr(e, "href");
                if (!href.empty()) href = ResolveWikiUrl(href);
                if (!text.empty()) out.push_back({ text, href });
                return;
            }

            const bool block = IsBlockTag(e.tag);
            if (block) FlushValue(pending, out);
            const GumboVector* children = &e.children;
            for (unsigned int i = 0; i < children->length; ++i)
                CollectCellItems((const GumboNode*)children->data[i], pending, out);
            if (block) FlushValue(pending, out);
        }

        std::vector<NativeRailItem> CellItems(const GumboNode* cell)
        {
            std::vector<NativeRailItem> out;
            std::string pending;
            CollectCellItems(cell, pending, out);
            FlushValue(pending, out);
            return out;
        }

        void DirectCells(const GumboNode* tr, std::vector<const GumboNode*>& cells)
        {
            if (!IsElement(tr, GUMBO_TAG_TR)) return;
            const GumboVector* children = &tr->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
            {
                const GumboNode* child = (const GumboNode*)children->data[i];
                if (IsElement(child, GUMBO_TAG_TH) || IsElement(child, GUMBO_TAG_TD))
                    cells.push_back(child);
            }
        }

        void CollectRows(const GumboNode* node, std::vector<const GumboNode*>& rows)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return;
            if (node->v.element.tag == GUMBO_TAG_TR)
            {
                rows.push_back(node);
                return;
            }

            const GumboVector* children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
            {
                const GumboNode* child = (const GumboNode*)children->data[i];
                if (IsElement(child, GUMBO_TAG_TABLE) && child != node) continue;
                CollectRows(child, rows);
            }
        }

        void CollectDefinitionLists(const GumboNode* node, std::vector<const GumboNode*>& lists)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return;
            if (node->v.element.tag == GUMBO_TAG_DL)
            {
                lists.push_back(node);
                return;
            }
            const GumboVector* children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i)
                CollectDefinitionLists((const GumboNode*)children->data[i], lists);
        }

        int ParseFirstInt(const std::vector<NativeRailItem>& values);
        std::string LabelKey(std::string s);

        void AppendDefinitionRows(const GumboNode* root, NativeRail& rail)
        {
            std::vector<const GumboNode*> lists;
            CollectDefinitionLists(root, lists);
            for (const GumboNode* dl : lists)
            {
                std::string label;
                std::vector<NativeRailItem> values;
                auto flush = [&]() {
                    if (label.empty()) return;
                    NativeRailRow row;
                    row.label = label;
                    row.values = std::move(values);
                    if (LabelKey(row.label) == "api") rail.apiId = ParseFirstInt(row.values);
                    rail.rows.push_back(std::move(row));
                    label.clear();
                    values.clear();
                };

                const GumboVector* children = &dl->v.element.children;
                for (unsigned int i = 0; i < children->length; ++i)
                {
                    const GumboNode* child = (const GumboNode*)children->data[i];
                    if (!child || child->type != GUMBO_NODE_ELEMENT) continue;
                    if (child->v.element.tag == GUMBO_TAG_DT)
                    {
                        flush();
                        label = NodeText(child);
                    }
                    else if (child->v.element.tag == GUMBO_TAG_DD && !label.empty())
                    {
                        std::vector<NativeRailItem> items = CellItems(child);
                        values.insert(values.end(), items.begin(), items.end());
                    }
                }
                flush();
            }
        }

        int ParseFirstInt(const std::vector<NativeRailItem>& values)
        {
            for (const NativeRailItem& v : values)
            {
                const char* s = v.text.c_str();
                while (*s && !std::isdigit((unsigned char)*s)) ++s;
                if (*s) return std::atoi(s);
            }
            return 0;
        }

        std::string LabelKey(std::string s)
        {
            s = Lower(CollapseSpace(s));
            s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) {
                return !std::isalnum(ch);
            }), s.end());
            return s;
        }

        // A region/area infobox stat is the OUTER <span style="white-space:nowrap;">86<span class="inline-icon">
        // ...</span></span>. Collect those outer spans (the ones that CONTAIN an .inline-icon but aren't it).
        void CollectStatSpans(const GumboNode* node, std::vector<const GumboNode*>& out)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return;
            const GumboElement& e = node->v.element;
            if (e.tag == GUMBO_TAG_SPAN && !HasClass(e, "inline-icon") && FindFirstWithClass(node, "inline-icon"))
            {
                out.push_back(node);
                return;   // don't recurse -- the inner .inline-icon span belongs to this stat
            }
            const GumboVector* children = &e.children;
            for (unsigned int i = 0; i < children->length; ++i)
                CollectStatSpans((const GumboNode*)children->data[i], out);
        }

        void CollectStats(const GumboNode* root, NativeRail& rail)
        {
            const GumboNode* stats = FindFirstWithClass(root, "statistics");
            if (!stats) return;
            std::vector<const GumboNode*> spans;
            CollectStatSpans(stats, spans);
            for (const GumboNode* span : spans)
            {
                NativeRailStat st;
                st.count = NodeText(span);   // img produces no text, so this is just the number
                std::string href;
                FindFirstImage(span, st.iconUrl, href);
                if (const GumboNode* a = FindFirst(span, GUMBO_TAG_A))
                    st.label = Attr(a->v.element, "title");
                if (!st.iconUrl.empty()) rail.stats.push_back(std::move(st));
            }
        }

        // Collect the non-icon infobox images: the area map (<p class="image_wrapper">, with its <small> caption),
        // any "Image(s)" screenshot, and (for item/decoration infoboxes) the gallery screenshots -- each a bare
        // <a class="image">. Skips the header icon (.infobox-icon) + stat icons (.statistics/.inline-icon). minWidth
        // gates by the rendered img width so small inline row icons aren't mistaken for gallery images (0 = no gate).
        void CollectAreaImageNodes(const GumboNode* node, bool inWrapper, std::string wrapperCaption,
                                   std::vector<NativeRailImage>& out, int minWidth)
        {
            if (!node || node->type != GUMBO_NODE_ELEMENT) return;
            const GumboElement& e = node->v.element;
            if (HasClass(e, "statistics") || HasClass(e, "inline-icon") || HasClass(e, "infobox-icon")) return;

            if (HasClass(e, "image_wrapper"))
            {
                std::string cap;
                if (const GumboNode* s = FindFirst(node, GUMBO_TAG_SMALL)) cap = NodeText(s);
                const GumboVector* ch = &e.children;
                for (unsigned int i = 0; i < ch->length; ++i)
                    CollectAreaImageNodes((const GumboNode*)ch->data[i], true, cap, out, minWidth);
                return;
            }

            if (e.tag == GUMBO_TAG_A && HasClass(e, "image"))
            {
                NativeRailImage im;
                FindFirstImage(node, im.url, im.href);
                int iw = 0;
                if (const GumboNode* img = FindFirst(node, GUMBO_TAG_IMG)) iw = std::atoi(Attr(img->v.element, "width").c_str());
                if (!im.href.empty()) im.href = ResolveWikiUrl(Attr(e, "href"));
                im.caption = inWrapper ? wrapperCaption : std::string();
                if (!im.url.empty() && iw >= minWidth && out.size() < 4) out.push_back(std::move(im));
                return;   // the <img> inside is this anchor's image
            }

            const GumboVector* ch = &e.children;
            for (unsigned int i = 0; i < ch->length; ++i)
                CollectAreaImageNodes((const GumboNode*)ch->data[i], inWrapper, wrapperCaption, out, minWidth);
        }
    }

    NativeRail BuildNativeRail(const std::string& railHtml, const std::string& fallbackTitle)
    {
        NativeRail rail;
        rail.title = CleanText(fallbackTitle);
        if (railHtml.empty()) return rail;

        try
        {
            GumboOutput* gumbo = gumbo_parse_with_options(&kGumboDefaultOptions, railHtml.data(), railHtml.size());
            if (!gumbo) return rail;
            struct Guard
            {
                GumboOutput* g;
                ~Guard() { if (g) gumbo_destroy_output(&kGumboDefaultOptions, g); }
            } guard{ gumbo };

            const GumboNode* root = FindRailRoot(gumbo->root);
            if (!root) root = gumbo->root;

            // Only an ITEM infobox's "API" row is an item id (looked up in the item catalog for the TP card / name /
            // icon). Area / zone / strike / instance infoboxes ("infobox area" etc.) put a MAP id in the API row, so
            // their apiId must NOT be treated as an item -- else the rail title/icon became a random item by id.
            rail.isItem = (root->type == GUMBO_NODE_ELEMENT) && HasClass(root->v.element, "item");

            // A region/area infobox has a completion-stats block and/or a captioned map image. For those we render
            // the stats row + the map/screenshot images (NOT a header icon -- the first <img> there is a stat icon,
            // which is what put a stray heart in the header). Plain item infoboxes keep the small header icon.
            const bool areaStyle = FindFirstWithClass(root, "statistics") || FindFirstWithClass(root, "image_wrapper");
            if (areaStyle)
            {
                CollectStats(root, rail);
                CollectAreaImageNodes(root, false, {}, rail.images, 0);
            }
            else
            {
                FindFirstImage(root, rail.imageUrl, rail.imageHref);          // the small header icon
                CollectAreaImageNodes(root, false, {}, rail.images, 80);      // + large gallery/screenshot images (item/decoration infoboxes)
                if (!rail.imageUrl.empty())                                   // never repeat the header icon as a gallery image
                    rail.images.erase(std::remove_if(rail.images.begin(), rail.images.end(),
                        [&](const NativeRailImage& im) { return im.url == rail.imageUrl; }), rail.images.end());
            }

            if (const GumboNode* h = FindFirst(root, GUMBO_TAG_H2))
            {
                const std::string t = NodeText(h);
                if (!t.empty()) rail.title = t;
            }
            if (const GumboNode* h = FindFirstWithClass(root, "heading"))
            {
                const std::string t = NodeText(h);
                if (!t.empty()) rail.title = t;
            }

            AppendDefinitionRows(root, rail);
            if (!rail.rows.empty()) return rail;

            const GumboNode* table = FindRailTable(root);
            if (!table) table = IsElement(root, GUMBO_TAG_TABLE) ? root : FindFirst(root, GUMBO_TAG_TABLE);
            if (!table) return rail;

            std::vector<const GumboNode*> trs;
            CollectRows(table, trs);
            for (const GumboNode* tr : trs)
            {
                std::vector<const GumboNode*> cells;
                DirectCells(tr, cells);
                if (cells.empty()) continue;

                if (cells.size() == 1)
                {
                    const std::string text = NodeText(cells[0]);
                    if (!text.empty() && (rail.title.empty() || rail.title == CleanText(fallbackTitle)))
                        rail.title = text;
                    continue;
                }

                NativeRailRow row;
                row.label = NodeText(cells[0]);
                for (size_t i = 1; i < cells.size(); ++i)
                {
                    std::vector<NativeRailItem> items = CellItems(cells[i]);
                    row.values.insert(row.values.end(), items.begin(), items.end());
                }
                if (row.label.empty() && row.values.empty()) continue;
                if (LabelKey(row.label) == "api") rail.apiId = ParseFirstInt(row.values);
                rail.rows.push_back(std::move(row));
            }
        }
        catch (...) {}
        return rail;
    }
}
