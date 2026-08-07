#include "Notify.h"
#include "app/Config.h"
#include <imgui.h>
#include <algorithm>

namespace
{
    const Config*           g_cfg = nullptr;
    std::vector<Notify::Item> g_items;        // created-order; live = (!dismissed && expires > now)
    uint64_t                g_nextId = 1;
    uint64_t                g_ver    = 0;
    int                     g_unread = 0;
    constexpr size_t        kRingCap = 50;

    // Per-kind look + behaviour. accent = the left bar colour; coalesce = replace the live lane item (the
    // catch-up fix) vs. stack. Icons left 0 for now (the toast draws an accent dot); real gw2dat ids can be
    // dropped in during polish without touching callers.
    const Notify::KindMeta kMeta[Notify::KindCount] = {
        /* Zone   */ { "Zone",   IM_COL32(235, 190,  85, 255), 0, true  },
        /* Travel */ { "Travel", IM_COL32(110, 180, 230, 255), 0, true  },
        /* Action */ { "Action", IM_COL32(140, 205, 130, 255), 0, false },
        /* Event  */ { "Event",  IM_COL32(240, 150,  70, 255), 0, false },
        /* Info   */ { "Info",   IM_COL32(205, 198, 180, 255), 0, false },
    };

    double Ttl() { const int t = g_cfg ? g_cfg->notifyTtl : 4; return (double)(t < 1 ? 1 : t); }

    // Drop the oldest fully-gone (dismissed or long-expired) entries once the ring is over cap. Never touches
    // a still-live toast, so it is safe to call from Push (no mid-frame pointer invalidation of Live()).
    void Compact(double now)
    {
        while (g_items.size() > kRingCap)
        {
            // erase the oldest entry that is not currently on screen; if all are live (unlikely at 50), drop front.
            auto it = std::find_if(g_items.begin(), g_items.end(),
                                   [&](const Notify::Item& i){ return i.dismissed || i.expires <= now; });
            g_items.erase(it != g_items.end() ? it : g_items.begin());
        }
    }
}

void Notify::Init(const Config* cfg) { g_cfg = cfg; }

uint64_t Notify::Push(Kind kind, std::string title, std::string body, uint32_t icon, Action action,
                      std::string payload)
{
    if (!g_cfg || !g_cfg->notifyEnabled) return 0;
    const int ki = (int)kind;
    if (ki < 0 || ki >= KindCount || !g_cfg->notifyKind[ki]) return 0;
    if (title.empty() && body.empty()) return 0;

    const double now = ImGui::GetTime();
    const double ttl = Ttl();

    // De-dupe an identical message repeated within ~1.2s (e.g. a setting that re-fires) -> just refresh it.
    if (!g_items.empty())
    {
        Item& last = g_items.back();
        if (last.kind == kind && !last.dismissed && last.title == title && last.body == body &&
            (now - last.created) < 1.2)
        {
            last.created = now; last.expires = now + ttl; last.icon = icon; last.action = action;
            last.payload = std::move(payload);
            ++g_unread; ++g_ver; return last.id;
        }
    }

    // Coalescing lane (Zone/Travel): if a toast of this kind is still on screen, REPLACE it in place and move
    // it to newest, so a burst shows ONE updating toast instead of a serial backlog (the catch-up fix).
    if (kMeta[ki].coalesce)
    {
        for (auto it = g_items.rbegin(); it != g_items.rend(); ++it)
        {
            if (it->kind == kind && !it->dismissed && it->expires > now)
            {
                Item live = *it;
                g_items.erase(std::next(it).base());     // remove from its position
                live.title = std::move(title); live.body = std::move(body); live.icon = icon;
                live.action = action; live.payload = std::move(payload);
                live.created = now; live.expires = now + ttl; live.dismissed = false;
                g_items.push_back(std::move(live));       // becomes the newest
                ++g_unread; ++g_ver;
                return g_items.back().id;
            }
        }
    }

    Item n;
    n.id = g_nextId++; n.kind = kind; n.title = std::move(title); n.body = std::move(body);
    n.icon = icon; n.created = now; n.expires = now + ttl; n.action = action; n.payload = std::move(payload);
    g_items.push_back(std::move(n));
    Compact(now);
    ++g_unread; ++g_ver;
    return g_items.back().id;
}

const std::vector<Notify::Item>& Notify::History() { return g_items; }

std::vector<const Notify::Item*> Notify::Live(double now)
{
    std::vector<const Item*> out;
    for (const Item& i : g_items)
        if (!i.dismissed && i.expires > now) out.push_back(&i);
    return out;   // oldest-first (push order)
}

void Notify::ExtendLive(uint64_t id, double now)
{
    for (Item& i : g_items)
        if (i.id == id) { i.expires = now + Ttl(); return; }
}

void Notify::Dismiss(uint64_t id)
{
    for (Item& i : g_items)
        if (i.id == id) { if (!i.dismissed) { i.dismissed = true; ++g_ver; } return; }
}

void Notify::ClearAll()
{
    if (g_items.empty()) return;
    g_items.clear(); g_unread = 0; ++g_ver;
}

int      Notify::UnreadCount() { return g_unread; }
void     Notify::MarkAllRead() { if (g_unread) { g_unread = 0; ++g_ver; } }
uint64_t Notify::Version()     { return g_ver; }

const Notify::KindMeta& Notify::Meta(Kind k)
{
    const int i = (int)k;
    return kMeta[(i >= 0 && i < KindCount) ? i : (int)Kind::Info];
}
