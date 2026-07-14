#include "app/SessionHistory.h"
#include "api/core/Json.h"   // Api::Json null-safe readers
#include "util/Json.h"       // Json::WriteAtomic (atomic, corruption-safe saves)

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

// Disk format (addon-root session-history.json): { "<character>": [ { "s": startEpoch, "d": durationSec,
// "c": { "<currencyId>": delta, ... } }, ... ], ... }.  Silent on missing/corrupt (-> empty), like ProgressStore.

void SessionHistoryStore::Load(const std::string& path)
{
    _path = path;
    _byChar.clear();
    std::ifstream f(path);
    if (!f) return;
    json j;
    try { f >> j; } catch (...) { return; }
    if (!j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        if (!it.value().is_array()) continue;
        std::vector<SessionRecord> recs;
        for (const auto& e : it.value())
        {
            if (!e.is_object()) continue;
            SessionRecord r;
            r.startEpoch       = Api::Json::Int64(e, "s", 0);
            r.durationSec      = Api::Json::Int(e, "d", 0);
            r.levelsGained     = Api::Json::Int(e, "lg", 0);
            r.objectivesGained = Api::Json::Int(e, "og", 0);
            if (e.contains("c") && e["c"].is_object())
                for (auto c = e["c"].begin(); c != e["c"].end(); ++c)
                    if (c.value().is_number_integer())
                        try { r.deltas[std::stoi(c.key())] = c.value().get<long long>(); } catch (...) {}
            recs.push_back(std::move(r));
        }
        _byChar[it.key()] = std::move(recs);
    }
}

void SessionHistoryStore::Save() const
{
    ++_ver;
    if (_path.empty()) return;
    json j = json::object();
    for (const auto& kv : _byChar)
    {
        json arr = json::array();
        for (const SessionRecord& r : kv.second)
        {
            json c = json::object();
            for (const auto& d : r.deltas) c[std::to_string(d.first)] = d.second;
            arr.push_back({ {"s", r.startEpoch}, {"d", r.durationSec}, {"c", c},
                            {"lg", r.levelsGained}, {"og", r.objectivesGained} });
        }
        j[kv.first] = arr;
    }
    Json::WriteAtomic(_path, j.dump());
}

void SessionHistoryStore::Record(const std::string& character, const SessionRecord& rec)
{
    if (character.empty()) return;
    _byChar[character].push_back(rec);
    Save();
}

const std::vector<SessionRecord>& SessionHistoryStore::Sessions(const std::string& character) const
{
    static const std::vector<SessionRecord> kEmpty;
    auto it = _byChar.find(character);
    return it != _byChar.end() ? it->second : kEmpty;
}

void SessionHistoryStore::Erase(const std::string& character, int index)
{
    auto it = _byChar.find(character);
    if (it == _byChar.end()) return;
    if (index < 0 || index >= (int)it->second.size()) return;
    it->second.erase(it->second.begin() + index);
    Save();
}

void SessionHistoryStore::Clear(const std::string& character)
{
    auto it = _byChar.find(character);
    if (it == _byChar.end() || it->second.empty()) return;
    it->second.clear();
    Save();
}

void SessionHistoryStore::RenameChar(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) return;
    auto itF = _byChar.find(from);
    if (itF == _byChar.end()) return;
    std::vector<SessionRecord>& dst = _byChar[to];
    dst.insert(dst.end(), itF->second.begin(), itF->second.end());
    std::stable_sort(dst.begin(), dst.end(),
                     [](const SessionRecord& a, const SessionRecord& b) { return a.startEpoch < b.startEpoch; });
    _byChar.erase(itF);
    Save();
}

void SessionHistoryStore::PurgeChar(const std::string& name)
{
    if (_byChar.erase(name)) Save();
}

void SessionHistoryStore::CollectCharNames(std::set<std::string>& out) const
{
    for (const auto& kv : _byChar) out.insert(kv.first);
}
