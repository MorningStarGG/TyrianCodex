#include "ui/viewer/ViewerEventPanels.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/viewer/ViewerLayout.h"        // ViewerScale (uniform layout-px zoom)
#include "ui/viewer/ViewerComponents.h"   // WithAlpha
#include "model/EventAreas.h"
#include "model/EventTimers.h"
#include "util/Coords.h"                   // ContinentToWorldXZ (player-relative area distance)
#include "ui/GuideViewer.h"                // SetClipboard, ViewerAlert (click-to-guide fallbacks)
#include "ui/wiki/WikiReader.h"            // Wiki::RequestOpen ("Open in Wiki" from the event menu)
#include "Shared.h"                        // MumbleLink, CurrentMapId (live player position / current map)
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <set>
#include <string>
#include <vector>
#include <cstdio>

// ---------------------------------------------------------------------------------------------------------
// ViewerEventPanels: the Timed Events / Event Chains card + the Nearby Event Areas card. Moved verbatim from
// ViewerStepRich.cpp so ONE implementation serves BOTH the Rich secondary panel and the Compact "Events" tab
// (no duplicated row/sort/format code). The two public entry points (ViewerEvents::DrawTimers / DrawAreas) are
// declared in ViewerEventPanels.h; everything else is file-local. The caller decides WHEN to draw.
// ---------------------------------------------------------------------------------------------------------

struct UpcomingEvent
{
    const EventTimer* Event = nullptr;
    int              UntilMinutes = 0;
    bool             Active = false;
    int              EndsInMinutes = -1;
};

static int UtcMinuteOfDay()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &t);
    return utc.tm_hour * 60 + utc.tm_min;
}

static UpcomingEvent NextOccurrence(const EventTimer& ev, int nowMinute)
{
    UpcomingEvent out;
    out.Event = &ev;
    int best = 1440;
    for (int start : ev.ScheduleMinutes)
    {
        const int s = ((start % 1440) + 1440) % 1440;
        const int until = (s - nowMinute + 1440) % 1440;
        const int elapsed = (nowMinute - s + 1440) % 1440;
        if (ev.DurationMinutes > 0 && elapsed < ev.DurationMinutes)
        {
            out.Active = true;
            out.UntilMinutes = 0;
            out.EndsInMinutes = ev.DurationMinutes - elapsed;
            return out;
        }
        best = std::min(best, until);
    }
    out.UntilMinutes = best == 1440 ? 0 : best;
    return out;
}

static std::string FormatTimerCountdown(const UpcomingEvent& up)
{
    char buf[32];
    if (up.Active)
    {
        if (up.EndsInMinutes > 0) std::snprintf(buf, sizeof(buf), "now %dm", up.EndsInMinutes);
        else                      std::snprintf(buf, sizeof(buf), "now");
        return buf;
    }
    const int minutes = std::max(0, up.UntilMinutes);
    if (minutes < 60)
        std::snprintf(buf, sizeof(buf), "%dm", minutes);
    else
        std::snprintf(buf, sizeof(buf), "%dh %02dm", minutes / 60, minutes % 60);
    return buf;
}

static std::string EventCategoryLabel(const std::string& category)
{
    if (category == "world_boss" || category == "hard_world_boss") return "Boss";
    if (category == "meta" || category == "map_meta" || category == "meta_event") return "Meta";
    if (category == "festival" || category == "seasonal") return "Festival";
    if (category == "dynamic_event" || category == "event_page") return "Event";
    if (category == "group_event") return "Group";
    if (category == "dry_top") return "Dry Top";
    if (category == "timeline") return "Timer";
    if (category == "public_instance") return "Instance";
    if (category.empty()) return "Event";
    std::string out = category;
    std::replace(out.begin(), out.end(), '_', ' ');
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

static ImU32 EventCategoryColor(const std::string& category)
{
    if (category == "world_boss" || category == "hard_world_boss" || category == "group_event") return IM_COL32(229, 112, 76, 235);
    if (category == "festival" || category == "seasonal") return IM_COL32(186, 132, 238, 235);
    if (category == "dynamic_event" || category == "event_page") return IM_COL32(120, 210, 150, 235);
    if (category == "defend" || category == "escort") return IM_COL32(235, 207, 105, 235);
    if (category == "combat") return IM_COL32(226, 100, 64, 235);
    if (category == "collect") return IM_COL32(222, 176, 82, 235);
    if (category == "interact" || category == "repair") return IM_COL32(115, 174, 224, 235);
    if (category == "map_wide" || category == "meta_event") return IM_COL32(255, 198, 82, 235);
    if (category == "dry_top") return IM_COL32(218, 174, 86, 235);
    if (category == "timeline" || category == "timer_page") return IM_COL32(122, 180, 224, 235);
    return IM_COL32(255, 205, 102, 235);
}

static std::string EventLevelText(int minLevel, int maxLevel, const std::string& label)
{
    // "Lv" is glued to the number with a NON-breaking space (U+00A0, UTF-8 C2 A0) so the level stays whole when
    // a narrow meta line wraps -- the wrap can still break at the normal spaces in the "zone  -  Lv x-y" join.
    if (!label.empty()) return "Lv\xC2\xA0" + label;
    if (minLevel > 0 && maxLevel > 0 && minLevel != maxLevel)
    {
        char buf[32]; std::snprintf(buf, sizeof(buf), "Lv\xC2\xA0%d-%d", minLevel, maxLevel);
        return buf;
    }
    if (minLevel > 0)
    {
        char buf[24]; std::snprintf(buf, sizeof(buf), "Lv\xC2\xA0%d", minLevel);
        return buf;
    }
    return "";
}

static void DrawEventGlyph(ImDrawList* dl, ImVec2 c, float size, ImU32 accent)
{
    const float r = size * 0.5f;
    dl->AddCircleFilled(c, r, IM_COL32(4, 6, 7, 210), 24);
    dl->AddCircle(c, r - 0.8f, WithAlpha(accent, 245), 24, 1.4f);
    dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.48f), ImVec2(c.x + r * 0.47f, c.y + r * 0.30f),
                          ImVec2(c.x - r * 0.47f, c.y + r * 0.30f), WithAlpha(accent, 140));
    dl->AddCircleFilled(c, r * 0.18f, IM_COL32(255, 232, 175, 245), 16);
}

