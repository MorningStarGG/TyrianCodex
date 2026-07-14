#include "guide/Progress.h"
#include "util/Json.h"   // Json::WriteAtomic (atomic, corruption-safe saves)

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{
    // Read a "{character: [stepId,...]}" JSON file into the map (silent on missing/corrupt -> empty).
    void LoadCharMap(const std::string& path, std::map<std::string, std::vector<std::string>>& out)
    {
        out.clear();
        std::ifstream f(path);
        if (!f) return;
        json j;
        try { f >> j; } catch (...) { return; }
        if (!j.is_object()) return;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (!it.value().is_array()) continue;
            std::vector<std::string> ids;
            for (const auto& id : it.value())
                if (id.is_string()) ids.push_back(id.get<std::string>());
            out[it.key()] = std::move(ids);
        }
    }
    void SaveCharMap(const std::string& path, const std::map<std::string, std::vector<std::string>>& m)
    {
        if (path.empty()) return;
        json j = json::object();
        for (const auto& kv : m) j[kv.first] = kv.second;
        Json::WriteAtomic(path, j.dump());
    }
    // "<dir>\progress.json" -> "<dir>\confirmed.json" (sibling file).
    std::string SiblingPath(const std::string& path, const char* name)
    {
        const size_t slash = path.find_last_of("\\/");
        return (slash == std::string::npos) ? std::string(name) : path.substr(0, slash + 1) + name;
    }
}

void ProgressStore::Load(const std::string& path)
{
    _path = path;
    _confirmedPath = SiblingPath(path, "confirmed.json");
    LoadCharMap(_path, _byChar);
    LoadCharMap(_confirmedPath, _confirmedByChar);
}

void ProgressStore::Save() const          { ++_ver; SaveCharMap(_path, _byChar); }
void ProgressStore::SaveConfirmed() const { ++_ver; SaveCharMap(_confirmedPath, _confirmedByChar); }

namespace
{
    // Union the `from` id list into `to` (dedup, preserving `to`'s order), then drop `from`. Returns true if a
    // `from` entry existed.
    bool MergeCharIds(std::map<std::string, std::vector<std::string>>& m, const std::string& from, const std::string& to)
    {
        auto itF = m.find(from);
        if (itF == m.end()) return false;
        std::vector<std::string>& dst = m[to];
        for (const std::string& id : itF->second)
            if (std::find(dst.begin(), dst.end(), id) == dst.end()) dst.push_back(id);
        m.erase(from);
        return true;
    }
}

void ProgressStore::RenameChar(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) return;
    const bool a = MergeCharIds(_byChar, from, to);
    const bool b = MergeCharIds(_confirmedByChar, from, to);
    if (a) Save();
    if (b) SaveConfirmed();
}

void ProgressStore::PurgeChar(const std::string& name)
{
    const bool a = _byChar.erase(name) != 0;
    const bool b = _confirmedByChar.erase(name) != 0;
    if (a) Save();
    if (b) SaveConfirmed();
}

void ProgressStore::CollectCharNames(std::set<std::string>& out) const
{
    for (const auto& kv : _byChar)          out.insert(kv.first);
    for (const auto& kv : _confirmedByChar) out.insert(kv.first);
}

bool ProgressStore::IsComplete(const std::string& character, const std::string& stepId) const
{
    auto it = _byChar.find(character);
    if (it == _byChar.end()) return false;
    return std::find(it->second.begin(), it->second.end(), stepId) != it->second.end();
}

void ProgressStore::MarkComplete(const std::string& character, const std::string& stepId)
{
    if (character.empty() || stepId.empty()) return;
    auto& list = _byChar[character];
    if (std::find(list.begin(), list.end(), stepId) == list.end())
    {
        list.push_back(stepId);
        Save();
    }
}

void ProgressStore::SetComplete(const std::string& character, const std::string& stepId, bool on)
{
    if (on) { MarkComplete(character, stepId); return; }
    auto it = _byChar.find(character);
    if (it == _byChar.end()) return;
    auto& list = it->second;
    auto pos = std::find(list.begin(), list.end(), stepId);
    if (pos != list.end()) { list.erase(pos); Save(); }
}

void ProgressStore::ResetMany(const std::string& character, const std::vector<std::string>& stepIds)
{
    auto it = _byChar.find(character);
    if (it == _byChar.end()) return;
    auto& list = it->second;
    const size_t before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(),
                   [&](const std::string& id) {
                       return std::find(stepIds.begin(), stepIds.end(), id) != stepIds.end();
                   }),
               list.end());
    if (list.size() != before) Save();
}

int ProgressStore::CompletedCount(const std::string& character, const std::vector<std::string>& stepIds) const
{
    auto it = _byChar.find(character);
    if (it == _byChar.end()) return 0;
    int n = 0;
    for (const auto& id : stepIds)
        if (std::find(it->second.begin(), it->second.end(), id) != it->second.end()) ++n;
    return n;
}

const std::vector<std::string>& ProgressStore::CompletedIds(const std::string& character) const
{
    static const std::vector<std::string> kEmpty;
    auto it = _byChar.find(character);
    return it != _byChar.end() ? it->second : kEmpty;
}

int ProgressStore::TotalCompleted() const
{
    int n = 0;
    for (const auto& kv : _byChar) n += (int)kv.second.size();   // live stored count across every character
    return n;
}

const std::vector<std::string>& ProgressStore::ConfirmedIds(const std::string& character) const
{
    static const std::vector<std::string> kEmpty;
    auto it = _confirmedByChar.find(character);
    return it != _confirmedByChar.end() ? it->second : kEmpty;
}

// -- confirmed (physically-reached waypoints; immune to reset/undo) ----------------------------------

bool ProgressStore::IsConfirmed(const std::string& character, const std::string& stepId) const
{
    auto it = _confirmedByChar.find(character);
    if (it == _confirmedByChar.end()) return false;
    return std::find(it->second.begin(), it->second.end(), stepId) != it->second.end();
}

void ProgressStore::SetConfirmed(const std::string& character, const std::string& stepId, bool on)
{
    if (character.empty() || stepId.empty()) return;
    auto& list = _confirmedByChar[character];
    const auto pos = std::find(list.begin(), list.end(), stepId);
    if (on)
    {
        if (pos == list.end()) { list.push_back(stepId); SaveConfirmed(); }
    }
    else if (pos != list.end()) { list.erase(pos); SaveConfirmed(); }
}

void ProgressStore::SeedConfirmedFromCompleted(const std::vector<std::string>& waypointStepIds)
{
    bool changed = false;
    for (auto& kv : _byChar)                      // for each character's completed list
    {
        auto& confirmed = _confirmedByChar[kv.first];
        for (const std::string& id : kv.second)   // completed step
        {
            if (std::find(waypointStepIds.begin(), waypointStepIds.end(), id) == waypointStepIds.end()) continue;
            if (std::find(confirmed.begin(), confirmed.end(), id) == confirmed.end())
            { confirmed.push_back(id); changed = true; }
        }
    }
    if (changed) SaveConfirmed();
}
