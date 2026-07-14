#pragma once
#include <imgui.h>   // ImVec2
#include <string>
#include <cstdint>
struct Zone;
struct Step;
class  App;

// A pin on a thumbnail: a continent coord + the objective's gw2dat type-icon id (0 -> a plain dot fallback).
struct MapMark { float cx = 0.f, cy = 0.f; uint32_t iconId = 0; };

// Live map-tile thumbnail compositing (no stb / no stitched atlas): composite the tile grid covering a
// continent rect at a fit-for-thumbnail zoom, straight from the disk-backed ImageCache. Shared by the Zones
// tab (zone + region hover previews), the Checklist tab (region/zone prefetch), and the Journal tab (episode
// location maps). Continent-1 imagery only (the tile server's max-zoom differs per continent). Defined in
// ui/tabs/MapThumbnail.cpp.
// `marks` (optional) draw the objective type icons over the crop; `highlightIdx` gets a larger icon + gold ring.
// `backing` fills the box with a dark tint behind the tiles (the letterbox); pass false to let the host (e.g. a
// tooltip) show through instead of a black box.
// `maxZoom` = the tile-server max zoom for this continent (7 for continent 1; 6 for continent 2 -- the Mists).
// `staticMap` (optional slug) -> draw the bundled static map image (maps.pack) instead of compositing live tiles,
// for instanced content whose floors have no public tiles (raids/strikes/fractals/dungeons/lounges/...).
bool DrawMapThumbnailRect(int continentId, float cl, float ct, float cR, float cb, ImVec2 origin, ImVec2 size,
                          const MapMark* marks = nullptr, int markCount = 0, int highlightIdx = -1, bool backing = true,
                          int maxZoom = 7, const char* staticMap = nullptr);
bool DrawMapThumbnail(const Zone* z, ImVec2 origin, ImVec2 size);
void MapHoverTooltip(const char* title, int lvlLo, int lvlHi, int contId, const float rect[4]);
void PrefetchRectTiles(int continentId, float cl, float ct, float cR, float cb);

// The ONE shared objective-hover affordance (viewer rows / checklist leaves / Atlas object rows / dashboard
// widgets). Title (type-colored) + DetailText; when `withMap` and the zone has a continent-1 rect, a zone
// thumbnail with the objective CIRCLED. `step` may be null (then no detail body). `footer` is an optional muted
// final hint/metadata line for compact callers that hide row details.
void ObjectiveTooltip(const char* name, const char* type, const Step* step,
                      const Zone* zone, float cx, float cy, bool withMap, const char* subtitle = nullptr,
                      const char* footer = nullptr, bool withTasks = false);   // withTasks: list a heart's task icons
// Hover tooltip for a GROUP (checklist region/zone node, Atlas zone row): title + level + optional (done/total)
// + when `withMap` a thumbnail of `rect` with ALL `objPts` (continent coords) pinned. total<=0 omits counts.
// `footer` is an optional muted final hint/metadata line for compact callers that hide row details.
void GroupTooltip(const char* title, int lvlLo, int lvlHi, int done, int total,
                  int contId, const float rect[4], const MapMark* objPts, int objCount, bool withMap,
                  const char* footer = nullptr, int maxZoom = 7, const char* staticMap = nullptr);
// Find a Step by its stable stepId ("{mapId}:{type}:{objId}") -- parses the mapId and searches that zone.
// `outZone` (optional) receives the owning zone (for the hover thumbnail).
const Step* FindStepById(App& app, const std::string& stepId, const Zone** outZone = nullptr);

// The crop rect (continent l,t,r,b) for a zone's hover map: its sector rect (tight playable bounds) when the
// data has one, else its continent rect. Frames the zone to its real area (continent_rect can overlap a
// neighbour map, e.g. Dry Top bleeding into the Silverwastes).
void ZoneMapCrop(const Zone* z, float out[4]);
