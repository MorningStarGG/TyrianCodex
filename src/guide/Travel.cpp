#include "guide/Travel.h"

#include <cfloat>
#include <cmath>
#include "util/Coords.h"
#include "guide/Progress.h"

namespace Travel
{
    void Controller::Init(const std::map<uint32_t, Zone>* zones, const ProgressStore* progress,
                          std::function<std::string()> currentChar, std::function<const Zone*()> activeZone,
                          std::function<void(const std::string&)> setClipboard,
                          std::function<void(const std::string&)> toast)
    {
        _zones = zones; _progress = progress;
        _char = std::move(currentChar); _activeZone = std::move(activeZone);
        _clip = std::move(setClipboard); _toast = std::move(toast);
    }

    // A waypoint's teleport chat link (its own, else its nearest-waypoint fallback).
    static std::string WpLink(const Step& s) { return !s.WaypointChatLink.empty() ? s.WaypointChatLink : s.ChatLink; }

    bool Controller::PlayerContinent(float playerWX, float playerWZ, float& cx, float& cy) const
    {
        const Zone* z = _activeZone ? _activeZone() : nullptr;
        if (!z || !z->HasRects) return false;
        Coords::WorldXZToContinent(playerWX, playerWZ, z->ContRect, z->MapRect, cx, cy);
        return true;
    }

    uint32_t Controller::MapIdForContinent(float cx, float cy) const
    {
        const Zone* az = _activeZone ? _activeZone() : nullptr;
        uint32_t best = az ? az->MapId : 0;
        float bestD = FLT_MAX;
        if (_zones)
            for (const auto& kv : *_zones)
            {
                const Zone& z = kv.second;
                if (!z.HasRects) continue;
                const float d = Coords::RectDistance(cx, cy, z.ContRect);
                if (d < bestD) { bestD = d; best = z.MapId; }
            }
        return best;
    }

    WpRef Controller::NearestWaypoint(float cx, float cy, const std::vector<uint32_t>& mapIds, bool knownOnly) const
    {
        WpRef best; float bestD = FLT_MAX;
        const std::string ch = _char ? _char() : std::string();
        if (!_zones) return best;
        for (uint32_t mid : mapIds)
        {
            auto it = _zones->find(mid);
            if (it == _zones->end()) continue;
            const Zone& z = it->second;
            for (const Step& s : z.Steps)
            {
                if (s.Type != "waypoint" || WpLink(s).empty()) continue;
                if (knownOnly && (!_progress || !_progress->IsConfirmed(ch, s.StepId))) continue;
                const float dx = s.CX - cx, dy = s.CY - cy, dd = dx * dx + dy * dy;
                if (dd < bestD)
                {
                    bestD = dd;
                    best = WpRef{ true, s.Name, WpLink(s), s.StepId, s.CX, s.CY, z.MapId };
                }
            }
        }
        return best;
    }

    // Teleporting to wp advances toward the goal if it's at least ~10% closer than the player is now.
    bool Controller::HopAdvances(const WpRef& wp, float gx, float gy, float playerCX, float playerCY) const
    {
        const float wgx = wp.cx - gx, wgy = wp.cy - gy;
        const float pgx = playerCX - gx, pgy = playerCY - gy;
        return std::sqrt(wgx * wgx + wgy * wgy) < std::sqrt(pgx * pgx + pgy * pgy) * 0.9f;
    }

