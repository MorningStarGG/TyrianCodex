#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace Wiki
{
    struct Section
    {
        std::string line;
        std::string anchor;
        int         level = 1;
    };

    struct Page
    {
        std::string title;
        std::string displayTitle;
        std::string canonicalUrl;
        std::string html;
        std::string css;
        std::string error;
        std::vector<Section> sections;
        long long revid = 0;
        bool fromCache = false;
    };

    struct SearchResult
    {
        std::string title;
        std::string snippet;
    };

    // The SINGLE auditable network allowlist for the wiki reader. Returns true only for the official
    // Guild Wars 2 / Guild Wars wiki image hosts. Used by the service (its own fetches build from
    // kWikiBase), by the litehtml container before any image fetch, and by the sanitizer to drop a
    // foreign-host <img src> before it ever reaches litehtml. `absoluteUrl` must be an absolute URL.
    bool IsAllowedImageHost(const std::string& absoluteUrl);

    // Minimal shared HTML entity decoder (&amp; &lt; &gt; &quot; &#39;). One definition used by both
    // the service and the sanitizer (was duplicated in each).
    std::string DecodeHtmlEntities(const std::string& s);

    class Service
    {
    public:
        void Init(const std::string& addonDir);
        void Shutdown();
        void Pump();

        void OpenPage(const std::string& titleOrUrl, bool forceRefresh = false);
        void Search(const std::string& query);
        void Refresh();

        bool IsReady() const { return m_ready; }
        bool IsLoadingPage() const { return m_pageLoading; }
        bool IsSearching() const { return m_searchLoading; }
        const Page& CurrentPage() const { return m_page; }
        const std::vector<SearchResult>& SearchResults() const { return m_searchResults; }
        const std::string& SearchResultQuery() const { return m_searchResultQuery; }
        const std::string& SearchLoadingQuery() const { return m_searchLoadingQuery; }
        const std::string& SearchError() const { return m_searchError; }

        const std::vector<std::string>& History() const { return m_history; }
        const std::vector<std::string>& Bookmarks() const { return m_bookmarks; }
        bool IsBookmarked(const std::string& title) const;
        void ToggleBookmark(const std::string& title);

        // Browser-style open ARTICLE tabs (titles + active index): reader-owned state, but persisted to the addon
        // root (beside bookmarks/history) so a reload restores what was open. The service only provides the file I/O.
        void SaveOpenTabs(const std::vector<std::string>& titles, int active);
        bool LoadOpenTabs(std::vector<std::string>& titles, int& active);

    private:
        struct PageResult { Page page; bool ok = false; };
        struct SearchJobResult { std::string query; std::vector<SearchResult> results; std::string error; };

        void Enqueue(std::function<void()> job);
        void Post(std::function<void()> done);
        void WorkerLoop();

        PageResult LoadPageWorker(std::string titleOrUrl, bool forceRefresh);
        SearchJobResult SearchWorker(std::string query);
        std::string LoadCssWorker(bool forceRefresh);

        void LoadLists();
        void SaveLists();
        void PushHistory(const std::string& title);

        std::string m_cacheDir;   // pages/search/css caches (safe to clear)
        std::string m_dataDir;    // addon root -- persistent user data (bookmarks/history/recent), NOT cache
        std::string m_currentRequestTitle;
        std::string m_currentSearchQuery;   // render-thread echo guard: drop stale search completions
        bool m_listsDirty = false;          // recent/history/bookmarks changed; flushed once in Pump()

        std::thread m_worker;
        mutable std::mutex m_mtx;
        std::condition_variable m_cv;
        std::queue<std::function<void()>> m_jobs;
        std::queue<std::function<void()>> m_completions;
        std::atomic<bool> m_running{ false };

        bool m_ready = false;
        bool m_pageLoading = false;
        bool m_searchLoading = false;
        Page m_page;
        std::vector<SearchResult> m_searchResults;
        std::string m_searchResultQuery;
        std::string m_searchLoadingQuery;
        std::string m_searchError;
        std::vector<std::string> m_history;
        std::vector<std::string> m_bookmarks;
    };
}
