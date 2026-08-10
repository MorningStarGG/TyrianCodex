#include "InfoData.h"
#include "ui/InfoHudShared.h"            // shared clock/time helpers (HudShared::)
#include "app/App.h"
#include "app/AccountData.h"
#include "Shared.h"                       // MumbleLink + MumbleIdent + IsInCombat + CurrentMount + CurrentMapId
#include "api/mumblelink/Mumble.h"        // MountName / ProfessionName / RaceName / MountType
#include "app/StaticData.h"               // elite-spec id->name + the currency catalog
#include "app/SectorData.h"               // region / map / sector names (Location data text)
#include "app/SessionTracker.h"           // live session deltas (the Session-earnings data text)
#include "ui/SessionFormat.h"             // SessionFmt:: currency label / signed delta formatting
#include "ui/tabs/SessionTab.h"           // OpenSessionTab (Session data text click)
#include "guide/Travel.h"                 // Travel::Controller::ClosestWaypoint (closest waypoint)
#include "guide/ZoneCompletion.h"         // Guide::ZoneCompletion (shared zone done/total; also the dashboard's)
#include "guide/Completion.h"             // CompleteStep (Current/Upcoming "Mark complete")
#include "util/Coords.h"                  // world<->continent (continent coords + waypoint distance)
#include "util/Draw.h"                    // FormatDistance / FormatEta (shared route formatters)
#include "util/Textures.h"                // Tex::GetTextureFromAssetId (upcoming-objective type icon)
#include "model/ObjectiveTypes.h"         // Objective::TryIconAssetId
#include "render/route/RouteArt.h"        // Render::DrawRouteArrow (the dashboard's route arrow, reused inline)
#include "render/LockIcon.h"              // Render::DrawLock (shared travel padlock for the locked-waypoint hint)
#include "ui/viewer/ViewerObjectiveList.h" // UpcomingObjectiveSteps (shared with the dashboard Next-objective widget)
#include "ui/viewer/ViewerModel.h"         // DisplayStepIndex (the SAME current-step the viewer/dashboard use)
#include "ui/tabs/MapThumbnail.h"          // ObjectiveTooltip (the shared rich objective hover + zone map)
#include "ui/GuideViewer.h"               // TravelToLocation / CopyWaypointForStep / SetClipboard / ViewerAlert / CurrentTargetOverride
#include "ui/QuickMenu.h"                 // shared nav-arrow menu (route-arrow data text)
#include "ui/viewer/ViewerLayout.h"       // FarmingRunActive / FarmingTabAvailable (the Mode data text)
#include "ui/SettingsWindow.h"            // OpenSettingsTab + SettingsTabId (tab-opener click actions)
#include "ui/tabs/ItemsTab.h"             // OpenItemsTab (inventory clicks -> My Inventory / Equipment scope)
#include "ui/tabs/TradingPostTab.h"       // OpenCraftingCart (crafting-cart data text)
#include "app/CraftCart.h"                // crafting cart count / items
#include "app/CraftCartSummary.h"         // cart still-need rollup
#include "app/ItemCatalog.h"              // cart item names (tooltip)
#include "util/Trading.h"                 // coin formatting
#include "ui/tabs/InventorySection.h"         // SetEquipmentChar (Alt Equipment data text)
#include "ui/items/ItemRender.h"          // ItemUI::DrawResultRow (Alt Equipment tooltip item rows)
#include "ui/dashboard/widgets/Widgets.h" // DashW::NearestWaypoints / FavoriteWaypoints (popup lists, reused)
#include "ui/WaypointFavorites.h"         // WaypointFavorites::List (favorites count)
#include "ui/profiles/Loadouts.h"         // Loadout switcher (the Loadout data-text + right-click submenu)
#include "ui/profiles/ProfileBar.h"       // Profiles::IProfileHost (per-family profile lists)
#include "ui/Gw2Ui.h"                     // rich hover tooltips
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>
#include <string>

// The launcher-button registry moved to ui/hud/HudButtons.* and the clock/time helpers to ui/InfoHudShared.*
// (HudShared::). This file is now ONLY the Info Panel's data-text catalog.

// ---------------------------------------------------------------------------------------------------------
// Info Panel data-text catalog. Each compute() reads existing live state (App / MumbleLink / AccountData) and
// returns a label+value (empty value -> the segment is hidden that frame). No new fetching here -- the API
// values come from the shared AccountData hub, gated by each text's `domains` mask (InfoPanel::NeededDomains).
// ---------------------------------------------------------------------------------------------------------
namespace
{
    using InfoData::HudSeg;
    using InfoData::HudOpts;

    std::string FmtGold(long long copper)
    {
        const long long g = copper / 10000, s = (copper / 100) % 100, c = copper % 100;
        char b[48];
        if (g > 0)      std::snprintf(b, sizeof(b), "%lldg %llds", g, s);
        else if (s > 0) std::snprintf(b, sizeof(b), "%llds %lldc", s, c);
        else            std::snprintf(b, sizeof(b), "%lldc", c);
        return b;
    }
    long DailyResetSecs()
    {
        std::time_t now = std::time(nullptr); std::tm g{}; gmtime_s(&g, &now);
        return 86400L - (g.tm_hour * 3600L + g.tm_min * 60 + g.tm_sec);
    }
    long WeeklyResetSecs()
    {
        std::time_t now = std::time(nullptr); std::tm g{}; gmtime_s(&g, &now);
        const long nowSec = g.tm_hour * 3600L + g.tm_min * 60 + g.tm_sec;
        const int daysToMon = ((1 - g.tm_wday) + 7) % 7;
        long toWeekly = (long)daysToMon * 86400L + (7 * 3600L + 30 * 60L) - nowSec;   // Monday 07:30 UTC
        if (toWeekly <= 0) toWeekly += 7 * 86400L;
        return toWeekly;
    }

    const ImU32 kDim = IM_COL32(140, 140, 150, 255);   // "no data" placeholder colour (cool grey, distinct from the warm gold labels)

    // ---- local (no API). Each takes the per-text options accessor `o`. These NEVER return empty -- a state
    // with no data shows a meaningful placeholder instead of vanishing. ----
    HudSeg SegZone(App& app, const HudOpts& o)   { HudSeg s;
        // Any combination of Region / Zone / Sector, shown in that geographic order. Defaults to Zone only
        // (the old behaviour). Sector + region fall back to the all-maps SectorData bundle, so it works on
        // cities / WvW / instances too, not just guided leveling zones.
        const bool wantReg  = o.Get("region", 0) != 0;
        const bool wantZone = o.Get("zone",   1) != 0;
        const bool wantSec  = o.Get("sector", 0) != 0;
        s.label = (wantReg || wantSec) ? "Location" : "Zone";
        const auto& z = app.state.zone;
        const uint32_t mid = CurrentMapId();
        if (mid) SectorData::EnsureMap(mid);
        std::string zone = (z.Loaded && !z.Name.empty()) ? z.Name : std::string();
        if (zone.empty() && mid)   { std::string m = StaticData::MapNameOr(mid, ""); if (!m.empty()) zone = m; }
        std::string region = z.Loaded ? z.RegionName : std::string();
        if (region.empty() && mid) { const char* r = SectorData::RegionName(mid); if (r && *r) region = r; }
        std::string sector;
        if (wantSec && mid && MumbleLink)
            sector = SectorData::SectorAt(mid, MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z);
        std::string out;
        auto add = [&](const std::string& p) { if (!p.empty()) { if (!out.empty()) out += "  >  "; out += p; } };
        if (wantReg)  add(region);
        if (wantZone) add(zone);
        if (wantSec)  add(sector);
        if (!out.empty()) { s.value = out; return s; }
        s.value = !zone.empty() ? zone : "Unknown";   // never vanish: fall back to the map name
        if (zone.empty()) s.color = kDim;
        return s; }
    HudSeg SegMapLevel(App& app, const HudOpts&) { HudSeg s; s.label = "Map"; const auto& z = app.state.zone;
        if (z.Loaded && z.MaxLevel > 0) { char b[24]; if (z.MinLevel == z.MaxLevel) std::snprintf(b, sizeof(b), "%d", z.MaxLevel); else std::snprintf(b, sizeof(b), "%d-%d", z.MinLevel, z.MaxLevel); s.value = b; }
        else { s.value = z.Loaded ? "Unavailable" : "Unknown"; s.color = kDim; } return s; }
    HudSeg SegCoords(App& app, const HudOpts& o) { HudSeg s; s.label = "Pos";
        if (!MumbleLink) { s.value = "Unknown"; s.color = kDim; return s; }
        const int fmt = o.Get("format", 0); char b[64];
        if (fmt == 2)   // continent coords (the 2D-map coordinates players quote)
        {
            const auto& z = app.state.zone;
            if (z.HasRects) { float cx, cy; Coords::WorldXZToContinent(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z, z.ContRect, z.MapRect, cx, cy); std::snprintf(b, sizeof(b), "%.0f, %.0f", cx, cy); s.value = b; }
            else { s.value = "Unavailable"; s.color = kDim; }
            return s;
        }
        if (fmt == 1) std::snprintf(b, sizeof(b), "%.0f, %.0f, %.0f", MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Y, MumbleLink->AvatarPosition.Z);
        else          std::snprintf(b, sizeof(b), "%.0f, %.0f", MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z);
        s.value = b; return s; }
    HudSeg SegMount(App&, const HudOpts&)        { HudSeg s; s.label = "Mount";
        if (MumbleLink && CurrentMount() != Mumble::MountType::None) s.value = Mumble::MountName(MumbleLink->Context.MountIndex);
        else { s.value = "Unmounted"; s.color = kDim; } return s; }
    HudSeg SegCombat(App&, const HudOpts&)       { HudSeg s;
        if (IsInCombat()) { s.value = "In Combat"; s.color = IM_COL32(228, 110, 74, 255); }
        else { s.value = "Out of Combat"; s.color = kDim; } return s; }
    HudSeg SegLevel(App& app, const HudOpts&)    { HudSeg s; s.label = "Lv";
        if (app.state.charLevel > 0) s.value = std::to_string(app.state.charLevel);
        else { s.value = AccountData::HasKey() ? "Loading" : "Needs API Key"; s.color = kDim; } return s; }
    HudSeg SegSessTime(App& app, const HudOpts&) { HudSeg s; s.label = "Session";   // the one session tracker (SessionTracker)
        s.value = HudShared::DurShort(SessionTracker::SessionDurationSec(app)); return s; }
    HudSeg SegSessObj(App& app, const HudOpts&)  { HudSeg s; s.label = "Objs";
        s.value = std::to_string(SessionTracker::ObjectivesGained(app)); return s; }
    HudSeg SegSessLvl(App& app, const HudOpts&)  { HudSeg s; s.label = "Lvls";
        if (!AccountData::HasKey()) { s.value = "Needs API Key"; s.color = kDim; return s; }
        s.value = std::to_string(SessionTracker::LevelsGained(app)); return s; }
    HudSeg SegFps(App&, const HudOpts&)          { HudSeg s; s.label = "FPS"; const float f = ImGui::GetIO().Framerate; char b[16]; std::snprintf(b, sizeof(b), "%.0f", f);
        s.value = b; s.color = f < 30.f ? IM_COL32(228, 110, 74, 255) : (f < 55.f ? IM_COL32(232, 202, 96, 255) : IM_COL32(150, 210, 140, 255)); return s; }
    HudSeg SegClkLocal(App&, const HudOpts& o)   { HudSeg s; s.label = "Local";  s.value = HudShared::ClockByType(0, o.Get("h24", 1) != 0, o.Get("ampm", 1) != 0); return s; }
    HudSeg SegClkServer(App&, const HudOpts& o)  { HudSeg s; s.label = "Server"; s.value = HudShared::ClockByType(1, o.Get("h24", 1) != 0, o.Get("ampm", 1) != 0); return s; }
    HudSeg SegClkTyrian(App&, const HudOpts& o)  { HudSeg s; s.label = "Tyria";  s.value = HudShared::ClockByType(2, o.Get("h24", 1) != 0, o.Get("ampm", 1) != 0); return s; }
    HudSeg SegDaily(App&, const HudOpts&)        { HudSeg s; s.label = "Daily";  s.value = HudShared::DurShort(DailyResetSecs()); return s; }
    HudSeg SegWeekly(App&, const HudOpts&)       { HudSeg s; s.label = "Weekly"; s.value = HudShared::DurShort(WeeklyResetSecs()); return s; }