    // A zone's entry point = the start of its route, in CONTINENT coords.
    //
    // This reads the zone's first STEP, not Trail.front(). The trail moved into the lazy <slug>.trail.json
    // side-file and is only loaded once you physically enter a zone (ZoneManager::EnsureActiveZone), so
    // keying off it returned an INVALID entry for every zone you had not visited -- which GetAnchor then
    // handed out as the continent origin, driving the map assist into the top-left corner and pointing
    // NearestWaypoint at the wrong side of the world. Steps stay in the zone METADATA and are already
    // sorted by route order, so the first one is resident for every zone and needs no coord conversion.
    const Entry& Controller::ZoneEntryCached(uint32_t mapId) const
    {
        auto cached = _entryCache.find(mapId);
        if (cached != _entryCache.end() && cached->second.valid) return cached->second;   // never trust a cached FAILURE:
                                                                                         // data can arrive after the miss
        Entry info;   // default invalid
        if (_zones)
        {
            auto it = _zones->find(mapId);
            if (it != _zones->end() && it->second.HasRects && !it->second.Steps.empty())
            {
                const Zone& z = it->second;
                const Step& s0 = z.Steps.front();   // route order (Dataset sorts Steps by Order on load)
                const float ex = s0.CX, ey = s0.CY; // already continent coords

                // Adjacent from-zone = the nearest OTHER same-continent loaded zone to the entry.
                uint32_t fromId = 0; float bestRect = FLT_MAX;
                for (const auto& kv : *_zones)
                {
                    const Zone& o = kv.second;
                    if (o.MapId == mapId || !o.HasRects || o.ContinentId != z.ContinentId) continue;
                    const float rd = Coords::RectDistance(ex, ey, o.ContRect);
                    if (rd < bestRect) { bestRect = rd; fromId = o.MapId; }
                }
                if (fromId != 0) { info.valid = true; info.ex = ex; info.ey = ey; info.fromMapId = fromId; }
            }
        }
        return _entryCache[mapId] = info;
    }

    bool Controller::GetAnchor(float& cx, float& cy, uint32_t& mapId) const
    {
        if (_personalActive) { cx = _ptx; cy = _pty; mapId = _ptMapId; return true; }
        // Only report an anchor we actually resolved. Returning true with a default-constructed Entry made
        // (0,0) -- the continent origin, i.e. the map's top-left corner -- look like a real destination to
        // every caller (map assist, NearestWaypoint, the travel offer).
        if (_destValid)
        {
            const Entry e = ZoneEntryCached(_destMapId);
            if (!e.valid) return false;
            cx = e.ex; cy = e.ey; mapId = _destMapId; return true;
        }
        return false;
    }

    bool Controller::HasPersonalAt(float& cx, float& cy) const
    {
        if (!_personalActive) return false;
        cx = _ptx; cy = _pty; return true;
    }

    std::string Controller::GoalZoneName() const
    {
        uint32_t mid = _personalActive ? _ptMapId : (_destValid ? _destMapId : 0);
        if (_zones && mid) { auto it = _zones->find(mid); if (it != _zones->end()) return it->second.Name; }
        return "another zone";
    }

    void Controller::RecomputeTargets(float playerCX, float playerCY)
    {
        _finalWp = WpRef{}; _knownWp = WpRef{}; _finalKnown = false;

        // Chat-link: the target IS a known waypoint -> it is the hop directly (no search), confirmed or not.
        if (_personalActive && _personalWp.valid)
        {
            _finalWp = _knownWp = _personalWp;
            _finalKnown = _progress && _char && _progress->IsConfirmed(_char(), _personalWp.stepId);
            return;
        }

        float ax, ay; uint32_t aMapId;
        const Zone* az = _activeZone ? _activeZone() : nullptr;
        // Resolve a teleport hop toward the goal. Zone-travel to the CURRENT zone is nothing to resolve (Recompute
        // clears it on entry); but a same-zone PERSONAL target still gets a closer-waypoint hop, exactly like steps.
        if (!GetAnchor(ax, ay, aMapId)) return;
        if (az && az->MapId == aMapId && !_personalActive) return;

        int continentId = 1;
        if (_zones) { auto it = _zones->find(aMapId); if (it != _zones->end()) continentId = it->second.ContinentId; else if (az) continentId = az->ContinentId; }
        std::vector<uint32_t> ids;
        if (_zones)
            for (const auto& kv : *_zones)
                if (kv.second.ContinentId == continentId) ids.push_back(kv.second.MapId);

        _finalWp = NearestWaypoint(ax, ay, ids, false);
        const std::string ch = _char ? _char() : std::string();
        _finalKnown = _finalWp.valid && _progress && _progress->IsConfirmed(ch, _finalWp.stepId);

        // Recommended (zone) travel: PREFER a confirmed waypoint INSIDE the destination zone (the best way IN --
        // any entrance, nearest the zone's route start); only if none is confirmed there fall back to the nearest
        // confirmed waypoint in any continent zone. Personal targets just take the nearest confirmed to the spot.
        WpRef known;
        if (_destValid && !_personalActive)
            known = NearestWaypoint(ax, ay, std::vector<uint32_t>{ _destMapId }, true);
        if (!known.valid)
            known = NearestWaypoint(ax, ay, ids, true);
        if (known.valid && HopAdvances(known, ax, ay, playerCX, playerCY)) _knownWp = known;
    }

