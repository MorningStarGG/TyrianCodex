#pragma once
#include <cstdint>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------------------------------
// EventFavorites: which timed events the player cares about. Two INDEPENDENT flags per event, because they
// mean different things -- `fav` pins it to the top of the Timers board and the Favorites filter, `notify`
// asks for the staged reminders before it starts. Pinning Teqatl without wanting a toast is a normal thing
// to want, and so is the reverse.
//
// ACCOUNT-WIDE, unlike WaypointFavorites (whose per-character keying exists because waypoint unlocks are
// per-character). Teqatl is Teqatl on every alt, so there is deliberately no character parameter here and
// the CharData rename/purge hooks do not apply.
//
// Keyed by EventTimer::Key, which is stable and comes from the bundled data. Persisted to
// event-favorites.json in the addon ROOT -- it is user data, not cache, so a cache clear must not wipe it.
// -----------------------------------------------------------------------------------------------------
namespace EventFavorites
{
    void Load();   // once at startup (entry.cpp), before any UI reads it

    bool IsFavorite(const std::string& eventKey);
    bool IsNotify(const std::string& eventKey);
    void SetFavorite(const std::string& eventKey, bool on);
    void SetNotify(const std::string& eventKey, bool on);
    void ToggleFavorite(const std::string& eventKey);
    void ToggleNotify(const std::string& eventKey);

    // Every event with reminders on. The ticker walks THIS, not the whole event list.
    const std::vector<std::string>& NotifyKeys();

    // Bumped on every change -> the change token immediate-mode surfaces cache their sorted rows behind,
    // instead of re-sorting the board every frame.
    uint64_t Version();
}