    // ---- API (via the shared AccountData hub). No key -> "Needs API Key"; key but not yet fetched -> "...". ----
    bool ApiPlaceholder(HudSeg& s, bool have)   // returns true (with a placeholder filled) when not ready
    {
        if (!AccountData::HasKey()) { s.value = "Needs API Key"; s.color = kDim; return true; }
        if (!have)                  { s.value = "Loading"; s.color = kDim; return true; }
        return false;
    }
    HudSeg SegCurrency(App&, const HudOpts& o)
    {
        // The chosen currency is an index into the live /v2/currencies catalog (any currency); until it warms,
        // fall back to the original curated handful so the option index still resolves.
        const std::vector<StaticData::CurrencyRef>& cat = StaticData::Currencies();
        int id; const char* nm;
        if (!cat.empty())
        {
            const int sel = std::min(std::max(o.Get("currency", 0), 0), (int)cat.size() - 1);
            id = cat[sel].id; nm = SessionFmt::Label(id);
        }
        else
        {
            static const int ids[5] = { 1, 2, 3, 23, 63 };
            static const char* names[5] = { "Gold", "Karma", "Laurels", "Spirit Shards", "Astral Acclaim" };
            const int sel = std::min(std::max(o.Get("currency", 0), 0), 4);
            id = ids[sel]; nm = names[sel];
        }
        HudSeg s; s.label = nm;
        const auto& m = AccountData::Get();
        if (ApiPlaceholder(s, m.haveWallet)) return s;
        long long v = 0; for (const auto& w : m.wallet) if (w.id == id) { v = w.value; break; }
        s.value = (id == 1) ? FmtGold(v) : std::to_string(v);
        return s;
    }
    HudSeg SegWvDaily(App&, const HudOpts&)  { HudSeg s; s.label = "WV Daily";  const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveWvDaily)) return s;
        if (m.wvUnavailable) { s.value = "n/a"; s.color = kDim; return s; }   // account hasn't unlocked the Wizard's Vault
        int done = 0; for (const auto& o : m.wvDaily) if (o.max > 0 && o.cur >= o.max) ++done; s.value = std::to_string(done) + "/" + std::to_string((int)m.wvDaily.size()); return s; }
    HudSeg SegWvWeekly(App&, const HudOpts&) { HudSeg s; s.label = "WV Weekly"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveWvWeekly)) return s;
        if (m.wvUnavailable) { s.value = "n/a"; s.color = kDim; return s; }
        int done = 0; for (const auto& o : m.wvWeekly) if (o.max > 0 && o.cur >= o.max) ++done; s.value = std::to_string(done) + "/" + std::to_string((int)m.wvWeekly.size()); return s; }

    // ---- Group A: read existing AccountData domains ----
    HudSeg SegTp(App&, const HudOpts&)       { HudSeg s; s.label = "TP"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveTp)) return s; s.value = FmtGold(m.tpCoins); return s; }
    HudSeg SegBank(App&, const HudOpts&)     { HudSeg s; s.label = "Bank Space"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveBank)) return s; s.value = std::to_string(m.bankUsed) + "/" + std::to_string(m.bankTotal); return s; }
    HudSeg SegMats(App&, const HudOpts&)     { HudSeg s; s.label = "Mats"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveMats)) return s; s.value = std::to_string(m.matTypes); return s; }
    HudSeg SegShared(App&, const HudOpts&)   { HudSeg s; s.label = "Shared"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveShared)) return s; s.value = std::to_string(m.sharedUsed) + "/" + std::to_string(m.sharedTotal); return s; }
    HudSeg SegAchv(App&, const HudOpts& o)   // independent toggles: show Points / Completed / In progress (any 1-3)
    {
        HudSeg s; const auto& m = AccountData::Get();
        bool wantPts = o.Get("points", 1) != 0;
        const bool wantDone = o.Get("completed", 0) != 0;
        const bool wantProg = o.Get("inprogress", 0) != 0;
        if (!wantPts && !wantDone && !wantProg) wantPts = true;   // all off -> fall back to points (never an empty segment)
        const bool have = (!wantPts || m.haveTotalAp) && ((!wantDone && !wantProg) || m.haveAch);
        if (ApiPlaceholder(s, have)) return s;
        // Each enabled metric is self-tagged so the bar shows which are on (e.g. "707 AP  55 done  144 prog").
        std::string v;
        auto add = [&](const std::string& part) { if (!v.empty()) v += "  "; v += part; };
        if (wantPts)  { std::string n = std::to_string(m.totalAp); for (int i = (int)n.size() - 3; i > 0; i -= 3) n.insert(i, ","); add(n + " AP"); }
        if (wantDone) add(std::to_string(m.achDone) + " done");
        if (wantProg) add(std::to_string(m.achInProgress) + " prog");
        s.value = v;
        return s;
    }
    HudSeg SegMasteriesU(App&, const HudOpts&) { HudSeg s; s.label = "Masteries"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveMastery)) return s; s.value = std::to_string(m.masteriesUnlocked); return s; }
    HudSeg SegPlaytime(App&, const HudOpts&) { HudSeg s; s.label = "Played"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveAccount)) return s; s.value = HudShared::DurShort(m.age); return s; }
    HudSeg SegDeaths(App& app, const HudOpts&) { HudSeg s; s.label = "Deaths"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveChars)) return s;
        int d = 0; for (const auto& c : m.characters) if (c.name == app.state.currentChar) { d = c.deaths; break; } s.value = std::to_string(d); return s; }

    // ---- Group B: local / guide (no key, except Spec which uses StaticData's anonymous fetch) ----
    HudSeg SegCurObj(App& app, const HudOpts& o)  { HudSeg s; s.label = "Current"; const auto& g = app.state.guidanceSnapshot;
        if (!g.active)  { s.value = "--"; s.color = kDim; return s; }   // no current target (off-route only applies WITH a route)
        if (o.Get("showicon", 1)) s.icon = g.iconTex;   // the objective/material TYPE icon, drawn before the name ("Show type icon" option)
        // Always keep the objective NAME visible: a heart you've "arrived" at still needs its tasks done, so don't
        // drop the name for a bare "Arrived". Green + "(here)" when arrived; name + distance otherwise.
        const std::string name = g.caption.empty() ? std::string("Objective") : g.caption;
        if (g.arrived)  { s.value = name + "  (here)"; s.color = IM_COL32(150, 210, 140, 255); return s; }
        char b[220];   // name + (distance[, ETA]); ETA only when moving (speed known)
        if (g.speed > 0.1f) std::snprintf(b, sizeof(b), "%s  (%.0fm, %s)", name.c_str(), g.distance, FormatEta(g.distance, g.speed).c_str());
        else                std::snprintf(b, sizeof(b), "%s  (%.0fm)", name.c_str(), g.distance);
        s.value = b; return s; }
    HudSeg SegRoute(App& app, const HudOpts&)    { HudSeg s; s.label = "Route"; const auto& g = app.state.guidanceSnapshot;
        if (g.arrived)       { s.value = "Arrived";   s.color = IM_COL32(150, 210, 140, 255); }
        else if (!g.active)  { s.value = "No route";  s.color = kDim; }
        else if (g.offRoute) { s.value = "Off route"; s.color = IM_COL32(232, 202, 96, 255); }
        else                 { s.value = "On route"; }
        return s; }
    HudSeg SegZonePct(App& app, const HudOpts&)  { HudSeg s; s.label = "Zone Completion"; const auto& z = app.state.zone;
        if (!z.Loaded || z.Steps.empty()) { s.value = z.Loaded ? "0/0" : "Unknown"; s.color = kDim; return s; }
        const Guide::ZoneCounts c = Guide::ZoneCompletion(app.progress, app.state.currentChar, z);
        char b[32]; std::snprintf(b, sizeof(b), "%d/%d (%d%%)", c.done, c.total, c.total ? c.done * 100 / c.total : 0); s.value = b; return s; }
    HudSeg SegCharName(App& app, const HudOpts&) { HudSeg s; s.label = "Char";
        if (!app.state.currentChar.empty() && app.state.currentChar != "default") s.value = app.state.currentChar;
        else { s.value = "Unknown"; s.color = kDim; } return s; }
    HudSeg SegProfession(App&, const HudOpts&)   { HudSeg s; s.label = "Prof";
        const char* n = MumbleIdent ? Mumble::ProfessionName(MumbleIdent->Profession) : "Unknown";
        s.value = n; if (std::string(n) == "Unknown") s.color = kDim; return s; }
    HudSeg SegRace(App&, const HudOpts&)          { HudSeg s; s.label = "Race";
        const char* n = MumbleIdent ? Mumble::RaceName(MumbleIdent->Race) : "Unknown";
        s.value = n; if (std::string(n) == "Unknown") s.color = kDim; return s; }
    HudSeg SegSpec(App&, const HudOpts&)          { HudSeg s; s.label = "Spec";
        if (!MumbleIdent || MumbleIdent->Spec == 0) { s.value = "None"; s.color = kDim; return s; }
        const char* n = StaticData::SpecName((int)MumbleIdent->Spec);
        if (n && *n) s.value = n; else { s.value = "Loading"; s.color = kDim; } return s; }
    HudSeg SegFacing(App&, const HudOpts&)        { HudSeg s; s.label = "Facing";
        if (!MumbleLink) { s.value = "Unknown"; s.color = kDim; return s; }
        const float fx = MumbleLink->AvatarFront.X, fz = MumbleLink->AvatarFront.Z;
        if (fx == 0.f && fz == 0.f) { s.value = "Unknown"; s.color = kDim; return s; }
        float deg = std::atan2(fx, fz) * 57.29578f; if (deg < 0.f) deg += 360.f;   // north = +Z, east = +X (per Coords)
        static const char* dirs[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        s.value = dirs[(int)((deg + 22.5f) / 45.f) % 8]; return s; }
    // Resolve the closest waypoint to the player in the active zone (shared by the text, its tooltip, and its
    // click actions). outMeters = straight-line world distance (or -1 when unknown).
    bool ResolveClosestWp(App& app, Travel::WpRef& out, float& outMeters)
    {
        outMeters = -1.f;
        const auto& z = app.state.zone;
        if (!MumbleLink || !z.HasRects) return false;
        float cx, cy; Coords::WorldXZToContinent(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z, z.ContRect, z.MapRect, cx, cy);
        out = app.travel.ClosestWaypoint(cx, cy, { z.MapId }, false);
        if (!out.valid) return false;
        float wx, wz; Coords::ContinentToWorldXZ(out.cx, out.cy, z.ContRect, z.MapRect, wx, wz);
        const float dx = wx - MumbleLink->AvatarPosition.X, dz = wz - MumbleLink->AvatarPosition.Z;
        outMeters = std::sqrt(dx * dx + dz * dz);
        return true;
    }
    HudSeg SegClosestWp(App& app, const HudOpts&) { HudSeg s; s.label = "Nearest"; const auto& z = app.state.zone;
        if (!MumbleLink || !z.HasRects) { s.value = "Unknown"; s.color = kDim; return s; }
        Travel::WpRef wp; float m;
        if (!ResolveClosestWp(app, wp, m)) { s.value = "None"; s.color = kDim; return s; }
        s.locked = !wp.stepId.empty() && !app.progress.IsConfirmed(app.state.currentChar, wp.stepId);   // not unlocked -> padlock
        char b[128]; std::snprintf(b, sizeof(b), "%s  %.0fm", wp.name.c_str(), m); s.value = b; return s; }
    void ClickClosestWp(App& app, const HudOpts&)   // left-click: travel to the waypoint (central distance-gated card)
    {
        Travel::WpRef wp; float m;
        if (!ResolveClosestWp(app, wp, m)) { ViewerAlert("No waypoint nearby."); return; }
        TravelToLocation(app, "waypoint", wp.mapId, wp.cx, wp.cy, wp.name, wp.chatLink);
    }
    void ActionsClosestWp(App&, const HudOpts&, std::vector<const char*>& out) { out.push_back("Copy waypoint link"); }
    void OnActionClosestWp(App& app, const HudOpts&, int idx)
    {
        if (idx != 0) return;
        Travel::WpRef wp; float m;
        if (!ResolveClosestWp(app, wp, m) || wp.chatLink.empty()) { ViewerAlert("No waypoint link to copy."); return; }
        SetClipboard(wp.chatLink); ViewerAlert("Copied waypoint link - paste/click to teleport.");
    }

    // ---- Guidance texts: the dashboard route arrow + its distance / ETA, inline (reuse the same data + art) ----
    void PaintRouteArrow(App& app, const HudOpts& opts, ImDrawList* dl, ImVec2 center, float size)
    {
        Render::DirectIndicatorStyle st;
        st.chevron   = opts.Get("chevron", 1) != 0;
        st.ping      = opts.Get("ping", 0) != 0;
        st.everySecs = opts.Get("every", 5);
        st.showSecs  = opts.Get("show", 2);
        Render::DrawRouteArrow(dl, center, size, app.state.guidanceSnapshot, opts.Get("directglow", 0) != 0, st);
    }
    HudSeg SegRouteArrow(App& app, const HudOpts&) { HudSeg s; s.label = "";
        const auto& g = app.state.guidanceSnapshot;
        if (!g.active) { s.value = "--"; s.color = kDim; return s; }   // no route -> a dim dash (text), no arrow
        s.value = " "; s.paint = &PaintRouteArrow; return s; }          // value " " keeps it visible; paint draws the arrow
    HudSeg SegDistance(App& app, const HudOpts&)  { HudSeg s; s.label = "Distance";
        const auto& g = app.state.guidanceSnapshot;
        if (!g.active)  { s.value = "--"; s.color = kDim; return s; }
        if (g.arrived)  { s.value = "0m"; return s; }
        char b[24]; FormatDistance(b, sizeof(b), g.distance); s.value = b; return s; }
    HudSeg SegEta(App& app, const HudOpts&)       { HudSeg s; s.label = "ETA";
        const auto& g = app.state.guidanceSnapshot;
        if (!g.active)  { s.value = "--"; s.color = kDim; return s; }
        if (g.arrived)  { s.value = "Arrived"; s.color = IM_COL32(150, 210, 140, 255); return s; }
        s.value = FormatEta(g.distance, g.speed); return s; }
    HudSeg SegUpcoming(App& app, const HudOpts& o)  { HudSeg s; s.label = "Upcoming";
        const std::vector<int> up = UpcomingObjectiveSteps(app, 1);   // the immediate next-after-current (shared with the dashboard)
        if (up.empty()) { s.value = app.state.guidanceSnapshot.active ? "End of route" : "--"; s.color = kDim; return s; }
        const Step& st = app.state.zone.Steps[up[0]];
        uint32_t aid = 0; if (o.Get("showicon", 1) && Objective::TryIconAssetId(st.Type, aid)) s.icon = Tex::GetTextureFromAssetId(aid);   // upcoming type icon ("Show type icon" option)
        s.value = st.Name.empty() ? "(unnamed)" : st.Name; return s; }

    // ---- simple left-click actions (copy / open a tab) ----
    void ClickCopyCoords(App& app, const HudOpts& o)
    {
        const HudSeg s = SegCoords(app, o);
        if (s.color == kDim) { ViewerAlert("No coordinates."); return; }
        SetClipboard(s.value); ViewerAlert("Copied coordinates.");
    }
    void ClickOpenInventory(App& app, const HudOpts&) { OpenItemsTab(app, 1); }   // owned (My Inventory)
    void ClickOpenChecklist(App& app, const HudOpts&) { OpenSettingsTab(app, SettingsTabChecklist); }
    void ClickOpenAtlas(App& app, const HudOpts&)     { OpenSettingsTab(app, SettingsTabAtlas); }
    void ClickOpenCart(App& app, const HudOpts&)      { OpenCraftingCart(app); }

    // ---- Crafting Cart data text (Economy): project + item count; tooltip lists the cart + still-need ----
    HudSeg SegCraftCart(App&, const HudOpts&)
    {
        HudSeg s; s.label = "Cart";
        const int n = CraftCart::Count(CraftCart::Active());
        if (n <= 0) { s.value = "empty"; s.color = kDim; return s; }
        s.value = CraftCart::Active() + " (" + std::to_string(n) + ")";
        return s;
    }
    void TipCraftCart(App& app, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        const std::string proj = CraftCart::Active();
        Gw2Ui::TooltipTitle(("Crafting Cart - " + proj).c_str());
        Gw2Ui::TooltipSeparator();
        const std::vector<std::pair<int, long long>> items = CraftCart::Items(proj);
        if (items.empty())
            Gw2Ui::TooltipText("Cart is empty. Add items in Trading Post -> Crafting.");
        else
        {
            int shown = 0;
            for (const auto& it : items)
            {
                const std::string line = std::to_string(it.second) + "x " + ItemCatalog::ById(it.first).name;
                Gw2Ui::TooltipText(line.c_str());
                if (++shown >= 14) { Gw2Ui::TooltipMuted("..."); break; }
            }
            const CraftCartSummary::Summary& cs = CraftCartSummary::Get(app);
            Gw2Ui::TooltipSeparator();
            Gw2Ui::TooltipText(("Estimated total cost: " + Trading::Coins(cs.stillNeed)).c_str());
            if (!cs.pricesComplete) Gw2Ui::TooltipMuted("(prices still loading)");
        }
        Gw2Ui::TooltipMuted("Click to open the cart.");
        Gw2Ui::TooltipEnd();
    }

    // ---- Group D: account fetches (typed endpoints / new domains) ----
    HudSeg SegBags(App& app, const HudOpts&)  { HudSeg s; s.label = "Bags";
        if (!AccountData::HasKey()) { s.value = "Needs API Key"; s.color = kDim; return s; }
        const std::string& cur = app.state.currentChar;
        const auto& m = AccountData::Get();
        auto it = m.inv.find(cur);
        if (it == m.inv.end() || !it->second.have) { AccountData::EnsureCharDetail(cur); s.value = "Loading"; s.color = kDim; return s; }
        char b[32]; std::snprintf(b, sizeof(b), "%d/%d", it->second.usedSlots, it->second.totalSlots); s.value = b; return s; }
    HudSeg SegRaids(App&, const HudOpts&)      { HudSeg s; s.label = "Raids"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveRaids)) return s; s.value = std::to_string(m.raidsCleared); return s; }
    HudSeg SegLuck(App&, const HudOpts&)       { HudSeg s; s.label = "Luck"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveLuck)) return s; s.value = std::to_string(m.luck); return s; }
    HudSeg SegWorldBosses(App&, const HudOpts&)  { HudSeg s; s.label = "Bosses"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveDailyDone)) return s; s.value = std::to_string(m.worldBosses); return s; }
    HudSeg SegMapChests(App&, const HudOpts&)    { HudSeg s; s.label = "Chests"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveDailyDone)) return s; s.value = std::to_string(m.mapChests); return s; }
    HudSeg SegDailyCrafting(App&, const HudOpts&){ HudSeg s; s.label = "Crafting"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveDailyDone)) return s; s.value = std::to_string(m.dailyCrafting); return s; }
    HudSeg SegMastery(App&, const HudOpts&) { HudSeg s; s.label = "Mastery"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveMastery)) return s; s.value = std::to_string(m.mpEarned - m.mpSpent); return s; }
    HudSeg SegFractal(App&, const HudOpts&) { HudSeg s; s.label = "Fractal"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveAccount)) return s; s.value = std::to_string(m.fractalLevel); return s; }   // 0 if none
    HudSeg SegWvw(App&, const HudOpts&)     { HudSeg s; s.label = "WvW"; const auto& m = AccountData::Get(); if (ApiPlaceholder(s, m.haveAccount)) return s; s.value = std::to_string(m.wvwRank); return s; }      // 0 if none

    // ---- WvW live match texts (anonymous data: plain "Loading" guard, NOT ApiPlaceholder which demands a key) ----
    inline ImU32 WvwColor(int c) { return c == 1 ? IM_COL32(228, 92, 92, 255) : c == 2 ? IM_COL32(92, 138, 228, 255) : c == 3 ? IM_COL32(96, 196, 112, 255) : IM_COL32(236, 230, 212, 255); }
    std::string WvwAbbrev(long long n)
    {
        char b[24];
        if (n >= 1000000)     std::snprintf(b, sizeof(b), "%.2fM", n / 1000000.0);
        else if (n >= 10000)  std::snprintf(b, sizeof(b), "%.1fk", n / 1000.0);
        else                  std::snprintf(b, sizeof(b), "%lld", n);
        return b;
    }
    const AccountData::WvwMapState* WvwCurrentMap(const AccountData::Model& m)
    {
        const uint32_t mid = CurrentMapId();
        if (!mid) return nullptr;
        for (const auto& ms : m.wvwMaps) if ((uint32_t)ms.mapId == mid) return &ms;
        return nullptr;
    }
    bool WvwGuard(HudSeg& s, const AccountData::Model& m) { if (m.haveWvwMatch) return false; s.value = AccountData::WvwLoadStage(); s.color = kDim; return true; }
    int WvwYou(const AccountData::Model& m) { return (m.wvwMyColor >= 1 && m.wvwMyColor <= 3) ? m.wvwMyColor : 1; }   // 1..3 -> wvwSides[you-1] stays in range

    HudSeg SegWvwScore(App&, const HudOpts& o) { HudSeg s; s.label = "WvW Score";
        const auto& m = AccountData::Get(); if (WvwGuard(s, m)) return s;
        int show = WvwYou(m);
        if (o.Get("highlight", 0) == 0)   // default = leader; 1 = your team
        { long long best = -1; for (int c = 0; c < 3; ++c) if (m.wvwSides[c].score > best) { best = m.wvwSides[c].score; show = c + 1; } }
        s.value = WvwAbbrev(m.wvwSides[show - 1].score); s.color = WvwColor(show); return s; }

    HudSeg SegWvwSkirmish(App&, const HudOpts&) { HudSeg s; s.label = "WvW Skirmish";
        const auto& m = AccountData::Get(); if (WvwGuard(s, m)) return s;
        const int you = WvwYou(m);
        char b[48]; std::snprintf(b, sizeof(b), "#%d  %d", m.wvwSkirmishIndex, m.wvwSides[you - 1].skirmishScore);
        s.value = b; s.color = WvwColor(you); return s; }

    HudSeg SegWvwPpt(App&, const HudOpts&) { HudSeg s; s.label = "WvW PPT";
        const auto& m = AccountData::Get(); if (WvwGuard(s, m)) return s;
        const int you = WvwYou(m);
        char b[24]; std::snprintf(b, sizeof(b), "+%lld", m.wvwSides[you - 1].ppt);
        s.value = b; s.color = WvwColor(you); return s; }

    HudSeg SegWvwKd(App&, const HudOpts&) { HudSeg s; s.label = "WvW K/D";
        const auto& m = AccountData::Get(); if (WvwGuard(s, m)) return s;
        const AccountData::WvwSide& sd = m.wvwSides[WvwYou(m) - 1];
        const double kd = sd.kdDeaths > 0 ? (double)sd.kdKills / (double)sd.kdDeaths : (double)sd.kdKills;
        char b[24]; std::snprintf(b, sizeof(b), "%.2f", kd);
        s.value = b; s.color = WvwColor(WvwYou(m)); return s; }

    HudSeg SegWvwControl(App&, const HudOpts&) { HudSeg s; s.label = "WvW Control";
        const auto& m = AccountData::Get(); if (WvwGuard(s, m)) return s;
        if (const AccountData::WvwMapState* cur = WvwCurrentMap(m))
        { char b[48]; std::snprintf(b, sizeof(b), "R:%d B:%d G:%d", cur->ownedByColor[0], cur->ownedByColor[1], cur->ownedByColor[2]); s.value = b; }
        else { const int c = WvwYou(m); int total = 0; for (const auto& ms : m.wvwMaps) total += ms.ownedByColor[c - 1];
               char b[32]; std::snprintf(b, sizeof(b), "%d owned", total); s.value = b; s.color = WvwColor(c); }
        return s; }

    HudSeg SegWvwStatus(App&, const HudOpts&) { HudSeg s; s.label = "WvW";
        if (IsInWvW()) s.value = Mumble::MapTypeName(CurrentMapType());
        else { s.value = "Not in WvW"; s.color = kDim; }
        return s; }

    void TipWvwScore(App&, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("WvW Matchup");
        const auto& m = AccountData::Get();
        if (!m.haveWvwMatch) { Gw2Ui::TooltipMuted("Loading match..."); Gw2Ui::TooltipEnd(); return; }
        for (int c = 0; c < 3; ++c)
        {
            const AccountData::WvwSide& sd = m.wvwSides[c];
            char b[112]; std::snprintf(b, sizeof(b), "%s   %s   (VP %d)",
                sd.worldName.empty() ? (c == 0 ? "Red" : c == 1 ? "Blue" : "Green") : sd.worldName.c_str(),
                WvwAbbrev(sd.score).c_str(), sd.victoryPoints);
            Gw2Ui::Label(b, WvwColor(c + 1), c + 1 == m.wvwMyColor, nullptr, 15.f);
        }
        Gw2Ui::TooltipEnd();
    }
    void TipWvwSkirmish(App&, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("WvW Skirmish");
        const auto& m = AccountData::Get();
        if (!m.haveWvwMatch) { Gw2Ui::TooltipMuted("Loading match..."); Gw2Ui::TooltipEnd(); return; }
        char b[64]; std::snprintf(b, sizeof(b), "Skirmish #%d", m.wvwSkirmishIndex);
        Gw2Ui::TooltipText(b);
        for (int c = 0; c < 3; ++c) { char l[48]; std::snprintf(l, sizeof(l), "%d", m.wvwSides[c].skirmishScore); Gw2Ui::Label(l, WvwColor(c + 1), false, nullptr, 15.f); }
        const double left = m.wvwSkirmishEndsAt - ImGui::GetTime();
        if (left > 0) { Gw2Ui::TooltipText(("Next in " + HudShared::DurShort((long long)left)).c_str()); }
        Gw2Ui::TooltipEnd();
    }

    // ---- launcher texts: a count on the bar, a left-click popup that REUSES the dashboard's exact lists ----
    HudSeg SegZoneWps(App& app, const HudOpts&) { HudSeg s; s.label = "Waypoints"; const auto& z = app.state.zone;
        if (!z.Loaded || z.Steps.empty()) { s.value = "Unknown"; s.color = kDim; return s; }
        const Guide::ZoneCounts c = Guide::ZoneCompletion(app.progress, app.state.currentChar, z);
        char b[24]; std::snprintf(b, sizeof(b), "%d/%d", c.kindDone[0], c.kindTotal[0]); s.value = b; return s; }  // kind 0 = waypoints (done = reached/unlocked)
    HudSeg SegFavWps(App& app, const HudOpts&)  { HudSeg s; s.label = "Favorites";
        s.value = std::to_string((int)WaypointFavorites::List().size()); return s; }
    // The popup lists reuse the dashboard widgets but render LARGER than the compact dashboard: push a text scale
    // (the widgets scale their rows + fonts by Gw2Ui::TextScale()) inside a wider child so names/pills have room.
    void PopupZoneWaypoints(App& app, const HudOpts&)
    {
        Gw2Ui::Label("Zone Waypoints", Gw2Ui::kGold, false, nullptr, 18.f, 1.1f);
        ImGui::BeginChild("##ipZoneWps", ImVec2(470.f, 360.f), false);
        Gw2Ui::PushTextScale(1.3f);
        DashW::NearestWaypoints(app, 470.f);   // the dashboard's "Zone Waypoints" list (unlocked, nearest-first, click-to-travel)
        Gw2Ui::PopTextScale();
        ImGui::EndChild();
    }
    void PopupFavWaypoints(App& app, const HudOpts&)
    {
        Gw2Ui::Label("Favorite Waypoints", Gw2Ui::kGold, false, nullptr, 18.f, 1.1f);
        ImGui::BeginChild("##ipFavWps", ImVec2(470.f, 360.f), false);
        Gw2Ui::PushTextScale(1.3f);
        DashW::FavoriteWaypoints(app, 470.f);  // the dashboard's favorites list
        Gw2Ui::PopTextScale();
        ImGui::EndChild();
    }

    // ---- Loadout switcher: value = active loadout; hover = the profiles it uses; right-click = switch (submenus) ----
    std::string ScopedProfileName(Profiles::IProfileHost& host, int i)
    {
        std::string name = host.NameAt(i);
        if (host.IsGlobalAt(i))
            name += " [global]";
        return name;
    }
    std::string ScopedLoadoutName(App& app, int i)
    {
        Profiles::IProfileHost& host = Loadouts::Host(app);
        return ScopedProfileName(host, i);
    }
    HudSeg SegLoadout(App& app, const HudOpts&) { HudSeg s; s.label = "Loadout";
        const int n = Loadouts::Count(app), a = Loadouts::Active(app);
        if (n <= 0) { s.value = "None"; s.color = kDim; return s; }
        s.value = (a >= 0 && a < n) ? ScopedLoadoutName(app, a) : std::string("--"); return s; }
    void TipLoadout(App& app, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        const int n = Loadouts::Count(app), a = Loadouts::Active(app);
        const std::string title = (n > 0 && a >= 0 && a < n) ? ScopedLoadoutName(app, a) : std::string("Loadout");
        Gw2Ui::TooltipTitle(title.c_str());
        for (int f = 0; f < Loadouts::FamCount; ++f)
        {
            Profiles::IProfileHost* h = Loadouts::FamilyHost(app, f);
            if (!h) continue;
            const int act = h->Active();
            const std::string pn = (act >= 0 && act < h->Count()) ? ScopedProfileName(*h, act) : std::string("-");
            Gw2Ui::TooltipText((std::string(Loadouts::FamilyName(f)) + ":  " + pn).c_str());
        }
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Right-click: switch loadout / area");
        Gw2Ui::TooltipEnd();
    }
    void MenuLoadoutNodes(App& app, const HudOpts&, std::vector<Gw2Ui::MenuNode>& nodes)
    {
        const int nL = Loadouts::Count(app), aL = Loadouts::Active(app);
        if (nL <= 0) { Gw2Ui::MenuNode z; z.label = "No loadouts (Settings -> Loadouts)"; z.id = -1; nodes.push_back(z); }
        else for (int i = 0; i < nL; ++i) { Gw2Ui::MenuNode n; n.label = ScopedLoadoutName(app, i); n.id = i; n.selected = (i == aL); nodes.push_back(n); }
        { Gw2Ui::MenuNode sep; sep.separator = true; nodes.push_back(sep); }
        for (int f = 0; f < Loadouts::FamCount; ++f)
        {
            Profiles::IProfileHost* h = Loadouts::FamilyHost(app, f);
            if (!h || h->Count() == 0) continue;
            Gw2Ui::MenuNode fam; fam.label = Loadouts::FamilyName(f);
            const int act = h->Active();
            for (int p = 0; p < h->Count(); ++p) { Gw2Ui::MenuNode c; c.label = ScopedProfileName(*h, p); c.id = 1000 + f * 100 + p; c.selected = (p == act); fam.children.push_back(std::move(c)); }
            nodes.push_back(std::move(fam));
        }
    }
    void MenuLoadoutPick(App& app, const HudOpts&, int picked)
    {
        if (picked < 0) return;
        if (picked < 1000) Loadouts::ApplyIndex(app, picked);
        else { const int f = (picked - 1000) / 100, p = (picked - 1000) % 100; Loadouts::SetFamily(app, f, p); }
    }

    // ---- rich hover tooltips (build their own Gw2Ui tooltip; shown on segment hover) ----
    void TipClock(App&, const HudOpts& o)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        const bool h = o.Get("h24", 1) != 0, a = o.Get("ampm", 1) != 0;
        Gw2Ui::TooltipTitle("Time");
        Gw2Ui::TooltipText((std::string("Local:   ") + HudShared::ClockByType(0, h, a)).c_str());
        Gw2Ui::TooltipText((std::string("Server:  ") + HudShared::ClockByType(1, h, a)).c_str());
        Gw2Ui::TooltipText((std::string("Tyrian:  ") + HudShared::ClockByType(2, h, a)).c_str());
        Gw2Ui::TooltipEnd();
    }
    void TipCurrency(App&, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Currencies");
        const auto& m = AccountData::Get();
        if (!m.haveWallet) { Gw2Ui::TooltipMuted("Needs an API key with the wallet scope."); Gw2Ui::TooltipEnd(); return; }
        std::map<int, long long> have; for (const auto& w : m.wallet) if (w.value > 0) have[w.id] = w.value;
        int shown = 0;
        for (const StaticData::CurrencyRef& c : StaticData::Currencies())   // ALL owned, GW2 display order
        {
            auto it = have.find(c.id);
            if (it == have.end()) continue;
            Gw2Ui::TooltipText((std::string(SessionFmt::Label(c.id)) + ": " + (c.id == 1 ? FmtGold(it->second) : std::to_string(it->second))).c_str());
            if (++shown >= 24) break;   // keep the tooltip bounded
        }
        if (shown == 0)   // catalog still warming -> the original curated handful so the tooltip is never blank
        {
            struct C { int id; const char* n; };
            static const C cs[] = { {1,"Gold"}, {2,"Karma"}, {3,"Laurels"}, {23,"Spirit Shards"}, {63,"Astral Acclaim"}, {26,"Geodes"} };
            for (const C& c : cs)
            {
                long long v = 0; for (const auto& w : m.wallet) if (w.id == c.id) { v = w.value; break; }
                Gw2Ui::TooltipText((std::string(c.n) + ": " + (c.id == 1 ? FmtGold(v) : std::to_string(v))).c_str());
            }
        }
        Gw2Ui::TooltipEnd();
    }
    void TipCoords(App& app, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Position");
        if (MumbleLink)
        {
            char b[96]; std::snprintf(b, sizeof(b), "World X %.1f   Y %.1f   Z %.1f", MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Y, MumbleLink->AvatarPosition.Z); Gw2Ui::TooltipText(b);
            const auto& z = app.state.zone;
            if (z.HasRects) { float cx, cy; Coords::WorldXZToContinent(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z, z.ContRect, z.MapRect, cx, cy); char c[64]; std::snprintf(c, sizeof(c), "Continent %.0f, %.0f", cx, cy); Gw2Ui::TooltipText(c); }
        }
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Left-click: copy the shown coordinates");
        Gw2Ui::TooltipEnd();
    }
    void TipFps(App&, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        const float f = ImGui::GetIO().Framerate;
        char b[48]; std::snprintf(b, sizeof(b), "%.0f FPS", f);
        Gw2Ui::TooltipTitle("Performance"); Gw2Ui::TooltipText(b);
        Gw2Ui::TooltipEnd();
    }
    void TipZone(App& app, const HudOpts& o)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        const auto& z = app.state.zone;
        const uint32_t mid = CurrentMapId();
        if (mid) SectorData::EnsureMap(mid);
        std::string zone = (z.Loaded && !z.Name.empty()) ? z.Name : std::string();
        if (zone.empty() && mid)   { std::string m = StaticData::MapNameOr(mid, ""); if (!m.empty()) zone = m; }
        std::string region = z.Loaded ? z.RegionName : std::string();
        if (region.empty() && mid) { const char* r = SectorData::RegionName(mid); if (r && *r) region = r; }
        std::string sector;
        if (mid && MumbleLink) sector = SectorData::SectorAt(mid, MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z);

        Gw2Ui::TooltipTitle(!zone.empty() ? zone.c_str() : "Zone");
        if (!region.empty()) Gw2Ui::TooltipText(("Region: " + region).c_str());
        if (!sector.empty()) Gw2Ui::TooltipText(("Sector: " + sector).c_str());
        if (z.Loaded && z.MaxLevel > 0) { char b[32]; if (z.MinLevel == z.MaxLevel) std::snprintf(b, sizeof(b), "Level %d", z.MaxLevel); else std::snprintf(b, sizeof(b), "Level %d-%d", z.MinLevel, z.MaxLevel); Gw2Ui::TooltipText(b); }

        // The zone map (option, default on), with a pin at your current position.
        if (o.Get("map", 1) != 0 && z.HasRects)
        {
            ImGui::Spacing();
            float zr[4]; ZoneMapCrop(&z, zr);
            const ImVec2 sz(280.f, 188.f);
            const float  availW = ImGui::GetContentRegionAvail().x;
            const float  offX = std::max(0.f, (availW - sz.x) * 0.5f);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            MapMark mark; int markN = 0;
            if (MumbleLink) { float cx, cy; Coords::WorldXZToContinent(MumbleLink->AvatarPosition.X, MumbleLink->AvatarPosition.Z, z.ContRect, z.MapRect, cx, cy); mark = MapMark{ cx, cy, 0 }; markN = 1; }
            if (DrawMapThumbnailRect(z.ContinentId, zr[0], zr[1], zr[2], zr[3], ImVec2(cur.x + offX, cur.y), sz, markN ? &mark : nullptr, markN, 0, /*backing*/ false))
                ImGui::Dummy(ImVec2(std::max(availW, sz.x), sz.y));
        }
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Left-click: open the Atlas");
        Gw2Ui::TooltipEnd();
    }
    void TipSession(App& app, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("This session");
        if (!app.state.sessionEarnings.active) { Gw2Ui::TooltipMuted("Starts when you enter the world."); Gw2Ui::TooltipEnd(); return; }
        Gw2Ui::TooltipText(("Time: " + HudShared::DurShort(SessionTracker::SessionDurationSec(app))).c_str());
        Gw2Ui::TooltipText(("Objectives: +" + std::to_string(SessionTracker::ObjectivesGained(app))).c_str());
        if (app.state.sessionEarnings.baseLevelKnown)
            Gw2Ui::TooltipText(("Levels: +" + std::to_string(SessionTracker::LevelsGained(app))).c_str());
        Gw2Ui::TooltipEnd();
    }
    void TipResets(App&, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Resets");
        Gw2Ui::TooltipText(("Daily:  " + HudShared::DurShort(DailyResetSecs())).c_str());
        Gw2Ui::TooltipText(("Weekly: " + HudShared::DurShort(WeeklyResetSecs())).c_str());
        Gw2Ui::TooltipEnd();
    }
    void TipWvList(const char* title, bool have, const std::vector<AccountData::WvObjective>& objs, int metaCur, int metaMax)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle(title);
        if (!AccountData::HasKey()) { Gw2Ui::TooltipMuted("Needs an API key."); Gw2Ui::TooltipEnd(); return; }
        if (!have)                  { Gw2Ui::TooltipMuted("Loading..."); Gw2Ui::TooltipEnd(); return; }
        if (AccountData::Get().wvUnavailable) { Gw2Ui::TooltipMuted("Wizard's Vault not unlocked on this account."); Gw2Ui::TooltipMuted("Requires a level-80 character."); Gw2Ui::TooltipEnd(); return; }
        if (objs.empty())           { Gw2Ui::TooltipMuted("No objectives."); }
        for (const AccountData::WvObjective& o : objs)
        {
            char b[180]; std::snprintf(b, sizeof(b), "%s   %d/%d%s", o.title.c_str(), o.cur, o.max, o.claimed ? "  (claimed)" : "");
            if (o.max > 0 && o.cur >= o.max) Gw2Ui::TooltipBullet(b); else Gw2Ui::TooltipText(b);
        }
        if (metaMax > 0) { Gw2Ui::TooltipSeparator(); char b[48]; std::snprintf(b, sizeof(b), "Meta reward: %d/%d", metaCur, metaMax); Gw2Ui::TooltipText(b); }
        Gw2Ui::TooltipEnd();
    }
    void TipWvDaily(App&, const HudOpts&)  { const auto& m = AccountData::Get(); TipWvList("Wizard's Vault - Daily",  m.haveWvDaily,  m.wvDaily,  m.wvDailyMetaCur,  m.wvDailyMetaMax); }
    void TipWvWeekly(App&, const HudOpts&) { const auto& m = AccountData::Get(); TipWvList("Wizard's Vault - Weekly", m.haveWvWeekly, m.wvWeekly, m.wvWeeklyMetaCur, m.wvWeeklyMetaMax); }
    void TipTp(App&, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Trading Post");
        const auto& m = AccountData::Get();
        if (!AccountData::HasKey()) { Gw2Ui::TooltipMuted("Needs an API key."); Gw2Ui::TooltipEnd(); return; }
        if (!m.haveTp)              { Gw2Ui::TooltipMuted("Loading..."); Gw2Ui::TooltipEnd(); return; }
        Gw2Ui::TooltipText(("Gold to collect:  " + FmtGold(m.tpCoins)).c_str());
        Gw2Ui::TooltipText(("Items to collect: " + std::to_string(m.tpItems)).c_str());
        Gw2Ui::TooltipText(("Buy orders:  " + std::to_string(m.tpBuys)).c_str());
        Gw2Ui::TooltipText(("Sell orders: " + std::to_string(m.tpSells)).c_str());
        Gw2Ui::TooltipEnd();
    }
    void TipAchv(App&, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Achievements");
        const auto& m = AccountData::Get();
        if (!AccountData::HasKey()) { Gw2Ui::TooltipMuted("Needs an API key."); Gw2Ui::TooltipEnd(); return; }
        if (!m.haveAch)            { Gw2Ui::TooltipMuted("Loading..."); Gw2Ui::TooltipEnd(); return; }
        std::string ap = std::to_string(m.totalAp); for (int i = (int)ap.size() - 3; i > 0; i -= 3) ap.insert(i, ",");
        Gw2Ui::TooltipText(("Points:      " + ap).c_str());
        Gw2Ui::TooltipText(("Completed:   " + std::to_string(m.achDone)).c_str());
        Gw2Ui::TooltipText(("In progress: " + std::to_string(m.achInProgress)).c_str());
        if (!m.haveTotalAp) Gw2Ui::TooltipMuted("(points need the account + progression scopes)");
        Gw2Ui::TooltipEnd();
    }
    void TipCurObj(App& app, const HudOpts& o)
    {
        const GuideState& st = app.state;
        // The SAME rich step tooltip the viewer + dashboard use, for the displayed current step. The zone map is
        // a per-text option ("Show map in tooltip"), mirroring the viewer/dashboard per-widget toggle.
        if (!st.inDungeon && !app.travel.Active() && st.zone.Loaded)
        {
            const int total = (int)st.zone.Steps.size();
            const int disp  = DisplayStepIndex(app, total);
            if (disp >= 0 && disp < total)
            {
                const Step& s = st.zone.Steps[disp];
                ObjectiveTooltip(s.Name.c_str(), s.Type.c_str(), &s, &st.zone, s.CX, s.CY, o.Get("map", 1) != 0, nullptr,
                                 "Left-click: mark complete (skip to next)", /*withTasks*/ true);
                return;
            }
        }
        // Fallback (travel target / dungeon / nothing active): the plain guidance summary.
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Current objective");
        const auto& g = st.guidanceSnapshot;
        if (!g.active) { Gw2Ui::TooltipMuted("No active route."); Gw2Ui::TooltipEnd(); return; }
        if (g.arrived) { Gw2Ui::TooltipText("Arrived."); Gw2Ui::TooltipEnd(); return; }
        if (!g.caption.empty()) Gw2Ui::TooltipText(g.caption.c_str());
        char b[64];
        std::snprintf(b, sizeof(b), "Distance: %.0fm", g.distance); Gw2Ui::TooltipText(b);
        if (g.speed > 0.1f) { std::snprintf(b, sizeof(b), "ETA: %s", HudShared::DurShort((long)(g.distance / g.speed)).c_str()); Gw2Ui::TooltipText(b); }
        if (g.offRoute) Gw2Ui::TooltipMuted("Off route.");
        Gw2Ui::TooltipEnd();
    }
    void TipRoute(App& app, const HudOpts&)   // shared by Route arrow / Distance / ETA
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Route");
        const auto& g = app.state.guidanceSnapshot;
        if (!g.active) { Gw2Ui::TooltipMuted("No active route."); Gw2Ui::TooltipEnd(); return; }
        if (!g.caption.empty()) Gw2Ui::TooltipText(g.caption.c_str());
        char b[64]; std::snprintf(b, sizeof(b), "Distance: %.0fm", g.distance); Gw2Ui::TooltipText(b);
        Gw2Ui::TooltipText(("ETA: " + FormatEta(g.distance, g.speed)).c_str());
        Gw2Ui::TooltipText(g.arrived ? "Status: arrived" : g.offRoute ? "Status: off route" : "Status: on route");
        Gw2Ui::TooltipEnd();
    }
    void TipClosestWp(App& app, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Closest waypoint");
        Travel::WpRef wp; float m;
        if (ResolveClosestWp(app, wp, m)) { Gw2Ui::TooltipText(wp.name.c_str()); char b[32]; std::snprintf(b, sizeof(b), "%.0f m away", m); Gw2Ui::TooltipText(b); }
        else Gw2Ui::TooltipMuted("No waypoint nearby.");
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Left-click: travel    Right-click: copy link");
        Gw2Ui::TooltipEnd();
    }
    void TipZonePct(App& app, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle("Zone Completion");
        const auto& z = app.state.zone;
        if (!z.Loaded || z.Steps.empty()) { Gw2Ui::TooltipMuted("Needs a guided zone."); Gw2Ui::TooltipEnd(); return; }
        const Guide::ZoneCounts c = Guide::ZoneCompletion(app.progress, app.state.currentChar, z);
        char b[48]; std::snprintf(b, sizeof(b), "%d of %d objectives  (%d%%)", c.done, c.total, c.total ? c.done * 100 / c.total : 0); Gw2Ui::TooltipText(b);
        static const char* kKind[5] = { "Waypoints", "POIs", "Vistas", "Hero pts", "Hearts" };
        for (int k = 0; k < 5; ++k) if (c.kindTotal[k] > 0) { char kb[48]; std::snprintf(kb, sizeof(kb), "%s: %d/%d", kKind[k], c.kindDone[k], c.kindTotal[k]); Gw2Ui::TooltipText(kb); }
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Left-click: open the Checklist");
        Gw2Ui::TooltipEnd();
    }
    // Shared "fullness + open Inventory" tooltip for the storage texts.
    void TipStorage(const char* title, bool have, int used, int total, const char* extra)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        Gw2Ui::TooltipTitle(title);
        if (!AccountData::HasKey()) { Gw2Ui::TooltipMuted("Needs an API key."); }
        else if (!have)            { Gw2Ui::TooltipMuted("Loading..."); }
        else { char b[64]; std::snprintf(b, sizeof(b), "%d / %d slots used", used, total); Gw2Ui::TooltipText(b); if (extra && *extra) Gw2Ui::TooltipText(extra); }
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Left-click: open the Inventory browser");
        Gw2Ui::TooltipEnd();
    }
    void TipBankSpace(App&, const HudOpts&) { const auto& m = AccountData::Get(); char e[32]; std::snprintf(e, sizeof(e), "%d items", m.bankItems); TipStorage("Bank Space", m.haveBank, m.bankUsed, m.bankTotal, e); }
    void TipSharedInv(App&, const HudOpts&) { const auto& m = AccountData::Get(); TipStorage("Shared inventory", m.haveShared, m.sharedUsed, m.sharedTotal, nullptr); }
    void TipMatStorage(App&, const HudOpts&){ const auto& m = AccountData::Get(); char e[48]; std::snprintf(e, sizeof(e), "%d stacks, %lld total", m.matTypes, (long long)m.matTotal); TipStorage("Material storage", m.haveMats, m.matTypes, m.matTypes, e); }
    void TipBags(App& app, const HudOpts&)
    {
        const auto& m = AccountData::Get();
        const auto it = m.inv.find(app.state.currentChar);
        const bool have = it != m.inv.end() && it->second.have;
        TipStorage("Bag space", have, have ? it->second.usedSlots : 0, have ? it->second.totalSlots : 0, nullptr);
    }
    void TipUpcoming(App& app, const HudOpts& o)
    {
        const int n = o.Get("count", 1) + 2;   // enum index 0..3 -> 2..5
        const std::vector<int> up = UpcomingObjectiveSteps(app, n);
        if (up.empty())
        {
            if (Gw2Ui::TooltipBegin()) { Gw2Ui::TooltipTitle("Upcoming objectives"); Gw2Ui::TooltipMuted("Nothing upcoming."); Gw2Ui::TooltipEnd(); }
            return;
        }
        // The displayed (first) upcoming step gets the SAME rich step tooltip (map = per-text option); the rest
        // of the next N list as a footer line.
        std::string footer;
        for (size_t i = 1; i < up.size(); ++i)
        {
            const Step& s = app.state.zone.Steps[up[i]];
            if (!footer.empty()) footer += ", ";
            footer += s.Name.empty() ? "(unnamed)" : s.Name;
        }
        if (!footer.empty()) footer = "Then: " + footer + "\n";
        footer += "Left-click: switch to it    Right-click: mark complete / skip";
        const Step& first = app.state.zone.Steps[up[0]];
        ObjectiveTooltip(first.Name.c_str(), first.Type.c_str(), &first, &app.state.zone, first.CX, first.CY,
                         o.Get("map", 1) != 0, nullptr, footer.c_str(), /*withTasks*/ true);
    }

    // ---- Current / Upcoming objective interactions (left-click + right-click on the data text) -----------------
    // Both gate on a real gameplay zone step (NOT dungeon / travel), so they no-op in those modes.
    const Step* CurZoneStep(App& app)
    {
        const GuideState& st = app.state;
        if (st.inDungeon || app.travel.Active() || !st.zone.Loaded) return nullptr;
        const int total = (int)st.zone.Steps.size();
        const int disp  = DisplayStepIndex(app, total);
        return (disp >= 0 && disp < total) ? &st.zone.Steps[disp] : nullptr;
    }
    const Step* FirstUpcomingStep(App& app)
    {
        if (app.state.inDungeon || app.travel.Active() || !app.state.zone.Loaded) return nullptr;
        const std::vector<int> up = UpcomingObjectiveSteps(app, 1);
        return up.empty() ? nullptr : &app.state.zone.Steps[up[0]];
    }
    void ClickCurObj(App& app, const HudOpts&)                                            // left-click -> mark complete
    {
        if (const Step* s = CurZoneStep(app)) { CompleteStep(app.progress, app.state, s->StepId); ViewerAlert("Marked the current objective complete."); }
    }
    // The Current-objective right-click items in a STABLE display order; actions() + onAction() SHARE this builder
    // so the picked index maps correctly even though items are conditional (no step -> no Mark complete; nothing
    // ticked yet -> no Back).
    enum { CA_Complete = 0, CA_Back };
    void CurObjActionIds(App& app, std::vector<int>& ids)
    {
        if (CurZoneStep(app))             ids.push_back(CA_Complete);
        if (!app.state.undoStack.empty()) ids.push_back(CA_Back);
    }
    void ActionsCurObj(App& app, const HudOpts&, std::vector<const char*>& out)           // right-click menu
    {
        std::vector<int> ids; CurObjActionIds(app, ids);
        for (int id : ids) out.push_back(id == CA_Complete ? "Mark complete" : "Back (undo last completion)");
    }
    void OnActionCurObj(App& app, const HudOpts&, int idx)
    {
        std::vector<int> ids; CurObjActionIds(app, ids);
        if (idx < 0 || idx >= (int)ids.size()) return;
        if (ids[idx] == CA_Complete) { if (const Step* s = CurZoneStep(app)) { CompleteStep(app.progress, app.state, s->StepId); ViewerAlert("Marked the current objective complete."); } }
        else if (!app.state.undoStack.empty()) { UncompleteStep(app.progress, app.state, app.state.undoStack.back()); ViewerAlert("Undid the last completion."); }
    }
    void ClickUpcoming(App& app, const HudOpts&)                                          // left-click -> aim the arrow at it
    {
        if (const Step* s = FirstUpcomingStep(app)) OnObjectiveTargetClicked(app, *s);
    }
    void ActionsUpcoming(App& app, const HudOpts&, std::vector<const char*>& out)
    {
        if (FirstUpcomingStep(app)) { out.push_back("Switch to it"); out.push_back("Mark complete"); }
    }
    void OnActionUpcoming(App& app, const HudOpts&, int idx)
    {
        const Step* s = FirstUpcomingStep(app);
        if (!s) return;
        if (idx == 0)      OnObjectiveTargetClicked(app, *s);                             // switch the arrow to it
        else if (idx == 1) { CompleteStep(app.progress, app.state, s->StepId); ViewerAlert("Marked the upcoming objective complete."); }
    }

    // ---- Alt Equipment: a selected alt's gear; click -> Equipment page; right-click -> pick the alt. ----
    std::string g_altSel;   // selected character; defaults to the active character
    const AccountData::AcctChar* AltChar(const AccountData::Model& m, const std::string& nm)
    { for (const auto& c : m.characters) if (c.name == nm) return &c; return nullptr; }

    // The selectable stat columns for the Alt Equipment bar/options (opt key -> attribute key + short tag + %).
    // The first four (primaries) default ON; the rest default off. OptionsFor("altequip") mirrors this.
    struct AltStat { const char* opt; const char* attr; const char* tag; bool pct; int def; };
    static const AltStat kAltStats[] = {
        { "power", "Power", "Pwr", false, 1 }, { "precision", "Precision", "Prc", false, 1 },
        { "toughness", "Toughness", "Tgh", false, 1 }, { "vitality", "Vitality", "Vit", false, 1 },
        { "ferocity", "Ferocity", "Fer", false, 0 }, { "conddmg", "ConditionDamage", "Cond", false, 0 },
        { "healing", "HealingPower", "Heal", false, 0 }, { "concentration", "Concentration", "Conc", false, 0 },
        { "expertise", "Expertise", "Exp", false, 0 }, { "armor", "Armor", "Armor", false, 0 },
        { "health", "Health", "HP", false, 0 }, { "critchance", "CritChance", "Crit", true, 0 },
        { "critdmg", "CritDamage", "CritDmg", true, 0 }, { "boondur", "BoonDuration", "Boon", true, 0 },
        { "conddur", "ConditionDuration", "CondDur", true, 0 } };

    HudSeg SegAltEquip(App& app, const HudOpts& o)
    {
        HudSeg s; s.label = "Equip";
        const auto& m = AccountData::Get();
        if (g_altSel.empty()) g_altSel = app.state.currentChar;
        if (g_altSel.empty()) { ApiPlaceholder(s, false); return s; }
        AccountData::EnsureCharDetail(g_altSel);
        auto it = m.equip.find(g_altSel);
        const bool have = it != m.equip.end() && it->second.have;
        if (ApiPlaceholder(s, have)) return s;
        AccountData::EnsureEquipAttributes(g_altSel);
        const AccountData::CharEquip& eq = it->second;
        if (eq.haveAttrs)
        {
            // Each enabled stat is self-tagged so the bar shows which are on (e.g. "424 Pwr  364 Prc  20.5% Crit").
            std::string v;
            auto add = [&](const std::string& p) { if (!v.empty()) v += "  "; v += p; };
            for (const AltStat& st : kAltStats)
            {
                if (o.Get(st.opt, st.def) == 0) continue;
                auto a = eq.attributes.find(st.attr); const double val = (a != eq.attributes.end()) ? a->second : 0.0;
                char b[32];
                if (st.pct) std::snprintf(b, sizeof(b), "%.1f%% %s", val * 100.0, st.tag);
                else        std::snprintf(b, sizeof(b), "%d %s", (int)std::lround(val), st.tag);
                add(b);
            }
            s.value = v.empty() ? g_altSel : v;   // all toggles off -> just the character name
        }
        else
        {
            int n = 0; for (const Api::V2::ItemSlot& p : eq.pieces) if (p.id > 0) ++n;
            char b[64]; std::snprintf(b, sizeof(b), "%s (%d)", g_altSel.c_str(), n);
            s.value = b;
        }
        return s;
    }

    void TipAltEquip(App& app, const HudOpts&)
    {
        if (!Gw2Ui::TooltipBegin()) return;
        const auto& m = AccountData::Get();
        if (!AccountData::HasKey()) { Gw2Ui::TooltipMuted("Needs an API key (characters)."); Gw2Ui::TooltipEnd(); return; }
        const std::string nm = g_altSel.empty() ? app.state.currentChar : g_altSel;
        Gw2Ui::TooltipTitle(nm.empty() ? "Alt Equipment" : nm.c_str());
        if (const AccountData::AcctChar* ch = AltChar(m, nm)) { char b[96]; std::snprintf(b, sizeof(b), "Level %d %s", ch->level, ch->profession.c_str()); Gw2Ui::TooltipMuted(b); }
        auto it = m.equip.find(nm);
        if (it == m.equip.end() || !it->second.have) { Gw2Ui::TooltipMuted("Loading equipment..."); Gw2Ui::TooltipEnd(); return; }
        Gw2Ui::TooltipSeparator();
        int row = 0;
        for (const Api::V2::ItemSlot& p : it->second.pieces)
        {
            if (p.id <= 0) continue;
            auto mi = m.itemMeta.find(p.id);
            const std::string name = (mi != m.itemMeta.end() && !mi->second.name.empty()) ? mi->second.name : ("Item " + std::to_string(p.id));
            const std::string icon = mi != m.itemMeta.end() ? mi->second.icon : std::string();
            const std::string rar  = mi != m.itemMeta.end() ? mi->second.rarity : std::string();
            const std::string link = mi != m.itemMeta.end() ? mi->second.chatLink : std::string();
            ItemUI::DrawResultRow(row++, 300.f, p.id, icon, name, rar, 0, link);
        }
        if (row == 0) Gw2Ui::TooltipMuted("No equipped items.");
        AccountData::EnsureEquipAttributes(nm);
        if (it->second.haveAttrs)
        {
            Gw2Ui::TooltipSeparator();
            const auto& A = it->second.attributes;
            auto gi = [&](const char* k) { auto a = A.find(k); return (int)std::lround(a != A.end() ? a->second : 0.0); };
            auto gp = [&](const char* k) { auto a = A.find(k); return (a != A.end() ? a->second : 0.0) * 100.0; };
            char b[112];
            std::snprintf(b, sizeof(b), "Power %d    Precision %d    Ferocity %d", gi("Power"), gi("Precision"), gi("Ferocity")); Gw2Ui::TooltipText(b);
            std::snprintf(b, sizeof(b), "Toughness %d    Vitality %d    Healing %d", gi("Toughness"), gi("Vitality"), gi("HealingPower")); Gw2Ui::TooltipText(b);
            std::snprintf(b, sizeof(b), "Condition %d    Armor %d    Health %d", gi("ConditionDamage"), gi("Armor"), gi("Health")); Gw2Ui::TooltipText(b);
            std::snprintf(b, sizeof(b), "Crit %.1f%%   Crit Dmg %.0f%%   Boon %.1f%%", gp("CritChance"), gp("CritDamage"), gp("BoonDuration")); Gw2Ui::TooltipText(b);
            Gw2Ui::TooltipMuted("Equipment only -- no traits / food / boons.");
        }
        Gw2Ui::TooltipSeparator();
        Gw2Ui::TooltipMuted("Click: open Equipment.   Right-click: pick character.");
        Gw2Ui::TooltipEnd();
    }

    void ClickOpenAltEquip(App& app, const HudOpts&)
    {
        const std::string nm = g_altSel.empty() ? app.state.currentChar : g_altSel;
        if (!nm.empty()) SetEquipmentChar(nm.c_str());
        OpenItemsTab(app, 2);
    }

    void MenuAltEquipNodes(App&, const HudOpts&, std::vector<Gw2Ui::MenuNode>& nodes)
    {
        const auto& m = AccountData::Get();
        if (m.characters.empty()) { Gw2Ui::MenuNode z; z.label = "No characters cached"; z.id = -1; nodes.push_back(z); }
        else for (int i = 0; i < (int)m.characters.size(); ++i)
        { Gw2Ui::MenuNode n; n.label = m.characters[i].name; n.id = i; n.selected = (m.characters[i].name == g_altSel); nodes.push_back(n); }
    }
    void MenuAltEquipPick(App& app, const HudOpts&, int picked)
    {
        const auto& m = AccountData::Get();
        if (picked >= 0 && picked < (int)m.characters.size()) { g_altSel = m.characters[picked].name; AccountData::EnsureCharDetail(g_altSel); }
        (void)app;
    }
}

