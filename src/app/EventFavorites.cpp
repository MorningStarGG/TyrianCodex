#include "app/EventFavorites.h"
#include "Shared.h"
#include "util/Json.h" // Json::WriteAtomic (atomic, corruption-safe saves)

#include <algorithm>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace
{
    struct Flags
    {
        bool fav = false;
        bool notify = false;
    };

    bool g_loaded = false;
    uint64_t g_version = 0;
    std::map<std::string, Flags> g_byKey;   // only entries with at least one flag set are kept
    std::vector<std::string> g_notifyKeys;  // cache of the notify set, rebuilt on change

    std::string FavoritesPath()
    {
        const char* d = AddonDir();
        return d ? std::string(d) + "\\event-favorites.json" : std::string();
    }

    void RebuildNotifyKeys()
    {
        g_notifyKeys.clear();
        for (const auto& kv : g_byKey)
            if (kv.second.notify) g_notifyKeys.push_back(kv.first);
    }

    void Save()
    {
        const std::string path = FavoritesPath();
        if (path.empty()) return;
        nlohmann::json j = nlohmann::json::object();
        for (const auto& kv : g_byKey)
        {
            if (!kv.second.fav && !kv.second.notify) continue;   // never persist a cleared entry
            nlohmann::json e = nlohmann::json::object();
            e["fav"] = kv.second.fav;
            e["notify"] = kv.second.notify;
            j[kv.first] = std::move(e);
        }
        Json::WriteAtomic(path, j.dump());
    }

    // Set one flag and persist. Dropping fully-cleared entries keeps the file from accumulating
    // {"fav":false,"notify":false} tombstones for every event the player has ever poked.
    void SetOne(const std::string& key, bool isFav, bool on)
    {
        if (key.empty()) return;
        Flags& f = g_byKey[key];
        bool& slot = isFav ? f.fav : f.notify;
        if (slot == on) return;
        slot = on;
        if (!f.fav && !f.notify) g_byKey.erase(key);
        ++g_version;
        RebuildNotifyKeys();
        Save();
    }
}

void EventFavorites::Load()
{
    if (g_loaded) return;
    g_loaded = true;
    g_byKey.clear();
    const std::string path = FavoritesPath();
    if (path.empty()) { RebuildNotifyKeys(); return; }
    try
    {
        std::ifstream f(path);
        if (f)
        {
            nlohmann::json j;
            f >> j;
            if (j.is_object())
                for (auto it = j.begin(); it != j.end(); ++it)
                {
                    if (!it.value().is_object()) continue;
                    Flags fl;
                    fl.fav = it.value().value("fav", false);
                    fl.notify = it.value().value("notify", false);
                    if (fl.fav || fl.notify) g_byKey[it.key()] = fl;
                }
        }
    }
    catch (...)
    {
        g_byKey.clear();   // a corrupt file must not take the addon down with it
    }
    RebuildNotifyKeys();
    ++g_version;
}

bool EventFavorites::IsFavorite(const std::string& eventKey)
{
    auto it = g_byKey.find(eventKey);
    return it != g_byKey.end() && it->second.fav;
}

bool EventFavorites::IsNotify(const std::string& eventKey)
{
    auto it = g_byKey.find(eventKey);
    return it != g_byKey.end() && it->second.notify;
}

void EventFavorites::SetFavorite(const std::string& eventKey, bool on) { SetOne(eventKey, true, on); }
void EventFavorites::SetNotify(const std::string& eventKey, bool on) { SetOne(eventKey, false, on); }
void EventFavorites::ToggleFavorite(const std::string& eventKey) { SetOne(eventKey, true, !IsFavorite(eventKey)); }
void EventFavorites::ToggleNotify(const std::string& eventKey) { SetOne(eventKey, false, !IsNotify(eventKey)); }

const std::vector<std::string>& EventFavorites::NotifyKeys() { return g_notifyKeys; }
uint64_t EventFavorites::Version() { return g_version; }
