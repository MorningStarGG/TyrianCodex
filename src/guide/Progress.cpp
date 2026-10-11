#include "guide/Progress.h"
#include <algorithm>

void ProgressStore::Load(const std::string& path)
{
    completed_.Load(path);
    const auto slash = path.find_last_of("\\/");
    confirmed_.Load((slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "confirmed.json");
}

bool ProgressStore::IsComplete(const std::string& character, const std::string& id) const { return completed_.Contains(character, id); }
void ProgressStore::MarkComplete(const std::string& character, const std::string& id) { completed_.Set(character, id, true); }
void ProgressStore::SetComplete(const std::string& character, const std::string& id, bool on) { completed_.Set(character, id, on); }
void ProgressStore::SetMany(const std::string& character, const std::vector<std::string>& ids, bool on) { completed_.SetMany(character, ids, on); }
void ProgressStore::ResetMany(const std::string& character, const std::vector<std::string>& ids) { completed_.SetMany(character, ids, false); }
int ProgressStore::CompletedCount(const std::string& character, const std::vector<std::string>& ids) const { return completed_.Count(character, ids); }
const std::vector<std::string>& ProgressStore::CompletedIds(const std::string& character) const { return completed_.Ids(character); }
int ProgressStore::TotalCompleted() const { return completed_.Total(); }
bool ProgressStore::IsConfirmed(const std::string& character, const std::string& id) const { return confirmed_.Contains(character, id); }
void ProgressStore::SetConfirmed(const std::string& character, const std::string& id, bool on) { confirmed_.Set(character, id, on); }
void ProgressStore::SetConfirmedMany(const std::string& character, const std::vector<std::string>& ids, bool on) { confirmed_.SetMany(character, ids, on); }
const std::vector<std::string>& ProgressStore::ConfirmedIds(const std::string& character) const { return confirmed_.Ids(character); }

void ProgressStore::RenameChar(const std::string& from, const std::string& to)
{
    completed_.RenameChar(from, to);
    confirmed_.RenameChar(from, to);
}
void ProgressStore::PurgeChar(const std::string& name)
{
    completed_.PurgeChar(name);
    confirmed_.PurgeChar(name);
}
void ProgressStore::CollectCharNames(std::set<std::string>& out) const
{
    completed_.CollectCharNames(out);
    confirmed_.CollectCharNames(out);
}
void ProgressStore::SeedConfirmedFromCompleted(const std::vector<std::string>& waypointIds)
{
    std::set<std::string> characters;
    completed_.CollectCharNames(characters);
    for (const auto& character : characters)
    {
        std::vector<std::string> reached;
        for (const auto& id : completed_.Ids(character))
            if (std::find(waypointIds.begin(), waypointIds.end(), id) != waypointIds.end()) reached.push_back(id);
        confirmed_.SetMany(character, reached, true);
    }
}
