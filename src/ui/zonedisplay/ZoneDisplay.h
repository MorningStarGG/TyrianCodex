#pragma once
#include <cstdint>
#include <string>
class App;

// -----------------------------------------------------------------------------------------------------
// ZoneDisplay: the bold, animated Region / Zone / Sector banner shown on entering a map and on crossing into
// a named sector (a fresh, inspired take of "Regions of Tyria" -- the "Heralded" reveal: a region
// eyebrow, a procedural gold filigree rule, the zone name's letters cascading in with a gold light-sweep, a
// sector subtitle, then a fade/drift out). Names come from the bundled, offline SectorData (no API at runtime).
//
// An App member (app.zoneDisplay). Update() detects map/sector changes + drives the timeline; Draw() renders
// on the foreground draw list each frame. Both take App& (config + fonts + api). All animation is driven off
// ImGui::GetTime(); the premium glint uses the real D3D11 Fx::ZoneSheen shader with a draw-list fallback.
// -----------------------------------------------------------------------------------------------------
class ZoneDisplay
{
public:
    void Update(App &app);           // per-frame: detect map/sector change + age out the banner (cheap; safe every frame)
    void Draw(App &app);             // per-frame: draw the active banner on the foreground draw list
    void Reannounce(App &app);       // force-show the full map banner (settings Preview button + keybind)
    void ReannounceSector(App &app); // force-show the sector announcement (settings "Preview sector" button)

private:
    enum class Kind
    {
        MapEntry,
        Sector
    };
    void Announce(App &app, Kind kind, const std::string &region, const std::string &zone, const std::string &sector);

    bool active_ = false;
    double t0_ = 0.0; // ImGui time of the current announcement
    Kind kind_ = Kind::MapEntry;
    bool sectorBig_ = false; // area announce drawn as the big banner (zdAreaStyle)
    std::string region_, zone_, sector_;

    uint32_t lastMapId_ = 0;
    std::string lastSector_;
    double nextSectorCheck_ = 0.0; // ~10 Hz throttle for the point-in-polygon sector test
};