static std::string EventDisplayName(const EventTimer& ev, const std::string& /*currentZoneName*/)
{
    return ev.Name;   // the builder now emits the canonical event name (wiki title) directly
}

static bool EventIsMapSlotTimer(const EventTimer& ev, const std::string& currentZoneName)
{
    return ev.Name == currentZoneName && !ev.WikiTitle.empty() &&
           (ev.WikiClassification == "timer_page" || ev.WikiClassification == "public_instance_or_timer");
}

static std::string EventTooltipText(const EventTimer& ev, const std::string& currentZoneName)
{
    if (EventIsMapSlotTimer(ev, currentZoneName))
    {
        std::string text = ev.WikiTitle + " has a scheduled slot in " + currentZoneName + ".";
        if (!ev.Description.empty())
            text += "\n\nThe wiki page is general timer context, not a map-specific event description.";
        return text;
    }
    return ev.Description;
}

static std::string EventScheduleSummary(const EventTimer& ev)
{
    if (ev.ScheduleMinutes.empty()) return "";
    std::string out = "Schedule: ";
    const int limit = std::min((int)ev.ScheduleMinutes.size(), 8);
    for (int i = 0; i < limit; ++i)
    {
        if (i) out += ", ";
        char buf[8];
        const int minute = ((ev.ScheduleMinutes[i] % 1440) + 1440) % 1440;
        std::snprintf(buf, sizeof(buf), "%02d:%02d", minute / 60, minute % 60);
        out += buf;
    }
    if ((int)ev.ScheduleMinutes.size() > limit) out += ", ...";
    return out;
}

// Click on an event (timer / chain): guide to its resolved spot (a personal target, like Alt+click), else --
// when there's no single point, e.g. a map-slot timer -- route INTO its zone (cross-zone travel, same as a
// recommended-zone "Travel here"), else copy the waypoint chat code as a last resort. Shared by both rows.
// Detail-card info for a clicked event = what its hover tooltip shows (minus the title): the wiki/slot blurb +
// schedule for a timed event; the description (or the static-area note) for an area.
static std::string EventTimerInfo(const EventTimer& ev, const std::string& zoneName)
{
    std::string s = EventTooltipText(ev, zoneName);
    const std::string sched = EventScheduleSummary(ev);
    if (!sched.empty()) { if (!s.empty()) s += "\n"; s += sched; }
    return s;
}

static std::string EventAreaInfo(const EventArea& a)
{
    return a.Description.empty() ? std::string("Static event area - active state isn't available from the public API.")
                                 : a.Description;
}

static void GuideToEvent(App& app, const std::string& name, const std::string& info, bool hasCoord,
                         float cx, float cy, const std::vector<uint32_t>& mapIds, const std::string& waypoint)
{
    if (hasCoord) { app.travel.SetPersonalTarget(cx, cy, name, info); return; }   // guide to the spot, labelled with the event
    const uint32_t cur = CurrentMapId();
    for (uint32_t m : mapIds)
        if (m != cur && app.zones.count(m)) { app.travel.StartTravel(m); return; }   // no point -> route into the zone
    if (!waypoint.empty()) { SetClipboard(waypoint); ViewerAlert("Copied waypoint - paste/click to teleport."); }
}

// ONE shared event-row renderer for BOTH the timed-event rows and the nearby-area rows (they used to be
// near-duplicate code that drifted apart -- the area row sized itself to the wrapped name while the timer row
// used a fixed height, so the timer name bled into its meta at narrow / half width). Everything responsive lives
// here: the row height is measured from the wrapped name + meta, the name is clipped to its column, and the
// right-hand pill is drawn ON TOP -- so nothing overlaps at any width. Callers fill an EventRow, then handle the
// returned click / hover themselves (each has its own travel action + tooltip).
struct EventRow
{
    ImU32       accent = 0;
    float       iconSize = 28.f;
    std::string name;
    float       titleFs = 14.f;
    bool        nameBold = true;
    std::string meta;
    float       metaFs = 14.f;
    std::string pill;            // empty -> no pill (event chains have no countdown)
    float       pillFs = 14.f;
    bool        active = false;  // timed-event "now" highlight (row tint + pill fill)
    const char* id = "##eventrow";
    int         row = 0;
};
struct EventRowHit { bool clicked = false; bool hovered = false; };

