#include "app/wiki/WikiService.h"

#include "api/core/Connection.h"
#include "api/core/Http.h"
#include "util/Json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Wiki
{
    namespace
    {
        constexpr const char* kWikiHost = "wiki.guildwars2.com";
        constexpr const char* kWikiBase = "https://wiki.guildwars2.com";
        constexpr const char* kCssUrl =
            "https://wiki.guildwars2.com/load.php?lang=en&modules=site.styles&only=styles&skin=monobook";

        std::string Trim(std::string s)
        {
            while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
            return s;
        }

        std::string Lower(std::string s)
        {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        std::uint64_t Fnv1a(const std::string& s)
        {
            std::uint64_t h = 1469598103934665603ull;
            for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
            return h;
        }

        std::string HexHash(const std::string& s)
        {
            char b[32];
            std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)Fnv1a(s));
            return b;
        }

        std::string ReadFile(const std::string& path)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return {};
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }

        // Delegate to the shared util/Json atomic writer (single implementation -> no drift, .tmp-leak fix).
        void WriteAtomic(const std::string& path, const std::string& bytes) { Json::WriteAtomic(path, bytes); }

        std::vector<std::string> LoadStringList(const std::string& path)
        {
            std::vector<std::string> out;
            const std::string raw = ReadFile(path);
            if (raw.empty()) return out;
            try
            {
                nlohmann::json j = nlohmann::json::parse(raw);
                if (!j.is_array()) return out;
                for (const auto& v : j)
                    if (v.is_string()) out.push_back(v.get<std::string>());
            }
            catch (...) {}
            return out;
        }

        void SaveStringList(const std::string& path, const std::vector<std::string>& values)
        {
            nlohmann::json j = nlohmann::json::array();
            for (const std::string& v : values) j.push_back(v);
            WriteAtomic(path, j.dump(2));
        }

        bool StartsWithI(const std::string& s, const char* prefix)
        {
            const std::string p(prefix);
            if (s.size() < p.size()) return false;
            for (size_t i = 0; i < p.size(); ++i)
                if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)p[i])) return false;
            return true;
        }

        std::string TitleFromUrlOrTitle(std::string input)
        {
            input = Trim(input);
            if (StartsWithI(input, "https://wiki.guildwars2.com/wiki/") ||
                StartsWithI(input, "http://wiki.guildwars2.com/wiki/"))
            {
                size_t p = input.find("/wiki/");
                input = (p == std::string::npos) ? input : input.substr(p + 6);
            }
            else if (StartsWithI(input, "wiki.guildwars2.com/wiki/"))
            {
                input = input.substr(std::string("wiki.guildwars2.com/wiki/").size());
            }
            const size_t hash = input.find('#');
            if (hash != std::string::npos) input.erase(hash);
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
            };
            std::string decoded;
            decoded.reserve(input.size());
            for (size_t i = 0; i < input.size(); ++i)
            {
                if (input[i] == '%' && i + 2 < input.size())
                {
                    const int hi = hex(input[i + 1]);
                    const int lo = hex(input[i + 2]);
                    if (hi >= 0 && lo >= 0)
                    {
                        decoded.push_back((char)((hi << 4) | lo));
                        i += 2;
                        continue;
                    }
                }
                decoded.push_back(input[i] == '+' ? ' ' : input[i]);
            }
            input = std::move(decoded);
            for (char& c : input) if (c == '_') c = ' ';
            return input;
        }

        std::string StripTags(const std::string& s)
        {
            std::string out;
            bool tag = false;
            for (char c : s)
            {
                if (c == '<') { tag = true; continue; }
                if (c == '>') { tag = false; continue; }
                if (!tag) out.push_back(c);
            }
            return out;
        }

        std::string PlainHtmlText(const std::string& html)
        {
            return Trim(DecodeHtmlEntities(StripTags(html)));
        }

        std::string JsonText(const nlohmann::json& j)
        {
            if (j.is_string()) return j.get<std::string>();
            if (j.is_object())
            {
                auto it = j.find("*");
                if (it != j.end() && it->is_string()) return it->get<std::string>();
            }
            return {};
        }

        std::vector<Section> ParseSections(const nlohmann::json& parse)
        {
            std::vector<Section> out;
            auto it = parse.find("sections");
            if (it == parse.end() || !it->is_array()) return out;
            for (const auto& s : *it)
            {
                Section sec;
                if (auto v = s.find("line"); v != s.end() && v->is_string()) sec.line = v->get<std::string>();
                if (auto v = s.find("anchor"); v != s.end() && v->is_string()) sec.anchor = v->get<std::string>();
                if (auto v = s.find("level"); v != s.end())
                {
                    if (v->is_number_integer()) sec.level = v->get<int>();
                    else if (v->is_string()) sec.level = std::atoi(v->get<std::string>().c_str());
                }
                if (!sec.line.empty() && !sec.anchor.empty()) out.push_back(std::move(sec));
            }
            return out;
        }

        std::vector<std::pair<std::string, std::string>> WikiHeaders()
        {
            return {
                { "User-Agent", "TyrianCodex/0.1 native wiki reader (https://github.com/MorningStarGG/TyrianCodex)" },
                { "Accept", "application/json, text/css;q=0.9, */*;q=0.8" },
                { "Accept-Language", "en-US,en;q=0.9" },
            };
        }

        std::string BuildApiUrl(const std::vector<std::pair<std::string, std::string>>& q)
        {
            std::string url = std::string(kWikiBase) + "/api.php?";
            for (size_t i = 0; i < q.size(); ++i)
            {
                if (i) url += '&';
                url += Api::UrlEncode(q[i].first);
                url += '=';
                url += Api::UrlEncode(q[i].second);
            }
            return url;
        }

        bool ParsePageBody(const std::string& raw, const std::string& fallbackTitle, const std::string& css, bool fromCache, Page& out)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(raw);
                const nlohmann::json* parse = nullptr;
                if (auto it = j.find("parse"); it != j.end() && it->is_object()) parse = &(*it);
                if (!parse) return false;

                out.title = fallbackTitle;
                if (auto it = parse->find("title"); it != parse->end() && it->is_string()) out.title = it->get<std::string>();
                out.displayTitle = out.title;
                if (auto it = parse->find("displaytitle"); it != parse->end() && it->is_string())
                {
                    const std::string plain = PlainHtmlText(it->get<std::string>());
                    if (!plain.empty()) out.displayTitle = plain;
                }
                if (auto it = parse->find("revid"); it != parse->end() && it->is_number()) out.revid = it->get<long long>();
                if (auto it = parse->find("text"); it != parse->end()) out.html = JsonText(*it);
                out.css = css;
                out.sections = ParseSections(*parse);
                out.canonicalUrl = std::string(kWikiBase) + "/wiki/" + Api::UrlEncode(out.title);
                out.fromCache = fromCache;
                return !out.html.empty();
            }
            catch (...) { return false; }
        }
    }

    bool IsAllowedImageHost(const std::string& absoluteUrl)
    {
        return StartsWithI(absoluteUrl, "https://wiki.guildwars2.com/")
            || StartsWithI(absoluteUrl, "https://wiki.guildwars.com/");
    }

    std::string DecodeHtmlEntities(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s.compare(i, 5, "&amp;") == 0) { out += '&'; i += 4; }
            else if (s.compare(i, 4, "&lt;") == 0) { out += '<'; i += 3; }
            else if (s.compare(i, 4, "&gt;") == 0) { out += '>'; i += 3; }
            else if (s.compare(i, 6, "&quot;") == 0) { out += '"'; i += 5; }
            else if (s.compare(i, 5, "&#39;") == 0) { out += '\''; i += 4; }
            else out.push_back(s[i]);
        }
        return out;
    }

    void Service::Init(const std::string& addonDir)
    {
        if (m_running.exchange(true)) return;
        m_cacheDir = addonDir + "\\cache\\wiki\\v1";
        m_dataDir  = addonDir;   // bookmarks/history/recent are user data -> the addon root, beside progress.json
        std::error_code ec;
        fs::create_directories(m_dataDir, ec);
        fs::create_directories(m_cacheDir + "\\pages", ec);
        fs::create_directories(m_cacheDir + "\\search", ec);
        fs::create_directories(m_cacheDir + "\\css", ec);
        LoadLists();
        m_worker = std::thread([this] { WorkerLoop(); });
        m_ready = true;
    }

    void Service::Shutdown()
    {
        if (!m_running.exchange(false)) return;
        m_cv.notify_all();
        Api::Http::Abort(Api::Http::Scope::Wiki);
        if (m_worker.joinable()) m_worker.join();
        SaveLists();
        std::queue<std::function<void()>>().swap(m_jobs);
        std::queue<std::function<void()>>().swap(m_completions);
    }

    void Service::Pump()
    {
        std::queue<std::function<void()>> ready;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            std::swap(ready, m_completions);
        }
        while (!ready.empty())
        {
            auto fn = std::move(ready.front());
            ready.pop();
            try { fn(); } catch (...) {}
        }
        if (m_listsDirty) { SaveLists(); m_listsDirty = false; }   // batched: one write per navigation, not three
    }

    void Service::Enqueue(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_jobs.push(std::move(job));
        }
        m_cv.notify_one();
    }

    void Service::Post(std::function<void()> done)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_completions.push(std::move(done));
    }

    void Service::WorkerLoop()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_mtx);
                m_cv.wait(lock, [this] { return !m_running || !m_jobs.empty(); });
                if (!m_running) return;
                job = std::move(m_jobs.front());
                m_jobs.pop();
            }
            try { job(); } catch (...) {}
        }
    }

    void Service::OpenPage(const std::string& titleOrUrl, bool forceRefresh)
    {
        const std::string title = TitleFromUrlOrTitle(titleOrUrl);
        if (title.empty() || !m_running) return;
        m_pageLoading = true;
        m_currentRequestTitle = title;
        Enqueue([this, title, forceRefresh] {
            PageResult r = LoadPageWorker(title, forceRefresh);
            Post([this, title, r = std::move(r)]() mutable {
                if (title != m_currentRequestTitle) return;
                m_pageLoading = false;
                if (r.ok)
                {
                    m_page = std::move(r.page);
                    PushHistory(m_page.title);   // sets m_listsDirty; Pump() flushes once this frame
                }
                else
                {
                    m_page.error = r.page.error.empty() ? "Could not load wiki page." : r.page.error;
                }
            });
        });
    }

    void Service::Refresh()
    {
        if (!m_page.title.empty()) OpenPage(m_page.title, true);
    }

    void Service::Search(const std::string& query)
    {
        const std::string q = Trim(query);
        if (q.size() < 2 || !m_running) return;
        m_searchLoading = true;
        m_searchLoadingQuery = q;
        m_currentSearchQuery = q;
        m_searchError.clear();
        Enqueue([this, q] {
            SearchJobResult r = SearchWorker(q);
            Post([this, q, r = std::move(r)]() mutable {
                if (q != m_currentSearchQuery) return;   // a newer query superseded this one; drop the stale result
                m_searchLoading = false;
                m_searchLoadingQuery.clear();
                m_searchResultQuery = r.query;
                m_searchResults = std::move(r.results);
                m_searchError = r.error;
            });
        });
    }

    std::string Service::LoadCssWorker(bool forceRefresh)
    {
        const std::string path = m_cacheDir + "\\css\\site.styles.css";
        if (!forceRefresh)
        {
            std::string cached = ReadFile(path);
            if (!cached.empty()) return cached;
        }
        Api::Http::RawResponse resp = Api::Http::Get(kCssUrl, WikiHeaders(), Api::Http::Scope::Wiki);
        if (resp.status == 200 && !resp.body.empty())
        {
            WriteAtomic(path, resp.body);
            return resp.body;
        }
        return ReadFile(path);
    }

    Service::PageResult Service::LoadPageWorker(std::string titleOrUrl, bool forceRefresh)
    {
        const std::string title = TitleFromUrlOrTitle(titleOrUrl);
        PageResult result;
        std::string css = LoadCssWorker(forceRefresh);
        const std::string pagePath = m_cacheDir + "\\pages\\" + HexHash(Lower(title)) + ".json";

        if (!forceRefresh)
        {
            const std::string cached = ReadFile(pagePath);
            if (!cached.empty() && ParsePageBody(cached, title, css, true, result.page))
            {
                result.ok = true;
                return result;
            }
        }

        const std::string url = BuildApiUrl({
            { "action", "parse" },
            { "format", "json" },
            { "formatversion", "2" },
            { "redirects", "1" },
            { "page", title },
            { "prop", "text|sections|displaytitle|revid" },   // only these are parsed into Page (smaller response)
            { "disablelimitreport", "1" },
            { "disableeditsection", "1" },
        });
        Api::Http::RawResponse resp = Api::Http::Get(url, WikiHeaders(), Api::Http::Scope::Wiki);
        if (resp.status == 200 && !resp.body.empty() && ParsePageBody(resp.body, title, css, false, result.page))
        {
            WriteAtomic(pagePath, resp.body);
            result.ok = true;
            return result;
        }

        const std::string cached = ReadFile(pagePath);
        if (!cached.empty() && ParsePageBody(cached, title, css, true, result.page))
        {
            result.ok = true;
            return result;
        }

        result.page.title = title;
        result.page.error = resp.networkError ? resp.errorText : ("HTTP " + std::to_string(resp.status));
        return result;
    }

    Service::SearchJobResult Service::SearchWorker(std::string query)
    {
        SearchJobResult out;
        out.query = query;
        const std::string path = m_cacheDir + "\\search\\" + HexHash(Lower(query)) + ".json";
        std::string body = ReadFile(path);
        if (body.empty())
        {
            const std::string url = BuildApiUrl({
                { "action", "query" },
                { "format", "json" },
                { "formatversion", "2" },
                { "list", "prefixsearch|search" },
                { "pssearch", query },
                { "pslimit", "8" },
                { "srsearch", query },
                { "srlimit", "8" },
            });
            Api::Http::RawResponse resp = Api::Http::Get(url, WikiHeaders(), Api::Http::Scope::Wiki);
            if (resp.status == 200 && !resp.body.empty())
            {
                body = resp.body;
                WriteAtomic(path, body);
            }
            else
            {
                out.error = resp.networkError ? resp.errorText : ("HTTP " + std::to_string(resp.status));
                return out;
            }
        }

        try
        {
            nlohmann::json j = nlohmann::json::parse(body);
            std::unordered_set<std::string> seen;
            const auto add = [&](const std::string& title, const std::string& snippet) {
                if (title.empty()) return;
                const std::string key = Lower(title);
                if (!seen.insert(key).second) return;
                out.results.push_back(SearchResult{ title, StripTags(snippet) });
            };
            const nlohmann::json& q = j["query"];
            if (auto it = q.find("prefixsearch"); it != q.end() && it->is_array())
                for (const auto& r : *it)
                    add(r.value("title", ""), "");
            if (auto it = q.find("search"); it != q.end() && it->is_array())
                for (const auto& r : *it)
                    add(r.value("title", ""), r.value("snippet", ""));
        }
        catch (...) { out.error = "Search parse failed."; }
        return out;
    }

    void Service::LoadLists()
    {
        m_history = LoadStringList(m_dataDir + "\\wiki_history.json");
        m_bookmarks = LoadStringList(m_dataDir + "\\wiki_bookmarks.json");
    }

    void Service::SaveLists()
    {
        SaveStringList(m_dataDir + "\\wiki_history.json", m_history);
        SaveStringList(m_dataDir + "\\wiki_bookmarks.json", m_bookmarks);
    }

    void Service::SaveOpenTabs(const std::vector<std::string>& titles, int active)
    {
        nlohmann::json j;
        j["active"] = active;
        nlohmann::json arr = nlohmann::json::array();
        for (const std::string& s : titles) arr.push_back(s);
        j["tabs"] = arr;
        WriteAtomic(m_dataDir + "\\wiki_tabs.json", j.dump(2));
    }

    bool Service::LoadOpenTabs(std::vector<std::string>& titles, int& active)
    {
        titles.clear();
        active = 0;
        const std::string raw = ReadFile(m_dataDir + "\\wiki_tabs.json");
        if (raw.empty()) return false;
        try
        {
            nlohmann::json j = nlohmann::json::parse(raw);
            if (j.is_object())
            {
                active = j.value("active", 0);
                if (auto it = j.find("tabs"); it != j.end() && it->is_array())
                    for (const auto& v : *it)
                        if (v.is_string()) titles.push_back(v.get<std::string>());
            }
        }
        catch (...) { return false; }
        return !titles.empty();
    }

    // The single "History" list: de-duplicated, most-recent-first. Re-opening a page moves its one entry to the
    // top rather than appending a repeat. (Replaced the old Recent/History split, which were near-duplicates.)
    void Service::PushHistory(const std::string& title)
    {
        if (title.empty()) return;
        m_history.erase(std::remove(m_history.begin(), m_history.end(), title), m_history.end());
        m_history.insert(m_history.begin(), title);
        if (m_history.size() > 100) m_history.resize(100);
        m_listsDirty = true;
    }

    bool Service::IsBookmarked(const std::string& title) const
    {
        return std::find(m_bookmarks.begin(), m_bookmarks.end(), title) != m_bookmarks.end();
    }

    void Service::ToggleBookmark(const std::string& title)
    {
        if (title.empty()) return;
        auto it = std::find(m_bookmarks.begin(), m_bookmarks.end(), title);
        if (it != m_bookmarks.end()) m_bookmarks.erase(it);
        else m_bookmarks.insert(m_bookmarks.begin(), title);
        m_listsDirty = true;   // flushed by Pump() next frame; Shutdown() also saves unconditionally
    }
}
