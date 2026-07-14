#include "ui/wiki/WikiReader.h"

#include "Shared.h"
#include "app/App.h"
#include "app/ItemCatalog.h"
#include "app/TpEligibility.h"
#include "app/TpPrices.h"
#include "app/wiki/WikiService.h"
#include "render/glyphs/Glyphs.h"
#include "ui/GuideViewer.h"
#include "ui/Gw2Ui.h"
#include "ui/wiki/WikiContainer.h"
#include "ui/wiki/WikiHtmlSanitizer.h"
#include "ui/wiki/WikiNativeRail.h"
#include "ui/wiki/WikiStyleAdapter.h"
#include "util/ImageCache.h"
#include "util/Trading.h"

#include <litehtml.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // Open-state lives in app.state.showWiki; window size + rail width live in app.config (persisted).
    // Only transient per-frame UI state stays file-static here (matches Dashboard/OptionsTab convention).
    bool s_wasOpen = false;
    int s_activeTab = 0;
    float s_prevWinW = 0.f; // last-seen window size, to persist a user resize via settingsDirty
    float s_prevWinH = 0.f;
    constexpr float kRailMinW = 250.f;
    char s_query[256] = "";
    std::string s_lastSearchQuery;
    double s_searchEditedAt = 0.0;
    std::string s_externalUrl;
    bool s_externalPromptRequested = false;
    bool s_libraryOpen = false;
    bool s_searchPanelOpen = false;
    std::string s_goPendingQuery;
    std::string s_pendingOpen; // a deferred App-free RequestOpen() target, applied next RenderReader frame
    std::string s_pendingArticleAnchor;
    std::string s_lightboxUrl;
    ImVec2 s_libraryAnchorMin = ImVec2(0.f, 0.f);
    ImVec2 s_libraryAnchorMax = ImVec2(0.f, 0.f);
    bool s_libraryAnchorValid = false;
    struct RenderDoc
    {
        std::unique_ptr<Wiki::Container> container;
        std::shared_ptr<litehtml::document> doc;
        float lastWidth = -1.f; // last width fed to doc->render(); -1 forces a layout (perf: relayout on width change only)
        int lastDocH = 0;       // cached content height from that layout

        RenderDoc() = default;
        RenderDoc(RenderDoc &&) = default;
        // litehtml's ~document() calls container->delete_font(), so `doc` MUST tear down BEFORE `container`.
        // Destruction already does (reverse declaration order destroys doc first), but the DEFAULT member-wise
        // move-assign runs in declaration order (container, then doc) -- it would free the container first and
        // then run ~document against the freed container = UAF. This override resets doc first so move-assigning
        // a RenderDoc (e.g. `s_page = PageRenderState{}` on a page rebuild, or s_tabs.erase shifting tabs) is safe.
        RenderDoc &operator=(RenderDoc &&o) noexcept
        {
            if (this != &o)
            {
                doc.reset();
                container = std::move(o.container);
                doc = std::move(o.doc);
                lastWidth = o.lastWidth;
                lastDocH = o.lastDocH;
            }
            return *this;
        }
    };

    struct PageRenderState
    {
        std::string key;
        Wiki::SanitizedHtml sanitized;
        Wiki::NativeRail nativeRail;
        std::string css;
        RenderDoc article;
    };

    // Browser-style ARTICLE TABS: each tab keeps its own loaded page + rendered litehtml doc + back/forward, so
    // switching is INSTANT (no re-load). The WikiService still loads ONE page at a time; a completed load is copied
    // into the tab that requested it. The ACTIVE tab is the one rendered. (Distinct from the Library top tabs
    // Wiki/Bookmarks/History in s_activeTab.)
    struct WikiTab
    {
        Wiki::Page page;                        // loaded page DATA (title / sections / url / html), cached per tab
        PageRenderState render;                 // the built litehtml doc + native rail (cached render state)
        std::string docKey;                     // render.key echo -> rebuild render only when the page changes
        std::vector<std::string> back, forward; // per-tab navigation history
        std::string pendingTitle;               // a title this tab is loading (empty when idle / loaded)
        uint32_t uid = 0;                       // stable id for the article child window (per-tab scroll memory)
    };
    std::vector<WikiTab> s_tabs;
    int s_artTab = 0; // active ARTICLE tab index
    uint32_t s_nextUid = 1;
    bool s_tabsRestored = false; // one-time restore of last session's open tabs
    bool s_tabsDirty = false;    // open-tab set / active changed -> persist (debounced, once per frame)
    WikiTab &Active()
    {
        if (s_tabs.empty())
        {
            s_tabs.push_back(WikiTab{});
            s_tabs.back().uid = s_nextUid++;
        }
        s_artTab = std::clamp(s_artTab, 0, (int)s_tabs.size() - 1);
        return s_tabs[s_artTab];
    }

    const Gw2Ui::Tab kWikiTabs[] = {
        {0, "Wiki", -1, "data\\textures\\ui\\gw2.png"},
        {0, "Bookmarks", (int)Render::Glyph::Star},
        {0, "History", (int)Render::Glyph::Clock},
    };
    constexpr int kWikiTabCount = (int)(sizeof(kWikiTabs) / sizeof(kWikiTabs[0]));

    bool StartsWithInsensitive(const std::string &s, const char *prefix)
    {
        const size_t n = std::strlen(prefix);
        if (s.size() < n)
            return false;
        for (size_t i = 0; i < n; ++i)
            if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i]))
                return false;
        return true;
    }

    uint64_t Fnv1a64(const std::string &s)
    {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

    std::string WikiImageTextureId(const std::string &url)
    {
        std::ostringstream oss;
        oss << "TC_WIKI_IMG_" << std::hex << Fnv1a64(url);
        return oss.str();
    }

    // Queue ONE wiki image onto the ImageCache worker pool (download-to-disk, off-thread). The
    // texture id matches the container/rail draw path (identical formula), so litehtml + the native rail reuse
    // this exact cache entry -- prefetching here just starts the parallel download earlier (before first layout).
    void PrefetchWikiImage(const std::string &url)
    {
        if (url.empty())
            return;
        const std::string id = WikiImageTextureId(url);
        ImageCache::PrefetchUrl(id.c_str(), url.c_str());
    }

    bool IsGw2WikiArticle(const std::string &url)
    {
        return StartsWithInsensitive(url, "https://wiki.guildwars2.com/wiki/") ||
               StartsWithInsensitive(url, "http://wiki.guildwars2.com/wiki/") ||
               StartsWithInsensitive(url, "wiki.guildwars2.com/wiki/");
    }

    std::string Trim(std::string s)
    {
        while (!s.empty() && std::isspace((unsigned char)s.front()))
            s.erase(s.begin());
        while (!s.empty() && std::isspace((unsigned char)s.back()))
            s.pop_back();
        return s;
    }

    std::string NormalizeTitle(std::string s)
    {
        s = Trim(s);
        if (IsGw2WikiArticle(s))
        {
            size_t p = s.find("/wiki/");
            if (p != std::string::npos)
                s = s.substr(p + 6);
        }
        const size_t hash = s.find('#');
        if (hash != std::string::npos)
            s.erase(hash);
        for (char &c : s)
        {
            if (c == '_')
                c = ' ';
            else
                c = (char)std::tolower((unsigned char)c);
        }
        return Trim(s);
    }

    bool SearchResultsMatchQuery(App &app, const std::string &query)
    {
        return NormalizeTitle(app.wiki.SearchResultQuery()) == NormalizeTitle(query);
    }

    const Wiki::SearchResult *ExactSearchMatch(App &app, const std::string &query)
    {
        const std::string want = NormalizeTitle(query);
        if (want.empty() || !SearchResultsMatchQuery(app, query))
            return nullptr;
        for (const Wiki::SearchResult &r : app.wiki.SearchResults())
            if (NormalizeTitle(r.title) == want)
                return &r;
        return nullptr;
    }

    void RequestSearch(App &app, const std::string &query)
    {
        const std::string q = Trim(query);
        if (q.size() < 2)
            return;
        if (NormalizeTitle(app.wiki.SearchLoadingQuery()) == NormalizeTitle(q))
            return;
        if (SearchResultsMatchQuery(app, q) && !app.wiki.SearchResults().empty())
            return;
        s_lastSearchQuery = q;
        app.wiki.Search(q);
    }

    void CopyToQuery(const std::string &title)
    {
        std::snprintf(s_query, sizeof(s_query), "%s", title.c_str());
    }

    void OpenBrowser(const std::string &url)
    {
        if (url.empty())
            return;
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void QueueExternalPrompt(const std::string &url)
    {
        if (url.empty())
            return;
        s_externalUrl = url;
        s_externalPromptRequested = true;
    }

    // Wiki thumbnails look like https://wiki.guildwars2.com/images/thumb/1/12/Ascalon_map.jpg/122px-Ascalon_map.jpg.
    // The full-size original is the same path with "/thumb/" removed and the trailing "/<NNNpx>-<file>" segment
    // dropped: https://wiki.guildwars2.com/images/1/12/Ascalon_map.jpg. Non-thumb URLs are returned unchanged.
    std::string FullSizeWikiImage(const std::string &url)
    {
        const size_t t = url.find("/images/thumb/");
        if (t == std::string::npos)
            return url;
        std::string s = url;
        s.erase(t + 8, 6); // remove "thumb/" -> ".../images/1/12/<file>/<NNNpx>-<file>"
        const size_t lastSlash = s.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash > t)
            s.erase(lastSlash); // drop the "/<NNNpx>-<file>"
        return s;
    }

    void OpenLightbox(const std::string &url)
    {
        if (url.empty())
            return;
        s_lightboxUrl = url;
        const std::string id = WikiImageTextureId(url);
        ImageCache::PrefetchUrl(id.c_str(), url.c_str());
    }

    void OpenTitle(App &app, const std::string &titleOrUrl, bool recordHistory)
    {
        WikiTab &t = Active();
        if (recordHistory && !t.page.title.empty())
        {
            t.back.push_back(t.page.title);
            t.forward.clear();
            if (t.back.size() > 80)
                t.back.erase(t.back.begin());
        }
        CopyToQuery(titleOrUrl);
        t.pendingTitle = titleOrUrl; // the active tab is awaiting this load
        app.wiki.OpenPage(titleOrUrl);
        s_tabsDirty = true; // the active tab's article changed -> persist
        s_searchPanelOpen = false;
        s_goPendingQuery.clear();
    }

    void SubmitSearchOrOpen(App &app)
    {
        const std::string q = Trim(s_query);
        if (q.empty())
            return;
        if (IsGw2WikiArticle(q))
        {
            OpenTitle(app, q, true);
            return;
        }
        if (const Wiki::SearchResult *exact = ExactSearchMatch(app, q))
        {
            OpenTitle(app, exact->title, true);
            return;
        }
        s_searchPanelOpen = true;
        s_goPendingQuery = q;
        RequestSearch(app, q);
    }

    void ResolvePendingGo(App &app)
    {
        if (s_goPendingQuery.empty() || app.wiki.IsSearching())
            return;
        const std::string pending = s_goPendingQuery;
        s_goPendingQuery.clear();
        if (const Wiki::SearchResult *exact = ExactSearchMatch(app, pending))
            OpenTitle(app, exact->title, true);
        else
            s_searchPanelOpen = true;
    }

    void HandleClickedUrl(App &app, const std::string &url)
    {
        if (url.empty())
            return;
        constexpr const char *kChatLinkPrefix = "#tc-chatlink:";
        constexpr size_t kChatLinkPrefixLen = 13;
        if (url.compare(0, kChatLinkPrefixLen, kChatLinkPrefix) == 0)
        {
            SetClipboard(url.substr(kChatLinkPrefixLen));
            ViewerAlert("Copied chat link.");
            return;
        }
        if (url[0] == '#')
        {
            s_pendingArticleAnchor = url.substr(1);
            return;
        }
        if (IsGw2WikiArticle(url))
        {
            OpenTitle(app, url, true);
            return;
        }
        if (app.config.wikiConfirmExternal)
            QueueExternalPrompt(url); // leaving = explicit intent (default)
        else
            OpenBrowser(url);
    }

    int FindAnchorY(const litehtml::element::ptr &el, const std::string &anchor)
    {
        if (!el || anchor.empty())
            return -1;
        const char *id = el->get_attr("id");
        const char *name = el->get_attr("name");
        if ((id && anchor == id) || (name && anchor == name))
            return (int)el->get_placement().y;
        for (const auto &child : el->children())
        {
            const int y = FindAnchorY(child, anchor);
            if (y >= 0)
                return y;
        }
        return -1;
    }

    int FindContentBottom(const litehtml::element::ptr &el)
    {
        if (!el)
            return 0;
        const litehtml::position pos = el->get_placement();
        const char *tag = el->get_tagName();
        const bool viewportElement = tag && (std::strcmp(tag, "html") == 0 || std::strcmp(tag, "body") == 0);
        int bottom = viewportElement ? 0 : (int)(pos.y + pos.height);
        for (const auto &child : el->children())
            bottom = std::max(bottom, FindContentBottom(child));
        return bottom;
    }

    bool DrawLiteDocument(const char *id, RenderDoc &rd, float width, float minHeight, App &app,
                          const std::string *scrollToAnchor = nullptr, bool reserveContentOnly = false)
    {
        if (!rd.doc || !rd.container || width <= 1.f)
            return false;
        const float viewportH = std::max(1.f, ImGui::GetContentRegionAvail().y);
        rd.container->SetViewport(ImVec2(width, viewportH));

        const litehtml::pixel_t layoutW = (litehtml::pixel_t)std::floor(width);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        // Layout + paint touch litehtml; keep them behind a try so a paint/layout failure can never
        // propagate out of this child and unbalance ImGui's Begin/End stack.
        try
        {
            // Relayout ONLY when the width changed (or the doc was just rebuilt: RebuildDocsIfNeeded
            // default-constructs RenderDoc with lastWidth == -1). litehtml retains its render tree
            // between frames, so steady-state cost is draw-only. (Hard perf rule: relayout on width/page.)
            if ((float)layoutW != rd.lastWidth)
            {
                rd.doc->render(layoutW);
                rd.lastDocH = reserveContentOnly
                                  ? std::max(1, FindContentBottom(rd.doc->root()))
                                  : std::max(1, (int)rd.doc->height());
                rd.lastWidth = (float)layoutW;
            }

            if (scrollToAnchor && !scrollToAnchor->empty())
            {
                const int y = FindAnchorY(rd.doc->root(), *scrollToAnchor);
                if (y >= 0)
                    ImGui::SetScrollY((float)std::max(0, y - 8));
            }

            rd.container->SetOrigin(origin);
            rd.container->ResetFrame();

            const float scrollY = ImGui::GetScrollY();
            litehtml::position clip(0, (litehtml::pixel_t)std::floor(scrollY),
                                    (litehtml::pixel_t)std::ceil(width + 2.f),
                                    (litehtml::pixel_t)std::ceil(viewportH + 24.f));
            rd.doc->draw(0, 0, 0, &clip);
        }
        catch (...)
        {
        }

        const float height = std::max((float)rd.lastDocH, minHeight);
        ImGui::InvisibleButton(id, ImVec2(width, std::max(1.f, height)));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
        {
            try
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const litehtml::pixel_t mx = (litehtml::pixel_t)std::floor(mouse.x - origin.x);
                const litehtml::pixel_t my = (litehtml::pixel_t)std::floor(mouse.y - origin.y);
                litehtml::position::vector redraw;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    rd.doc->on_lbutton_down(mx, my, mx, my, redraw);
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    rd.doc->on_lbutton_up(mx, my, mx, my, redraw);
                rd.doc->on_mouse_over(mx, my, mx, my, redraw);
            }
            catch (...)
            {
            }
        }

        if (!rd.container->Tooltip().empty())
            Gw2Ui::Tooltip(rd.container->Tooltip().c_str());

        std::string image = rd.container->TakeClickedImageUrl();
        if (!image.empty())
        {
            OpenLightbox(image);
            return true;
        }

        std::string clicked = rd.container->TakeClickedUrl();
        if (!clicked.empty())
        {
            HandleClickedUrl(app, clicked);
            return true;
        }
        return false;
    }

    std::string LowerAscii(std::string s)
    {
        for (char &c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    }

    std::string RailLabelKey(std::string s)
    {
        s = LowerAscii(s);
        s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c)
                               { return !std::isalnum(c); }),
                s.end());
        return s;
    }

    int FindRailRow(std::vector<Wiki::NativeRailRow> &rows, const char *label)
    {
        const std::string want = RailLabelKey(label ? label : "");
        for (int i = 0; i < (int)rows.size(); ++i)
            if (RailLabelKey(rows[i].label) == want)
                return i;
        return -1;
    }

    void SetRailRow(std::vector<Wiki::NativeRailRow> &rows, const char *label,
                    std::initializer_list<std::string> values)
    {
        int idx = FindRailRow(rows, label);
        if (idx < 0)
        {
            Wiki::NativeRailRow row;
            row.label = label ? label : "";
            rows.push_back(std::move(row));
            idx = (int)rows.size() - 1;
        }

        rows[idx].values.clear();
        for (const std::string &v : values)
            if (!v.empty())
                rows[idx].values.push_back({v, {}});
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

    std::string SimpleItemChatLink(int itemId)
    {
        if (itemId <= 0)
            return {};
        unsigned char bytes[6] = {
            0x02, 0x01,
            (unsigned char)(itemId & 0xFF),
            (unsigned char)((itemId >> 8) & 0xFF),
            (unsigned char)((itemId >> 16) & 0xFF),
            (unsigned char)((itemId >> 24) & 0xFF)};
        return "[&" + Base64(bytes, sizeof(bytes)) + "]";
    }

    constexpr float kRailTitleFs = 18.f;
    constexpr float kRailLabelFs = 18.f;
    constexpr float kRailValueFs = 18.f;
    constexpr float kRailStatFs = 15.5f; // the compact completion-stats row stays small (count next to its icon)

    bool IsChatLinkText(const std::string &text)
    {
        return text.size() >= 4 && text.rfind("[&", 0) == 0 && text.back() == ']';
    }

    float RailValueHeight(const Wiki::NativeRailItem &item, float width, float fontSize)
    {
        if (item.text.empty())
            return 0.f;
        return std::max(20.f, Gw2Ui::MeasureWrappedHeight(item.text.c_str(), fontSize, std::max(20.f, width)));
    }

    float RailRowHeight(const Wiki::NativeRailRow &row, float labelW, float valueW)
    {
        float valuesH = 0.f;
        for (const Wiki::NativeRailItem &item : row.values)
        {
            const float h = RailValueHeight(item, valueW, kRailValueFs);
            if (h <= 0.f)
                continue;
            if (valuesH > 0.f)
                valuesH += 2.f;
            valuesH += h;
        }
        if (valuesH <= 0.f)
            valuesH = 20.f;
        const float labelH = row.label.empty() ? 0.f : Gw2Ui::MeasureWrappedHeight(row.label.c_str(), kRailLabelFs, std::max(20.f, labelW));
        return std::ceil(std::max(30.f, std::max(valuesH, labelH) + 9.f));
    }

    void EnrichRailRows(std::vector<Wiki::NativeRailRow> &rows, int itemId)
    {
        if (itemId <= 0)
            return;

        const ItemCatalog::Item &item = ItemCatalog::ById(itemId);
        const std::string chat = !item.chatLink.empty() ? item.chatLink : SimpleItemChatLink(itemId);
        if (!chat.empty())
            SetRailRow(rows, "Game link", {chat});
        SetRailRow(rows, "API", {std::to_string(itemId)});

        if (item.id > 0 && item.vendorValue > 0)
            SetRailRow(rows, "Value", {Trading::Coins(item.vendorValue)});

        TpEligibility::Warm();
        if (!TpEligibility::IsReady())
        {
            SetRailRow(rows, "Trading post", {"Loading Trading Post item data..."});
        }
        else if (!TpEligibility::IsTradeable(itemId))
        {
            SetRailRow(rows, "Trading post", {"Not sellable"});
        }
        else
        {
            TpPrices::Want(itemId);
            TpPrices::Price tp;
            if (!TpPrices::Get(itemId, tp))
            {
                SetRailRow(rows, "Trading post", {"Loading prices..."});
            }
            else if (!tp.tradeable)
            {
                SetRailRow(rows, "Trading post", {"Not sellable"});
            }
            else
            {
                std::vector<std::string> priceLines;
                if (tp.sellUnit > 0)
                    priceLines.push_back("Sell: " + Trading::Coins(tp.sellUnit));
                if (tp.buyUnit > 0)
                    priceLines.push_back("Buy: " + Trading::Coins(tp.buyUnit));
                if (priceLines.empty())
                    priceLines.push_back("No active orders");

                int idx = FindRailRow(rows, "Trading post");
                if (idx < 0)
                {
                    Wiki::NativeRailRow row;
                    row.label = "Trading post";
                    rows.push_back(std::move(row));
                    idx = (int)rows.size() - 1;
                }
                rows[idx].values.clear();
                for (const std::string &line : priceLines)
                    rows[idx].values.push_back({line, {}});
            }
        }
    }

    void DrawRailValue(App &app, const Wiki::NativeRailItem &item, ImVec2 a, ImVec2 b, int rowIndex, int valueIndex)
    {
        if (item.text.empty())
            return;
        const bool link = !item.href.empty();
        const bool chatLink = IsChatLinkText(item.text);
        const bool clickable = link || chatLink;
        bool hovered = false;
        bool clicked = false;
        if (clickable)
        {
            ImGui::PushID(rowIndex);
            ImGui::PushID(valueIndex);
            ImGui::SetCursorScreenPos(a);
            clicked = ImGui::InvisibleButton("##railValue", ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)));
            hovered = ImGui::IsItemHovered();
            if (hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (hovered && chatLink)
                Gw2Ui::Tooltip("Click to copy chat link");
            ImGui::PopID();
            ImGui::PopID();
        }

        const ImU32 col = (link || chatLink)
                              ? (hovered ? IM_COL32(156, 203, 255, 255) : IM_COL32(103, 175, 255, 255))
                              : IM_COL32(236, 228, 206, 255);
        Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), a, b, item.text.c_str(), Gw2Ui::HAlign::Left,
                       Gw2Ui::VAlign::Top, col, false, nullptr, kRailValueFs, std::max(20.f, b.x - a.x));
        if (clickable && hovered)
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y - 2.f), ImVec2(std::min(b.x, a.x + Gw2Ui::MeasureWidth(item.text.c_str(), kRailValueFs)), b.y - 2.f),
                                                Gw2Ui::Alpha(col, 180), 1.f);
        if (clicked && chatLink)
        {
            SetClipboard(item.text);
            ViewerAlert("Copied chat link.");
        }
        else if (clicked)
        {
            HandleClickedUrl(app, item.href);
        }
    }

    void DrawNativeRail(App &app, const Wiki::NativeRail &rail, float width)
    {
        const float w = std::max(120.f, width);
        std::vector<Wiki::NativeRailRow> rows = rail.rows;
        const int itemId = rail.isItem ? rail.apiId : 0; // only an ITEM infobox's API id is an item; area/zone/strike use a MAP id
        const ItemCatalog::Item &item = itemId > 0 ? ItemCatalog::ById(itemId) : ItemCatalog::ById(0);
        EnrichRailRows(rows, itemId);

        std::string title = !rail.title.empty() ? rail.title : "Wiki";
        if (item.id > 0 && !item.name.empty())
            title = item.name;

        std::string imageUrl = rail.imageUrl;
        if (item.id > 0 && !item.icon.empty())
            imageUrl = item.icon;

        const bool bannerOnly = rows.empty() && !imageUrl.empty();
        const std::string imageTexId = imageUrl.empty() ? std::string() : WikiImageTextureId(imageUrl);
        const Texture_t *railImageTex = imageUrl.empty() ? nullptr : ImageCache::GetUrl(imageTexId.c_str(), imageUrl.c_str());
        const float pad = 8.f;
        const float labelW = std::clamp(w * 0.36f, 92.f, 124.f);
        const float sepLocalX = labelW + 8.f;
        const float valueLocalX = sepLocalX + 9.f;
        const float valueW = std::max(36.f, w - valueLocalX - pad);
        const float iconSize = (imageUrl.empty() || bannerOnly) ? 0.f : 42.f;
        const float titleW = std::max(40.f, w - (iconSize > 0.f ? iconSize + 22.f : 18.f));
        const float titleH = Gw2Ui::MeasureWrappedHeight(title.c_str(), kRailTitleFs, titleW);
        const float headerH = std::ceil(std::max(64.f, titleH + 22.f));

        std::vector<float> rowHeights;
        rowHeights.reserve(rows.size());
        const float bannerW = std::max(1.f, w - pad * 2.f);
        float bannerH = 0.f;
        if (bannerOnly)
        {
            if (railImageTex && railImageTex->Width > 0 && railImageTex->Height > 0)
                bannerH = bannerW * (float)railImageTex->Height / (float)railImageTex->Width;
            else
                bannerH = 126.f;
            bannerH = std::clamp(bannerH, 82.f, 180.f);
        }

        // ---- region/area sections (stats + map/screenshot images), only when NOT the simple banner-only case ----
        const float statsIcon = 18.f, statsTextGap = 4.f, statsGap = 14.f, statsLineH = 24.f;
        std::vector<ImVec2> statPos; // per-stat (dx, dy) within the stats block
        float statsBlockH = 0.f;
        if (!bannerOnly && !rail.stats.empty())
        {
            float x = 0.f, lineY = 0.f;
            for (const Wiki::NativeRailStat &st : rail.stats)
            {
                const float tw = Gw2Ui::MeasureWidth(st.count.c_str(), kRailStatFs);
                const float sw = tw + statsTextGap + statsIcon;
                if (x > 0.f && x + sw > bannerW)
                {
                    x = 0.f;
                    lineY += statsLineH;
                }
                statPos.push_back(ImVec2(x, lineY));
                x += sw + statsGap;
            }
            statsBlockH = lineY + statsLineH;
        }

        std::vector<float> imageHeights; // drawn image height (excludes its caption)
        imageHeights.reserve(rail.images.size());
        float imagesBlockH = 0.f;
        if (!bannerOnly)
        {
            for (const Wiki::NativeRailImage &im : rail.images)
            {
                const std::string tid = WikiImageTextureId(im.url);
                const Texture_t *tex = ImageCache::GetUrl(tid.c_str(), im.url.c_str());
                float ih = (tex && tex->Width > 0 && tex->Height > 0)
                               ? bannerW * (float)tex->Height / (float)tex->Width
                               : 120.f;
                ih = std::clamp(ih, 60.f, 240.f);
                imageHeights.push_back(ih);
                imagesBlockH += ih + (im.caption.empty() ? 0.f : 18.f) + 8.f; // image + caption + gap
            }
        }

        float rowsH = 0.f;
        for (const Wiki::NativeRailRow &row : rows)
        {
            const float h = RailRowHeight(row, labelW, valueW);
            rowHeights.push_back(h);
            rowsH += h;
        }

        // bodyH must be summed with the SAME increments the draw pass applies to `y` below.
        float bodyH;
        if (bannerOnly)
        {
            bodyH = bannerH + pad * 2.f;
        }
        else
        {
            bodyH = 0.f;
            if (!rail.stats.empty())
                bodyH += pad + statsBlockH;
            if (!rail.images.empty())
                bodyH += pad + imagesBlockH;
            if (!rows.empty())
                bodyH += 12.f + rowsH + 8.f;
        }
        const float panelH = headerH + bodyH;

        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 b(p.x + w, p.y + panelH);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        // Rounded corners (3px) to match the Contents card below it. The header fill rounds only its TOP corners
        // (ImDrawCornerFlags_Top) so its bottom edge stays flush with the body; the 1px shadow line beneath it is
        // an internal divider and stays square.
        dl->AddRectFilled(p, b, IM_COL32(24, 22, 21, 214), 3.f);
        dl->AddRect(p, b, IM_COL32(156, 128, 82, 128), 3.f);

        const ImVec2 headerB(p.x + w, p.y + headerH);
        dl->AddRectFilled(p, headerB, IM_COL32(127, 50, 28, 222), 3.f, ImDrawCornerFlags_Top);
        dl->AddRectFilled(ImVec2(p.x, headerB.y - 1.f), headerB, IM_COL32(0, 0, 0, 95));

        ImVec2 titleA(p.x + 10.f, p.y + 7.f);
        ImVec2 titleB(p.x + w - 10.f - (iconSize > 0.f ? iconSize + 8.f : 0.f), p.y + headerH - 7.f);
        Gw2Ui::LabelDL(dl, titleA, titleB, title.c_str(), Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       IM_COL32(255, 244, 221, 255), true, nullptr, kRailTitleFs, titleW, 1.35f);

        if (!imageUrl.empty() && !bannerOnly)
        {
            const ImVec2 ia(std::floor(p.x + w - iconSize - 9.f), std::floor(p.y + (headerH - iconSize) * 0.5f));
            const ImVec2 ib(ia.x + iconSize, ia.y + iconSize);
            ImGui::SetCursorScreenPos(ia);
            ImGui::InvisibleButton("##railImage", ImVec2(iconSize, iconSize));
            const bool imageHovered = ImGui::IsItemHovered();
            if (imageHovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (railImageTex)
                dl->AddImage((ImTextureID)railImageTex->Resource, ia, ib);
            else
                dl->AddRectFilled(ia, ib, IM_COL32(42, 34, 24, 210));
            dl->AddRect(ia, ib, IM_COL32(0, 0, 0, 210), 0.f, 0, 2.f);
            if (imageHovered)
            {
                Gw2Ui::Tooltip("Open image");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    OpenLightbox(FullSizeWikiImage(imageUrl));
            }
        }

        if (bannerOnly)
        {
            const ImVec2 ia(std::floor(p.x + pad), std::floor(p.y + headerH + pad));
            const ImVec2 ib(std::floor(p.x + w - pad), std::floor(ia.y + bannerH));
            ImGui::SetCursorScreenPos(ia);
            ImGui::InvisibleButton("##railBanner", ImVec2(std::max(1.f, ib.x - ia.x), std::max(1.f, ib.y - ia.y)));
            const bool imageHovered = ImGui::IsItemHovered();
            if (imageHovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (railImageTex)
            {
                dl->AddRectFilled(ImVec2(ia.x - 1.f, ia.y - 1.f), ImVec2(ib.x + 1.f, ib.y + 1.f), IM_COL32(0, 0, 0, 120));
                dl->AddImage((ImTextureID)railImageTex->Resource, ia, ib);
            }
            else
            {
                dl->AddRectFilled(ia, ib, IM_COL32(42, 34, 24, 210));
            }
            dl->AddRect(ia, ib, IM_COL32(156, 128, 82, 120), 0.f, 0, 1.f);
            if (imageHovered)
            {
                Gw2Ui::Tooltip("Open image");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    OpenLightbox(FullSizeWikiImage(imageUrl));
            }
        }

        // Stats + images + rows all flow from a single running y under the header (the banner-only case drew its
        // own body above). The increments here MUST match the bodyH summation so panelH bounds the content.
        if (!bannerOnly)
        {
            float y = p.y + headerH;

            // Completion stats: a flowing row of [count][icon] pairs (86 [heart] 119 [waypoint] ...).
            if (!rail.stats.empty())
            {
                y += pad;
                const float ox = p.x + pad;
                for (size_t i = 0; i < rail.stats.size(); ++i)
                {
                    const Wiki::NativeRailStat &st = rail.stats[i];
                    const ImVec2 sp(ox + statPos[i].x, y + statPos[i].y);
                    const float tw = Gw2Ui::MeasureWidth(st.count.c_str(), kRailStatFs);
                    Gw2Ui::LabelDL(dl, ImVec2(sp.x, sp.y), ImVec2(sp.x + tw + 2.f, sp.y + statsLineH),
                                   st.count.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                                   IM_COL32(245, 236, 214, 255), true, nullptr, kRailStatFs);
                    const ImVec2 ic(sp.x + tw + statsTextGap, sp.y + (statsLineH - statsIcon) * 0.5f);
                    const std::string tid = WikiImageTextureId(st.iconUrl);
                    if (const Texture_t *itex = ImageCache::GetUrl(tid.c_str(), st.iconUrl.c_str()))
                        dl->AddImage((ImTextureID)itex->Resource, ic, ImVec2(ic.x + statsIcon, ic.y + statsIcon));
                    if (!st.label.empty() &&
                        ImGui::IsMouseHoveringRect(sp, ImVec2(sp.x + tw + statsTextGap + statsIcon, sp.y + statsLineH)))
                        Gw2Ui::Tooltip(st.label.c_str());
                }
                y += statsBlockH;
            }

            // Area map + screenshot(s): each a centered banner with an optional caption; click opens the lightbox.
            if (!rail.images.empty())
            {
                y += pad;
                for (size_t i = 0; i < rail.images.size(); ++i)
                {
                    const Wiki::NativeRailImage &im = rail.images[i];
                    const float ih = imageHeights[i];
                    const ImVec2 ia(std::floor(p.x + pad), std::floor(y));
                    const ImVec2 ib(std::floor(p.x + w - pad), std::floor(y + ih));
                    ImGui::PushID((int)i);
                    ImGui::SetCursorScreenPos(ia);
                    ImGui::InvisibleButton("##railAreaImg", ImVec2(std::max(1.f, ib.x - ia.x), std::max(1.f, ib.y - ia.y)));
                    const bool imageHovered = ImGui::IsItemHovered();
                    ImGui::PopID();
                    if (imageHovered)
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    const std::string tid = WikiImageTextureId(im.url);
                    if (const Texture_t *tex = ImageCache::GetUrl(tid.c_str(), im.url.c_str()))
                    {
                        dl->AddRectFilled(ImVec2(ia.x - 1.f, ia.y - 1.f), ImVec2(ib.x + 1.f, ib.y + 1.f), IM_COL32(0, 0, 0, 120));
                        dl->AddImage((ImTextureID)tex->Resource, ia, ib);
                    }
                    else
                    {
                        dl->AddRectFilled(ia, ib, IM_COL32(42, 34, 24, 210));
                    }
                    dl->AddRect(ia, ib, IM_COL32(156, 128, 82, 120), 0.f, 0, 1.f);
                    if (imageHovered)
                    {
                        Gw2Ui::Tooltip("Open image");
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            OpenLightbox(FullSizeWikiImage(im.url));
                    }
                    y += ih;
                    if (!im.caption.empty())
                    {
                        Gw2Ui::LabelDL(dl, ImVec2(p.x + pad, y + 2.f), ImVec2(p.x + w - pad, y + 18.f),
                                       im.caption.c_str(), Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                                       IM_COL32(206, 198, 178, 255), true, nullptr, 14.f);
                        y += 18.f;
                    }
                    y += 8.f;
                }
            }

            // Label rows (Levels / Type / Within / ...).
            if (!rows.empty())
            {
                y += 12.f;
                const float rowsTop = y;
                const float sepX = std::floor(p.x + sepLocalX);
                dl->AddRectFilled(ImVec2(sepX - 1.f, rowsTop + 2.f), ImVec2(sepX + 1.f, rowsTop + rowsH - 2.f),
                                  IM_COL32(196, 72, 24, 235));
                for (int ri = 0; ri < (int)rows.size(); ++ri)
                {
                    const Wiki::NativeRailRow &row = rows[ri];
                    const float rowH = rowHeights[ri];
                    const ImVec2 ra(p.x + 1.f, y);
                    const ImVec2 rb(p.x + w - 1.f, y + rowH);
                    if (ri & 1)
                        dl->AddRectFilled(ra, rb, IM_COL32(0, 0, 0, 22));

                    Gw2Ui::LabelDL(dl, ImVec2(p.x + 6.f, y + 3.f), ImVec2(sepX - 8.f, y + rowH - 3.f),
                                   row.label.c_str(), Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                                   IM_COL32(245, 236, 214, 255), true, nullptr, kRailLabelFs, labelW, 1.1f);

                    float vy = y + 4.f;
                    for (int vi = 0; vi < (int)row.values.size(); ++vi)
                    {
                        const Wiki::NativeRailItem &value = row.values[vi];
                        const float vh = RailValueHeight(value, valueW, kRailValueFs);
                        DrawRailValue(app, value, ImVec2(p.x + valueLocalX, vy),
                                      ImVec2(p.x + valueLocalX + valueW, vy + vh), ri, vi);
                        vy += vh + 2.f;
                    }
                    y += rowH;
                }
            }
        }

        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, panelH));
    }

    void RebuildDocsIfNeeded(WikiTab &t)
    {
        const Wiki::Page &page = t.page;    // the tab's cached page data
        PageRenderState &s_page = t.render; // aliases so the existing build body stays unchanged
        std::string &s_docKey = t.docKey;
        if (page.html.empty())
            return;
        const std::string key = page.title + "|" + std::to_string(page.revid) + "|" +
                                std::to_string(page.html.size()) + "|" + std::to_string(page.css.size()) + "|" +
                                std::to_string(Gw2Ui::FontRevision());
        if (s_docKey == key)
            return;

        s_docKey = key;
        s_page = PageRenderState{};
        s_page.key = key;
        s_page.sanitized = Wiki::SanitizeArticleHtml(page.html, page.displayTitle.empty() ? page.title : page.displayTitle);
        s_page.nativeRail = Wiki::BuildNativeRail(s_page.sanitized.railHtml,
                                                  page.displayTitle.empty() ? page.title : page.displayTitle);
        s_page.css = Wiki::BuildTyrianWikiCss(page.css);
        s_page.article.container = std::make_unique<Wiki::Container>();
        s_page.article.container->SetMinimumFontSize(15.f);
        s_page.article.doc = Wiki::CreateDocument(s_page.sanitized.articleHtml, s_page.css, *s_page.article.container);

        // Warm every image this page will draw NOW, in parallel on the ImageCache worker pool,
        // so they download BEFORE litehtml's first layout asks per-<img> -- removes the first-paint blank.
        for (const std::string &u : s_page.sanitized.imageUrls)
            PrefetchWikiImage(u);
        PrefetchWikiImage(s_page.nativeRail.imageUrl);
        for (const Wiki::NativeRailImage &im : s_page.nativeRail.images)
            PrefetchWikiImage(im.url);
        for (const Wiki::NativeRailStat &st : s_page.nativeRail.stats)
            PrefetchWikiImage(st.iconUrl);
    }

    float ClampFloat(float v, float lo, float hi)
    {
        if (hi < lo)
            return lo;
        return std::clamp(v, lo, hi);
    }

    float LibraryListHeight(const std::vector<std::string> &values)
    {
        const size_t rows = values.empty() ? 1u : std::min<size_t>(values.size(), 12u);
        return 20.f + (values.empty() ? 18.f : rows * 26.f);
    }

    void AddLibraryWidth(float &width, const std::vector<std::string> &values)
    {
        for (size_t i = 0; i < values.size() && i < 12; ++i)
            width = std::max(width, Gw2Ui::MeasureWidth(values[i].c_str(), 16.f) + 38.f);
    }

    float LibraryPopupWidth(App &app, float maxW)
    {
        float width = 360.f;
        AddLibraryWidth(width, app.wiki.Bookmarks());
        AddLibraryWidth(width, app.wiki.History());
        return ClampFloat(width, std::min(360.f, maxW), maxW);
    }

    float LibraryPopupHeight(App &app)
    {
        return 22.f +
               LibraryListHeight(app.wiki.Bookmarks()) + 6.f +
               LibraryListHeight(app.wiki.History()) + 10.f;
    }

    void DrawLibraryList(App &app, const char *title, const std::vector<std::string> &values)
    {
        ImGui::PushID(title);
        Gw2Ui::Label(title, Gw2Ui::kTextSelected, true, nullptr, 16.f, 1.4f);
        if (values.empty())
        {
            Gw2Ui::Label("None yet.", Gw2Ui::kTextDim, false, nullptr, 14.f);
            ImGui::PopID();
            return;
        }
        for (size_t i = 0; i < values.size() && i < 12; ++i)
        {
            ImGui::PushID((int)i);
            if (Gw2Ui::MenuItem(values[i].c_str(), false, 26.f, (int)i))
            {
                OpenTitle(app, values[i], true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    void DrawLibraryPopup(App &app)
    {
        if (s_libraryOpen)
        {
            ImGui::OpenPopup("##wikiLibrary");
            s_libraryOpen = false;
        }
        if (s_libraryAnchorValid)
        {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const ImVec2 boundsMin(8.f, 8.f);
            const ImVec2 boundsMax(std::max(8.f, display.x - 8.f), std::max(8.f, display.y - 8.f));
            const float boundsW = std::max(260.f, boundsMax.x - boundsMin.x);
            const float boundsH = std::max(180.f, boundsMax.y - boundsMin.y);
            const float popupW = LibraryPopupWidth(app, std::min(560.f, boundsW));
            const float desiredH = LibraryPopupHeight(app);
            const float maxPopupH = std::min(620.f, boundsH);
            const float belowH = std::max(0.f, boundsMax.y - s_libraryAnchorMax.y - 4.f);
            const float aboveH = std::max(0.f, s_libraryAnchorMin.y - boundsMin.y - 4.f);
            const bool placeAbove = belowH < std::min(desiredH, 260.f) && aboveH > belowH;
            const float availableH = std::max(140.f, placeAbove ? aboveH : belowH);
            const float popupH = ClampFloat(std::min(desiredH, std::min(maxPopupH, availableH)), 140.f, maxPopupH);

            const float x = ClampFloat(s_libraryAnchorMax.x - popupW, boundsMin.x, boundsMax.x - popupW);
            float y = placeAbove ? s_libraryAnchorMin.y - popupH - 4.f : s_libraryAnchorMax.y + 4.f;
            y = ClampFloat(y, boundsMin.y, boundsMax.y - popupH);

            ImGui::SetNextWindowPos(ImVec2(std::floor(x), std::floor(y)), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(std::floor(popupW), std::floor(popupH)), ImGuiCond_Always);
        }
        if (ImGui::BeginPopup("##wikiLibrary"))
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 8.f);
            DrawLibraryList(app, "Bookmarks", app.wiki.Bookmarks());
            ImGui::Dummy(ImVec2(1.f, 6.f));
            DrawLibraryList(app, "History", app.wiki.History());
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
    }

    void DrawExternalPrompt()
    {
        if (s_externalPromptRequested)
        {
            ImGui::OpenPopup("Open external wiki link?");
            s_externalPromptRequested = false;
        }
        if (ImGui::BeginPopupModal("Open external wiki link?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            Render::DrawGlyph(ImGui::GetWindowDrawList(), ImVec2(hp.x + 9.f, hp.y + 11.f), 18.f,
                              Render::Glyph::ExternalLink, Gw2Ui::kTextSelected);
            ImGui::Indent(24.f);
            Gw2Ui::Label("External link", Gw2Ui::kTextSelected, true, nullptr, 18.f, 1.6f);
            ImGui::Unindent(24.f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.f);
            Gw2Ui::Label("This link leaves the Guild Wars 2 Wiki. Choose how to handle it.", Gw2Ui::kTextSub, false, nullptr, 16.f);
            Gw2Ui::Label(s_externalUrl.c_str(), IM_COL32(135, 190, 255, 255), false, nullptr, 14.f);
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(1.f, 8.f));
            if (Gw2Ui::ActionButton("Open browser", 130.f, 28.f, Gw2Ui::ActionButtonVariant::Primary))
            {
                OpenBrowser(s_externalUrl);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::ActionButton("Copy link", 110.f, 28.f))
            {
                SetClipboard(s_externalUrl);
                ViewerAlert("Copied wiki link.");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::ActionButton("Cancel", 90.f, 28.f))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    bool DrawTextActionAt(const char *label, float x, float y, float w, float h,
                          Gw2Ui::ActionButtonVariant variant = Gw2Ui::ActionButtonVariant::Normal,
                          const char *tooltip = nullptr, bool disabled = false)
    {
        ImGui::SetCursorScreenPos(ImVec2(std::floor(x), std::floor(y)));
        return Gw2Ui::ActionButton(label, w, h, variant, tooltip, disabled);
    }

    bool DrawIconActionAt(const char *id, float x, float y, float w, float h,
                          const char *tooltip, bool disabled,
                          Render::Glyph glyph, Render::GlyphStyle style = {},
                          Gw2Ui::ActionButtonVariant variant = Gw2Ui::ActionButtonVariant::Normal)
    {
        ImGui::SetCursorScreenPos(ImVec2(std::floor(x), std::floor(y)));
        const Gw2Ui::ActionButtonResult r = Gw2Ui::ActionButtonFrame(id, ImVec2(w, h), variant, disabled, tooltip);
        const ImU32 col = disabled                ? IM_COL32(120, 110, 90, 150)
                          : (r.hovered || r.held) ? Gw2Ui::kTextSelected
                                                  : Gw2Ui::kGold;
        style.disabled = disabled;
        const float glyphSize = std::min(w, h) * 0.68f;
        Render::DrawGlyph(ImGui::GetWindowDrawList(),
                          ImVec2((r.min.x + r.max.x) * 0.5f, (r.min.y + r.max.y) * 0.5f),
                          glyphSize, glyph, col, style);
        return r.clicked;
    }

    // Bookmarks / History tab body: a full-window list; clicking a row opens it and jumps to the Wiki tab.
    void DrawListTab(App &app, const char *title, const std::vector<std::string> &values, const char *emptyMsg)
    {
        ImGui::Dummy(ImVec2(1.f, 6.f));
        Gw2Ui::SectionHeader(title, nullptr, 18.f, Gw2Ui::kTextSelected, false);
        ImGui::Dummy(ImVec2(1.f, 6.f));
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##wikiListTab", ImVec2(avail.x, std::max(60.f, avail.y)), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        if (values.empty())
            Gw2Ui::EmptyState(title, emptyMsg);
        else
            for (size_t i = 0; i < values.size(); ++i)
            {
                ImGui::PushID((int)i);
                if (Gw2Ui::MenuItem(values[i].c_str(), false, 30.f, (int)i))
                {
                    OpenTitle(app, values[i], true);
                    s_activeTab = 0;
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
    }

    std::string TabLabel(const WikiTab &tb)
    {
        if (!tb.pendingTitle.empty() && tb.page.html.empty())
            return tb.pendingTitle;
        if (!tb.page.displayTitle.empty())
            return tb.page.displayTitle;
        if (!tb.page.title.empty())
            return tb.page.title;
        return "New tab";
    }
    void SwitchTab(App &app, int i)
    {
        if (i < 0 || i >= (int)s_tabs.size())
            return;
        s_artTab = i;
        s_tabsDirty = true;
        WikiTab &tb = s_tabs[i];
        // Lazy-load a restored-but-never-rendered tab (its title is remembered but nothing's loaded yet).
        if (!tb.render.article.doc && tb.page.html.empty() && tb.pendingTitle.empty() && !tb.page.title.empty())
        {
            tb.pendingTitle = tb.page.title;
            app.wiki.OpenPage(tb.page.title);
        }
        CopyToQuery(tb.page.title.empty() ? tb.pendingTitle : tb.page.title); // sync the search box to the active tab
    }
    void NewTab(App &)
    {
        s_tabs.push_back(WikiTab{});
        s_tabs.back().uid = s_nextUid++;
        s_artTab = (int)s_tabs.size() - 1;
        s_tabsDirty = true;
        s_query[0] = '\0';
        s_searchPanelOpen = true; // a blank tab -> open search so the user can pick an article
    }
    void CloseTab(App &app, int i)
    {
        if (i < 0 || i >= (int)s_tabs.size())
            return;
        const bool wasActive = (i == s_artTab);
        s_tabs.erase(s_tabs.begin() + i);
        s_tabsDirty = true;
        if (s_tabs.empty())
        {
            s_tabs.push_back(WikiTab{});
            s_tabs.back().uid = s_nextUid++;
        }
        if (i < s_artTab)
            s_artTab--;
        s_artTab = std::clamp(s_artTab, 0, (int)s_tabs.size() - 1);
        if (wasActive)
            SwitchTab(app, s_artTab);
    }
    // Open an article from OUTSIDE the reader (item/fish right-click, OpenWiki). If the active tab already has
    // content, open in a NEW tab so it does not replace  what the user is reading; otherwise reuse the blank tab.
    void OpenExternal(App &app, const std::string &titleOrUrl)
    {
        WikiTab &a = Active();
        if (!a.page.title.empty() || !a.pendingTitle.empty() || a.render.article.doc)
            NewTab(app);
        OpenTitle(app, titleOrUrl, true);
    }
    // The article TAB STRIP. Each tab is the SAME frame primitive as the reader's chrome buttons
    // (Gw2Ui::ActionButtonFrame) so it matches them 1:1 -- square 3px corners, same fill/border/sheen/hover.
    // The active tab uses the Primary variant (the "selected/on" look, exactly like the bookmarked star), plus a
    // kGold underline. Click to switch; the x / middle-click closes; "+" reuses the chrome icon-button helper.
    void DrawTabStrip(App &app)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float h = 28.f, gap = 6.f, padL = 10.f, closeW = 22.f, fs = 15.f, maxW = 215.f;
        ImGui::Dummy(ImVec2(1.f, 2.f));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        float x = start.x;
        int switchTo = -1, closeIdx = -1;
        bool addTab = false;
        for (int i = 0; i < (int)s_tabs.size(); ++i)
        {
            const std::string lbl = TabLabel(s_tabs[i]);
            const float tw = std::min(maxW, padL + Gw2Ui::MeasureWidth(lbl.c_str(), fs) + closeW + 4.f);
            const bool active = (i == s_artTab);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(ImVec2(std::floor(x), std::floor(start.y)));
            const Gw2Ui::ActionButtonResult r = Gw2Ui::ActionButtonFrame(
                "##wt", ImVec2(tw, h),
                active ? Gw2Ui::ActionButtonVariant::Primary : Gw2Ui::ActionButtonVariant::Normal, false, nullptr);
            const bool hot = r.hovered || r.held;
            const ImU32 txt = (active || hot) ? Gw2Ui::kTextSelected : IM_COL32(214, 196, 150, 255);
            Gw2Ui::LabelDL(dl, ImVec2(r.min.x + padL, r.min.y), ImVec2(r.max.x - closeW, r.max.y), lbl.c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, txt, true, nullptr, fs);
            if (active) // active underline (kGold), square -- the selected-tab cue on top of the Primary frame
                dl->AddRectFilled(ImVec2(r.min.x + 2.f, r.max.y - 2.f), ImVec2(r.max.x - 2.f, r.max.y), Gw2Ui::kGold, 0.f);
            // close x inside the frame (right). One button (the frame); the x is a region check, not a 2nd item.
            const bool overX = r.hovered && ImGui::IsMouseHoveringRect(ImVec2(r.max.x - closeW, r.min.y), ImVec2(r.max.x, r.max.y));
            Render::DrawGlyph(dl, ImVec2(r.max.x - closeW * 0.5f, (r.min.y + r.max.y) * 0.5f), 11.f, Render::Glyph::Cross,
                              overX ? Gw2Ui::kTextSelected : IM_COL32(168, 150, 110, 220), {});
            const bool midClose = r.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
            if (midClose || (r.clicked && overX))
                closeIdx = i;
            else if (r.clicked)
                switchTo = i;
            ImGui::PopID();
            x += tw + gap;
        }
        // "+" new tab -- identical to a chrome icon button (same ActionButtonFrame + glyph helper).
        if (DrawIconActionAt("##wtAdd", x, start.y, h, h, "New tab", false, Render::Glyph::Plus))
            addTab = true;
        if (closeIdx >= 0)
            CloseTab(app, closeIdx);
        else if (switchTo >= 0)
            SwitchTab(app, switchTo);
        if (addTab)
            NewTab(app);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + h + 6.f));
    }

    void DrawTopBar(App &app)
    {
        WikiTab &t = Active(); // the active article tab (its cached page + back/forward)
        const float yPad = 2.f;
        ImGui::Dummy(ImVec2(1.f, yPad));
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float rowAvailW = ImGui::GetContentRegionAvail().x;
        const float buttonH = 28.f;
        ImFont *uiFont = Gw2Ui::UiFontResolved();
        const float searchH = std::max(28.f, (uiFont ? uiFont->FontSize : ImGui::GetFontSize()) + 10.f);
        const float rowH = std::max(buttonH, searchH);
        const float btnY = std::floor(rowStart.y + (rowH - buttonH) * 0.5f);
        const float searchY = std::floor(rowStart.y + (rowH - searchH) * 0.5f);
        const float gap = 6.f;
        const float navW = 36.f;
        const float goW = 54.f;
        const float iconW = 32.f;

        float x = rowStart.x;
        Render::GlyphStyle backStyle;
        backStyle.mirrorX = true; // CaretRight mirrored -> points left
        if (DrawIconActionAt("##wikiBack", x, btnY, navW, buttonH, "Back", t.back.empty(), Render::Glyph::CaretRight, backStyle))
        {
            const std::string cur = t.page.title;
            const std::string title = t.back.back();
            t.back.pop_back();
            if (!cur.empty())
                t.forward.push_back(cur);
            OpenTitle(app, title, false);
        }
        x += navW + gap;
        if (DrawIconActionAt("##wikiFwd", x, btnY, navW, buttonH, "Forward", t.forward.empty(), Render::Glyph::CaretRight))
        {
            const std::string cur = t.page.title;
            const std::string title = t.forward.back();
            t.forward.pop_back();
            if (!cur.empty())
                t.back.push_back(cur);
            OpenTitle(app, title, false);
        }
        x += navW + 10.f;

        const float rightEdge = rowStart.x + rowAvailW;
        const float afterSearchW =
            gap + goW +
            gap + iconW +  // refresh
            gap + iconW +  // star
            10.f + iconW + // browser (globe)
            gap + iconW;   // library (book)
        const float searchW = std::max(120.f, rightEdge - x - afterSearchW);
        ImGui::SetCursorScreenPos(ImVec2(std::floor(x), searchY));
        if (Gw2Ui::SearchBox("##wikiSearch", s_query, sizeof(s_query), searchW, "Search the Guild Wars 2 Wiki..."))
        {
            s_searchEditedAt = ImGui::GetTime();
            s_searchPanelOpen = true;
            s_goPendingQuery.clear();
        }

        const std::string q = Trim(s_query);
        if (q.size() >= 2 && NormalizeTitle(s_lastSearchQuery) != NormalizeTitle(q) &&
            ImGui::GetTime() - s_searchEditedAt > 0.35 && !app.wiki.IsSearching())
            RequestSearch(app, q);

        x += searchW + gap;
        if (DrawTextActionAt("Go", x, btnY, goW, buttonH, Gw2Ui::ActionButtonVariant::Primary))
            SubmitSearchOrOpen(app);
        x += goW + gap;

        const bool hasPage = !t.page.title.empty();
        if (DrawIconActionAt("##wikiRefresh", x, btnY, iconW, buttonH, "Refresh this article", !hasPage,
                             Render::Glyph::Refresh))
        {
            t.pendingTitle = t.page.title;
            app.wiki.OpenPage(t.page.title, /*forceRefresh*/ true);
        } // re-load THIS tab
        x += iconW + gap;

        const bool bookmarked = app.wiki.IsBookmarked(t.page.title);
        Render::GlyphStyle starStyle;
        starStyle.filled = bookmarked;
        if (DrawIconActionAt("##wikiBookmark", x, btnY, iconW, buttonH, bookmarked ? "Remove bookmark" : "Bookmark this article",
                             !hasPage, Render::Glyph::Star, starStyle,
                             bookmarked ? Gw2Ui::ActionButtonVariant::Primary : Gw2Ui::ActionButtonVariant::Normal))
            app.wiki.ToggleBookmark(t.page.title);
        x += iconW + 10.f;

        if (DrawIconActionAt("##wikiBrowser", x, btnY, iconW, buttonH, "Open current page in your browser",
                             t.page.canonicalUrl.empty(), Render::Glyph::Globe))
            OpenBrowser(t.page.canonicalUrl);
        x += iconW + gap;

        if (DrawIconActionAt("##wikiLibrary", x, btnY, iconW, buttonH, "Bookmarks / Recent / History", false, Render::Glyph::Book))
        {
            s_libraryOpen = true;
            s_libraryAnchorMin = ImGui::GetItemRectMin();
            s_libraryAnchorMax = ImGui::GetItemRectMax();
            s_libraryAnchorValid = true;
        }
        DrawLibraryPopup(app);
        ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + rowH));
    }

    void DrawSearchPanel(App &app)
    {
        const std::string q = Trim(s_query);
        if (!s_searchPanelOpen || q.size() < 2)
            return;

        const bool resultsForQuery = SearchResultsMatchQuery(app, q);
        const auto &results = app.wiki.SearchResults();
        const float panelH = 170.f;
        ImGui::BeginChild("##wikiSearchResults", ImVec2(ImGui::GetContentRegionAvail().x, panelH), true,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        Gw2Ui::SectionHeader("Search results", nullptr, 16.f, Gw2Ui::kTextSelected, false);

        if (app.wiki.IsSearching() && NormalizeTitle(app.wiki.SearchLoadingQuery()) == NormalizeTitle(q))
        {
            Gw2Ui::Label("Searching...", Gw2Ui::kTextSub, false, nullptr, 16.f);
        }
        else if (!resultsForQuery)
        {
            Gw2Ui::Label("Type to search or press Go.", Gw2Ui::kTextSub, false, nullptr, 16.f);
        }
        else if (results.empty())
        {
            const std::string &err = app.wiki.SearchError();
            Gw2Ui::Label(err.empty() ? "No matching wiki articles." : err.c_str(),
                         Gw2Ui::kTextDim, false, nullptr, 16.f);
        }
        else
        {
            for (size_t i = 0; i < results.size(); ++i)
            {
                ImGui::PushID((int)i);
                if (Gw2Ui::MenuItem(results[i].title.c_str(), false, 28.f, (int)i))
                    OpenTitle(app, results[i].title, true);
                if (!results[i].snippet.empty())
                {
                    ImGui::Indent(12.f);
                    Gw2Ui::Label(results[i].snippet.c_str(), Gw2Ui::kTextSub, false, nullptr, 14.f);
                    ImGui::Unindent(12.f);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::Dummy(ImVec2(1.f, 6.f));
    }

    void DrawRail(App &app, float railW, float height)
    {
        WikiTab &t = Active();
        ImGui::BeginChild("##wikiRail", ImVec2(railW, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const ImVec2 railCursor = ImGui::GetCursorScreenPos();
        const float railAvailW = ImGui::GetContentRegionAvail().x;
        const float gutter = ImGui::GetStyle().ScrollbarSize + 6.f;
        const float laneW = std::max(80.f, railAvailW - gutter);
        if (!t.page.html.empty())
        {
            DrawNativeRail(app, t.render.nativeRail, laneW);
            ImGui::SetCursorScreenPos(ImVec2(railCursor.x, ImGui::GetCursorScreenPos().y));
            ImGui::Dummy(ImVec2(1.f, 8.f));
        }
        ImGui::SetCursorScreenPos(ImVec2(railCursor.x, ImGui::GetCursorScreenPos().y));
        if (Gw2Ui::BeginCard("##wikiTocCard", laneW, IM_COL32(0, 0, 0, 62), IM_COL32(155, 128, 82, 105)))
        {
            Gw2Ui::SectionHeader("Contents", nullptr, 16.f, Gw2Ui::kTextSelected, false);
            ImGui::Dummy(ImVec2(1.f, 3.f));

            const float cardLeft = ImGui::GetCursorScreenPos().x;
            const float cardRight = cardLeft + Gw2Ui::CardInnerWidth();
            const Wiki::Page &page = t.page;
            if (page.sections.empty())
            {
                Gw2Ui::Label("No sections.", Gw2Ui::kTextDim, false, nullptr, 14.f);
            }
            else
            {
                for (size_t i = 0; i < page.sections.size(); ++i)
                {
                    const Wiki::Section &sec = page.sections[i];
                    const float indent = (float)std::max(0, sec.level - 2) * 14.f;
                    const float rowLeft = cardLeft + indent;
                    const float rowRight = std::max(rowLeft + 40.f, cardRight);
                    // Wrap long entries (e.g. "Ascalon Explorer achievement") to the card's inner width instead of
                    // bleeding past the border. fontSize 0 == the same default size LabelDL uses, so MeasureWrappedHeight
                    // matches the draw exactly; the row grows to fit the wrapped text.
                    const float textW = std::max(1.f, rowRight - rowLeft);
                    const float textH = Gw2Ui::MeasureWrappedHeight(sec.line.c_str(), 0.f, textW);
                    const float rowH = std::max(24.f, std::ceil(textH) + 6.f);
                    const float y = ImGui::GetCursorScreenPos().y;
                    ImGui::PushID((int)i);
                    ImGui::SetCursorScreenPos(ImVec2(rowLeft, y));
                    const bool clicked = ImGui::InvisibleButton("##wikiTocItem", ImVec2(textW, rowH));
                    const bool hovered = ImGui::IsItemHovered();
                    Gw2Ui::RowBackground(ImVec2(rowLeft, y), ImVec2(rowRight, y + rowH), hovered, false, ImGui::GetID("##wikiTocItem"), (int)i);
                    Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), ImVec2(rowLeft, y), ImVec2(rowRight, y + rowH),
                                   sec.line.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                                   IM_COL32(255, 255, 255, 255), true, nullptr, 0.f, textW);
                    ImGui::PopID();
                    if (clicked)
                        s_pendingArticleAnchor = sec.anchor;
                }
            }
            Gw2Ui::EndCard();
        }
        ImGui::EndChild();
    }

    void DrawArticle(App &app, float width, float height)
    {
        WikiTab &t = Active();
        char childId[24];
        std::snprintf(childId, sizeof(childId), "##wikiArt%u", t.uid); // per-tab scroll memory
        ImGui::BeginChild(childId, ImVec2(width, height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const float articleAvailW = ImGui::GetContentRegionAvail().x;
        const float scrollbarGutter = ImGui::GetStyle().ScrollbarSize + 10.f;
        const float docW = std::max(120.f, articleAvailW - scrollbarGutter);
        const Wiki::Page &page = t.page;
        if ((app.wiki.IsLoadingPage() || !t.pendingTitle.empty()) && page.html.empty())
        {
            Gw2Ui::EmptyState("Loading wiki article", s_query);
        }
        else if (!page.error.empty() && page.html.empty())
        {
            Gw2Ui::EmptyState("Could not load article", page.error.c_str());
        }
        else if (t.render.article.doc)
        {
            const std::string anchor = s_pendingArticleAnchor;
            DrawLiteDocument("##wikiArticleDoc", t.render.article, docW, height - 4.f, app,
                             anchor.empty() ? nullptr : &anchor);
            if (!anchor.empty())
                s_pendingArticleAnchor.clear();
            if (app.wiki.IsLoadingPage())
            {
                const ImVec2 p = ImGui::GetWindowPos();
                const ImVec2 s = ImGui::GetWindowSize();
                Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y + 8.f), ImVec2(p.x + s.x - 18.f, p.y + 36.f),
                               "Refreshing...", Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle,
                               Gw2Ui::kTextSelected, true, nullptr, 14.f, 0.f, 1.4f);
            }
        }
        else
        {
            Gw2Ui::EmptyState("Guild Wars 2 Wiki", "Search for an article to open it here.");
        }
        ImGui::EndChild();
    }

    std::string FileNameFromUrl(const std::string &url)
    {
        const size_t slash = url.find_last_of('/');
        std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
        for (char &c : name)
            if (c == '_')
                c = ' ';
        return name;
    }

    void DrawLightbox()
    {
        if (s_lightboxUrl.empty())
            return;

        const ImVec2 windowMin = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
        ImGui::SetNextWindowPos(windowMin, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.f);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::Begin("##wikiLightboxOverlay", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float margin = 28.f;
        const ImVec2 panelMin(std::floor(windowMin.x + margin), std::floor(windowMin.y + margin));
        const ImVec2 panelMax(std::floor(windowMax.x - margin), std::floor(windowMax.y - margin));
        const ImVec2 closeMin(panelMax.x - 86.f, panelMin.y + 10.f);
        const ImVec2 closeMax(closeMin.x + 72.f, closeMin.y + 26.f);
        const bool closeHovered = ImGui::IsMouseHoveringRect(closeMin, closeMax, true);

        if (!closeHovered)
        {
            ImGui::SetCursorScreenPos(windowMin);
            ImGui::InvisibleButton("##wikiLightboxBlocker", windowSize);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            s_lightboxUrl.clear();
        if (s_lightboxUrl.empty())
        {
            ImGui::End();
            return;
        }

        dl->PushClipRect(windowMin, windowMax, true);
        dl->AddRectFilled(windowMin, windowMax, IM_COL32(0, 0, 0, 180));
        dl->AddRectFilled(panelMin, panelMax, IM_COL32(10, 12, 12, 235), 4.f);
        dl->AddRect(panelMin, panelMax, IM_COL32(164, 128, 61, 210), 4.f, 0, 1.f);

        const std::string id = WikiImageTextureId(s_lightboxUrl);
        const Texture_t *tex = ImageCache::GetUrl(id.c_str(), s_lightboxUrl.c_str());
        const float titleH = 42.f;
        const float pad = 16.f;
        Gw2Ui::LabelDL(dl, ImVec2(panelMin.x + pad, panelMin.y + 9.f), ImVec2(panelMax.x - 96.f, panelMin.y + titleH),
                       FileNameFromUrl(s_lightboxUrl).c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                       Gw2Ui::kTextSelected, true, nullptr, 18.f, 0.f, 1.4f);

        if (tex && tex->Resource && tex->Width > 0 && tex->Height > 0)
        {
            const float maxW = std::max(1.f, panelMax.x - panelMin.x - pad * 2.f);
            const float maxH = std::max(1.f, panelMax.y - panelMin.y - titleH - pad * 2.f);
            const float scale = std::min(maxW / (float)tex->Width, maxH / (float)tex->Height);
            const float drawScale = std::min(1.f, std::max(0.01f, scale));
            const ImVec2 imageSize(std::floor((float)tex->Width * drawScale), std::floor((float)tex->Height * drawScale));
            const ImVec2 imageMin(std::floor(panelMin.x + (panelMax.x - panelMin.x - imageSize.x) * 0.5f),
                                  std::floor(panelMin.y + titleH + (panelMax.y - panelMin.y - titleH - imageSize.y) * 0.5f));
            const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
            dl->AddRectFilled(ImVec2(imageMin.x - 1.f, imageMin.y - 1.f), ImVec2(imageMax.x + 1.f, imageMax.y + 1.f),
                              IM_COL32(0, 0, 0, 180));
            dl->AddImage((ImTextureID)tex->Resource, imageMin, imageMax);
        }
        else
        {
            Gw2Ui::LabelDL(dl, panelMin, panelMax, "Loading image...", Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                           Gw2Ui::kTextSub, true, nullptr, 18.f, 0.f, 1.4f);
        }
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(closeMin);
        ImGui::PushID("wikiLightboxClose");
        if (Gw2Ui::ActionButton("Close", 72.f, 26.f))
            s_lightboxUrl.clear();
        ImGui::PopID();
        ImGui::End();
    }
}

namespace Wiki
{
    void OpenReader(App &app)
    {
        app.state.showWiki = true;
    }

    void OpenWiki(App &app, const std::string &titleOrUrl)
    {
        app.state.showWiki = true;
        OpenExternal(app, titleOrUrl);
    }

    void RequestOpen(const std::string &titleOrUrl)
    {
        if (!titleOrUrl.empty())
            s_pendingOpen = titleOrUrl; // drained by RenderReader (which has App&)
    }

    void RenderReader(App &app)
    {
        // One-time: restore last session's open tabs (BEFORE any external open, so it does not replace them). The
        // active tab loads now so it shows on open; the rest lazy-load on first switch.
        if (!s_tabsRestored)
        {
            s_tabsRestored = true;
            std::vector<std::string> titles;
            int active = 0;
            if (app.wiki.LoadOpenTabs(titles, active) && !titles.empty())
            {
                s_tabs.clear();
                for (const std::string &tt : titles)
                {
                    s_tabs.push_back(WikiTab{});
                    s_tabs.back().uid = s_nextUid++;
                    s_tabs.back().page.title = tt; // remembered title -> lazy-loads on first switch
                }
                s_artTab = std::clamp(active, 0, (int)s_tabs.size() - 1);
                WikiTab &a = s_tabs[s_artTab];
                if (!a.page.title.empty())
                {
                    a.pendingTitle = a.page.title;
                    app.wiki.OpenPage(a.pendingTitle);
                    CopyToQuery(a.pendingTitle);
                }
            }
        }
        // Apply a deferred App-free open (e.g. an item right-click) before the open-state gate.
        if (!s_pendingOpen.empty())
        {
            const std::string title = s_pendingOpen;
            s_pendingOpen.clear();
            app.state.showWiki = true;
            OpenExternal(app, title);
        }
        if (!app.state.showWiki)
        {
            s_wasOpen = false;
            return;
        }
        const bool justOpened = !s_wasOpen;
        s_wasOpen = true;
        if (justOpened)
        {
            s_prevWinW = app.config.wikiWindowW;
            s_prevWinH = app.config.wikiWindowH;
        }
        if (Gw2Ui::BeginWindow("Tyrian Codex Wiki", &app.state.showWiki, "Tyrian Codex", "Guild Wars 2 wiki",
                               kWikiTabs, kWikiTabCount, &s_activeTab, &app.config.wikiWindowW, &app.config.wikiWindowH, justOpened))
        {
            // The wiki feeds untrusted HTML/CSS through gumbo + litehtml. The outer entry.cpp Render
            // try/catch already guarantees GW2 can't crash; this LOCAL backstop additionally keeps the
            // rest of the overlay (Tray/Dashboard/HUD/Info Panel/Toast) alive when a page throws -- and
            // EndWindow is reached on both paths so ImGui stays balanced. (app.wiki.Pump() runs once per
            // frame in entry.cpp Render, so it is intentionally NOT called again here.)
            try
            {
                ResolvePendingGo(app);
                if (s_activeTab == 1)
                    DrawListTab(app, "Bookmarks", app.wiki.Bookmarks(), "No bookmarks yet -- open an article and tap the star to add it.");
                else if (s_activeTab == 2)
                    DrawListTab(app, "History", app.wiki.History(), "No history yet -- pages you open appear here.");
                else
                {
                    // The service loads ONE page at a time; deliver a completed load into the tab that requested it
                    // (the active tab), then (re)build THAT tab's litehtml doc. Switching to an already-built tab does
                    // not re-load -- its page + render are cached, so RebuildDocsIfNeeded is a no-op.
                    {
                        WikiTab &t = Active();
                        if (!t.pendingTitle.empty() && !app.wiki.IsLoadingPage() && !app.wiki.CurrentPage().title.empty())
                        {
                            t.page = app.wiki.CurrentPage(); // copy the service's loaded page into the active tab
                            t.pendingTitle.clear();
                            s_tabsDirty = true; // the tab's title resolved -> persist the canonical title
                        }
                        RebuildDocsIfNeeded(t);
                    }

                    DrawTabStrip(app);
                    DrawTopBar(app);
                    ImGui::Dummy(ImVec2(1.f, 8.f));
                    DrawSearchPanel(app);

                    float &railW = app.config.wikiRailW;
                    const ImVec2 avail = ImGui::GetContentRegionAvail();
                    railW = std::clamp(railW, kRailMinW, std::max(kRailMinW, avail.x * 0.42f));
                    const float splitterX = ImGui::GetCursorScreenPos().x + railW + 8.f;
                    const float height = std::max(80.f, avail.y);

                    DrawRail(app, railW, height);
                    ImGui::SameLine(0.f, 8.f);
                    if (Gw2Ui::VSplitter("##wikiRailSplit", splitterX, ImGui::GetCursorScreenPos().y, height,
                                         &railW, kRailMinW, std::max(kRailMinW + 10.f, avail.x * 0.48f)))
                        app.settingsDirty = true; // persist the rail width on drag
                    ImGui::SameLine(0.f, 8.f);
                    DrawArticle(app, std::max(120.f, ImGui::GetContentRegionAvail().x), height);

                    DrawLightbox();
                    DrawExternalPrompt();
                }
            }
            catch (...)
            {
                static bool s_loggedWikiDrawError = false;
                if (!s_loggedWikiDrawError && APIDefs)
                {
                    APIDefs->Log(LOGL_CRITICAL, "Tyrian Codex", "Exception while rendering the wiki reader suppressed.");
                    s_loggedWikiDrawError = true;
                }
                Gw2Ui::EmptyState("Could not display this page", "An error occurred while rendering this wiki article.");
            }
            Gw2Ui::EndWindow();
        }
        // Persist a user window-resize: BeginWindow writes the new size back through the config pointers.
        if (std::fabs(app.config.wikiWindowW - s_prevWinW) > 0.5f || std::fabs(app.config.wikiWindowH - s_prevWinH) > 0.5f)
        {
            s_prevWinW = app.config.wikiWindowW;
            s_prevWinH = app.config.wikiWindowH;
            app.settingsDirty = true;
        }
        // Persist the open article tabs (debounced: one write per change, captured after this frame's draw).
        if (s_tabsDirty)
        {
            s_tabsDirty = false;
            std::vector<std::string> titles;
            titles.reserve(s_tabs.size());
            for (const WikiTab &tb : s_tabs)
                titles.push_back(!tb.page.title.empty() ? tb.page.title : tb.pendingTitle);
            app.wiki.SaveOpenTabs(titles, s_artTab);
        }
        if (!app.state.showWiki)
            s_wasOpen = false;
    }
}
