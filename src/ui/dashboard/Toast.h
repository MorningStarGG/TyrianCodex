#pragma once
class App;

// Toast: the transient on-screen notification pop-ups (the modern, self-rendered replacement for Nexus's
// GUI_SendAlert queue). Reads the Notify store, positions a small stack at the configured corner, animates
// slide-in + fade-out, caps the visible count, and pauses a toast's timer while the cursor hovers it.
// Drawn to the foreground draw list (no input-capturing window -> never blocks game clicks). Called once per
// frame at the end of Render().
namespace Toast { void Render(App& app); }
