#pragma once
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include "Response.h"

// In-memory response cache, keyed by request signature (the Connection builds the key from URL + auth/lang/
// schema). Mirrors Gw2Sharp's CacheMiddleware: a fresh hit short-circuits the request entirely (no network,
// no rate-limit token). Time-to-live: an EXPLICIT per-request TTL (reqTtlSec > 0) is authoritative -- the
// accessor knows how fresh the data must be (e.g. character level = 30s) and the GW2 API often advertises a
// much LONGER `Cache-Control: max-age` than we want, so the explicit value must win. Only when the request
// asked to derive from headers (kCacheFromHeaders) do we read the response's max-age (else a 10-min fallback).
// Static reference data (maps, items, colors) can be promoted to a disk cache later; for now memory only.
namespace Api
{
    class Cache
    {
    public:
        // Returns true and fills `out` if a non-expired entry exists for `key`.
        bool TryGet(const std::string& key, Response& out);

        // Store a successful response. `reqTtlSec` is the request's cacheTtlSec: > 0 = an authoritative fixed
        // TTL (wins over any server max-age); <= 0 = derive the TTL from the response's max-age (10-min fallback).
        void Store(const std::string& key, const Response& resp, int reqTtlSec);

        void Clear();

        // Parse `Cache-Control: max-age=N` from a response. Returns N seconds, or -1 if absent/unparseable.
        static int MaxAgeFromHeaders(const Response& resp);

    private:
        struct Entry
        {
            Response                              resp;
            std::chrono::steady_clock::time_point expires;
        };
        std::map<std::string, Entry> _mem;
        std::mutex                   _mtx;
        static constexpr size_t kMaxEntries = 512;   // bound the response cache; evict the soonest-to-expire over this
    };
}
