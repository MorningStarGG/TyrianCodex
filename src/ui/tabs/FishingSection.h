#pragma once
class App;

// The Fishing tab: a fishing-collection browser over the bundled data/fish.json (App::fish / FishData),
// gated on the account+progression API scopes. Grouped by collection (caught/total), grid or list, with
// Uncaught / Time / Bait / Collection filters, a "catchable now" toggle synced to the Tyrian day/night cycle,
// per-fish tooltips (bait/time/hole/region/power + caught + catchable countdown), right-click copy, and a click
// to travel to a map the fish can be caught in. Dispatched from DrawCollectionsContent (the Collections tab's
// Fishing scope; formerly its own Settings tab).
void DrawFishingContent(App &app);
