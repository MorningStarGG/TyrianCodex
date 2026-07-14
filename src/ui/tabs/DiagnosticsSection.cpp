#include "ui/tabs/DiagnosticsSection.h"
#include "ui/tabs/SettingsCommon.h"
#include "ui/SettingsWindow.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "app/Glue.h"
#include "app/AccountData.h"   // live roster (in-account status) for the character-data cleanup
#include "app/CharData.h"      // AllCharacterNames / PurgeCharacter
#include "Shared.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "ui/dashboard/Notify.h"   // test-fire the notification toasts/log
#include "util/Textures.h"
#include "util/ImageCache.h"
#include "util/OwnedTex.h"   // live addon-owned image VRAM (diagnostics readout)
#include "app/DataBootstrap.h"   // self-download bootstrap status (source / core / assets progress)
#include "api/core/Http.h"       // shared-transport diagnostics (session / abort state)
#include "util/Dyes.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ui/tabs/JournalTab.h"

// Diagnostics section body - rendered in Options > Diagnostics (under that panel's "Diagnostics" header) and
// surfaced in settings search. Live MumbleLink/guide state + reset progress, then the image-cache stats +
// force-clear buttons. (This is the consolidated Diagnostics & Debug home; there is no separate tab.)
static const char* MapAssistTransportLabel(MapAssistTransport transport)
{
    switch (transport)
    {
        case MapAssistTransport::WndProc:   return "wndproc";
        case MapAssistTransport::SendInput: return "sendinput";
        case MapAssistTransport::Blocked:   return "blocked";
        default:                            return "none";
    }
}