// Route-arrow data text right-click -> the shared nav-arrow menu (composed ABOVE the panel's "Bar Control" submenu
// by the unified InfoPanel menu). No lock/reset items -- the data text's placement is the Info Panel's job.
static void MenuRouteArrowNodes(App& app, const InfoData::HudOpts&, std::vector<Gw2Ui::MenuNode>& out)
{ QuickMenu::ArrowNodes(app.config, CurrentTargetOverride(app), /*dungeon*/ false, app.state.activeContent == ContentType::Farming, app.state.activeContent == ContentType::Fishing, /*inWorld*/ false, out); }
static void MenuRouteArrowPick(App& app, const InfoData::HudOpts&, int picked)
{ QuickMenu::ArrowDispatch(app, picked); }

// "Mode": shows Objectives vs Farming; left-click toggles the shared farming run (in sync with the viewer chip,
// the dashboard toggle widget + the tray). Green while a gathering run is active.
static InfoData::HudSeg SegMode(App& app, const InfoData::HudOpts&)
{
    InfoData::HudSeg s; s.label = "Mode";
    const bool farming = FarmingRunActive(app);
    s.value = farming ? "Farming" : "Objectives";
    s.color = farming ? IM_COL32(150, 230, 150, 255) : IM_COL32(236, 230, 212, 255);
    return s;
}
static void ClickMode(App& app, const InfoData::HudOpts&)
{
    if (app.state.farmRunActive)       app.state.farmRunActive = false;   // -> back to the leveling guide
    else if (FarmingTabAvailable(app)) app.state.farmRunActive = true;    // -> start a gathering run (farming maps only)
    app.settingsDirty = true;
}

