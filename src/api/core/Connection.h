#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

#include "ApiError.h"
#include "Cache.h"
#include "Localization.h"
#include "Middleware.h"
#include "Permissions.h"
#include "RateLimiter.h"
#include "Request.h"
#include "Response.h"
#include "Result.h"

// The heart of the client: configuration (key / schema / locale), the cache + rate limiter, the middleware
// pipeline, and the threading model. Typed accessors call Get<T>; the request runs on a single worker thread
// (so the render thread never blocks on WinHTTP) and the typed Result is delivered to the caller's callback
// on the MAIN thread via Pump() A failed request resolves to Result<T>.error; it never throws into the game loop.
namespace Api
{
    // Percent-encode a string for use as a single URL path/query component (e.g. a character name with
    // spaces). Defined here so accessors can build safe paths without each re-implementing it.
    std::string UrlEncode(const std::string& s);

    class Connection
    {
    public:
        Connection();
        ~Connection();

        // Start / stop the worker thread. Init is idempotent; Shutdown joins the worker, then closes WinHTTP.
        void Init();
        void Shutdown();

        // --- configuration (main thread) ---
        void        SetApiKey(const std::string& key);   // also clears the cache (account data is per-key)
        std::string ApiKey();                            // thread-safe copy (worker reads it)
        bool        HasKey();
        void        SetLocale(Locale l) { _locale = l; }

        // --- token scopes (main thread; written when /v2/tokeninfo resolves) ---
        void      SetTokenInfo(const TokenInfo& ti) { _tokenInfo = ti; }
        TokenInfo GetTokenInfo() const              { return _tokenInfo; }
        bool      HasPermission(TokenPermission p) const { return _tokenInfo.valid && _tokenInfo.Has(p); }

        // --- the typed request entry point (main thread) ---
        // Enqueues `req`; on the worker it runs the pipeline + `parse`, then delivers Result<T> to `cb` via
        // Pump. Auth requests with no key / missing scope short-circuit to an error without hitting the wire.
        template <class T>
        void Get(Request req,
                 std::function<T(const nlohmann::json&)> parse,
                 std::function<void(Result<T>)> cb)
        {
            if (req.auth && !HasKey())
            {
                PostCompletion([cb] {
                    ApiError e; e.kind = ErrorKind::Unauthorized; e.text = "No API key set.";
                    cb(Result<T>::Fail(e));
                });
                return;
            }
            if (req.auth && req.hasScope && !HasPermission(req.scope))
            {
                TokenPermission sc = req.scope;
                PostCompletion([cb, sc] {
                    ApiError e; e.kind = ErrorKind::Forbidden;
                    e.text = std::string("API key lacks the '") + PermissionName(sc) + "' permission.";
                    cb(Result<T>::Fail(e));
                });
                return;
            }

            Enqueue([this, req, parse, cb]() {
                Response resp = Fetch(req);
                Result<T> r;
                if (!resp.ok())
                {
                    r = Result<T>::Fail(resp.error);
                }
                else
                {
                    try
                    {
                        nlohmann::json j = nlohmann::json::parse(resp.body);
                        r = Result<T>::Ok(parse(j));
                    }
                    catch (const std::exception& ex)
                    {
                        ApiError e; e.kind = ErrorKind::Parse; e.status = resp.status; e.text = ex.what();
                        r = Result<T>::Fail(e);
                    }
                    catch (...)
                    {
                        ApiError e; e.kind = ErrorKind::Parse; e.status = resp.status; e.text = "JSON parse error";
                        r = Result<T>::Fail(e);
                    }
                }
                PostCompletion([cb, r]() { cb(r); });
            });
        }

        // Raw escape hatch: ANY endpoint, parsed to nlohmann::json (the value, or an error). This is what
        // makes the framework complete without a hand-typed model for every one of the ~100 v2 families.
        void GetJson(Request req, std::function<void(Result<nlohmann::json>)> cb)
        {
            Get<nlohmann::json>(std::move(req),
                                [](const nlohmann::json& j) { return j; },
                                std::move(cb));
        }

        // Drain completed requests, invoking their callbacks on the calling (main/render) thread. Once/frame.
        void Pump();

        // --- pipeline support (called by middleware on the worker thread) ---
        std::string  BuildUrl(const Request& req);
        std::string  CacheKey(const Request& req);
        Cache&       GetCache()             { return _cache; }
        RateLimiter& GetRateLimiter()       { return _rateLimiter; }

    private:
        Response Fetch(const Request& req);        // compose + run the middleware chain (worker thread)
        Response Transport(const Request& req);    // build URL + headers, WinHTTP, RawResponse -> Response

        void Enqueue(std::function<void()> job);
        void PostCompletion(std::function<void()> done);
        void WorkerLoop();

        // config
        std::mutex  _keyMtx;
        std::string _apiKey;
        Locale      _locale = Locale::En;
        TokenInfo   _tokenInfo;   // main-thread only

        // pipeline
        Cache       _cache;
        RateLimiter _rateLimiter;
        std::vector<std::unique_ptr<IApiMiddleware>> _middleware;

        // worker pool + queues. The pool overlaps the network latency of queued requests (login cache-warm
        // queues hundreds) -- safe because _jobs/_completions are mutex-guarded and the pipeline middleware
        // (Cache + RateLimiter) are internally synchronized; _tokenInfo is touched only on the main thread.
        std::vector<std::thread>          _workers;
        std::atomic<bool>                 _running{ false };
        std::mutex                        _jobMtx;
        std::condition_variable           _jobCv;
        std::queue<std::function<void()>> _jobs;
        std::mutex                        _compMtx;
        std::queue<std::function<void()>> _completions;
    };
}
