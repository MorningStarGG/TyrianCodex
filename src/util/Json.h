#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <string>

// -----------------------------------------------------------------------------------------------------
// util/Json: small shared JSON helpers. LoadStoriesKeyed parses the `{ "stories": { "<id>": {...}, ... } }`
// shape used by both data/journal_overrides.json and data/journal_locations.json, calling fn(id, entry) for
// each numeric-keyed entry (non-numeric keys like "_about" are skipped). Best-effort: a missing/unreadable
// file or a malformed root silently no-ops (same as the hand-written loaders it replaces).
// -----------------------------------------------------------------------------------------------------
namespace Json
{
    void LoadStoriesKeyed(const std::string& path, const std::function<void(int, const nlohmann::json&)>& fn);

    // Atomically write `bytes` to `path` via a temp file + rename, so a crash/kill mid-write can never leave a
    // truncated/corrupt file (critical for reset-immune user data like confirmed.json). Creates parent dirs and
    // cleans up the temp on failure. Best-effort; never throws.
    void WriteAtomic(const std::string& path, const std::string& bytes);
}
