#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// User-owned sets of stable IDs. Catalog refreshes and display filters never
// discard history. Both objectives and events use this persistence foundation.
class CharacterMarks
{
public:
    void Load(const std::string& path);
    bool Contains(const std::string& character, const std::string& id) const;
    bool Set(const std::string& character, const std::string& id, bool marked);
    bool SetMany(const std::string& character, const std::vector<std::string>& ids, bool marked);
    const std::vector<std::string>& Ids(const std::string& character) const;
    int Count(const std::string& character, const std::vector<std::string>& ids) const;
    int Total() const;
    uint64_t Version() const { return version_; }
    void RenameChar(const std::string& from, const std::string& to);
    void PurgeChar(const std::string& character);
    void CollectCharNames(std::set<std::string>& out) const;
private:
    void Save();
    std::string path_;
    std::map<std::string, std::vector<std::string>> byCharacter_;
    uint64_t version_ = 0;
};
