#pragma once
class App;

// -----------------------------------------------------------------------------------------------------
// EventWatch: the producer for event reminders. Once per frame (self-throttled) it asks EventSchedule how
// long until each belled event starts and pushes a Notify when a configured lead time is crossed.
//
// Runs from the Render orchestration rather than the Timers tab, because a reminder is worthless if it only
// fires while you happen to be looking at the board.
// -----------------------------------------------------------------------------------------------------
namespace EventWatch
{
    void Tick(App& app);
}
