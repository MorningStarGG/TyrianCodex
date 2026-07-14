#pragma once
#include <set>
#include <string>
#include <utility>
#include <vector>

// Account-wide LIFETIME total of character levels, seeded from the live API roster and persisted to the addon
// root (account-levels.json). Per character (keyed by name) it remembers the levels contributed and only ever
// ADDS -- a level-up adds the delta, a new (or recreated) character adds its current level -- so a deleted or
// recreated character never reduces the account total. Backs the Sessions tab's Levels "Account total" tile.
namespace AccountLevels
{
    void Init(const std::string& path);   // load account-levels.json (call once at AddonLoad)
    void Shutdown();                       // clear the ledger (addon disable / re-enable)

    // Reconcile against the live roster (character name -> current level). Adds positive level-ups + new
    // characters; a name reappearing at a LOWER level (recreated) adds the new character's levels and keeps the
    // old contribution; gone characters are kept. Persists only when something changed.
    void Reconcile(const std::vector<std::pair<std::string, int>>& roster);

    long long Total();   // account-wide lifetime sum of contributed levels

    // -- per-character maintenance (rename detector + Diagnostics cleanup) --
    // RenameChar TRANSFERS (moves) the ledger entry from->to. It MOVES rather than sums so it stays correct after
    // Reconcile may have already seeded `to` as a fresh new character (the async created-id rename fires later) --
    // the move discards that erroneous seed; a reverse rename stays correct too.
    void RenameChar(const std::string& fromName, const std::string& toName);
    void PurgeChar(const std::string& name);                    // remove a character's lifetime contribution (destructive)
    void CollectCharNames(std::set<std::string>& out);          // every character in the ledger
}