static EventRowHit DrawSharedEventRow(const EventRow& e)
{
    const float availW = Gw2Ui::ContentWidth();     // card-aware: keep the row + right pill inside the card border
                                                    // (== GetContentRegionAvail outside a card)
    const float pillW = e.pill.empty() ? 0.f : std::max(54.f, Gw2Ui::MeasureWidth(e.pill.c_str(), e.pillFs) + 18.f);
    const float textXOff = 9.f + e.iconSize + 10.f;
    const float textW = std::max(46.f, availW - textXOff - (pillW > 0.f ? pillW + 10.f : 10.f));
    const float lineH = std::max(e.titleFs + 3.f, Gw2Ui::MeasureWrappedHeight("Ag", e.titleFs, textW));
    const float titleH = std::min(Gw2Ui::MeasureWrappedHeight(e.name.c_str(), e.titleFs, textW), lineH * 2.05f);
    const float metaH = e.meta.empty() ? 0.f : std::max(e.metaFs + 3.f, Gw2Ui::MeasureWrappedHeight(e.meta.c_str(), e.metaFs, textW));
    const float rowH = std::max(46.f, 6.f + titleH + (metaH > 0.f ? metaH + 1.f : 0.f) + 6.f);

    ImGui::PushID(e.row);
    const Gw2Ui::RowHotspot row = Gw2Ui::Row(e.id, e.row, rowH, availW);
    ImGui::PopID();
    const ImVec2 p = row.min;
    EventRowHit hit;
    hit.clicked = row.clicked;
    hit.hovered = row.hovered;
    if (hit.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Active events get a LEFT-EDGE accent bar (+ the filled "now" pill below) rather than a persistent full-row
    // highlight, so the hover highlight stays clearly visible even when many rows are active (the global board).
    if (e.active) dl->AddRectFilled(ImVec2(p.x, p.y + 3.f), ImVec2(p.x + 3.5f, p.y + rowH - 3.f), WithAlpha(e.accent, 235), 1.5f);
    DrawEventGlyph(dl, ImVec2(p.x + 9.f + e.iconSize * 0.5f, p.y + rowH * 0.5f), e.iconSize, e.accent);

    const float textX = p.x + textXOff;
    const float titleY = p.y + 6.f;
    dl->PushClipRect(ImVec2(textX, titleY), ImVec2(textX + textW, titleY + titleH), true);
    Gw2Ui::LabelIn(ImVec2(textX, titleY), ImVec2(textX + textW, titleY + titleH), e.name.c_str(),
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(234, 228, 212, 255), e.nameBold, nullptr, e.titleFs, textW);
    dl->PopClipRect();
    if (metaH > 0.f)
        Gw2Ui::LabelIn(ImVec2(textX, titleY + titleH + 1.f), ImVec2(textX + textW, p.y + rowH - 4.f), e.meta.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(194, 182, 152, 245), false, nullptr, e.metaFs, textW);

    if (pillW > 0.f)
    {
        const ImVec2 ba = Gw2Ui::RowRight(row, pillW, 22.f, 5.f);
        const ImVec2 bb(p.x + availW - 5.f, ba.y + 22.f);
        dl->AddRectFilled(ba, bb, e.active ? WithAlpha(e.accent, 105) : IM_COL32(0, 0, 0, 120), 11.f);
        dl->AddRect(ba, bb, WithAlpha(e.accent, 168), 11.f);
        Gw2Ui::LabelIn(ba, bb, e.pill.c_str(), Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       IM_COL32(255, 232, 178, 255), true, nullptr, e.pillFs);
    }
    return hit;
}

// Right-click context menu for an event (a timeline bar or a list row): copy the waypoint chat link, copy the
// name, or open it in the in-overlay Wiki -- mirrors the item copy menu. `popupId` MUST be unique per event
// (callers pass one built from the stable event pointer) so the right event's menu opens.
static void EventMenu(const char* popupId, bool openNow, const std::string& name, const std::string& waypoint, const std::string& wikiTitle)
{
    if (name.empty() && waypoint.empty()) return;
    std::vector<Gw2Ui::MenuNode> nodes;
    if (!waypoint.empty()) nodes.push_back({ "Copy chat link", 1 });
    if (!name.empty())     nodes.push_back({ "Copy name", 2 });
    if (!name.empty())     nodes.push_back({ "Open in Wiki", 3 });
    const int a = Gw2Ui::ContextMenuTree(popupId, nodes, openNow);
    if      (a == 1) { SetClipboard(waypoint); ViewerAlert("Copied event waypoint link -- paste it in chat."); }
    else if (a == 2) { SetClipboard(name);     ViewerAlert("Copied event name."); }
    else if (a == 3) { Wiki::RequestOpen(wikiTitle.empty() ? name : wikiTitle); }
}

static void DrawEventTimerRow(App& app, const UpcomingEvent& up, float fs, int row, const std::string& currentZoneName)
{
    const EventTimer& ev = *up.Event;
    const std::string category = ev.SuggestedCategory.empty() ? ev.Category : ev.SuggestedCategory;
    const std::string displayName = EventDisplayName(ev, currentZoneName);
    const std::string level = EventLevelText(ev.LevelMin, ev.LevelMax, ev.LevelLabel);
    const float vs = ViewerScale(app);

    EventRow e;
    e.accent   = EventCategoryColor(category);
    e.iconSize = 28.f * vs;
    e.name     = displayName;               e.titleFs = std::max(12.f, fs - 2.f); e.nameBold = true;
    e.meta     = (ev.Subtitle.empty() ? std::string() : ev.Subtitle + "  -  ") +
                 (EventIsMapSlotTimer(ev, currentZoneName) ? currentZoneName : EventCategoryLabel(category)) +
                 (level.empty() ? std::string() : "  -  " + level);
    e.metaFs   = std::max(12.f, fs - 6.f);
    e.pill     = FormatTimerCountdown(up);   e.pillFs = std::max(12.f, fs - 4.f); e.active = up.Active;
    e.id = "##eventtimer"; e.row = row;
    const EventRowHit hit = DrawSharedEventRow(e);

    char mid[48]; std::snprintf(mid, sizeof(mid), "##evmenu_%p", (const void*)&ev);
    EventMenu(mid, hit.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), displayName, ev.Waypoint, ev.WikiTitle);
    if (hit.clicked) GuideToEvent(app, displayName, EventTimerInfo(ev, currentZoneName), ev.HasCoord, ev.ContinentX, ev.ContinentY, ev.MapIds, ev.Waypoint);
    if (hit.hovered)
    {
        Gw2Ui::TooltipBegin();
        Gw2Ui::TooltipTitle(ev.Name.c_str());
        if (!ev.Subtitle.empty()) Gw2Ui::TooltipMuted(ev.Subtitle.c_str());
        const std::string tooltip = EventTooltipText(ev, currentZoneName);
        if (!tooltip.empty()) Gw2Ui::TooltipText(tooltip.c_str());
        const std::string schedule = EventScheduleSummary(ev);
        if (!schedule.empty()) Gw2Ui::TooltipMuted(schedule.c_str());
        if (!ev.Waypoint.empty()) { const std::string wp = "Waypoint: " + ev.Waypoint; Gw2Ui::TooltipMuted(wp.c_str()); }
        Gw2Ui::TooltipMuted("Click: travel   -   Right-click: menu");
        Gw2Ui::TooltipEnd();
    }
}

