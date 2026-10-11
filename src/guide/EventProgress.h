#pragma once
#include "app/CharacterMarks.h"

// Permanent manual event marks, independent of objective resets and sessions.
class EventProgressStore
{
public:
    void Load(const std::string& path) { marks_.Load(path); }
    static bool CanMark(const std::string& character) { return !character.empty() && character != "default"; }
    bool IsComplete(const std::string& character, const std::string& id) const { return marks_.Contains(character, id); }
    bool SetComplete(const std::string& character, const std::string& id, bool on)
    { return CanMark(character) && marks_.Set(character, id, on); }
    bool SetMany(const std::string& character, const std::vector<std::string>& ids, bool on)
    { return CanMark(character) && marks_.SetMany(character, ids, on); }
    const std::vector<std::string>& CompletedIds(const std::string& character) const { return marks_.Ids(character); }
    uint64_t Version() const { return marks_.Version(); }
    void RenameChar(const std::string& from, const std::string& to) { if (CanMark(to)) marks_.RenameChar(from, to); }
    void PurgeChar(const std::string& name) { marks_.PurgeChar(name); }
    void CollectCharNames(std::set<std::string>& out) const { marks_.CollectCharNames(out); }
private:
    CharacterMarks marks_;
};
