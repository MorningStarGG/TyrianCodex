#include "app/SessionTracker.h"
#include "app/App.h"
#include "app/AccountData.h"

#include <algorithm>
#include <chrono>
#include <imgui.h>

namespace
{
    // Re-baseline the CURRENCY run: snapshot the wallet + anchor the currency timer to now. Only call with a FRESH
    // wallet (walletAt > 0) so we never diff the stale disk cache. (Used by entry, Reset, and Save.)
    void CaptureCurrencyBaseline(SessionEarnings& se)
    {
        const AccountData::Model& m = AccountData::Get();
        se.base.clear();
        for (const AccountData::WalletEntry& w : m.wallet) se.base[w.id] = w.value;
        se.haveBase = true;
        se.startedAt = ImGui::GetTime();
        se.startedEpoch = SessionTracker::NowEpoch();
    }

    // Baseline the PER-CHARACTER progress (level + objectives) at session start. Level may be 0/unknown here ->
    // Update() captures it on the first known frame (baseLevelKnown).
    void CaptureProgressBaseline(App& app, SessionEarnings& se)
    {
        const int lvl = app.state.charLevel;
        se.baseLevel = lvl > 0 ? lvl : 0;
        se.baseLevelKnown = lvl > 0;
        se.baseObjectives = (int)app.progress.CompletedIds(se.character).size();
        se.lastLevel = se.baseLevel;
        se.lastObjectives = se.baseObjectives;
    }

    void StartFresh(App& app, SessionEarnings& se, const std::string& character)
    {
        se = SessionEarnings();
        se.active = true;
        se.character = character;
        se.sessionStartedAt = ImGui::GetTime();              // overall session timer (entry; survives Reset/Save)
        se.sessionStartedEpoch = SessionTracker::NowEpoch();
        se.startedAt = se.sessionStartedAt;                  // currency timer starts together (until the wallet lands)
        se.startedEpoch = se.sessionStartedEpoch;
        CaptureProgressBaseline(app, se);
        const AccountData::Model& m = AccountData::Get();
        if (m.haveWallet && m.walletAt > 0.0) CaptureCurrencyBaseline(se);   // else Update() grabs it once fresh
    }
}

long long SessionTracker::NowEpoch()
{
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool SessionTracker::Tracking(const App& app)
{
    const SessionEarnings& se = app.state.sessionEarnings;
    return se.active && se.haveBase;
}

// FULL finalize -- the automatic session boundary (character switch / logout / addon unload). Records currency
// deltas (since the currency base) AND levels/objectives gained (since the entry progress base), then resets.
void SessionTracker::Finalize(App& app)
{
    SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active) { se = SessionEarnings(); return; }
    if (!se.character.empty())
    {
        SessionRecord rec;
        rec.startEpoch  = se.sessionStartedEpoch;                                   // the overall session
        rec.durationSec = (int)std::max(0.0, ImGui::GetTime() - se.sessionStartedAt);
        if (se.haveBase)
        {
            const AccountData::Model& m = AccountData::Get();
            if (m.haveWallet)
                for (const AccountData::WalletEntry& w : m.wallet)
                {
                    auto b = se.base.find(w.id);
                    const long long delta = w.value - (b != se.base.end() ? b->second : 0);
                    if (delta != 0) rec.deltas[w.id] = delta;
                }
        }
        rec.levelsGained     = (se.baseLevelKnown && se.lastLevel > se.baseLevel) ? (se.lastLevel - se.baseLevel) : 0;
        rec.objectivesGained = se.lastObjectives > se.baseObjectives ? (se.lastObjectives - se.baseObjectives) : 0;
        if (!rec.deltas.empty() || rec.levelsGained > 0 || rec.objectivesGained > 0)
            app.sessionHistory.Record(se.character, rec);
    }
    se = SessionEarnings();   // back to inactive
}

