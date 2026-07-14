#include "Middleware.h"

#include <nlohmann/json.hpp>
#include <thread>

#include "ApiError.h"
#include "Cache.h"
#include "Connection.h"
#include "RateLimiter.h"

namespace Api
{
    // ---- Cache --------------------------------------------------------------------------------------
    Response CacheMiddleware::Handle(MiddlewareContext& ctx, const Next& next)
    {
        if (ctx.request.cacheTtlSec == kCacheNever)
            return next();

        const std::string key = ctx.conn->CacheKey(ctx.request);
        Response cached;
        if (ctx.conn->GetCache().TryGet(key, cached))
            return cached;

        Response resp = next();
        if (resp.ok() && resp.status >= 200 && resp.status < 300)
            ctx.conn->GetCache().Store(key, resp, ctx.request.cacheTtlSec);
        return resp;
    }

    // ---- Request splitter ---------------------------------------------------------------------------
    Response RequestSplitterMiddleware::Handle(MiddlewareContext& ctx, const Next& next)
    {
        constexpr size_t kMax = 200;
        if (ctx.request.bulkIds.size() <= kMax)
            return next();   // single resource, or a bulk request small enough for one call

        const std::vector<std::string> all = ctx.request.bulkIds;
        nlohmann::json merged = nlohmann::json::array();
        Response last;
        for (size_t i = 0; i < all.size(); i += kMax)
        {
            ctx.request.bulkIds.assign(all.begin() + i, all.begin() + std::min(all.size(), i + kMax));
            Response chunk = next();
            if (!chunk.ok())
            {
                ctx.request.bulkIds = all;
                return chunk;   // surface the first failing chunk
            }
            try
            {
                nlohmann::json arr = nlohmann::json::parse(chunk.body);
                if (arr.is_array())
                    for (auto& e : arr) merged.push_back(std::move(e));
            }
            catch (...) { /* a malformed chunk is dropped; the rest still merge */ }
            last = chunk;
        }
        ctx.request.bulkIds = all;
        last.body = merged.dump();
        return last;
    }

    // ---- Rate limit ---------------------------------------------------------------------------------
    Response RateLimitMiddleware::Handle(MiddlewareContext& ctx, const Next& next)
    {
        for (int attempt = 0; ; ++attempt)
        {
            ctx.conn->GetRateLimiter().Acquire();
            Response resp = next();
            if (resp.status != 429 || attempt >= 3)
                return resp;
            // 429 despite the bucket (shared IP): back off increasingly, then retry.
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
        }
    }

    // ---- Exception ----------------------------------------------------------------------------------
    Response ExceptionMiddleware::Handle(MiddlewareContext& ctx, const Next& next)
    {
        Response resp = next();
        if (resp.error.kind != ErrorKind::None)
            return resp;   // the transport already flagged a network failure

        if (resp.status < 200 || resp.status >= 300)
        {
            resp.error.kind   = ClassifyStatus(resp.status);
            resp.error.status = resp.status;
            try
            {
                nlohmann::json j = nlohmann::json::parse(resp.body);
                if (j.is_object() && j.contains("text") && j["text"].is_string())
                    resp.error.text = j["text"].get<std::string>();
            }
            catch (...) { /* non-JSON error body */ }
        }
        return resp;
    }

    std::vector<std::unique_ptr<IApiMiddleware>> DefaultMiddleware()
    {
        std::vector<std::unique_ptr<IApiMiddleware>> v;
        v.push_back(std::make_unique<CacheMiddleware>());
        v.push_back(std::make_unique<RequestSplitterMiddleware>());
        v.push_back(std::make_unique<RateLimitMiddleware>());
        v.push_back(std::make_unique<ExceptionMiddleware>());
        return v;
    }
}
