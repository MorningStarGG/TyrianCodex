#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

// The ONLY code that touches WinHTTP. A blocking HTTPS GET, run on the Connection's worker thread (never the
// render thread). Returns the raw status/body/headers; classification into ErrorKind and JSON parsing happen
// above this layer. Body is a byte-safe std::string, so the same call serves JSON and binary (map tiles).
namespace Api::Http
{
    // Each subsystem that runs its own worker(s) owns a cancellation SCOPE. Abort(scope)/Resume(scope) affect
    // ONLY that scope's requests, and Get(scope) tags each request with its scope -- so one module tearing down
    // its worker can NEVER cancel another's transport. (This replaces the old single global abort flag, where
    // e.g. PriceHistory::Shutdown's Abort() would leave the whole transport aborted and break the API client.)
    enum class Scope { Api, Images, PriceHistory, Market, Wiki, Bootstrap, Count };

    struct RawResponse
    {
        long        status = 0;                          // HTTP status, or 0 if the transport never completed
        std::string body;                                // response bytes (JSON text or image data)
        std::map<std::string, std::string> headers;      // lowercased header name -> value
        bool        networkError = false;                // true if WinHTTP failed before a status was received
        std::string errorText;                           // a short transport error message when networkError
    };

    // Perform a GET on behalf of `scope`. `headers` are extra request headers as {name, value} pairs (User-Agent,
    // Authorization, Accept-Language, ...). Blocking; safe to call only off the render thread. A request whose
    // scope is currently aborted returns immediately with networkError + errorText "aborted".
    RawResponse Get(const std::string& url,
                    const std::vector<std::pair<std::string, std::string>>& headers,
                    Scope scope = Scope::Api);

    // Close the cached WinHTTP session (call on addon unload, after every worker thread has stopped).
    void Shutdown();

    // Per-scope cancellation, so a worker's join never waits out an in-flight blocking request. Abort(scope)
    // closes that scope's in-flight request handles (a concurrent close makes the blocking WinHTTP call return
    // at once) and blocks NEW requests in that scope -- call it BEFORE joining the scope's workers. Resume(scope)
    // re-enables the scope on (re)start (e.g. after a disable/re-enable). Only the owning subsystem touches its
    // own scope; scopes are independent.
    void Abort(Scope scope);
    void Resume(Scope scope);

    // Diagnostics snapshot. apiAborting is Scope::Api specifically (the GW2-API transport); an UNBALANCED
    // abort/resume count with apiAborting==true would mean the API scope is stuck aborted. sessionOpen==false
    // with nothing aborting just means the WinHTTP session hasn't been (re)opened yet.
    struct DiagState { bool apiAborting = false; bool sessionOpen = false; int abortCount = 0; int resumeCount = 0; int activeReqs = 0; };
    DiagState Diag();
}
