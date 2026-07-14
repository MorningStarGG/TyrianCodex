#include "app/AccountLevels.h"
#include "api/core/Json.h"   // Api::Json null-safe readers
#include "util/Json.h"       // Json::WriteAtomic (atomic, corruption-safe saves)

#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

// Single-threaded (main/render thread): Reconcile is called once per frame from the Render loop, so no locking.
namespace
{
    struct Entry { int seen = 0; long long total = 0; };   // seen = last-observed level; total = lifetime contributed
    std::map<std::string, Entry> g_ledger;
    std::string g_path;

    void Save()
    {
        if (g_path.empty()) return;
        nlohmann::json j = nlohmann::json::object();
        for (const auto& kv : g_ledger) j[kv.first] = { {"s", kv.second.seen}, {"t", kv.second.total} };
        Json::WriteAtomic(g_path, j.dump());
    }
}

void AccountLevels::Init(const std::string& path)
{
    g_ledger.clear();
    g_path = path;
    std::ifstream f(path);
    if (!f) return;
    nlohmann::json j;
    try { f >> j; } catch (...) { return; }
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        if (!it.value().is_object()) continue;
        Entry e;
        e.seen  = Api::Json::Int(it.value(), "s", 0);
        e.total = Api::Json::Int64(it.value(), "t", 0);
        g_ledger[it.key()] = e;
    }
}

void AccountLevels::Shutdown() { g_ledger.clear(); g_path.clear(); }

void AccountLevels::Reconcile(const std::vector<std::pair<std::string, int>>& roster)
{
    bool changed = false;
    for (const auto& nl : roster)
    {
        const std::string& name = nl.first;
        const int level = nl.second;
        if (name.empty() || level <= 0) continue;
        auto it = g_ledger.find(name);
        if (it == g_ledger.end())               { g_ledger[name] = { level, level }; changed = true; }                 // new -> seed
        else if (level > it->second.seen)        { it->second.total += level - it->second.seen; it->second.seen = level; changed = true; }  // leveled up
        else if (level < it->second.seen)        { it->second.total += level; it->second.seen = level; changed = true; }  // recreated -> add fresh, keep old
        // level == seen: no change
    }
    if (changed) Save();
}

long long AccountLevels::Total()
{
    long long t = 0;
    for (const auto& kv : g_ledger) t += kv.second.total;
    return t;
}

void AccountLevels::RenameChar(const std::string& fromName, const std::string& toName)
{
    if (fromName.empty() || toName.empty() || fromName == toName) return;
    auto itF = g_ledger.find(fromName);
    if (itF == g_ledger.end()) return;
    g_ledger[toName] = itF->second;   // MOVE/replace: discards any fresh new-char seed Reconcile added for toName
    g_ledger.erase(itF);
    Save();
}

void AccountLevels::PurgeChar(const std::string& name)
{
    if (g_ledger.erase(name)) Save();
}

void AccountLevels::CollectCharNames(std::set<std::string>& out)
{
    for (const auto& kv : g_ledger) out.insert(kv.first);
}
