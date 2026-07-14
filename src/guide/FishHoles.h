#pragma once
#include <imgui.h>
#include <set>
#include <string>
#include <vector>

class App;

// Shared fishing-hole taxonomy (mirrors guide/FarmCategories.h for farming): the pretty name + marker tint for a
// hole kind, the alias from a fish's wiki "hole" string to the pack's hole kinds, and the set
// of kinds the player's watchlist (State.fishWatch) lights up. One header so the overlay (world/FishMarkers), the
// nav (guide/FishingGuidance), and the Fishing tab all agree.
namespace FishHoles
{
    // "lake" -> "Lake", "fracfresh" -> "Fractured Freshwater", etc.
    const char *Pretty(const std::string &type);

    // A water-family tint for the in-world / map marker (alpha 0-255).
    ImU32 Color(const std::string &type, int alpha);

    // The pack hole kinds a fish's wiki "hole" string maps to (e.g. "Lake Fish" -> {"lake"}).
    // An EMPTY result means "any hole" -- the fish is open-water / unknown, so every hole qualifies.
    std::vector<std::string> TypesForHole(const std::string &fishHole);

    // The hole kinds the watchlist (App::state.fishWatch) highlights + navigates to. An EMPTY set means "any hole"
    // (the watchlist is empty, or a watched fish is an any/open-water fish).
    std::set<std::string> WatchedTypes(const App &app);
}
