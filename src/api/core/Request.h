#pragma once
#include <string>
#include <utility>
#include <vector>
#include "Permissions.h"

// A single API request, fully described by the typed accessor that builds it: where to go (host/path/query),
// whether it is authenticated and which scope it needs, how long its response may be cached, whether the body
// is binary (a map tile), and - for bulk-expanded endpoints - the list of ids to fetch (the RequestSplitter
// chunks > 200 into multiple sub-requests and merges them, mirroring Gw2Sharp's RequestSplitterMiddleware).
// The Connection fills in the cross-cutting query params (schema `v`, `lang`) and the Authorization header.
namespace Api
{
    // Cache lifetime for a request's response. -1 = derive from response headers (Cache-Control max-age /
    // Expires), falling back to a per-endpoint default the accessor sets; 0 = never cache; >0 = fixed seconds.
    constexpr int kCacheFromHeaders = -1;
    constexpr int kCacheNever       = 0;

    struct Request
    {
        std::string host;                                            // empty -> kApiHost
        bool        secure = true;                                   // https
        std::string path;                                            // e.g. "/v2/characters/My%20Char"
        std::vector<std::pair<std::string, std::string>> query;      // extra params besides v/lang/auth

        bool            auth     = false;                            // attach Authorization: Bearer <key>
        bool            hasScope = false;                            // `scope` is meaningful (auth endpoints)
        TokenPermission scope    = TokenPermission::Account;         // the scope this endpoint requires

        int  cacheTtlSec = kCacheFromHeaders;                        // see constants above
        bool binary      = false;                                    // raw bytes (image) - skip JSON parse

        // Bulk expansion: when non-empty, the request fetches these ids via `?<bulkKey>=a,b,c`. The splitter
        // breaks > 200 ids into chunks and concatenates the returned arrays. Empty -> a single resource.
        std::vector<std::string> bulkIds;
        std::string              bulkKey = "ids";
    };
}
