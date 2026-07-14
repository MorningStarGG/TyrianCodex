#include "guide/CharacterLevel.h"
#include "util/Json.h"   // Json::WriteAtomic (atomic, corruption-safe saves)

#include <fstream>
#include <nlohmann/json.hpp>

void CharacterLevel::Load(const std::string& path)
{
    cachePath_ = path;
    levelCache_.clear();
    std::ifstream f(path);
    if (!f) return;
    nlohmann::json j;
    try { f >> j; } catch (...) { return; }
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it)
        if (it.value().is_number_integer())
            levelCache_[it.key()] = it.value().get<int>();
}

void CharacterLevel::SaveCache() const
{
    if (cachePath_.empty()) return;
    nlohmann::json j = nlohmann::json::object();
    for (const auto& kv : levelCache_) j[kv.first] = kv.second;
    Json::WriteAtomic(cachePath_, j.dump());
}

void CharacterLevel::SetCachedLevel(GuideState& st, const std::string& name, int level)
{
    if (name.empty() || level <= 0) return;
    if (levelCache_[name] != level) { levelCache_[name] = level; SaveCache(); }   // persist (stale-ok recall next login)
    if (name == st.currentChar && st.charLevel != level) st.charLevel = level;    // apply live if it's the active character
}

void CharacterLevel::RenameChar(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) return;
    auto itF = levelCache_.find(from);
    if (itF == levelCache_.end()) return;
    if (levelCache_.find(to) == levelCache_.end()) levelCache_[to] = itF->second;   // keep `to` if the API already set it
    levelCache_.erase(itF);
    SaveCache();
}

void CharacterLevel::PurgeChar(const std::string& name)
{
    if (levelCache_.erase(name)) SaveCache();
}

void CharacterLevel::CollectCharNames(std::set<std::string>& out) const
{
    for (const auto& kv : levelCache_) out.insert(kv.first);
}

int CharacterLevel::CachedLevel(const std::string& name) const
{
    auto it = levelCache_.find(name);
    return it == levelCache_.end() ? 0 : it->second;
}

void CharacterLevel::RefreshNow(Api::Client& api, GuideState& st, const std::string& charName)
{
    if (levelRefreshing_) return;
    if (!api.HasPermission(Api::TokenPermission::Characters)) return;   // no key/scope yet (retry on scopes/timer)
    if (charName.empty()) return;
    levelRefreshing_ = true;
    // /core is light AND carries level, and it's the SAME endpoint the personal-story preload warms for every
    // character -- so this shares that 30s cache instead of issuing a heavier full-character fetch.
    api.V2().Characters().Core(charName, [this, &st, charName](Api::Result<Api::V2::CharacterCore> r) {
        levelRefreshing_ = false;
        if (r.ok) SetCachedLevel(st, charName, r.value.level);
    });
}

void CharacterLevel::MaybeRefresh(Api::Client& api, GuideState& st, const std::string& charName, float dtSeconds)
{
    if (charName.empty()) return;
    sinceLevelMs_ += (double)dtSeconds * 1000.0;
    const bool   changed  = charName != levelAttemptedFor_;
    if (changed)
    {
        auto it = levelCache_.find(charName);          // show the last-known level INSTANTLY on a character switch
        st.charLevel = (it != levelCache_.end()) ? it->second : 0;   // 0 = unknown -> "Loading..." until the fetch lands
    }
    const double interval = st.charLevel > 0 ? 20000.0 : 3000.0;
    if (changed || sinceLevelMs_ >= interval)
    {
        sinceLevelMs_ = 0.0;
        levelAttemptedFor_ = charName;
        RefreshNow(api, st, charName);
    }
}

// True while we're still WAITING on the API for the level: a key is present and its scopes are resolving, or it
// has 'characters' but the fetch hasn't returned yet. The recommendation panels show "Loading recommendations..."
// in this window rather than flashing wrong-level zones right after login. False with no key/scope (keyless
// accounts get the lowest-zones fallback instead of a stuck spinner).
bool CharacterLevel::LevelLoading(Api::Client& api, const GuideState& st) const
{
    return st.charLevel <= 0 && api.HasKey()
        && (api.ScopesResolving() || api.HasPermission(Api::TokenPermission::Characters));
}