static void DrawEventChainRow(App& app, const EventChain& chain, float fs, int row)
{
    const std::string category = chain.SuggestedCategory.empty() ? chain.Category : chain.SuggestedCategory;
    const std::string level = EventLevelText(chain.LevelMin, chain.LevelMax, chain.LevelLabel);
    const float vs = ViewerScale(app);

    EventRow e;
    e.accent   = EventCategoryColor(category);
    e.iconSize = 24.f * vs;
    e.name     = chain.Name;   e.titleFs = std::max(12.f, fs - 3.f); e.nameBold = false;
    e.meta     = EventCategoryLabel(category) + (level.empty() ? std::string() : "  -  " + level);
    e.metaFs   = std::max(10.f, fs - 8.f);
    e.pill     = "";           // event chains have no countdown
    e.id = "##eventchain"; e.row = row;
    const EventRowHit hit = DrawSharedEventRow(e);

    char mid[48]; std::snprintf(mid, sizeof(mid), "##evmenu_%p", (const void*)&chain);
    EventMenu(mid, hit.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), chain.Name, chain.Waypoint, std::string());
    if (hit.clicked) GuideToEvent(app, chain.Name, chain.Description, chain.HasCoord, chain.ContinentX, chain.ContinentY, chain.MapIds, chain.Waypoint);
    if (hit.hovered)
    {
        Gw2Ui::TooltipBegin();
        if (!chain.Description.empty()) Gw2Ui::TooltipText(chain.Description.c_str());
        Gw2Ui::TooltipMuted("Click: travel   -   Right-click: menu");
        Gw2Ui::TooltipEnd();
    }
}

// =========================================================================================================
// Timers TAB -- a GLOBAL, all-maps event board: a List view (filter + sort by ETA) and a GW2-wiki-style
// Timeline view (a row per event, colored blocks at each scheduled occurrence, a NOW line). Reuses every
// helper above (NextOccurrence / DrawEventTimerRow / category colour+label / GuideToEvent) so the tab and the
// map-scoped DrawTimers share ONE implementation. Tab-local UI state is file-static (transient by design).
// =========================================================================================================
namespace
{
    int  g_tView   = 0;     // 0 = List, 1 = Timeline
    int  g_tCat    = 0;     // 0 All, 1 Boss, 2 Meta, 3 Festival, 4 Other, 5 Cycles
    int  g_tWindow = 12;     // timeline horizon, hours (2 / 4 / 6 / 8 / 12); fills the panel width
    char g_tSearch[96] = "";

    int TimerCatGroup(const std::string& cat)    // raw category -> 1 Boss / 2 Meta / 3 Festival / 4 Other
    {
        if (cat == "world_boss" || cat == "hard_world_boss" || cat == "group_event") return 1;
        if (cat == "meta" || cat == "map_meta" || cat == "meta_event" || cat == "map_wide") return 2;
        if (cat == "festival" || cat == "seasonal") return 3;
        return 4;
    }
    // Case-insensitive substring match of the search box against any of the given fields. Empty search matches
    // everything. Shared by the timed-event rows AND the Cycles scope so the search box works in every scope.
    bool TimerSearchMatch(const std::string& a, const std::string& b = std::string(), const std::string& c = std::string())
    {
        if (!g_tSearch[0]) return true;
        std::string q = g_tSearch;
        for (char& ch : q) ch = (char)std::tolower((unsigned char)ch);
        auto has = [&](const std::string& s) {
            if (s.empty()) return false;
            std::string n = s;
            for (char& ch : n) ch = (char)std::tolower((unsigned char)ch);
            return n.find(q) != std::string::npos;
        };
        return has(a) || has(b) || has(c);
    }
    bool TimerMatches(const EventTimer& ev)
    {
        const std::string cat = ev.SuggestedCategory.empty() ? ev.Category : ev.SuggestedCategory;
        if (g_tCat != 0 && TimerCatGroup(cat) != g_tCat) return false;
        return TimerSearchMatch(ev.Name, ev.Subtitle, ev.WikiTitle);   // match name / subtitle / wiki title
    }

