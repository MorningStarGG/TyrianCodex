#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Config;

// -----------------------------------------------------------------------------------------------------
// Notify: the ONE notification store. Everything that used to call Nexus's GUI_SendAlert pushes here
// instead (ZoneManager zone announce, the Travel toast callback, ViewerAlert). Two views read this store:
//   - Toast (src/ui/dashboard/Toast.cpp) renders the transient on-screen pop-ups.
//   - NotificationsWidget (the dashboard log) renders the persistent history with dismiss/clear.
// The catch-up fix lives here: Zone + Travel are COALESCING lanes -- a new push in that lane replaces the
// live item (swap text + reset timer) instead of stacking, so a burst of zone hops shows ONE updating toast
// rather than a back-to-back backlog. Action/Event/Info stack (with a short identical-text de-dupe).
// All pushes happen on the render thread (ImGui time), so no locking is needed.
// -----------------------------------------------------------------------------------------------------
namespace Notify
{
    enum class Kind { Zone, Travel, Action, Event, Info, COUNT };
    constexpr int KindCount = (int)Kind::COUNT;

    // Optional click-through. A notification with a non-None action is clickable (in the toast AND the dashboard
    // log): the UI layer dispatches it (it owns App&). Extend as more click targets are wanted.
    // ShowEvent/OpenTimers carry the event in `Item::payload` (the EventTimer::Key) -- a bare enum cannot say
    // WHICH event a reminder was about, and a reminder you cannot act on is half a feature.
    enum class Action { None, OpenApiSettings, ShowEvent, OpenTimers };

    struct Item
    {
        uint64_t    id      = 0;
        Kind        kind    = Kind::Info;
        std::string title;
        std::string body;
        uint32_t    icon    = 0;        // gw2dat asset id override (0 = the kind's default icon)
        double      created = 0.0;      // ImGui time when (re)posted
        double      expires = 0.0;      // created + ttl; the toast is live until then (hover extends it)
        bool        dismissed = false;
        Action      action  = Action::None;   // click target (None = not clickable)
        std::string payload;                  // action argument (e.g. the event key for ShowEvent)
    };

    // Per-kind metadata: display name, accent colour (ImU32), default icon (gw2dat id), and whether the kind
    // coalesces (replace the live item in its lane vs. stack a new one).
    struct KindMeta { const char* name; unsigned int accent; uint32_t icon; bool coalesce; };

    void Init(const Config* cfg);     // hand the store the config (master + per-kind enables + ttl)

    // Push a notification. Suppressed (returns 0) when the master toggle or this kind's toggle is off.
    // Coalescing kinds replace their live lane item; others stack (identical title+body within ~1.2s deduped).
    uint64_t Push(Kind kind, std::string title, std::string body = std::string(), uint32_t icon = 0,
                  Action action = Action::None, std::string payload = std::string());

    const std::vector<Item>& History();              // newest-last capped ring (the log widget reverses it)
    std::vector<const Item*> Live(double now);       // unexpired + undismissed, oldest-first (the toast stack)
    void     ExtendLive(uint64_t id, double now);    // hover-pause: keep this toast on screen a little longer
    void     Dismiss(uint64_t id);
    void     ClearAll();
    int      UnreadCount();
    void     MarkAllRead();
    uint64_t Version();                              // bumped on every mutation (immediate-mode cache token)

    const KindMeta& Meta(Kind k);
}
