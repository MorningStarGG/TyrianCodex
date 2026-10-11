#include "app/CharacterMarks.h"
#include "util/Json.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

void CharacterMarks::Load(const std::string& path)
{
    path_ = path;
    byCharacter_.clear();
    ++version_;
    std::ifstream file(path);
    if (!file) return;
    nlohmann::json json;
    try { file >> json; } catch (...) { return; }
    if (!json.is_object()) return;
    for (auto entry = json.begin(); entry != json.end(); ++entry)
    {
        if (entry.key().empty() || !entry.value().is_array()) continue;
        std::set<std::string> seen;
        std::vector<std::string> ids;
        for (const auto& value : entry.value())
            if (value.is_string())
            {
                auto id = value.get<std::string>();
                if (!id.empty() && seen.insert(id).second) ids.push_back(std::move(id));
            }
        byCharacter_[entry.key()] = std::move(ids);
    }
}

void CharacterMarks::Save()
{
    ++version_;
    if (!path_.empty()) Json::WriteAtomic(path_, nlohmann::json(byCharacter_).dump());
}

const std::vector<std::string>& CharacterMarks::Ids(const std::string& character) const
{
    static const std::vector<std::string> empty;
    const auto found = byCharacter_.find(character);
    return found == byCharacter_.end() ? empty : found->second;
}

bool CharacterMarks::Contains(const std::string& character, const std::string& id) const
{
    const auto& ids = Ids(character);
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool CharacterMarks::Set(const std::string& character, const std::string& id, bool marked)
{
    return SetMany(character, {id}, marked);
}

bool CharacterMarks::SetMany(const std::string& character, const std::vector<std::string>& ids, bool marked)
{
    std::set<std::string> requested(ids.begin(), ids.end());
    requested.erase("");
    if (character.empty() || requested.empty()) return false;
    auto found = byCharacter_.find(character);
    if (!marked && found == byCharacter_.end()) return false;
    auto& stored = byCharacter_[character];
    const auto before = stored.size();
    if (marked)
    {
        std::set<std::string> existing(stored.begin(), stored.end());
        for (const auto& id : ids)
            if (!id.empty() && existing.insert(id).second) stored.push_back(id);
    }
    else
        stored.erase(std::remove_if(stored.begin(), stored.end(), [&](const auto& id) {
            return requested.count(id) != 0;
        }), stored.end());
    if (before == stored.size()) return false;
    Save();
    return true;
}

int CharacterMarks::Count(const std::string& character, const std::vector<std::string>& ids) const
{
    const auto& stored = Ids(character);
    const std::set<std::string> marks(stored.begin(), stored.end()), unique(ids.begin(), ids.end());
    int count = 0;
    for (const auto& id : unique) if (marks.count(id)) ++count;
    return count;
}

int CharacterMarks::Total() const
{
    int count = 0;
    for (const auto& entry : byCharacter_) count += (int)entry.second.size();
    return count;
}

void CharacterMarks::RenameChar(const std::string& from, const std::string& to)
{
    if (from.empty() || to.empty() || from == to) return;
    auto source = byCharacter_.find(from);
    if (source == byCharacter_.end()) return;
    auto& target = byCharacter_[to];
    std::set<std::string> existing(target.begin(), target.end());
    for (const auto& id : source->second) if (existing.insert(id).second) target.push_back(id);
    byCharacter_.erase(source);
    Save();
}

void CharacterMarks::PurgeChar(const std::string& character)
{
    if (byCharacter_.erase(character)) Save();
}

void CharacterMarks::CollectCharNames(std::set<std::string>& out) const
{
    for (const auto& entry : byCharacter_) out.insert(entry.first);
}