    void DrawTimersList(App& app, const std::vector<UpcomingEvent>& up)
    {
        ImGui::BeginChild("##timerlist", ImVec2(0.f, 0.f), false);
        const float fs = 22.f;
        int row = 0;
        for (const UpcomingEvent& u : up)
        {
            ImGui::PushID(u.Event->Key.c_str());
            DrawEventTimerRow(app, u, fs, row++, app.state.zone.Loaded ? app.state.zone.Name : std::string());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void DrawTimersTimeline(App& app, const std::vector<UpcomingEvent>& up, int now)
    {
        const float gutter   = 172.f;                  // event-name column
        const float rowH     = 28.f;
        const float headH    = 22.f;
        const int   pastMin  = 30;                      // show the last 30 min so the NOW line is INSET (not at the edge)
        const int   windowMin = g_tWindow * 60;         // forward horizon
        const int   totalMin = pastMin + windowMin;
        const std::string zone = app.state.zone.Loaded ? app.state.zone.Name : std::string();

        ImGui::BeginChild("##timeline", ImVec2(0.f, 0.f), false, ImGuiWindowFlags_HorizontalScrollbar);
        // FIXED scale (bars keep a readable size); the canvas is as wide as the window needs and SCROLLS horizontally
        // when it exceeds the panel -- it does not compress to fit. 12h (default) fills a typical panel then scrolls.
        const float pxPerMin = 116.f / 60.f;            // ~116 px / hour, constant across window sizes
        const float timeW    = (float)totalMin * pxPerMin;

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(gutter + timeW, headH + rowH * (float)up.size() + 8.f));   // reserve the scroll canvas

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float x0     = origin.x + gutter;         // left edge = now - pastMin
        const float bottom = origin.y + headH + rowH * (float)up.size();
        auto xAt = [&](int offMin) { return x0 + (float)(offMin + pastMin) * pxPerMin; };
        const float nowX = xAt(0);

        dl->AddRectFilled(ImVec2(x0, origin.y + headH), ImVec2(nowX, bottom), IM_COL32(0, 0, 0, 46));   // past shade
        for (int h = 0; h <= g_tWindow; ++h)            // forward hour gridlines + labels ("now", "+1h", ...)
        {
            const float gx = xAt(h * 60);
            dl->AddLine(ImVec2(gx, origin.y + headH), ImVec2(gx, bottom), IM_COL32(92, 84, 64, 80));
            char lbl[12];
            if (h == 0) std::snprintf(lbl, sizeof(lbl), "now");
            else        std::snprintf(lbl, sizeof(lbl), "+%dh", h);
            Gw2Ui::LabelDL(dl, ImVec2(gx + 3.f, origin.y), ImVec2(gx + 48.f, origin.y + 18.f), lbl,
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                           h == 0 ? IM_COL32(255, 216, 130, 255) : IM_COL32(180, 168, 140, 220), h == 0, nullptr, 14.f);
        }
        dl->AddLine(ImVec2(nowX, origin.y + headH), ImVec2(nowX, bottom), IM_COL32(255, 210, 120, 245), 2.5f);   // NOW line (inset)

        float y = origin.y + headH;
        for (const UpcomingEvent& u : up)
        {
            const EventTimer& ev = *u.Event;
            const std::string cat = ev.SuggestedCategory.empty() ? ev.Category : ev.SuggestedCategory;
            const ImU32 col = EventCategoryColor(cat);
            if ((int)((y - origin.y) / rowH) % 2 == 0)   // faint zebra
                dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + gutter + timeW, y + rowH), IM_COL32(255, 255, 255, 8));

            dl->PushClipRect(ImVec2(origin.x + 4.f, y), ImVec2(origin.x + gutter - 6.f, y + rowH), true);
            Gw2Ui::LabelDL(dl, ImVec2(origin.x + 6.f, y), ImVec2(origin.x + gutter - 6.f, y + rowH), ev.Name.c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(224, 216, 198, 245), false, nullptr, 16.f);
            dl->PopClipRect();

            bool rclick = false;   // any of this event's bars right-clicked -> open its context menu (after the bar loop)
            const int dur = ev.DurationMinutes > 0 ? ev.DurationMinutes : 10;
            for (int start : ev.ScheduleMinutes)
            {
                const int s = ((start % 1440) + 1440) % 1440;
                const int until = (s - now + 1440) % 1440;
                const int offsets[2] = { until, until - 1440 };   // next start + the one that may still be running
                for (int off : offsets)
                {
                    if (off + dur <= -pastMin || off >= windowMin) continue;   // fully outside [-pastMin, +window]
                    float bx0 = xAt(off), bx1 = xAt(off + dur);                 // active events show their elapsed tail
                    bx0 = std::max(bx0, x0); bx1 = std::min(bx1, x0 + timeW);
                    if (bx1 - bx0 < 2.f) bx1 = bx0 + 2.f;                   // keep brief events visible
                    const ImVec2 a(bx0, y + 3.f), b(bx1, y + rowH - 3.f);
                    const bool active = off <= 0;
                    dl->AddRectFilled(a, b, WithAlpha(col, active ? 210 : 150), 3.f);
                    dl->AddRect(a, b, WithAlpha(col, 235), 3.f);
                    if (ImGui::IsMouseHoveringRect(a, b))
                    {
                        Gw2Ui::TooltipBegin();
                        Gw2Ui::TooltipTitle(ev.Name.c_str());
                        char tb[48];
                        if (active) std::snprintf(tb, sizeof(tb), "Active now");
                        else        std::snprintf(tb, sizeof(tb), off < 60 ? "in %dm" : "in %dh %02dm", off < 60 ? off : off / 60, off % 60);
                        Gw2Ui::TooltipMuted(tb);
                        if (!ev.Waypoint.empty())
                        {
                            const std::string wp = "Waypoint: " + ev.Waypoint; Gw2Ui::TooltipMuted(wp.c_str());
                            Gw2Ui::TooltipMuted("Left-click: travel   -   Right-click: menu");
                        }
                        Gw2Ui::TooltipEnd();
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            GuideToEvent(app, ev.Name, EventTimerInfo(ev, zone), ev.HasCoord, ev.ContinentX, ev.ContinentY, ev.MapIds, ev.Waypoint);
                        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                            rclick = true;
                    }
                }
            }
            char mid[48]; std::snprintf(mid, sizeof(mid), "##evmenu_%p", (const void*)&ev);
            EventMenu(mid, rclick, ev.Name, ev.Waypoint, ev.WikiTitle);
            y += rowH;
        }
        ImGui::EndChild();
    }

    // Cycles scope: world clocks (day/night, resets) + PvP automated tournaments, grouped by their cycle group.
    // These are scheduled non-event timers -- no waypoint, no travel; just the next-occurrence countdown.
    void DrawTimersCycles(App& app, int now)
    {
        const float vs = ViewerScale(app);
        struct CGroup { std::string name; std::vector<const CycleTimer*> items; ImU32 accent; };
        std::vector<CGroup> groups;
        auto addAll = [&](const std::vector<CycleTimer>& list, ImU32 accent) {
            for (const CycleTimer& c : list)
            {
                if (!TimerSearchMatch(c.Name, c.CycleGroup)) continue;   // honor the search box in the Cycles scope too
                CGroup* g = nullptr;
                for (CGroup& gg : groups) if (gg.name == c.CycleGroup) { g = &gg; break; }
                if (!g) { groups.push_back({ c.CycleGroup, {}, accent }); g = &groups.back(); }
                g->items.push_back(&c);
            }
        };
        addAll(app.eventTimers.Cycles(), IM_COL32(122, 180, 224, 235));   // world clocks: blue
        addAll(app.eventTimers.Pvp(),    IM_COL32(214, 150, 120, 235));   // PvP tournaments: warm
        if (groups.empty()) { Gw2Ui::Label(g_tSearch[0] ? "No cycles match the search." : "No cycle timers loaded.", Gw2Ui::kTextDim, false, nullptr, 16.f); return; }

        int rowIdx = 0;
        for (const CGroup& g : groups)
        {
            if (g.items.size() > 1) Gw2Ui::SectionHeader(g.name.c_str());   // single-member groups (resets) need no header
            for (const CycleTimer* c : g.items)
            {
                int best = 1440; bool any = false;
                if (c->Recurrence == "weekly")
                {
                    // weekly reset: count down to the next Monday at the scheduled time, UTC (GW2 weekly reset)
                    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    std::tm gm{}; gmtime_s(&gm, &t);
                    const int nowMin = gm.tm_hour * 60 + gm.tm_min;
                    const int sched = c->ScheduleMinutes.empty() ? 450 : (((c->ScheduleMinutes[0] % 1440) + 1440) % 1440);
                    const int daysToMon = ((1 - gm.tm_wday) + 7) % 7;   // tm_wday: Sun=0, Mon=1
                    best = daysToMon * 1440 + sched - nowMin;
                    if (best <= 0) best += 7 * 1440;
                    any = true;
                }
                else
                {
                    for (int s : c->ScheduleMinutes) { best = std::min(best, ((s % 1440) - now + 1440) % 1440); any = true; }
                }
                char pill[24];
                if (!any)             std::snprintf(pill, sizeof(pill), "--");
                else if (best == 0)   std::snprintf(pill, sizeof(pill), "now");
                else if (best < 60)   std::snprintf(pill, sizeof(pill), "%dm", best);
                else if (best < 1440) std::snprintf(pill, sizeof(pill), "%dh %02dm", best / 60, best % 60);
                else                  std::snprintf(pill, sizeof(pill), "%dd %dh", best / 1440, (best % 1440) / 60);

                EventRow e;
                e.accent = g.accent;  e.iconSize = 22.f * vs;
                e.name = (c->Name.empty() ? g.name : c->Name);  e.titleFs = 20.f;  e.nameBold = true;
                e.meta = "";  e.metaFs = 14.f;
                e.pill = pill;  e.pillFs = 18.f;  e.active = (any && best == 0);
                e.id = "##cyclerow";  e.row = rowIdx++;
                const EventRowHit hit = DrawSharedEventRow(e);
                if (hit.hovered)
                {
                    Gw2Ui::TooltipBegin();
                    Gw2Ui::TooltipTitle((c->Name.empty() ? g.name : c->Name).c_str());
                    if (c->Recurrence == "weekly")     Gw2Ui::TooltipMuted("Weekly -- Mondays 07:30 UTC");
                    else if (c->Recurrence == "daily") Gw2Ui::TooltipMuted("Daily -- 00:00 UTC");
                    else
                    {
                        std::string sched;
                        for (size_t i = 0; i < c->ScheduleMinutes.size() && i < 8; ++i)
                        {
                            char hm[8]; const int m = ((c->ScheduleMinutes[i] % 1440) + 1440) % 1440;
                            std::snprintf(hm, sizeof(hm), "%02d:%02d", m / 60, m % 60);
                            sched += (i ? ", " : "") + std::string(hm);
                        }
                        if (!sched.empty()) Gw2Ui::TooltipMuted(("Schedule: " + sched).c_str());
                    }
                    Gw2Ui::TooltipEnd();
                }
            }
        }
    }
}

