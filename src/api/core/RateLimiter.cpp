#include "RateLimiter.h"

#include <algorithm>
#include <thread>

namespace Api
{
    void RateLimiter::Acquire()
    {
        for (;;)
        {
            double waitSec = 0.0;
            {
                std::lock_guard<std::mutex> lock(_mtx);
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - _last).count();
                _last = now;
                _tokens = std::min(kCapacity, _tokens + elapsed * kRefillPerS);
                if (_tokens >= 1.0) { _tokens -= 1.0; return; }
                // Not enough yet: sleep just long enough to earn the shortfall (outside the lock).
                waitSec = (1.0 - _tokens) / kRefillPerS;
            }
            std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.001, waitSec)));
        }
    }
}
