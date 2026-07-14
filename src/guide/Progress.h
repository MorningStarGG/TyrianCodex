#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

// Per-character progress store. TWO independent per-character sets, keyed by the stable stepId
// ("{mapId}:{type}:{objectiveId}") so they survive rebuilds:
//   * COMPLETED (progress.json): checklist completion (proximity/manual). The source of truth - the GW2
//     API doesn't expose visited POIs/hearts. Reset/undo mutate THIS set.
//
//   * CONFIRMED (confirmed.json): waypoints you've PHYSICALLY reached, so they're unlocked for travel.
//     Kept SEPARATE from completion and IMMUNE to reset/undo - reaching a waypoint unlocks it permanently
//     in GW2, so un-checking the checklist must never "forget" it. The travel system's known-hop query.
//
// Saves are best-effort on every mutation.
class ProgressStore
{
public:
    void Load(const std::string &path); // reads progress.json (+ confirmed.json beside it); remembers paths

    // -- completed (checklist) --
    bool IsComplete(const std::string &character, const std::string &stepId) const;
    void MarkComplete(const std::string &character, const std::string &stepId);
    void SetComplete(const std::string &character, const std::string &stepId, bool on);
    void ResetMany(const std::string &character, const std::vector<std::string> &stepIds);
    int CompletedCount(const std::string &character, const std::vector<std::string> &stepIds) const;
    // The character's completed-id list (empty if none) - for building a fast lookup set when counting many
    // groups per frame (the Checklist tree), instead of repeated linear IsComplete() scans.
    const std::vector<std::string> &CompletedIds(const std::string &character) const;

    // Account-wide total of completed objectives across EVERY character (the live stored count, so a zone reset /
    // un-check lowers it). Backs the Sessions tab's Objectives "Account total".
    int TotalCompleted() const;

    // A monotonically-increasing change counter (bumped on every real mutation of either set). Lets a UI cache
    // derived state (e.g. the Checklist tree's counts) and recompute only when progress actually changed.
    uint64_t Version() const { return _ver; }

    // -- confirmed (physically-reached waypoints; travel) --
    bool IsConfirmed(const std::string &character, const std::string &stepId) const;
    void SetConfirmed(const std::string &character, const std::string &stepId, bool on);
    // The character's confirmed-waypoint id list (empty if none) - lets the checklist treat reached/locked
    // waypoints as "done" (faded) on load from saved data, without an IsConfirmed scan per row.
    const std::vector<std::string> &ConfirmedIds(const std::string &character) const;
    // Idempotent migration: for every character, any COMPLETED step that is a waypoint also becomes
    // confirmed (bridges progress made before confirmation tracking existed). Call once after zones load.
    void SeedConfirmedFromCompleted(const std::vector<std::string> &waypointStepIds);

    // -- per-character maintenance (rename detector + Diagnostics cleanup) --
    void RenameChar(const std::string &from, const std::string &to); // union from's sets into to + save both
    void PurgeChar(const std::string &name);                         // erase from both sets + save both
    void CollectCharNames(std::set<std::string> &out) const;         // every character with stored progress

private:
    void Save() const;
    void SaveConfirmed() const;

    std::string _path;
    std::string _confirmedPath;
    std::map<std::string, std::vector<std::string>> _byChar;          // character -> completed stepIds
    std::map<std::string, std::vector<std::string>> _confirmedByChar; // character -> confirmed waypoint stepIds
    mutable uint64_t _ver = 0;                                        // bumped by Save()/SaveConfirmed() (the real-mutation paths) - see Version()
};
