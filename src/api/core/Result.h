#pragma once
#include "ApiError.h"

// The outcome of a typed API call: either a parsed value (ok) or an ApiError. Returned to the caller's
// callback on the MAIN thread (after Connection::Pump), so a callback can safely touch globals / ImGui.
// Modeled on Gw2Sharp returning a typed model or throwing - here the "throw" is folded into `error` so a
// failed call never crashes the render loop (the whole point of staying additive).
namespace Api
{
    template <class T>
    struct Result
    {
        bool ok = false;
        T value{};
        ApiError error{};

        static Result Ok(T v)
        {
            Result r;
            r.ok = true;
            r.value = std::move(v);
            return r;
        }
        static Result Fail(ApiError e)
        {
            Result r;
            r.ok = false;
            r.error = std::move(e);
            return r;
        }

        explicit operator bool() const { return ok; }
    };
}
