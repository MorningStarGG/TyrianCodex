#pragma once
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// WaypointFavorites: the player's starred waypoints, ACCOUNT-WIDE and in a user-arranged order.
//
// The vector IS the manual order -- there is no separate order field to keep in sync. Sort modes in the
// widget are a VIEW over this list and never rewrite it, so switching to A-Z and back returns exactly the
// arrangement the player made.
//
// Account-wide (it used to be per-character): a waypoint is the same place on every alt, and organising the
// list eight times is not a feature. What remains per-character is whether YOU have reached a given waypoint
// -- callers test that with ProgressStore::IsConfirmed and dim the row, so an account-wide favorite can
// legitimately show as not-yet-unlocked on the character you are playing.
//
// Persisted to waypoint-favorites.json in the addon ROOT. An older per-character file is folded into one
// list on load (see Load) and rewritten in the new shape.
// -----------------------------------------------------------------------------------------------------
namespace WaypointFavorites
{
    struct Favorite
    {
        std::string stepId;
        std::string name;
        std::string zoneName;
        std::string chatLink;
        uint32_t    mapId = 0;
        int         continentId = 1;
        float       cx = 0.f;
        float       cy = 0.f;
    };

    const std::vector<Favorite>& List();
    bool Contains(const std::string& stepId);
    bool Add(const Favorite& favorite);      // appends; no-op if already starred
    bool Remove(const std::string& stepId);
    bool Toggle(const Favorite& favorite);

    // Manual re-ordering. Moves the entry at `from` so it sits at `to`, shifting the rest -- the move the
    // widget's drag and the settings arrows both apply. Out-of-range or no-op moves return false.
    bool Move(int from, int to);

    // Bumped on every mutation -> the change token immediate-mode surfaces cache derived (sorted/grouped)
    // views behind, instead of rebuilding them every frame.
    uint64_t Version();
}