    void Controller::MaybeCopyTeleport()
    {
        if (!_knownWp.valid || _knownWp.chatLink.empty() || _copiedWpId == _knownWp.stepId) return;
        _copiedWpId = _knownWp.stepId;
        if (_clip) _clip(_knownWp.chatLink);
        if (_toast) _toast("Copied " + _knownWp.name + " - paste to teleport.");
    }

    void Controller::Recompute(float playerWX, float playerWZ)
    {
        if (!Active()) return;
        // Recommended (zone) travel ARRIVES when you enter the destination zone (teleported in OR walked in):
        // clear it so normal in-zone guidance resumes, instead of routing you to the zone's mid-map route-start.
        // Keys on the TRUE destination, so a zone you only pass through en route does not clear it. Personal
        // (Alt+click) targets PERSIST -- they're a specific spot you still want to reach inside the zone.
        if (_destValid && !_personalActive)
        {
            const Zone* az = _activeZone ? _activeZone() : nullptr;
            if (az && az->MapId == _destMapId) { Clear(); return; }
        }
        float pcx = 0.f, pcy = 0.f;
        const bool havePlayer = PlayerContinent(playerWX, playerWZ, pcx, pcy);
        RecomputeTargets(havePlayer ? pcx : 0.f, havePlayer ? pcy : 0.f);
        MaybeCopyTeleport();
    }

    void Controller::SetPersonalTarget(float cx, float cy, std::string label, std::string info)
    {
        _personalActive = true; _ptx = cx; _pty = cy; _ptMapId = MapIdForContinent(cx, cy);
        _personalLabel = std::move(label); _personalInfo = std::move(info);
        _personalWp = WpRef{};          // a bare spot (Alt+click) -> resolve the hop normally, no stored waypoint
        _destValid = false;             // a personal spot supersedes a Zones-tab destination
        _copiedWpId.clear();
        if (_toast) _toast(_personalLabel.empty() ? std::string("Personal target set") : ("Guiding to " + _personalLabel));
    }

    void Controller::SetPersonalWaypoint(float cx, float cy, const WpRef& wp, std::string label, std::string info)
    {
        SetPersonalTarget(cx, cy, std::move(label), std::move(info));   // sets the spot + clears _personalWp + toasts
        _personalWp = wp; _personalWp.valid = true;                     // ...then record the linked waypoint as the hop
    }

    void Controller::StartTravel(uint32_t destMapId)
    {
        if (!_zones || _zones->find(destMapId) == _zones->end()) return;
        _personalActive = false;        // a Zones-tab trip supersedes an Alt+click spot
        _personalLabel.clear(); _personalInfo.clear(); _personalWp = WpRef{};
        _destValid = true; _destMapId = destMapId; _copiedWpId.clear();
        if (_toast) _toast("Traveling to " + GoalZoneName() + " - follow the arrow.");
    }

    void Controller::Clear()
    {
        _personalActive = false; _destValid = false;
        _personalLabel.clear(); _personalInfo.clear(); _personalWp = WpRef{};
        _finalWp = WpRef{}; _knownWp = WpRef{}; _finalKnown = false; _copiedWpId.clear();
    }
}