void ViewerEvents::DrawTimersTab(App& app)
{
    if (!app.eventTimers.Loaded()) { Gw2Ui::Label("Loading event timers...", Gw2Ui::kTextSub, false, nullptr, 18.f); return; }

    // Row 1: the search box fills the left; the List/Timeline toggle (and, in Timeline view, the 5 timeline-range chips (2/4/6/8/12h))
    // ride the right of the SAME row. (The search box used to share the category-chip row, but the 6th "Cycles"
    // chip filled that row and pushed the SameLine'd search box off the right edge -> it vanished.)
    const float topGap = 8.f;
    const float listTimeW = 104.f + 6.f + 122.f;                                 // List + gap + Timeline chips
    const float winW = (g_tView == 1) ? (topGap + 5 * 44.f + 4 * 4.f) : 0.f;      // + the 5 timeline-range chips (2/4/6/8/12h) (Timeline view)
    const float rowStartX = ImGui::GetCursorPosX();
    const float rowAvail  = ImGui::GetContentRegionAvail().x;
    const float rightW    = listTimeW + winW;                                     // the right cluster, pinned to the row's right edge
    const float searchW   = std::max(180.f, rowAvail - rightW - topGap);
    Gw2Ui::SearchBox("##timersearch", g_tSearch, sizeof(g_tSearch), searchW, "Search events...");
    // Pin the cluster by EXPLICIT x, not SameLine: SearchBox advances the cursor by only (width - iconW) -- it
    // excludes its magnifier/X column -- so a plain SameLine slides List back under the search icon.
    ImGui::SameLine();
    ImGui::SetCursorPosX(rowStartX + rowAvail - rightW);
    const Gw2Ui::ChipItem viewChips[2] = { { "List", nullptr, {}, 104.f }, { "Timeline", nullptr, {}, 122.f } };
    Gw2Ui::ChipFlow("tview", viewChips, 2, &g_tView, 32.f, 18.f, 6.f);
    if (g_tView == 1)
    {
        const int hrs[5] = { 2, 4, 6, 8, 12 };
        char winLbl[5][8];
        Gw2Ui::ChipItem winChips[5];
        int winSel = 0;
        for (int i = 0; i < 5; ++i)
        {
            std::snprintf(winLbl[i], sizeof(winLbl[i]), "%dh", hrs[i]);
            winChips[i] = Gw2Ui::ChipItem{ winLbl[i], nullptr, {}, 44.f };
            if (g_tWindow == hrs[i]) winSel = i;
        }
        ImGui::SameLine(0.f, topGap);
        if (Gw2Ui::ChipFlow("twin", winChips, 5, &winSel, 32.f, 16.f, 4.f)) g_tWindow = hrs[winSel];
    }
    // Row 2: the six category chips, scaled to fill the row.
    const Gw2Ui::ChipItem catChips[6] = {
        { "All" }, { "Bosses" }, { "Metas" }, { "Festival" }, { "Other" }, { "Cycles" } };
    Gw2Ui::ChipRow("tcat", catChips, 6, &g_tCat, 30.f, 16.f, 5.f, 56.f);
    Gw2Ui::Divider(0.f);

    const int now = UtcMinuteOfDay();
    if (g_tCat == 5) { DrawTimersCycles(app, now); return; }   // Cycles scope -- world clocks + PvP, grouped (no travel)
    std::vector<UpcomingEvent> up;
    up.reserve(256);
    for (const EventTimer& ev : app.eventTimers.Events())
    {
        if (ev.ScheduleMinutes.empty() || !TimerMatches(ev)) continue;
        up.push_back(NextOccurrence(ev, now));
    }
    std::sort(up.begin(), up.end(), [](const UpcomingEvent& a, const UpcomingEvent& b) {
        if (a.Active != b.Active) return a.Active > b.Active;               // active first
        if (a.Active) return a.EndsInMinutes < b.EndsInMinutes;            // active: soonest-ending first (not A-Z)
        if (a.UntilMinutes != b.UntilMinutes) return a.UntilMinutes < b.UntilMinutes;   // upcoming: soonest first
        return a.Event->Name < b.Event->Name;
    });
    if (up.empty()) { Gw2Ui::Label("No events match the filters.", Gw2Ui::kTextDim, false, nullptr, 16.f); return; }

    if (g_tView == 0) DrawTimersList(app, up);
    else              DrawTimersTimeline(app, up, now);
}

