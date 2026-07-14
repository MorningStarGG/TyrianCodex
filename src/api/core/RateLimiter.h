#pragma once
#include <chrono>
#include <mutex>

// Client-side leaky bucket matching the documented GW2 API limit: 300-request burst, refilling 5 tokens/sec
// (300/min), enforced PER IP. We are a single addon making a handful of calls, so this almost never bites -
// but it is the correct, polite behaviour and guarantees we never spray 429s that would also throttle other
// addons sharing the IP. Acquire() blocks the worker thread (never the render thread) until a token is free.
namespace Api
{
    class RateLimiter
    {
    public:
        // Block until a token is available, then consume one. Worker-thread only.
        void Acquire();

    private:
        std::mutex _mtx;
        double     _tokens = kCapacity;
        std::chrono::steady_clock::time_point _last = std::chrono::steady_clock::now();

        static constexpr double kCapacity   = 300.0;
        static constexpr double kRefillPerS = 5.0;
    };
}
