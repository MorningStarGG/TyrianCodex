#pragma once
#include <functional>
#include <string>

// -----------------------------------------------------------------------------------------------------
// CharRegistry: persists character NAME -> stable created-id (the GW2 API /v2/characters/{name}/core "created"
// timestamp). It is the rename DETECTOR: created-ids are unique per character and never change, so when a
// created-id that was recorded under one name is later observed under a DIFFERENT name, that is an unambiguous
// rename -> we invoke the merge callback and re-key the registry.
//
// created comes from the API and is available ONLY with an API key that has the 'characters' scope. Without a key
// the registry simply stays empty: per-character data still works (keyed by the MumbleLink name, which needs no
// key), a rename just can't be auto-merged (the same limitation as having no stable id at all). Persisted to
// char-registry.json in the addon root (survives a cache clear, like progress.json).
// -----------------------------------------------------------------------------------------------------
namespace CharRegistry
{
    using RenameFn = std::function<void(const std::string& fromName, const std::string& toName)>;

    void Init(const std::string& path);   // load char-registry.json (call once at AddonLoad)
    void Shutdown();                       // clear the registry (addon disable / re-enable)

    // Record a character's stable created-id. If `created` was previously recorded under a DIFFERENT name, that is
    // a rename: onRename(oldName, name) runs (the cross-store merge) and the registry is re-keyed. Persists only
    // when the map changes. Empty name/created (no key, or the "default" pre-login bucket) is a no-op.
    void Observe(const std::string& name, const std::string& created, const RenameFn& onRename);

    bool        Known(const std::string& name);       // does this name have a recorded created-id (is it ours)?
    std::string CreatedOf(const std::string& name);   // its created-id, or "" if unknown
}
