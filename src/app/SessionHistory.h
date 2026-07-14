#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// One completed play session's earnings: per-currency wallet deltas over the session, plus when it began and how
// long it ran. Built by app/SessionTracker (Finalize/Save -> sessionHistory.Record) when a session is finalized
// (logout / character switch / manual Save), then appended to the per-character log below.
struct SessionRecord
{
    long long startEpoch = 0;             // wall-clock (system_clock seconds) the session began -- for dates
    int       durationSec = 0;            // session length in seconds
    std::map<int, long long> deltas;      // currency id -> net change over the session (gold = id 1, in copper)
    int       levelsGained = 0;           // character levels gained over the session (full-session records only)
    int       objectivesGained = 0;       // checklist objectives completed over the session (full-session records only)
};

// Per-character persistent log of past sessions (the Session Tracker's history). Mirrors ProgressStore: a
// per-character map saved as JSON in the addon ROOT (session-history.json -- survives a cache clear, like
// progress.json), with a Version() change-token so the UI recomputes its derived view only when the log changes.
// Saves are best-effort on every mutation.
class SessionHistoryStore
{
public:
    void Load(const std::string& path);   // reads session-history.json; remembers the path

    void Record(const std::string& character, const SessionRecord& rec);              // append + save
    const std::vector<SessionRecord>& Sessions(const std::string& character) const;   // chronological; empty if none
    const std::map<std::string, std::vector<SessionRecord>>& All() const { return _byChar; }   // every character (account-wide rollups)
    void Erase(const std::string& character, int index);                              // delete one record + save
    void Clear(const std::string& character);                                         // wipe a character's log + save

    // -- per-character maintenance (rename detector + Diagnostics cleanup) --
    void RenameChar(const std::string& from, const std::string& to);   // append from's log into to (chronological) + save
    void PurgeChar(const std::string& name);                           // wipe a character's log + save
    void CollectCharNames(std::set<std::string>& out) const;           // every character with a session log

    // Monotonic change counter (bumped on every save). Lets the UI cache its derived display + recompute only when
    // the history actually changed (the F5 change-token rule).
    uint64_t Version() const { return _ver; }

private:
    void Save() const;
    std::string _path;
    std::map<std::string, std::vector<SessionRecord>> _byChar;   // character -> chronological session records
    mutable uint64_t _ver = 0;
};