void DrawDiagnosticsSection(App& app)
{
    const ImU32 kDim = IM_COL32(176, 166, 140, 255);
    const ImU32 kVal = IM_COL32(226, 222, 212, 255);
    const float fs = SettingsText::Body;
    char b[256];

    // A label/value row that wraps the value and never clips off the panel edge.
    auto kv = [&](const char* label, const char* value, ImU32 vcol, float lwOverride = 0.f) {
        const float w  = ImGui::GetContentRegionAvail().x;
        const float lw = lwOverride > 0.f ? std::min(lwOverride, w - 46.f) : std::min(150.f, w * 0.40f);
        const float vw = std::max(40.f, w - lw - 6.f);
        const float vh = std::max(fs + 4.f, Gw2Ui::MeasureWrappedHeight(value, fs, vw));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        Gw2Ui::LabelIn(p, ImVec2(p.x + lw, p.y + vh), label, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, kDim, false, nullptr, fs);
        Gw2Ui::LabelIn(ImVec2(p.x + lw + 6.f, p.y), ImVec2(p.x + lw + 6.f + vw, p.y + vh), value,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, vcol, false, nullptr, fs, vw);
        ImGui::Dummy(ImVec2(w, vh));
    };

    // ---- Live state ----
    Gw2Ui::SectionHeader("Live state", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-live");
    if (MumbleLink)
    {
        const Mumble::Vector3& a = MumbleLink->AvatarPosition;
        std::snprintf(b, sizeof(b), "%u", MumbleLink->Context.MapId); kv("Map", b, kVal);
        std::snprintf(b, sizeof(b), "%s   (level %d)", app.state.currentChar.c_str(), app.state.charLevel); kv("Character", b, kVal);
        std::snprintf(b, sizeof(b), "%.1f, %.1f, %.1f", a.X, a.Y, a.Z); kv("Avatar", b, kVal);
        std::snprintf(b, sizeof(b), "%s    nearest trail %.1f m    on-screen %d",
                      (NexusLink && NexusLink->IsGameplay) ? "gameplay" : "not gameplay",
                      app.trailRenderer.NearestDist(), app.trailRenderer.OnScreen()); kv("State", b, kVal);
        if (app.state.inDungeon && app.state.inst)
        { std::snprintf(b, sizeof(b), "dungeon '%s'", app.state.inst->Name.c_str()); kv("Active", b, kVal); }
        else if (app.state.zone.Loaded)
        { std::snprintf(b, sizeof(b), "%s  (Lv %d-%d)   step %d / %d", app.state.zone.Name.c_str(),
                        app.state.zone.MinLevel, app.state.zone.MaxLevel, app.state.curStep, (int)app.state.zone.Steps.size()); kv("Active zone", b, kVal); }
        else kv("Active zone", "(none / uncovered map - recommendations shown)", kVal);
        static const char* kMode[] = { "Trail", "HybridTrail", "HybridNearest", "DirectTrail", "DirectNearest" };
        std::snprintf(b, sizeof(b), "%s    off-route: %s", kMode[std::clamp(app.config.routeMode, 0, 4)], app.state.offRoute ? "yes" : "no"); kv("Routing", b, kVal);
        if (app.config.travelOfferDebug && !app.state.travelOffer.debugLine.empty())
            kv("Travel offer", app.state.travelOffer.debugLine.c_str(), IM_COL32(214, 200, 150, 255));
    }
    else
        Gw2Ui::Label("MumbleLink unavailable (not in game yet).", kDim, false, nullptr, fs);
    Gw2Ui::EndCard();

    // ---- Vista camera probe (does MumbleLink report the cinematic camera pan?) -- only when toggled on ----
    if (MumbleLink && app.config.debugVistaProbe)
    {
        const VistaProbe& vp = app.state.vistaProbe;
        Gw2Ui::SectionHeader("Vista camera probe", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-vista");
        SettingsParagraph("Stand at a vista, click Reset peak, then interact. If the pan is reported, peak jumps far "
                          "past your camera zoom while the avatar stays put (last-2s camera >> avatar).", kDim);
        ImGui::Spacing();
        const double ago = vp.peakAt > 0.0 ? (ImGui::GetTime() - vp.peakAt) : -1.0;
        if (ago >= 0.0) std::snprintf(b, sizeof(b), "cur %.1f m    peak %.1f m  (%.0f s ago)", vp.curDist, vp.peakDist, ago);
        else            std::snprintf(b, sizeof(b), "cur %.1f m    peak %.1f m", vp.curDist, vp.peakDist);
        kv("Cam-avatar", b, kVal);
        std::snprintf(b, sizeof(b), "camera %.1f m   avatar %.1f m", vp.camMoved2s, vp.avaMoved2s); kv("Last 2s moved", b, kVal);
        std::snprintf(b, sizeof(b), "gameplay %s    UITick %u (should be climbing)", vp.gameplayNow ? "yes" : "no", vp.tickNow); kv("Link", b, kVal);
        // The disambiguator: what happened WHILE not-gameplay (the cutscene, if the vista flips it).
        std::snprintf(b, sizeof(b), "not-gameplay seen: %s    peak then %.1f m    link live then: %s",
                      vp.sawNotGameplay ? "yes" : "no", vp.peakDistNoGp, vp.tickAdvNoGp ? "yes" : "no");
        kv("During cutscene", b, vp.sawNotGameplay ? IM_COL32(255, 224, 150, 255) : kVal);
        ImGui::Spacing();
        if (Gw2Ui::Button("Reset peak", 140.f, 28.f))
        {
            VistaProbe& w = app.state.vistaProbe;
            w.peakDist = 0.f; w.peakAt = 0.0;
            w.sawNotGameplay = false; w.peakDistNoGp = 0.f; w.tickAdvNoGp = false;
        }
        Gw2Ui::EndCard();
    }

    // ---- Map assist (only while a pan/assist is active) ----
    if (MumbleLink && app.state.mapAssist.active)
    {
        const MapAssistTarget& ma = app.state.mapAssist;
        const char* probeState = ma.focusProbeWaiting ? "waiting" :
                                 ma.focusProbeDone ? (ma.focusProbeInvoked ? "done" : "unavailable") :
                                 ma.focusProbeRequested ? "pending" : "off";
        Gw2Ui::SectionHeader("Map assist", nullptr, SettingsText::Header);
        Gw2Ui::BeginAccentCard("diag-mapassist", 0.f, IM_COL32(226, 166, 70, 220), IM_COL32(3, 5, 5, 150), IM_COL32(130, 110, 70, 120));
        std::snprintf(b, sizeof(b), "%.1f, %.1f   attempts %d   %s", ma.cx, ma.cy, ma.panAttempts, ma.panStatus.empty() ? "-" : ma.panStatus.c_str()); kv("Target", b, kVal);
        std::snprintf(b, sizeof(b), "%s   wnd %d   send %d   zero %d/%d   wndRet %lld", MapAssistTransportLabel(ma.lastTransport),
                      ma.wndProcDragAttempts, ma.sendInputDragAttempts, ma.consecutiveZeroMoveReadbacks, ma.zeroMoveReadbacks, (long long)ma.lastWndProcResult); kv("Input", b, kVal);
        std::snprintf(b, sizeof(b), "game %s   textbox %s   imgui mouse %s", ma.lastGameFocus ? "yes" : "no", ma.lastTextboxFocus ? "yes" : "no", ma.lastWantCaptureMouse ? "yes" : "no"); kv("Focus", b, kVal);
        std::snprintf(b, sizeof(b), "%s | %s", ma.lastWndProcClass.empty() ? "-" : ma.lastWndProcClass.c_str(), ma.lastWndProcTitle.empty() ? "-" : ma.lastWndProcTitle.c_str()); kv("Window", b, kVal);
        std::snprintf(b, sizeof(b), "%s   before %.0f,%.0f  after %.0f,%.0f  player %.0f,%.0f", probeState,
                      ma.focusProbeBeforeX, ma.focusProbeBeforeY, ma.focusProbeAfterX, ma.focusProbeAfterY, ma.focusProbePlayerX, ma.focusProbePlayerY); kv("Focus probe", b, kVal);
        Gw2Ui::EndCard();
    }

    // ---- Map scale & DPI (base = MapScale / Scaling, auto-adjusts to UI/DPI; custom only fine-tunes it) ----
    if (MumbleLink)
    {
        Gw2Ui::SectionHeader("Map scale & DPI", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-scale");
        kv("Mode", app.config.mapScaleCustom ? "CUSTOM fine-tune" : "AUTO (MapScale / Scaling)", kVal);
        std::snprintf(b, sizeof(b), "Nexus %.4f   uiSize %.3f   MapScale %.4f", NexusLink ? NexusLink->Scaling : 0.f, UiScaleNow(), MumbleLink->Context.MapScale); kv("Scaling", b, kVal);
        std::snprintf(b, sizeof(b), "osScale %.4f   gameDPI %s   nexusDPI %s", app.dpiWatcher.State().osDpiScale, app.dpiWatcher.State().gameDpiOn ? "ON" : "off", app.dpiWatcher.State().nexusDpiOn ? "ON" : "off"); kv("DPI", b, kVal);
        ImGui::Spacing();
        if (Gw2Ui::Button(app.config.mapScaleCustom ? "Use auto" : "Fine-tune (custom)", 180.f))
        {
            app.config.mapScaleCustom = !app.config.mapScaleCustom;
            if (!app.config.mapScaleCustom) { app.config.mapOpenScale = 1.0f; app.config.miniMapScale = 1.0f; }
            app.settingsDirty = true;
        }
        if (app.config.mapScaleCustom)
        {
            std::snprintf(b, sizeof(b), "open x%.4f   mini x%.4f   (1.0000 = pure auto)", app.config.mapOpenScale, app.config.miniMapScale); kv("Fine-tune", b, kVal);
            auto bump = [&](float d) { app.config.mapOpenScale += d; app.settingsDirty = true; };
            ImGui::PushID("opencalib");
            if (Gw2Ui::Button("open -0.01", 86.f))                  bump(-0.01f);
            ImGui::SameLine(); if (Gw2Ui::Button("-0.001", 64.f)) bump(-0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.001", 64.f)) bump(+0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.01", 60.f))  bump(+0.01f);
            ImGui::PopID();
            auto bumpm = [&](float d) { app.config.miniMapScale += d; app.settingsDirty = true; };
            ImGui::PushID("minicalib");
            if (Gw2Ui::Button("mini -0.01", 86.f))                  bumpm(-0.01f);
            ImGui::SameLine(); if (Gw2Ui::Button("-0.001", 64.f)) bumpm(-0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.001", 64.f)) bumpm(+0.001f);
            ImGui::SameLine(); if (Gw2Ui::Button("+0.01", 60.f))  bumpm(+0.01f);
            ImGui::PopID();
        }
        Gw2Ui::EndCard();
    }

    // ---- Data bootstrap (self-download) ----
    {
        const ImU32 kAmber = IM_COL32(255, 200, 120, 255);
        const ImU32 kRed   = IM_COL32(255, 140, 120, 255);
        const ImU32 kGreen = IM_COL32(150, 220, 150, 255);
        Gw2Ui::SectionHeader("Data bootstrap", nullptr, SettingsText::Header);
        Gw2Ui::BeginCard("diag-bootstrap");
        SettingsParagraph("The DLL self-downloads its data/ (Nexus ships only the DLL). Core (datasets + fonts) is a "
                          "blocking download; the image packs stream in the BACKGROUND and switch in LIVE -- no reload.", kDim);
        ImGui::Spacing();

        std::string src = DataBootstrap::SourceUrl();
        if (src.size() > 58) src = "..." + src.substr(src.size() - 55);
        std::snprintf(b, sizeof(b), "%s%s", src.c_str(), DataBootstrap::SourceOverridden() ? "   (override)" : "");
        kv("Source", b, DataBootstrap::SourceOverridden() ? kAmber : kVal);

        const DataBootstrap::Phase ph = DataBootstrap::CurrentPhase();
        if (ph == DataBootstrap::Phase::Ready)
        { std::snprintf(b, sizeof(b), "ready%s", DataBootstrap::CoreAdopted() ? " (adopted existing data)" : " (downloaded)"); kv("Core", b, kVal); }
        else if (ph == DataBootstrap::Phase::DownloadingCore)
        { std::snprintf(b, sizeof(b), "downloading %d%%", DataBootstrap::CorePct()); kv("Core", b, kAmber); }
        else if (ph == DataBootstrap::Phase::CoreFailed)
        { std::snprintf(b, sizeof(b), "FAILED: %s", DataBootstrap::Error().c_str()); kv("Core", b, kRed); }
        else kv("Core", "checking...", kDim);

        const int need = DataBootstrap::AssetsNeeded(), got = DataBootstrap::AssetsFetched(), fail = DataBootstrap::AssetsFailed();
        if (DataBootstrap::AssetsActive())
        { std::string cur = DataBootstrap::AssetsCurrent(); std::snprintf(b, sizeof(b), "downloading %d/%d   %s", got, need, cur.c_str()); kv("Packs", b, kAmber); }
        else if (!DataBootstrap::AssetsSettled()) kv("Packs", "waiting for core...", kDim);
        else if (need == 0) kv("Packs", "all present (adopted, no download)", kVal);
        else { std::snprintf(b, sizeof(b), "complete: %d fetched live%s", got, fail ? "  (some failed)" : ""); kv("Packs", b, fail ? kAmber : kVal); }

        // Per-pack LIVE status: watch a pack flip missing/downloading -> open (N) mid-session, no reload. Size the
        // label column to the WIDEST pack name (they're long, e.g. cosmetic_renders.pack) so the value never overlaps.
        float packLw = 130.f;
        for (int i = 0; i < ImageCache::kPackCount; ++i)
        {
            char lbl[48]; std::snprintf(lbl, sizeof(lbl), "  %s", ImageCache::PackFileName((ImageCache::Pack)i));
            packLw = std::max(packLw, Gw2Ui::MeasureWidth(lbl, fs) + 16.f);
        }
        for (int i = 0; i < ImageCache::kPackCount; ++i)
        {
            const ImageCache::Pack pp = (ImageCache::Pack)i;
            const char* name = ImageCache::PackFileName(pp);
            char lbl[48]; std::snprintf(lbl, sizeof(lbl), "  %s", name);
            ImU32 col; char st[48];
            if (ImageCache::IsPackOpen(pp))                    { std::snprintf(st, sizeof(st), "open (%d)", ImageCache::PackEntryCount(pp)); col = kGreen; }
            else if (DataBootstrap::AssetsCurrent() == name)   { std::snprintf(st, sizeof(st), "downloading..."); col = kAmber; }
            else if (DataBootstrap::AssetsActive())            { std::snprintf(st, sizeof(st), "queued"); col = kDim; }
            else                                               { std::snprintf(st, sizeof(st), "missing"); col = kAmber; }
            kv(lbl, st, col, packLw);
        }

        // Shared HTTP transport (the reward-fetch "WinHttpOpen failed" symptom lived here): apiAborting should be
        // NO in normal operation; a stuck YES would mean the API scope was left aborted.
        const Api::Http::DiagState hd = Api::Http::Diag();
        std::snprintf(b, sizeof(b), "session %s   api-aborting %s   aborts/resumes %d/%d   in-flight %d",
                      hd.sessionOpen ? "open" : "closed", hd.apiAborting ? "YES" : "no", hd.abortCount, hd.resumeCount, hd.activeReqs);
        kv("Transport", b, hd.apiAborting ? kRed : kVal);
        Gw2Ui::EndCard();
    }

    // ---- Image cache ----
    static bool s_loaded = false;
    static ImageCache::Stats s_tiles, s_icons;
    static int  s_arm = 0;   // two-click confirm: 0 none, 1 clear-tiles armed, 2 free-image-memory armed, 3 clear-image-storage armed
    auto refresh = [&] { s_tiles = ImageCache::GetStats(ImageCache::Ns::Tiles);
                         s_icons = ImageCache::GetStats(ImageCache::Ns::Assets); s_loaded = true; };
    if (!s_loaded) refresh();
    auto mb = [](long long x) { return x / (1024.0 * 1024.0); };

    Gw2Ui::SectionHeader("Image cache", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-cache");
    SettingsParagraph("Maps + icons download once and cache to disk for instant load next session.", kDim);
    ImGui::Spacing();
    std::snprintf(b, sizeof(b), "%d files   (%.1f MB)", s_tiles.files, mb(s_tiles.bytes)); kv("Map tiles", b, kVal);
    std::snprintf(b, sizeof(b), "%d files   (%.1f MB)", s_icons.files, mb(s_icons.bytes)); kv("Icons", b, kVal);
    if (!JournalRewardDiag().empty()) kv("Reward fetch", JournalRewardDiag().c_str(), kVal);

    // Seed-source breakdown -- proves the from-memory pack path is actually in use (per-pack open status is in the
    // Data bootstrap section above). A cached file shadows the pack, so clear the caches to force fresh seeds.
    const ImageCache::PackInfo pk = ImageCache::GetPackInfo();
    std::snprintf(b, sizeof(b), "%d from pack   %d from loose files   %d downloaded", pk.packSeeded, pk.looseSeeded, pk.downloaded);
    kv("Seeded this session", b, kVal);
    // Live addon-owned texture VRAM: total decoded footprint + the evictable (tile/icon/wiki) subset the byte
    // budget bounds. The evictable figure is what auto-eviction keeps under budgetMB.
    const OwnedTex::MemInfo vm = OwnedTex::Mem();
    std::snprintf(b, sizeof(b), "%d textures   %.1f MB   (%.1f / %.0f MB evictable)",
                  vm.textures, mb((long long)vm.bytes), mb((long long)vm.evictableBytes), mb((long long)vm.budgetBytes));
    kv("Image VRAM", b, vm.evictableBytes > vm.budgetBytes ? IM_COL32(255, 200, 120, 255) : kVal);
    ImGui::Spacing();
    if (Gw2Ui::Button(s_arm == 1 ? "Confirm: clear tiles" : "Clear map tiles", 200.f, 30.f))
    { if (s_arm == 1) { ImageCache::Clear(ImageCache::Ns::Tiles); refresh(); s_arm = 0; } else s_arm = 1; }
    ImGui::SameLine();
    if (Gw2Ui::Button("Refresh", 100.f, 30.f)) { refresh(); s_arm = 0; }
    // Downloaded images (icons + wiki art): FREE the decoded GPU memory (the disk cache stays, so they re-load
    // instantly) vs CLEAR the on-disk storage (deletes the downloaded files -> re-downloads on next view).
    if (Gw2Ui::Button(s_arm == 2 ? "Confirm: free memory" : "Free image memory", 200.f, 30.f))
    { if (s_arm == 2) { ImageCache::FreeVram(ImageCache::Ns::Assets); s_arm = 0; } else s_arm = 2; }
    ImGui::SameLine();
    if (Gw2Ui::Button(s_arm == 3 ? "Confirm: clear storage" : "Clear image storage", 210.f, 30.f))
    { if (s_arm == 3) { ImageCache::Clear(ImageCache::Ns::Assets); refresh(); s_arm = 0; } else s_arm = 3; }
    if (s_arm == 2)
        SettingsParagraph("Frees the decoded images from video memory; the disk cache stays, so they re-load instantly. Click again to confirm.",
                          IM_COL32(255, 200, 120, 255));
    else if (s_arm)
        SettingsParagraph("Clearing deletes the on-disk files (re-downloads on next view, non-destructive). Click the same button again to confirm.",
                          IM_COL32(255, 200, 120, 255));
    Gw2Ui::EndCard();

    // ---- Notifications (test) ----
    Gw2Ui::SectionHeader("Notifications (test)", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-notify");
    SettingsParagraph("Fire a sample notification of each type to preview the toast + log styling and placement. "
                      "Disabled types are greyed out -- toggle them in Options > Notifications.", kDim);
    if (!app.config.notifyEnabled)
        SettingsParagraph("Notifications are OFF (master toggle). Enable in Options > Notifications to see them.",
                          IM_COL32(255, 200, 120, 255));
    ImGui::Spacing();

    struct NotifySample { Notify::Kind kind; const char* title; const char* body; };
    static const NotifySample kSamples[] = {
        { Notify::Kind::Zone,   "Now guiding: Queensdale", "Levels 1-15" },
        { Notify::Kind::Travel, "Waypoint copied",         "Paste in chat (or click) to teleport." },
        { Notify::Kind::Action, "Objective complete",      "Heart: Help Farmer Eda with her crops." },
        { Notify::Kind::Event,  "Event nearby",            "Defend the tavern from the Skritt raiders." },
        { Notify::Kind::Info,   "Tyrian Codex",            "This is a test notification." },
    };
    for (int i = 0; i < (int)(sizeof(kSamples) / sizeof(kSamples[0])); ++i)
    {
        const NotifySample& s = kSamples[i];
        const bool kindOn = app.config.notifyEnabled && app.config.notifyKind[(int)s.kind];
        char tip[96];
        std::snprintf(tip, sizeof(tip), kindOn ? "Fire a sample %s notification" : "%s notifications are disabled",
                      Notify::Meta(s.kind).name);
        if (i % 3 != 0) ImGui::SameLine();
        if (Gw2Ui::ActionButton(Notify::Meta(s.kind).name, 110.f, 28.f, Gw2Ui::ActionButtonVariant::Normal, tip, !kindOn))
            Notify::Push(s.kind, s.title, s.body);
    }
    ImGui::Spacing();
    if (Gw2Ui::ActionButton("Test all types", 160.f, 28.f, Gw2Ui::ActionButtonVariant::Primary,
                            "Fire one of every enabled type", !app.config.notifyEnabled))
        for (const NotifySample& s : kSamples) Notify::Push(s.kind, s.title, s.body);
    Gw2Ui::EndCard();

    // ---- Actions ----
    Gw2Ui::SectionHeader("Actions", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-actions");
    if (Gw2Ui::Button("Reset progress (this zone)", 230.f))
    {
        std::vector<std::string> ids; ids.reserve(app.state.zone.Steps.size());
        for (const Step& s : app.state.zone.Steps) ids.push_back(s.StepId);
        app.progress.ResetMany(app.state.currentChar, ids);
        app.state.manualTarget.clear(); app.follower.Reset(); app.gpsArrow.ResetEase();
    }
    Gw2Ui::EndCard();

    // ---- Character data ----
    Gw2Ui::SectionHeader("Character data", nullptr, SettingsText::Header);
    Gw2Ui::BeginCard("diag-chardata");
    SettingsParagraph("Per-character data the addon has stored (progress, profiles, sessions, levels, favorites). "
                      "Renamed characters merge automatically (with an API key); use this to clear characters you've "
                      "deleted. Account-level totals are kept.", kDim);
    ImGui::Spacing();
    {
        const std::vector<std::string> names = CharData::AllCharacterNames(app);
        const AccountData::Model& acc = AccountData::Get();
        const bool haveRoster = acc.haveChars && app.api.HasPermission(Api::TokenPermission::Characters);
        auto inAccount = [&](const std::string& n) {
            for (const AccountData::AcctChar& c : acc.characters) if (c.name == n) return true;
            return false;
        };

        static std::string s_armPurge;     // name armed for delete (two-click confirm)
        static int         s_armClear = 0; // "clear all not in account" two-click confirm

        if (names.empty())
            Gw2Ui::Label("No stored character data.", kDim, false, nullptr, fs);

        for (const std::string& n : names)
        {
            const bool active = (n == app.state.currentChar);
            const bool stale  = haveRoster && !inAccount(n);
            const int  objs   = (int)app.progress.CompletedIds(n).size();
            const int  sess   = (int)app.sessionHistory.Sessions(n).size();
            const int  lvl    = app.characterLevel.CachedLevel(n);

            char foot[160];
            if (lvl > 0) std::snprintf(foot, sizeof(foot), "lv %d    %d obj    %d sessions", lvl, objs, sess);
            else         std::snprintf(foot, sizeof(foot), "%d obj    %d sessions", objs, sess);

            ImGui::PushID(n.c_str());
            const bool armed = (s_armPurge == n);
            const char* tip = active ? "The active character can't be purged"
                            : (armed ? "Click again to permanently delete this character's stored data"
                                     : "Delete this character's stored data");
            if (Gw2Ui::ActionButton(armed ? "Confirm delete" : "Delete", 130.f, 24.f,
                                    armed ? Gw2Ui::ActionButtonVariant::Danger : Gw2Ui::ActionButtonVariant::Normal,
                                    tip, active))
            {
                if (armed) { CharData::PurgeCharacter(app, n, false); s_armPurge.clear(); }
                else       { s_armPurge = n; }
            }
            ImGui::SameLine();
            const ImU32 col = stale ? IM_COL32(255, 200, 120, 255) : kVal;
            const char* status = !haveRoster ? "" : (active ? "  (active)" : (stale ? "  (not in account)" : "  (in account)"));
            std::snprintf(b, sizeof(b), "%s%s     %s", n.c_str(), status, foot);
            Gw2Ui::Label(b, col, false, nullptr, fs);
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (haveRoster)
        {
            int staleCount = 0;
            for (const std::string& n : names) if (n != app.state.currentChar && !inAccount(n)) ++staleCount;
            char lbl[96];
            if (s_armClear) std::snprintf(lbl, sizeof(lbl), "Confirm: clear %d character(s)", staleCount);
            else            std::snprintf(lbl, sizeof(lbl), "Clear all not in my account (%d)", staleCount);
            if (Gw2Ui::ActionButton(lbl, 300.f, 28.f,
                                    s_armClear ? Gw2Ui::ActionButtonVariant::Danger : Gw2Ui::ActionButtonVariant::Primary,
                                    "Delete data for every character no longer on your account (keeps account-level totals)",
                                    staleCount == 0))
            {
                if (s_armClear)
                {
                    for (const std::string& n : names)
                        if (n != app.state.currentChar && !inAccount(n)) CharData::PurgeCharacter(app, n, false);
                    s_armClear = 0;
                }
                else { s_armClear = 1; }
            }
        }
        else
            SettingsParagraph("Add an API key (with the 'characters' scope) to flag characters no longer in your account.",
                              kDim);
    }
    Gw2Ui::EndCard();
}