// "Session earnings": the live current-session delta for a chosen currency (the same per-slot currency option as
// the Currency data text). Green for gains, red for losses; left-click opens the Sessions tab. Driven by the
// shared SessionTracker (baselines the wallet at login). "--" until a wallet baseline exists.
static InfoData::HudSeg SegSessEarn(App& app, const InfoData::HudOpts& o)
{
    const std::vector<StaticData::CurrencyRef>& cat = StaticData::Currencies();
    int id = 1;
    if (!cat.empty()) { const int sel = std::min(std::max(o.Get("currency", 0), 0), (int)cat.size() - 1); id = cat[sel].id; }
    InfoData::HudSeg s; s.label = SessionFmt::Label(id);
    const auto& m = AccountData::Get();
    if (ApiPlaceholder(s, m.haveWallet)) return s;
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active || !se.haveBase) { s.value = "--"; s.color = kDim; return s; }
    const long long d = SessionTracker::CurrentDelta(app, id);
    s.value = SessionFmt::Signed(id, d);
    s.color = d > 0 ? IM_COL32(150, 200, 120, 255) : d < 0 ? IM_COL32(214, 130, 110, 255) : IM_COL32(236, 230, 212, 255);
    return s;
}
static void TipSessEarn(App& app, const InfoData::HudOpts&)
{
    if (!Gw2Ui::TooltipBegin()) return;
    Gw2Ui::TooltipTitle("Session earnings");
    const auto& m = AccountData::Get();
    if (!m.haveWallet) { Gw2Ui::TooltipMuted("Needs an API key with the wallet scope."); Gw2Ui::TooltipEnd(); return; }
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active || !se.haveBase) { Gw2Ui::TooltipMuted("Waiting for a character / wallet baseline."); Gw2Ui::TooltipEnd(); return; }
    Gw2Ui::TooltipText(("Time: " + SessionFmt::Clock(SessionTracker::DurationSec(app))).c_str());
    const std::map<int, long long> deltas = SessionTracker::CurrentDeltas(app);
    if (deltas.empty()) { Gw2Ui::TooltipMuted("No earnings yet this session."); Gw2Ui::TooltipEnd(); return; }
    if (auto g = deltas.find(1); g != deltas.end())
        Gw2Ui::TooltipText((std::string("Gold: ") + SessionFmt::Signed(1, g->second)).c_str());
    for (const StaticData::CurrencyRef& c : StaticData::Currencies())
    {
        if (c.id == 1) continue;
        auto it = deltas.find(c.id);
        if (it == deltas.end()) continue;
        Gw2Ui::TooltipText((std::string(SessionFmt::Label(c.id)) + ": " + SessionFmt::Signed(c.id, it->second)).c_str());
    }
    Gw2Ui::TooltipEnd();
}
static void ClickSessEarn(App& app, const InfoData::HudOpts&) { OpenSessionTab(app); }

