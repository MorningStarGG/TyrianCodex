#include "Cache.h"

#include <cstdlib>

namespace Api
{
    int Cache::MaxAgeFromHeaders(const Response& resp)
    {
        const std::string cc = resp.Header("cache-control");
        if (cc.empty()) return -1;
        const std::string needle = "max-age=";
        size_t p = cc.find(needle);
        if (p == std::string::npos) return -1;
        p += needle.size();
        long v = std::strtol(cc.c_str() + p, nullptr, 10);
        return v > 0 ? (int)v : -1;
    }

    bool Cache::TryGet(const std::string& key, Response& out)
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto it = _mem.find(key);
        if (it == _mem.end()) return false;
        if (std::chrono::steady_clock::now() >= it->second.expires)
        {
            _mem.erase(it);
            return false;
        }
        out = it->second.resp;
        out.fromCache = true;
        return true;
    }

    void Cache::Store(const std::string& key, const Response& resp, int reqTtlSec)
    {
        // An explicit per-request TTL (>0) is authoritative: the accessor chose how fresh the data must be, and
        // the GW2 API frequently advertises a much longer max-age than we want (e.g. character level / account
        // data). Only fall back to the server's Cache-Control max-age when the request asked to (reqTtlSec <= 0).
        int ttl = (reqTtlSec > 0) ? reqTtlSec
                                  : (MaxAgeFromHeaders(resp) >= 0 ? MaxAgeFromHeaders(resp) : 600);
        if (ttl <= 0) return;
        std::lock_guard<std::mutex> lock(_mtx);
        Entry e;
        e.resp = resp;
        e.resp.fromCache = false;   // store the live copy; TryGet sets fromCache on the served copy
        e.expires = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);
        _mem[key] = std::move(e);
        if (_mem.size() > kMaxEntries)   // bound growth: evict the soonest-expiring entry that ISN'T the one we just stored
        {
            auto victim = _mem.end();
            for (auto it = _mem.begin(); it != _mem.end(); ++it)
                if (it->first != key && (victim == _mem.end() || it->second.expires < victim->second.expires)) victim = it;
            if (victim != _mem.end()) _mem.erase(victim);   // always drops back to the bound (was: skipped eviction when the soonest WAS key)
        }
    }

    void Cache::Clear()
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _mem.clear();
    }
}
