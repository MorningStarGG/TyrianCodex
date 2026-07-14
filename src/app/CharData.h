#pragma once
#include <set>
#include <string>
#include <vector>
class App;

// -----------------------------------------------------------------------------------------------------
// CharData: the ONE place that operates on a character's data ACROSS every per-character store. Used by the
// rename detector (CharRegistry) to merge a renamed character's data into its new name, and by the Diagnostics
// "Character data" cleanup to purge a character or enumerate every character the addon has data for.
// -----------------------------------------------------------------------------------------------------
namespace CharData
{
    // A detected rename: merge fromName's data into toName across every store. Config/profiles REPLACE (the
    // renamed character IS the same character -- its settings win), accumulated data (progress/sessions/story/
    // favorites) UNIONS, the level ledger TRANSFERS (move, no double-count). Persists each store + settings.json.
    void OnCharRename(App& app, const std::string& fromName, const std::string& toName);

    // Delete a character's per-character data from every store. The account-level lifetime contribution is KEPT
    // unless alsoLevels is set (a deleted character's levels normally stay in the account total). Persists.
    void PurgeCharacter(App& app, const std::string& name, bool alsoLevels);

    // Every character name the addon currently has stored data for (union across all stores), sorted, with the
    // reserved "default"/"__template__" buckets removed.
    std::vector<std::string> AllCharacterNames(App& app);
}