const std::vector<InfoData::HudText>& InfoData::HudTexts()
{
    using F = InfoData::HudFam;
    static const std::vector<HudText> r = {
        { "zone",        "Zone",                 F::Location,  0,                       true, 0, 8,  SegZone,      TipZone,   ClickOpenAtlas },
        { "maplevel",    "Map level range",      F::Location,  0,                       false, 0, 0, SegMapLevel },
        { "coords",      "Coordinates",          F::Location,  0,                       false, 0, 0,  SegCoords,    TipCoords, ClickCopyCoords },
        { "mount",       "Mount",                F::Location,  0,                       false, 0, 0, SegMount },
        { "combat",      "In-combat indicator",  F::Location,  0,                       false, 0, 0, SegCombat },
        { "level",       "Character level",      F::Character, 0,                       false, 0, 0,  SegLevel },
        { "sessiontime", "Session time",         F::Character, 0,                       false, 0, 0,  SegSessTime,  TipSession },
        { "sessionobj",  "Session objectives",   F::Character, 0,                       false, 0, 0, SegSessObj,   TipSession },
        { "sessionlvl",  "Session levels gained",F::Character, 0,                       false, 0, 0, SegSessLvl,   TipSession },
        { "fps",         "FPS",                  F::System,    0,                       true, 0, 1,  SegFps,       TipFps },
        { "clocklocal",  "Clock (local)",        F::System,    0,                       false, 1, 0,  SegClkLocal,  TipClock },
        { "clockserver", "Clock (server)",       F::System,    0,                       false, 1, 0, SegClkServer, TipClock },
        { "clocktyrian", "Clock (Tyrian)",        F::System,    0,                       false, 1, 0, SegClkTyrian, TipClock },
        { "dailyreset",  "Daily reset",          F::System,    0,                       false, 1, 0,  SegDaily,     TipResets },
        { "weeklyreset", "Weekly reset",         F::System,    0,                       false, 1, 0, SegWeekly,    TipResets },
        { "gold",        "Currency",             F::Economy,   AccountData::DomWallet,  true, 2, 9, SegCurrency,  TipCurrency },
        { "session",     "Session earnings",     F::Economy,   AccountData::DomWallet,  true, 2, 5, SegSessEarn,  TipSessEarn, ClickSessEarn },
        { "wvdaily",     "Wizard's Vault (daily)",  F::Economy, AccountData::DomWizardVault, false, 2, 0, SegWvDaily,  TipWvDaily },
        { "wvweekly",    "Wizard's Vault (weekly)", F::Economy, AccountData::DomWizardVault, false, 2, 0, SegWvWeekly, TipWvWeekly },
        { "mastery",     "Mastery points",       F::Economy,   AccountData::DomMastery, false, 2, 0, SegMastery },
        { "fractal",     "Fractal level",        F::Economy,   AccountData::DomAccount, false, 2, 0, SegFractal },
        { "wvwrank",     "WvW rank",             F::Economy,   AccountData::DomAccount, false, 2, 0, SegWvw },
        { "wvwscore",    "WvW Score",            F::Economy,   0u, false, 2, 0, SegWvwScore,    TipWvwScore },
        { "wvwskirmish", "WvW Skirmish",         F::Economy,   0u, false, 2, 0, SegWvwSkirmish, TipWvwSkirmish },
        { "wvwppt",      "WvW PPT",              F::Economy,   0u, false, 2, 0, SegWvwPpt },
        { "wvwkd",       "WvW K/D",              F::Economy,   0u, false, 2, 0, SegWvwKd },
        { "wvwcontrol",  "WvW Map Control",      F::Economy,   0u, false, 2, 0, SegWvwControl },
        { "wvwstatus",   "WvW Status",           F::Location,  0u, false, 0, 0, SegWvwStatus },
        { "tp",          "Trading Post",         F::Economy,   AccountData::DomTradingPost,  false, 2, 0, SegTp,        TipTp },
        { "craftcart",   "Crafting Cart",        F::Economy,   AccountData::DomMaterials,    false, 2, 0, SegCraftCart, TipCraftCart, ClickOpenCart },
        { "bank",        "Bank Space",           F::Economy,   AccountData::DomBank,         false, 2, 0, SegBank,      TipBankSpace, ClickOpenInventory },
        { "materials",   "Material storage",     F::Economy,   AccountData::DomMaterials,    false, 2, 0, SegMats,      TipMatStorage, ClickOpenInventory },
        { "shared",      "Shared inventory",     F::Economy,   AccountData::DomShared,       false, 2, 0, SegShared,    TipSharedInv,  ClickOpenInventory },
        { "achv",        "Achievements",         F::Economy,   AccountData::DomAchievements | AccountData::DomAccount, false, 2, 0, SegAchv, TipAchv },
        { "masteriesu",  "Masteries unlocked",   F::Economy,   AccountData::DomMastery,      false, 2, 0, SegMasteriesU },
        { "playtime",    "Account playtime",     F::Economy,   AccountData::DomAccount,      false, 2, 0, SegPlaytime },
        { "deaths",      "Deaths (character)",   F::Character, AccountData::DomCharacters,   false, 0, 0, SegDeaths },
        // Group B: local / guide (no key)
        { "facing",      "Facing",               F::Location,  0,                       false, 0, 0, SegFacing },
        { "closestwp",   "Closest waypoint",     F::Location,  0,                       false, 0, 0, SegClosestWp, TipClosestWp, ClickClosestWp, ActionsClosestWp, OnActionClosestWp },
        { "zonewps",     "Zone waypoints",       F::Location,  0,                       true, 0, 4, SegZoneWps, nullptr, nullptr, nullptr, nullptr, PopupZoneWaypoints },
        { "favwps",      "Favorite waypoints",   F::Location,  0,                       false, 0, 0, SegFavWps,  nullptr, nullptr, nullptr, nullptr, PopupFavWaypoints },
        { "loadout",     "Loadout",              F::Character, 0,                       false, 0, 0, SegLoadout, TipLoadout, nullptr, nullptr, nullptr, nullptr, MenuLoadoutNodes, MenuLoadoutPick },
        { "altequip",    "Alt Equipment",        F::Character, AccountData::DomCharacters, false, 0, 0, SegAltEquip, TipAltEquip, ClickOpenAltEquip, nullptr, nullptr, nullptr, MenuAltEquipNodes, MenuAltEquipPick },
        { "charname",    "Character name",       F::Character, 0,                       false, 0, 0, SegCharName },
        { "profession",  "Profession",           F::Character, 0,                       false, 0, 0, SegProfession },
        { "race",        "Race",                 F::Character, 0,                       false, 0, 0, SegRace },
        { "spec",        "Elite spec",           F::Character, 0,                       false, 0, 0, SegSpec },
        { "nextobj",     "Current Objective",    F::Location,  0,                       true, 1, 2, SegCurObj,    TipCurObj, ClickCurObj, ActionsCurObj, OnActionCurObj },
        { "route",       "Route status",         F::Location,  0,                       false, 0, 0, SegRoute,     TipRoute },
        { "zonepct",     "Zone Completion",      F::Character, 0,                       false, 0, 0, SegZonePct,   TipZonePct,   ClickOpenChecklist },
        { "routearrow",  "Route arrow",          F::Location,  0,                       true, 1, 3, SegRouteArrow, TipRoute, nullptr, nullptr, nullptr, nullptr, MenuRouteArrowNodes, MenuRouteArrowPick },
        { "mode",        "Mode (Objectives/Farming)", F::Location, 0,                    false, 0, 0, SegMode,      nullptr,   ClickMode },
        { "distance",    "Distance to objective",F::Location,  0,                       false, 0, 0, SegDistance,  TipRoute },
        { "eta",         "ETA to objective",     F::Location,  0,                       false, 0, 0, SegEta,       TipRoute },
        { "upcoming",    "Upcoming objectives",  F::Location,  0,                       true, 1, 7, SegUpcoming,  TipUpcoming, ClickUpcoming, ActionsUpcoming, OnActionUpcoming },
        // Group D: account fetches (new domains)
        { "bags",        "Bag space",            F::Character, 0,                            true, 2, 6, SegBags,      TipBags,      ClickOpenInventory },
        { "raids",       "Raids cleared (week)", F::Economy,   AccountData::DomRaids,        false, 2, 0, SegRaids },
        { "luck",        "Luck (total)",         F::Economy,   AccountData::DomLuck,         false, 2, 0, SegLuck },
        { "worldbosses", "World bosses (today)", F::Economy,   AccountData::DomDailyDone,    false, 2, 0, SegWorldBosses },
        { "mapchests",   "Map chests (today)",   F::Economy,   AccountData::DomDailyDone,    false, 2, 0, SegMapChests },
        { "dailycraft",  "Daily crafting (today)",F::Economy,  AccountData::DomDailyDone,    false, 2, 0, SegDailyCrafting },
    };
    return r;
}