int ViewerEvents::DrawTimers(App& app, float fs)
{
    if (!app.eventTimers.Loaded() || !app.state.zone.Loaded) return 0;

    const int now = UtcMinuteOfDay();
    std::vector<UpcomingEvent> upcoming;
    for (const EventTimer* ev : app.eventTimers.EventsForMap(app.state.zone.MapId))
    {
        if (!ev || ev->ScheduleMinutes.empty()) continue;
        upcoming.push_back(NextOccurrence(*ev, now));
    }
    std::sort(upcoming.begin(), upcoming.end(), [](const UpcomingEvent& a, const UpcomingEvent& b) {
        if (a.Active != b.Active) return a.Active > b.Active;
        if (a.UntilMinutes != b.UntilMinutes) return a.UntilMinutes < b.UntilMinutes;
        return a.Event->Name < b.Event->Name;
    });

    std::vector<const EventChain*> chains = app.eventTimers.ChainsForMap(app.state.zone.MapId);
    if (upcoming.empty() && chains.empty()) return 0;

    ImGui::Spacing();
    Gw2Ui::BeginAccentCard("event-timers", 0.f, IM_COL32(255, 198, 82, 220),
                           IM_COL32(3, 5, 5, 126), IM_COL32(150, 118, 64, 128));
    char right[32];
    std::snprintf(right, sizeof(right), upcoming.empty() ? "%d chains" : "next %d", upcoming.empty() ? (int)chains.size() : (int)upcoming.size());
    Gw2Ui::SectionHeader(upcoming.empty() ? "Event Chains" : "Timed Events", right, fs - 3.f);

    int rowsDrawn = 0;
    const int timerLimit = std::min((int)upcoming.size(), 5);
    for (int i = 0; i < timerLimit; ++i)
    {
        ImGui::PushID(upcoming[i].Event->Key.c_str());
        DrawEventTimerRow(app, upcoming[i], fs, i, app.state.zone.Name);
        ImGui::PopID();
        ++rowsDrawn;
    }

    if (timerLimit == 0 && !chains.empty())
    {
        const int chainLimit = std::min((int)chains.size(), 5);
        for (int i = 0; i < chainLimit; ++i)
        {
            ImGui::PushID(chains[i]->Key.c_str());
            DrawEventChainRow(app, *chains[i], fs, i);
            ImGui::PopID();
            ++rowsDrawn;
        }
    }
    else if (!chains.empty() && timerLimit < 4)
    {
        ImGui::Spacing();
        Gw2Ui::Label("Untimed chains", IM_COL32(205, 190, 150, 245), true, nullptr, fs - 5.f);
        const int chainLimit = std::min((int)chains.size(), 4 - timerLimit);
        for (int i = 0; i < chainLimit; ++i)
        {
            ImGui::PushID(chains[i]->Key.c_str());
            DrawEventChainRow(app, *chains[i], fs, i + timerLimit);
            ImGui::PopID();
            ++rowsDrawn;
        }
    }

    Gw2Ui::EndCard();
    return rowsDrawn;
}

struct NearbyEventArea
{
    const EventArea* Area = nullptr;
    float DistanceMeters = 0.f;
    float EdgeDistanceMeters = 0.f;
};

static bool EventAreaLevelFitsZone(const EventArea& area, const Zone& zone)
{
    if (area.Level <= 0) return false;
    if (zone.MinLevel > 0 && area.Level + 5 < zone.MinLevel) return false;
    if (zone.MaxLevel > 0 && area.Level > zone.MaxLevel + 10 && zone.MaxLevel < 80) return false;
    return true;
}

static std::string EventAreaDistanceText(float edgeDistanceMeters)
{
    if (edgeDistanceMeters <= 8.f) return "near";
    char buf[32];
    if (edgeDistanceMeters < 1000.f)
        std::snprintf(buf, sizeof(buf), "%.0fm", edgeDistanceMeters);
    else
        std::snprintf(buf, sizeof(buf), "%.1fkm", edgeDistanceMeters / 1000.f);
    return buf;
}

