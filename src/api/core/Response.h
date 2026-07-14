#pragma once
#include <map>
#include <string>
#include "ApiError.h"

// The raw outcome of one request after it has traversed the middleware pipeline: the HTTP status, the body
// (JSON text, or raw bytes for a tile), the response headers (keys lowercased), pagination totals, whether it
// was served from cache, and the classified error. Typed accessors parse `body` into their model; the
// Connection turns a non-2xx status / transport failure into the Result<T>.error the caller sees.
namespace Api
{
    struct Response
    {
        long        status   = 0;
        std::string body;
        std::map<std::string, std::string> headers;   // lowercased header name -> value

        bool     fromCache   = false;
        ApiError error{};                              // None on a 2xx with a usable body

        int pageTotal   = -1;                          // X-Page-Total   (paginated endpoints)
        int resultTotal = -1;                          // X-Result-Total (paginated endpoints)

        std::string Header(const std::string& lowercaseKey) const
        {
            auto it = headers.find(lowercaseKey);
            return it != headers.end() ? it->second : std::string();
        }
        bool ok() const { return error.kind == ErrorKind::None; }
    };
}
