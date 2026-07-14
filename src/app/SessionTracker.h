#pragma once
#include <map>
#include <string>
class App;

// The Session Tracker's hybrid lifecycle, operating purely on App state + the AccountData wallet (no MumbleLink /
// Nexus globals -- the caller passes the gameplay context), so both the per-frame loop and the UI Reset/Save
// buttons can drive it. A "session" baselines the wallet on character entry, accrues per-currency deltas as the
// wallet repolls, and is finalized into App::sessionHistory on logout / character switch / addon unload / manual
// Save. The baseline waits for a FRESH wallet (AccountData walletAt > 0) so it never diffs the stale disk cache.
namespace SessionTracker
{
    // Per-frame: inGameplay = in a playable map with MumbleLink; character = current character (empty if not in
    // gameplay). Starts/continues a session, captures the baseline once the wallet is fresh, and finalizes when
    // gameplay ends or the character changes.
    void Update(App& app, bool inGameplay, const std::string& character);

    void Reset(App& app);     // manual: re-baseline now (deltas zero out), same session continues
    void Save(App& app);      // manual: finalize the current session to history, then immediately start a fresh one
    void Finalize(App& app);  // record the active session to history (if worthwhile) + deactivate (call on unload)

    bool Tracking(const App& app);   // a session is active AND has captured its baseline (UI "counting" indicator)
    long long NowEpoch();            // wall-clock seconds since the Unix epoch (for record timestamps)

    // Live per-currency deltas for the ACTIVE session (current wallet minus baseline), non-zero entries only;
    // empty if no session / no baseline yet. Keyed by currency id (the UI orders via StaticData::Currencies()).
    std::map<int, long long> CurrentDeltas(const App& app);
    long long CurrentDelta(const App& app, int currencyId);   // one currency's live session delta (0 if none)
    int       DurationSec(const App& app);                    // CURRENCY run length in seconds (resets on Reset/Save)
    int       SessionDurationSec(const App& app);             // OVERALL session length (since character entry)
    int       LevelsGained(const App& app);                   // live character levels gained this session
    int       ObjectivesGained(const App& app);               // live checklist objectives completed this session
}
