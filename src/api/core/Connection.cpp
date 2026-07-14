#include "Connection.h"

#include <cstdio>

#include "Http.h"
#include "Schema.h"

namespace Api
{
    std::string UrlEncode(const std::string& s)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~')
                out.push_back((char)c);
            else
            {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    }

    Connection::Connection()
    {
        _middleware = DefaultMiddleware();
    }

    Connection::~Connection()
    {
        Shutdown();
    }

    void Connection::Init()
    {
        if (_running.exchange(true)) return;   // already running
        Http::Resume(Http::Scope::Api);        // clear our scope's abort from a previous disable so requests work
        constexpr int kWorkers = 6;            // overlaps request latency (login warm queues hundreds); rate limiter still caps total
        for (int i = 0; i < kWorkers; ++i) _workers.emplace_back([this] { WorkerLoop(); });
    }

    void Connection::Shutdown()
    {
        if (!_running.exchange(false)) return;
        _jobCv.notify_all();                   // wake every pool worker
        Http::Abort(Http::Scope::Api);         // cancel our in-flight requests so the joins return at once (no ~12s wait)
        for (std::thread& t : _workers) if (t.joinable()) t.join();
        _workers.clear();
        Http::Shutdown();
    }

    void Connection::SetApiKey(const std::string& key)
    {
        {
            std::lock_guard<std::mutex> lock(_keyMtx);
            _apiKey = key;
        }
        _tokenInfo = TokenInfo{};   // scopes unknown until the next /v2/tokeninfo resolves
        _cache.Clear();             // cached account data belonged to the previous key
    }

    std::string Connection::ApiKey()
    {
        std::lock_guard<std::mutex> lock(_keyMtx);
        return _apiKey;
    }

    bool Connection::HasKey()
    {
        return !ApiKey().empty();
    }

    std::string Connection::BuildUrl(const Request& req)
    {
        const std::string host   = req.host.empty() ? kApiHost : req.host;
        const std::string scheme = req.secure ? kApiScheme : "http";

        std::string url = scheme + "://" + host + req.path;

        std::string q;
        auto add = [&q](const std::string& k, const std::string& v) {
            if (!q.empty()) q += '&';
            q += k; q += '='; q += v;
        };
        for (const auto& kv : req.query) add(UrlEncode(kv.first), UrlEncode(kv.second));
        if (!req.bulkIds.empty())
        {
            std::string ids;
            for (size_t i = 0; i < req.bulkIds.size(); ++i) { if (i) ids += ','; ids += req.bulkIds[i]; }
            add(req.bulkKey, ids);
        }
        // Cross-cutting params on every request: schema version + language.
        add("v", kSchemaVersion);
        add("lang", LocaleCode(_locale));

        if (!q.empty()) { url += '?'; url += q; }
        return url;
    }

    std::string Connection::CacheKey(const Request& req)
    {
        // Per-key isolation for authed data; anon endpoints share one entry across keys.
        return BuildUrl(req) + "|" + (req.auth ? ApiKey() : std::string());
    }

    Response Connection::Transport(const Request& req)
    {
        const std::string url = BuildUrl(req);

        std::vector<std::pair<std::string, std::string>> headers;
        headers.emplace_back("User-Agent", kUserAgent);
        headers.emplace_back("Accept", req.binary ? "*/*" : "application/json");
        headers.emplace_back("Accept-Language", LocaleCode(_locale));
        if (req.auth)
        {
            std::string key = ApiKey();
            if (!key.empty()) headers.emplace_back("Authorization", "Bearer " + key);
        }

        Http::RawResponse raw = Http::Get(url, headers, Http::Scope::Api);

        Response resp;
        resp.status  = raw.status;
        resp.body    = std::move(raw.body);
        resp.headers = std::move(raw.headers);
        if (raw.networkError)
        {
            resp.error.kind = ErrorKind::Network;
            resp.error.text = raw.errorText;
        }
        auto pt = resp.headers.find("x-page-total");
        if (pt != resp.headers.end())   resp.pageTotal   = std::atoi(pt->second.c_str());
        auto rt = resp.headers.find("x-result-total");
        if (rt != resp.headers.end())   resp.resultTotal = std::atoi(rt->second.c_str());
        return resp;
    }

    Response Connection::Fetch(const Request& req)
    {
        MiddlewareContext ctx;
        ctx.request = req;
        ctx.conn    = this;

        Next chain = [this, &ctx]() { return Transport(ctx.request); };
        for (auto it = _middleware.rbegin(); it != _middleware.rend(); ++it)
        {
            IApiMiddleware* mw = it->get();
            Next inner = chain;
            chain = [mw, &ctx, inner]() { return mw->Handle(ctx, inner); };
        }
        return chain();
    }

    void Connection::Enqueue(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(_jobMtx);
            _jobs.push(std::move(job));
        }
        _jobCv.notify_one();
    }

    void Connection::PostCompletion(std::function<void()> done)
    {
        std::lock_guard<std::mutex> lock(_compMtx);
        _completions.push(std::move(done));
    }

    void Connection::Pump()
    {
        std::queue<std::function<void()>> ready;
        {
            std::lock_guard<std::mutex> lock(_compMtx);
            std::swap(ready, _completions);
        }
        while (!ready.empty())
        {
            auto fn = std::move(ready.front());
            ready.pop();
            try { fn(); } catch (...) { /* a consumer callback must never break the pump */ }
        }
    }

    void Connection::WorkerLoop()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(_jobMtx);
                _jobCv.wait(lock, [this] { return !_running || !_jobs.empty(); });
                if (!_running) return;          // shutdown: drop any queued jobs and exit immediately
                if (_jobs.empty()) continue;
                job = std::move(_jobs.front());
                _jobs.pop();
            }
            try { job(); } catch (...) { /* never let a request kill the worker thread */ }
        }
    }
}
