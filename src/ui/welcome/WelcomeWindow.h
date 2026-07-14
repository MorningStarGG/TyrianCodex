#pragma once
class App;

// The first-run Welcome window: a one-time, on-brand prompt to add a GW2 API key (the field, where to get one,
// what it unlocks). Shown once when the addon has never been onboarded and no key is set; gated by the
// persisted Config::onboardedV1 flag (set on Save/Skip). The key field binds straight to Config::apiKey, which
// entry.cpp's watcher applies live -- no separate commit. See builder/docs/api-client.md.
namespace Welcome
{
    void Draw(App& app);   // per-frame from entry.cpp Render; self-gates + latches the decision for the session
}
