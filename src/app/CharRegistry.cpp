#include "app/CharRegistry.h"
#include "util/Json.h"   // Json::WriteAtomic (atomic, corruption-safe saves)

#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace
{
    std::map<std::string, std::string> g_byName;   // character name -> stable created-id
    std::string g_path;

    void Save()
    {
        if (g_path.empty()) return;
        nlohmann::json j = nlohmann::json::object();
        for (const auto& kv : g_byName) j[kv.first] = kv.second;
        Json::WriteAtomic(g_path, j.dump());
    }
}

void CharRegistry::Init(const std::string& path)
{
    g_path = path;
    g_byName.clear();
    std::ifstream f(path);
    if (!f) return;
    nlohmann::json j;
    try { f >> j; } catch (...) { return; }
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.value().is_string())
            g_byName[it.key()] = it.value().get<std::string>();
}

void CharRegistry::Shutdown()
{
    g_byName.clear();
    g_path.clear();
}

void CharRegistry::Observe(const std::string& name, const std::string& created, const RenameFn& onRename)
{
    if (name.empty() || created.empty() || name == "default") return;

    auto cur = g_byName.find(name);
    if (cur != g_byName.end() && cur->second == created) return;   // already recorded under this exact name

    // Is this created-id currently recorded under a DIFFERENT name? created-ids are unique + immutable, so a match
    // under another name means that character was renamed to `name`. (At most one match exists.)
    std::string oldName;
    for (const auto& kv : g_byName)
        if (kv.first != name && kv.second == created) { oldName = kv.first; break; }

    if (!oldName.empty())
    {
        // Merge FIRST, then re-key: if the merge throws mid-way, the registry still holds the old name so the next
        // login retries (the per-store merges are idempotent -- a missing source name is a no-op).
        if (onRename) onRename(oldName, name);
        g_byName.erase(oldName);
    }
    g_byName[name] = created;   // record (rename target, recreate with a new id, or a brand-new character)
    Save();
}

bool CharRegistry::Known(const std::string& name)
{
    return g_byName.find(name) != g_byName.end();
}

std::string CharRegistry::CreatedOf(const std::string& name)
{
    auto it = g_byName.find(name);
    return it == g_byName.end() ? std::string() : it->second;
}