const InfoData::HudText* InfoData::FindText(const char* key)
{
    if (!key) return nullptr;
    for (const HudText& t : HudTexts()) if (std::string(t.key) == key) return &t;
    return nullptr;
}

const char* InfoData::FamilyName(HudFam f)
{
    switch (f)
    {
        case HudFam::Location:  return "Location & navigation";
        case HudFam::Character: return "Character & progress";
        case HudFam::Economy:   return "Account & economy";
        default:                return "System & time";
    }
}

// A per-slot "currency" option enumerating the WHOLE live /v2/currencies catalog (any currency), rebuilt when the
// catalog grows. Shared by the Currency + Session-earnings data texts; falls back to a curated handful until the
// catalog warms, so the saved option index still resolves at frame 0.
static const std::vector<InfoData::HudOption>& CurrencyOption()
{
    using K = InfoData::HudOption::Kind;
    static const char* fallbackNames[] = { "Gold", "Karma", "Laurels", "Spirit Shards", "Astral Acclaim" };
    static const std::vector<InfoData::HudOption> fallback = { { "currency", "Currency", K::Enum, 0, fallbackNames, 5 } };
    static std::vector<std::string>          store;
    static std::vector<const char*>          ptrs;
    static std::vector<InfoData::HudOption>  dyn;
    const std::vector<StaticData::CurrencyRef>& cat = StaticData::Currencies();
    if (cat.empty()) return fallback;
    if ((int)ptrs.size() != (int)cat.size())   // (re)build the names storage when the catalog first lands / grows
    {
        store.clear(); store.reserve(cat.size());
        for (const StaticData::CurrencyRef& c : cat) store.push_back(SessionFmt::Label(c.id));
        ptrs.clear(); ptrs.reserve(store.size());
        for (const std::string& s : store) ptrs.push_back(s.c_str());
        dyn = { { "currency", "Currency", K::Enum, 0, ptrs.data(), (int)ptrs.size() } };
    }
    return dyn;
}

