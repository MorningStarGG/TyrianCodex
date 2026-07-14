#pragma once
#include <functional>
#include <memory>
#include "Request.h"
#include "Response.h"

// The request pipeline, A request flowsouter -> inner on the way down and inner -> outer on the way back up
// each middleware may short-circuit (cache hit), retry (rate limit), fan-out (splitter),
// or annotate (exception). The Connection composes the chain around a base "transport" step
// (build URL -> WinHTTP -> Response) and runs it on the worker thread.
// Default order (outermost first): Cache -> Splitter -> RateLimit -> Exception -> [transport].
//   * Cache is outermost so a hit costs neither a rate-limit token nor a network call.
//   * Splitter is above RateLimit/transport so each bulk chunk independently passes through them.
namespace Api
{
    class Connection;

    struct MiddlewareContext
    {
        Request request; // mutable: the splitter rewrites bulkIds per chunk
        Connection *conn = nullptr;
    };

    using Next = std::function<Response()>;

    class IApiMiddleware
    {
    public:
        virtual ~IApiMiddleware() = default;
        virtual Response Handle(MiddlewareContext &ctx, const Next &next) = 0;
    };

    // Fresh-cache short-circuit; stores successful responses with a header/default TTL.
    class CacheMiddleware : public IApiMiddleware
    {
    public:
        Response Handle(MiddlewareContext &ctx, const Next &next) override;
    };

    // Splits a bulk request (> 200 ids) into chunks, calls `next` per chunk, and concatenates the JSON arrays.
    class RequestSplitterMiddleware : public IApiMiddleware
    {
    public:
        Response Handle(MiddlewareContext &ctx, const Next &next) override;
    };

    // Acquires a leaky-bucket token before each send; on a 429 backs off and retries a few times.
    class RateLimitMiddleware : public IApiMiddleware
    {
    public:
        Response Handle(MiddlewareContext &ctx, const Next &next) override;
    };

    // Classifies a non-2xx status into Response.error (and lifts the GW2 {"text": ...} message).
    class ExceptionMiddleware : public IApiMiddleware
    {
    public:
        Response Handle(MiddlewareContext &ctx, const Next &next) override;
    };

    // The default chain (Cache, Splitter, RateLimit, Exception), outermost first.
    std::vector<std::unique_ptr<IApiMiddleware>> DefaultMiddleware();
}
