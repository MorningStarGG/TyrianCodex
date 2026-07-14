#pragma once
#include <string>

// A typed failure from the API client. Each request resolves to either a value or this error 
// (see Result<T>). Maps the HTTP status the ExceptionMiddleware
// classified (or a transport/parse failure) to a small, switchable kind so callers can react (e.g. "no
// permission" vs "rate limited" vs "network down") without string-matching.
namespace Api
{
    enum class ErrorKind
    {
        None = 0,        // success (Result<T>.ok)
        Network,         // transport failed (no connection, DNS, TLS, timeout) - retry later
        Parse,           // 2xx but the body did not match the expected JSON shape
        BadRequest,      // 400
        Unauthorized,    // 401 - missing/invalid API key
        Forbidden,       // 403 - key lacks the required permission (or invalid key)
        NotFound,        // 404 - unknown id / endpoint
        RateLimited,     // 429 - leaky bucket emptied; the RateLimiter backs off + retries first
        ServiceDown,     // 5xx / 503 - API partial or full downtime (happens; stay additive)
        Unknown,         // any other non-2xx
    };

    struct ApiError
    {
        ErrorKind   kind = ErrorKind::None;
        long        status = 0;     // raw HTTP status (0 if transport never completed)
        std::string text;           // GW2 sends {"text": "..."} on errors; or our transport message

        bool ok() const { return kind == ErrorKind::None; }
    };

    // Classify an HTTP status into an ErrorKind (used by the ExceptionMiddleware). 2xx -> None.
    inline ErrorKind ClassifyStatus(long status)
    {
        if (status >= 200 && status < 300) return ErrorKind::None;
        switch (status)
        {
            case 400: return ErrorKind::BadRequest;
            case 401: return ErrorKind::Unauthorized;
            case 403: return ErrorKind::Forbidden;
            case 404: return ErrorKind::NotFound;
            case 429: return ErrorKind::RateLimited;
            case 502:
            case 503:
            case 504: return ErrorKind::ServiceDown;
            default:  return status >= 500 ? ErrorKind::ServiceDown : ErrorKind::Unknown;
        }
    }
}