const std::vector<InfoData::HudOption>& InfoData::OptionsFor(const char* key)
{
    using K = HudOption::Kind;
    static const std::vector<HudOption> none;
    static const char* coordFmt[] = { "World X, Z", "World X, Y, Z", "Continent" };
    static const std::vector<HudOption> coords    = { { "format", "Coordinate format", K::Enum, 0, coordFmt, 3 } };
    static const std::vector<HudOption> clock     = { { "h24", "24-hour time", K::Bool, 1, nullptr, 0 },
                                                      { "ampm", "Show AM/PM", K::Bool, 1, nullptr, 0 } };
    static const std::vector<HudOption> achv = { { "points",     "Show points",      K::Bool, 1, nullptr, 0 },
                                                 { "completed",  "Show completed",   K::Bool, 0, nullptr, 0 },
                                                 { "inprogress", "Show in progress", K::Bool, 0, nullptr, 0 } };
    static const std::vector<HudOption> altequip = {   // one toggle per stat (primaries default on); matches kAltStats in SegAltEquip
        { "power", "Power", K::Bool, 1, nullptr, 0 }, { "precision", "Precision", K::Bool, 1, nullptr, 0 },
        { "toughness", "Toughness", K::Bool, 1, nullptr, 0 }, { "vitality", "Vitality", K::Bool, 1, nullptr, 0 },
        { "ferocity", "Ferocity", K::Bool, 0, nullptr, 0 }, { "conddmg", "Condition Damage", K::Bool, 0, nullptr, 0 },
        { "healing", "Healing Power", K::Bool, 0, nullptr, 0 }, { "concentration", "Concentration", K::Bool, 0, nullptr, 0 },
        { "expertise", "Expertise", K::Bool, 0, nullptr, 0 }, { "armor", "Armor", K::Bool, 0, nullptr, 0 },
        { "health", "Health", K::Bool, 0, nullptr, 0 }, { "critchance", "Critical Chance", K::Bool, 0, nullptr, 0 },
        { "critdmg", "Critical Damage", K::Bool, 0, nullptr, 0 }, { "boondur", "Boon Duration", K::Bool, 0, nullptr, 0 },
        { "conddur", "Condition Duration", K::Bool, 0, nullptr, 0 } };
    static const char* upCounts[] = { "2", "3", "4", "5" };
    static const std::vector<HudOption> curobj    = { { "showicon", "Show type icon", K::Bool, 1, nullptr, 0 },
                                                      { "map", "Show map in tooltip", K::Bool, 1, nullptr, 0 } };
    static const std::vector<HudOption> upcoming  = { { "showicon", "Show type icon", K::Bool, 1, nullptr, 0 },
                                                      { "count", "Show in tooltip", K::Enum, 1, upCounts, 4 },
                                                      { "map", "Show map in tooltip", K::Bool, 1, nullptr, 0 } };
    static const std::vector<HudOption> routearrow = {
        { "directglow", "Hybrid direct-objective glow",     K::Bool, 0, nullptr, 0 },
        { "chevron",    "Glow: chevron tip",                K::Bool, 1, nullptr, 0 },
        { "ping",       "Glow: ping instead of constant",   K::Bool, 0, nullptr, 0 },
        { "every",      "Glow: ping every (s)",             K::Int,  5, nullptr, 0, 2, 10 },
        { "show",       "Glow: show for (s)",               K::Int,  2, nullptr, 0, 1, 5  },
    };
    static const std::vector<HudOption> zoneloc = { { "region", "Show region",         K::Bool, 0, nullptr, 0 },
                                                    { "zone",   "Show zone",           K::Bool, 1, nullptr, 0 },
                                                    { "sector", "Show sector",         K::Bool, 0, nullptr, 0 },
                                                    { "map",    "Show map in tooltip", K::Bool, 1, nullptr, 0 } };
    static const char* wvwHl[] = { "Leader", "Your team" };
    static const std::vector<HudOption> wvwscore = { { "highlight", "Highlight", K::Enum, 0, wvwHl, 2 } };
    const std::string k = key ? key : "";
    if (k == "coords") return coords;
    if (k == "achv") return achv;
    if (k == "altequip") return altequip;
    if (k == "gold" || k == "session") return CurrencyOption();   // any currency, from the live catalog
    if (k == "clocklocal" || k == "clockserver" || k == "clocktyrian") return clock;
    if (k == "nextobj") return curobj;
    if (k == "upcoming") return upcoming;
    if (k == "routearrow") return routearrow;
    if (k == "zone") return zoneloc;
    if (k == "wvwscore") return wvwscore;
    return none;
}