void SessionTracker::Update(App& app, bool inGameplay, const std::string& character)
{
    SessionEarnings& se = app.state.sessionEarnings;

    // Out of gameplay (loading screen / character select): PAUSE -- do not finalize or reset, so a map transition
    // never fragments a session. The session resumes on the next in-gameplay frame (real boundaries below).
    if (!inGameplay || character.empty()) return;

    if (se.active && se.character != character)   // a genuine character switch -> save the old session, start new
    {
        Finalize(app);
        app.state.undoStack.clear();   // the mark-done undo stack is per-character; never let "Back" un-tick onto the new one
    }

    if (!se.active) StartFresh(app, se, character);

    if (!se.haveBase)   // currency baseline waits for a FRESH wallet (walletAt > 0); kicked by the DomWallet poll
    {
        const AccountData::Model& m = AccountData::Get();
        if (m.haveWallet && m.walletAt > 0.0) CaptureCurrencyBaseline(se);
    }

    // Progress: capture the level baseline once the level is known, then track the latest values every frame so
    // Finalize has the OLD character's final counts even after a switch (per-character data vanishes otherwise).
    if (!se.baseLevelKnown && app.state.charLevel > 0) { se.baseLevel = app.state.charLevel; se.baseLevelKnown = true; }
    se.lastLevel = app.state.charLevel;
    se.lastObjectives = (int)app.progress.CompletedIds(se.character).size();
}

void SessionTracker::Reset(App& app)
{
    SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active) return;   // currency-only: re-baseline the wallet + currency timer; progress is untouched
    se.haveBase = false;
    se.base.clear();
    se.startedAt = ImGui::GetTime();
    se.startedEpoch = NowEpoch();
    const AccountData::Model& m = AccountData::Get();
    if (m.haveWallet && m.walletAt > 0.0) CaptureCurrencyBaseline(se);   // else Update() recaptures next frame
}

void SessionTracker::Save(App& app)
{
    SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active || !se.haveBase) return;   // currency-only: bank the currency run; nothing without a wallet base
    SessionRecord rec;
    rec.startEpoch  = se.startedEpoch;                                        // the currency run (not the session)
    rec.durationSec = (int)std::max(0.0, ImGui::GetTime() - se.startedAt);
    const AccountData::Model& m = AccountData::Get();
    if (m.haveWallet)
        for (const AccountData::WalletEntry& w : m.wallet)
        {
            auto b = se.base.find(w.id);
            const long long delta = w.value - (b != se.base.end() ? b->second : 0);
            if (delta != 0) rec.deltas[w.id] = delta;
        }
    // levelsGained/objectivesGained stay 0 -- progress is only recorded at the automatic full-session boundary,
    // so manual currency Saves never double-count it.
    if (!rec.deltas.empty()) app.sessionHistory.Record(se.character, rec);
    CaptureCurrencyBaseline(se);   // restart the currency run; the overall session + progress baseline continue
}

std::map<int, long long> SessionTracker::CurrentDeltas(const App& app)
{
    std::map<int, long long> out;
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active || !se.haveBase) return out;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveWallet) return out;
    for (const AccountData::WalletEntry& w : m.wallet)
    {
        auto b = se.base.find(w.id);
        const long long delta = w.value - (b != se.base.end() ? b->second : 0);
        if (delta != 0) out[w.id] = delta;
    }
    return out;
}

long long SessionTracker::CurrentDelta(const App& app, int currencyId)
{
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active || !se.haveBase) return 0;
    const AccountData::Model& m = AccountData::Get();
    if (!m.haveWallet) return 0;
    long long cur = 0; for (const AccountData::WalletEntry& w : m.wallet) if (w.id == currencyId) { cur = w.value; break; }
    auto b = se.base.find(currencyId);
    return cur - (b != se.base.end() ? b->second : 0);
}

int SessionTracker::DurationSec(const App& app)
{
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active) return 0;
    return (int)std::max(0.0, ImGui::GetTime() - se.startedAt);   // the currency run (resets on Reset/Save)
}

int SessionTracker::SessionDurationSec(const App& app)
{
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active) return 0;
    return (int)std::max(0.0, ImGui::GetTime() - se.sessionStartedAt);   // the overall session (entry)
}

int SessionTracker::LevelsGained(const App& app)
{
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active || !se.baseLevelKnown) return 0;
    return app.state.charLevel > se.baseLevel ? app.state.charLevel - se.baseLevel : 0;
}

int SessionTracker::ObjectivesGained(const App& app)
{
    const SessionEarnings& se = app.state.sessionEarnings;
    if (!se.active) return 0;
    const int now = (int)app.progress.CompletedIds(se.character).size();
    return now > se.baseObjectives ? now - se.baseObjectives : 0;
}