static int EventAreaCategoryPriority(const std::string& category)
{
    if (category == "meta_event" || category == "map_wide") return 0;
    if (category == "group_event") return 1;
    if (category == "defend" || category == "escort") return 2;
    return 3;
}

static std::string EventAreaMetaText(const EventArea& area)
{
    const std::string level = area.Level > 0 ? EventLevelText(area.Level, area.Level, "") : std::string();
    return EventCategoryLabel(area.Category) + (level.empty() ? std::string() : "  -  " + level);
}

static void DrawEventAreaRow(App& app, const NearbyEventArea& nearby, float fs, int row)
{
    const EventArea& area = *nearby.Area;
    const float vs = ViewerScale(app);

    EventRow e;
    e.accent   = EventCategoryColor(area.Category);
    e.iconSize = 25.f * vs;
    e.name     = area.Name;   e.titleFs = std::max(12.f, fs - 3.f); e.nameBold = false;
    e.meta     = EventAreaMetaText(area);
    e.metaFs   = std::max(10.f, fs - 8.f);
    e.pill     = EventAreaDistanceText(nearby.EdgeDistanceMeters); e.pillFs = std::max(11.f, fs - 7.f);
    e.id = "##eventarea"; e.row = row;
    const EventRowHit hit = DrawSharedEventRow(e);

    if (hit.clicked) app.travel.SetPersonalTarget(area.ContinentX, area.ContinentY, area.Name, EventAreaInfo(area));   // guide to the event area
    if (hit.hovered)
    {
        Gw2Ui::TooltipBegin();
        const std::string title = area.WikiTitle.empty() ? area.Name : area.WikiTitle;
        Gw2Ui::TooltipTitle(title.c_str());
        Gw2Ui::TooltipMuted("Static event area. Active state is not available from the public API.");
        if (!area.Description.empty())
            Gw2Ui::TooltipText(area.Description.c_str());
        if (area.RadiusMeters > 0.f)
        {
            char rb[32]; std::snprintf(rb, sizeof(rb), "Area radius: %.0f m", area.RadiusMeters);
            Gw2Ui::TooltipMuted(rb);
        }
        Gw2Ui::TooltipEnd();
    }
}

int ViewerEvents::DrawAreas(App& app, int currentIndex, float fs, int maxRows)
{
    if (!app.eventAreas.Loaded() || !app.state.zone.Loaded ||
        currentIndex < 0 || currentIndex >= (int)app.state.zone.Steps.size() || maxRows <= 0)
        return 0;

    // Rank by distance from the PLAYER's live world position (the same MumbleLink avatar source the arrow +
    // hybrid-nearest routing use), NOT the current objective -- "Nearby" should mean near YOU, matching what you
    // see in-game. Falls back to the current objective's position only when MumbleLink isn't available.
    float originX, originZ;
    if (MumbleLink) { originX = MumbleLink->AvatarPosition.X; originZ = MumbleLink->AvatarPosition.Z; }
    else { const Step& cur = app.state.zone.Steps[currentIndex];
           Coords::ContinentToWorldXZ(cur.CX, cur.CY, app.state.zone.ContRect, app.state.zone.MapRect, originX, originZ); }

    std::vector<NearbyEventArea> ranked;
    for (const EventArea* area : app.eventAreas.AreasForMap(app.state.zone.MapId))
    {
        if (!area || !area->ViewerEligible) continue;
        if (area->WorldX == 0.f && area->WorldZ == 0.f) continue;   // missing world coords -> can't rank
        if (!EventAreaLevelFitsZone(*area, app.state.zone)) continue;
        const float dist = std::hypot(area->WorldX - originX, area->WorldZ - originZ);
        const float edge = (area->RadiusMeters > 0.f) ? std::max(0.f, dist - area->RadiusMeters) : dist;
        ranked.push_back({ area, dist, edge });
    }
    if (ranked.empty()) return 0;

    std::sort(ranked.begin(), ranked.end(), [](const NearbyEventArea& a, const NearbyEventArea& b) {
        if (std::fabs(a.EdgeDistanceMeters - b.EdgeDistanceMeters) > 0.01f)
            return a.EdgeDistanceMeters < b.EdgeDistanceMeters;
        const int ap = EventAreaCategoryPriority(a.Area->Category);
        const int bp = EventAreaCategoryPriority(b.Area->Category);
        if (ap != bp) return ap < bp;
        return a.Area->Name < b.Area->Name;
    });

    std::vector<NearbyEventArea> deduped;
    std::set<std::string> seenNames;
    for (const NearbyEventArea& area : ranked)
    {
        if (!seenNames.insert(area.Area->Name).second) continue;
        deduped.push_back(area);
        if ((int)deduped.size() >= maxRows) break;
    }
    if (deduped.empty()) return 0;

    ImGui::Spacing();
    Gw2Ui::BeginAccentCard("event-areas", 0.f, IM_COL32(120, 210, 150, 220),
                           IM_COL32(3, 5, 5, 126), IM_COL32(98, 142, 84, 128));
    char right[32];
    std::snprintf(right, sizeof(right), "%d near", (int)deduped.size());
    // the long parenthetical doesn't fit a narrow / half-width card -> short title there (full title stays on the wide viewer panel)
    const float headW = ImGui::GetContentRegionAvail().x;
    Gw2Ui::SectionHeader(headW < 360.f ? "Nearby Events" : "Nearby Events (might not actually be active)", right, fs - 3.f);

    int rowsDrawn = 0;
    for (const NearbyEventArea& area : deduped)
    {
        ImGui::PushID(area.Area->EventId.c_str());
        DrawEventAreaRow(app, area, fs, rowsDrawn);
        ImGui::PopID();
        ++rowsDrawn;
    }
    Gw2Ui::EndCard();
    return rowsDrawn;
}
