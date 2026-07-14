#pragma once

// RouteMode index helpers (Config::routeMode): Trail=0, HybridTrail=1, HybridNearest=2, DirectTrail=3,
// DirectNearest=4. The 5 values fold "selection" (authored order vs nearest) x "approach" (full ribbon /
// trail entrance / straight line). Shared by the guidance + the map/in-world trail-clip, so they live here
// rather than as entry.cpp file-statics.
inline bool ModeIsNearest(int routeMode)     { return routeMode == 2 || routeMode == 4; }
inline bool ModeIsDirect(int routeMode)       { return routeMode == 3 || routeMode == 4; }
inline bool ModeIsHybrid(int routeMode)       { return routeMode == 1 || routeMode == 2; }
inline bool ModeFollowsRibbon(int routeMode)  { return routeMode == 0; }
