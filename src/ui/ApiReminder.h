#pragma once
class App;

// The shared "add an API key" nudge used by every API-gated surface, so the wording + the deep-link live in
// ONE place. Pairs with UiStr::NoKey (the copy) and OpenSettingsTab/OpenOptionsSection (the deep-link). The
// no-key notification (Notify::Action::OpenApiSettings) also dispatches through OpenApiSettings().
namespace ApiReminder
{
    // Open the main window at Options -> Account / API.
    void OpenApiSettings(App& app);

    // Draw the reminder card for an API-gated surface (call only when there is no key).
    //   purpose    -- follows "for " in "Add an API key for <purpose>." (UiStr::NoKey).
    //   surfaceId  -- stable id for the per-session "Not now" dismiss (e.g. "journal").
    //   gated=true  (Items/Inventory: nothing renders without a key) -> the card IS the empty state; Open-only.
    //   gated=false (Journal/Atlas/Checklist: content still shows)    -> a dismissible banner with "Not now".
    // Returns true if the card was drawn (false if it was dismissed this session; the dismiss resets on reload).
    bool Card(App& app, const char* surfaceId, const char* purpose, bool gated);
}
