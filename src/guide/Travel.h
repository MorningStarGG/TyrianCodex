#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "model/Dataset.h"

class ProgressStore;

// Smart-travel state machine + waypoint resolver Two goal kinds: an Alt+click "personal target"
// (a continent spot) or a Zones-tab "Travel here" destination zone. When the goal is in ANOTHER zone it
// resolves two waypoints toward it -- the FINAL (closest to the goal overall, may be locked) and the KNOWN
// (nearest one you've CONFIRMED/unlocked that meaningfully advances), auto-copying the known hop's chat
// link once per hop so you can paste to teleport. All in continent coords (what MumbleLink MapCenter uses).
namespace Travel
{
    // A resolved waypoint (a hop / a pin). cx,cy = continent coords.
    struct WpRef
    {
        bool valid = false;
        std::string name, chatLink, stepId;
        float cx = 0.f, cy = 0.f;
        uint32_t mapId = 0;
    };
    // A zone's walk-in entry (continent) + the adjacent zone you'd come from (for the cross-zone arrow aim).
    struct Entry
    {
        bool valid = false;
        float ex = 0.f, ey = 0.f;
        uint32_t fromMapId = 0;
    };

    class Controller
    {
    public:
        // Injected once at addon load (raw pointers to the long-lived globals + small getters/sinks).
        void Init(const std::map<uint32_t, Zone> *zones, const ProgressStore *progress,
                  std::function<std::string()> currentChar, std::function<const Zone *()> activeZone,
                  std::function<void(const std::string &)> setClipboard,
                  std::function<void(const std::string &)> toast);

        bool Active() const { return _personalActive || _destValid; }
        bool IsPersonal() const { return _personalActive; }

        void SetPersonalTarget(float cx, float cy, std::string label = {}, std::string info = {}); // Alt+click spot / clicked event (continent); optional name + info for the panel
        // A personal target that IS a known waypoint (a clicked chat-link): the resolved waypoint becomes the hop
        // directly (Known/Final), so the normal card + pan + click path is used instead of the bare-point pan.
        void SetPersonalWaypoint(float cx, float cy, const WpRef &wp, std::string label = {}, std::string info = {});
        void StartTravel(uint32_t destMapId); // Zones-tab "Travel here"
        void Clear();                         // personal first, then destination

        // Call each frame while Active() (player world XZ -> player-continent for the hop test).
        void Recompute(float playerWX, float playerWZ);

        // --- queries for the guidance / panel / pins ---
        bool GetAnchor(float &cx, float &cy, uint32_t &mapId) const; // the goal point + its zone
        bool HasPersonalAt(float &cx, float &cy) const;              // for pin-hit-to-clear
        uint32_t DestinationMapId() const { return _destValid ? _destMapId : 0u; }
        const WpRef &Final() const { return _finalWp; }
        const WpRef &Known() const { return _knownWp; }
        bool FinalKnown() const { return _finalKnown; }
        Entry GetZoneEntry(uint32_t mapId) const { return ZoneEntryCached(mapId); }
        // General "closest waypoint to a continent point" query (HUD data-text / pins). Public wrapper over the
        // internal resolver so callers reuse the one canonical scan instead of re-implementing it.
        WpRef ClosestWaypoint(float cx, float cy, const std::vector<uint32_t> &mapIds, bool knownOnly) const { return NearestWaypoint(cx, cy, mapIds, knownOnly); }
        std::string GoalZoneName() const;
        // Panel title / hint for a personal target -- the clicked event's name + info when given, else generic.
        std::string PersonalTitle() const { return _personalLabel.empty() ? std::string("Personal target") : _personalLabel; }
        std::string PersonalHint() const { return _personalInfo.empty() ? std::string("Heading to your marked spot. Alt+click the map to move it.") : _personalInfo; }

    private:
        WpRef NearestWaypoint(float cx, float cy, const std::vector<uint32_t> &mapIds, bool knownOnly) const;
        bool HopAdvances(const WpRef &wp, float gx, float gy, float playerCX, float playerCY) const;
        uint32_t MapIdForContinent(float cx, float cy) const;
        const Entry &ZoneEntryCached(uint32_t mapId) const;
        void RecomputeTargets(float playerCX, float playerCY);
        void MaybeCopyTeleport();
        bool PlayerContinent(float playerWX, float playerWZ, float &cx, float &cy) const;

        // injected context
        const std::map<uint32_t, Zone> *_zones = nullptr;
        const ProgressStore *_progress = nullptr;
        std::function<std::string()> _char;
        std::function<const Zone *()> _activeZone;
        std::function<void(const std::string &)> _clip;
        std::function<void(const std::string &)> _toast;

        // state
        bool _personalActive = false;
        float _ptx = 0.f, _pty = 0.f;
        uint32_t _ptMapId = 0;
        std::string _personalLabel, _personalInfo; // optional name + info shown in the travel-target panel
        WpRef _personalWp;                         // chat-link: the linked waypoint, used directly as the hop (Known/Final)
        bool _destValid = false;
        uint32_t _destMapId = 0;
        WpRef _finalWp, _knownWp;
        bool _finalKnown = false;
        std::string _copiedWpId;
        mutable std::map<uint32_t, Entry> _entryCache;
    };
}
