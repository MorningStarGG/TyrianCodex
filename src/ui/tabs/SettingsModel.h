#pragma once
#include <cstddef>
#include <vector>
class App;

// ---- Settings model -------------------------------------------------------------------------------
// Each setting declares its section, display name, search keywords, and TYPE; the dispatcher (DrawSetting)
// renders the matching GW2 control (bool->Checkbox, float/int->Slider, enum->Dropdown, keybind->KeybindAssigner)
// so new settings automatically get the right control. The left menu picks a section; the search box filters
// across ALL sections by name + keywords. Lives in ui/tabs/SettingsModel.cpp; the Options tab consumes it.
enum
{
    SEC_GENERAL,
    SEC_ARROW,
    SEC_PANEL,
    SEC_ROUTING,
    SEC_TRAIL,
    SEC_MAPTRAIL,
    SEC_MARKERS,
    SEC_NOTIFY,
    SEC_ZONEDISPLAY,
    SEC_WIDGETS,
    SEC_DASHBOARD,
    SEC_HUD,
    SEC_INFO,
    SEC_LOADOUTS,
    SEC_WIKI,
    SEC_API,
    SEC_KEYS,
    SEC_DIAG,
    SEC_COUNT
};

extern const char *kSections[SEC_COUNT];

enum class SKind
{
    Bool,
    Float,
    Int,
    Enum,
    Keybind,
    String
};

struct Setting
{
    int section;
    const char *groupId = nullptr;   // optional subsection id within the section
    const char *groupName = nullptr; // optional subsection display name
    const char *key;                 // STABLE persistence key
    const char *name;
    const char *search; // extra keywords for the search box
    SKind kind;
    bool *vb = nullptr;
    float *vf = nullptr;
    float fmin = 0.f, fmax = 1.f;
    const char *ffmt = "%.0f";
    int *vi = nullptr;
    int imin = 0, imax = 100;
    int *ve = nullptr;
    const char *const *enames = nullptr;
    int ecount = 0;
    char *vk = nullptr;
    size_t kbuf = 0;
    char *vs = nullptr;
    size_t sbuf = 0;     // String: a GW2 TextBox (e.g. the API key)
    bool secret = false; // String: mask the text (password dots) with a reveal toggle
};

namespace SettingGroups
{
    // General
    inline constexpr const char *GeneralBehaviorId = "general_behavior";
    inline constexpr const char *GeneralBehaviorName = "Guide";
    inline constexpr const char *GeneralTextId = "general_text";
    inline constexpr const char *GeneralTextName = "Text & Fonts";

    // GPS Arrow
    inline constexpr const char *ArrowMainId = "arrow_main";
    inline constexpr const char *ArrowMainName = "Arrow";
    inline constexpr const char *ArrowGlowId = "arrow_glow";
    inline constexpr const char *ArrowGlowName = "Direct-objective Glow";

    // Guide Panel
    inline constexpr const char *PanelLayoutId = "panel_layout";
    inline constexpr const char *PanelLayoutName = "Layout";
    inline constexpr const char *PanelObjectiveId = "panel_objective";
    inline constexpr const char *PanelObjectiveName = "Objective Display";
    inline constexpr const char *PanelTextId = "panel_text";
    inline constexpr const char *PanelTextName = "Text";
    inline constexpr const char *PanelHoverTravelId = "panel_hover_travel";
    inline constexpr const char *PanelHoverTravelName = "Hover & Travel";

    // Routing
    inline constexpr const char *RoutingBehaviorId = "routing_behavior";
    inline constexpr const char *RoutingBehaviorName = "Route Behavior";
    inline constexpr const char *RoutingMapAssistId = "routing_map_assist";
    inline constexpr const char *RoutingMapAssistName = "Map Assist";
    inline constexpr const char *RoutingAutoTravelId = "routing_auto_travel";
    inline constexpr const char *RoutingAutoTravelName = "Auto Travel";
    inline constexpr const char *RoutingChatZoneId = "routing_chat_zone";
    inline constexpr const char *RoutingChatZoneName = "Chat / Zone Travel";

    // Route Trail
    inline constexpr const char *TrailAppearanceId = "trail_appearance";
    inline constexpr const char *TrailAppearanceName = "Appearance";
    inline constexpr const char *TrailFadeBehaviorId = "trail_fade_behavior";
    inline constexpr const char *TrailFadeBehaviorName = "Fade & Behavior";

    // Map Trails
    inline constexpr const char *MapTrailLinesId = "map_trail_lines";
    inline constexpr const char *MapTrailLinesName = "Route Lines";
    inline constexpr const char *MapTrailCirclesId = "map_trail_circles";
    inline constexpr const char *MapTrailCirclesName = "Objective Circles";

    // Markers
    inline constexpr const char *MarkersVisibilityId = "markers_visibility";
    inline constexpr const char *MarkersVisibilityName = "Visibility & Types";
    inline constexpr const char *MarkersAppearanceId = "markers_appearance";
    inline constexpr const char *MarkersAppearanceName = "Appearance";

    // Keybinds
    inline constexpr const char *KeysGuideId = "keys_guide";
    inline constexpr const char *KeysGuideName = "Guide";
    inline constexpr const char *KeysLoadoutId = "keys_loadout";
    inline constexpr const char *KeysLoadoutName = "Loadouts";
    inline constexpr const char *KeysDisplayId = "keys_display";
    inline constexpr const char *KeysDisplayName = "Display";

    // Notifications
    inline constexpr const char *NotifyPlacementId = "notify_placement";
    inline constexpr const char *NotifyPlacementName = "Placement";
    inline constexpr const char *NotifyDisplayId = "notify_display";
    inline constexpr const char *NotifyDisplayName = "Display";
    inline constexpr const char *NotifyTypesId = "notify_types";
    inline constexpr const char *NotifyTypesName = "Types";

    // Zone Display (the animated Region/Zone/Sector banner)
    inline constexpr const char *ZdBehaviorId = "zd_behavior";
    inline constexpr const char *ZdBehaviorName = "Behaviour";
    inline constexpr const char *ZdAppearanceId = "zd_appearance";
    inline constexpr const char *ZdAppearanceName = "Appearance";
    inline constexpr const char *ZdAnimationId = "zd_animation";
    inline constexpr const char *ZdAnimationName = "Animation";
    inline constexpr const char *ZdAreaId = "zd_area";
    inline constexpr const char *ZdAreaName = "Area";

    // Widgets
    inline constexpr const char *WidgetCurrentId = "widget_current";
    inline constexpr const char *WidgetCurrentName = "Current Objective";
    inline constexpr const char *WidgetZoneWpId = "widget_zone_wp";
    inline constexpr const char *WidgetZoneWpName = "Zone Waypoints";
    inline constexpr const char *WidgetSearchId = "widget_search";
    inline constexpr const char *WidgetSearchName = "Search";
    inline constexpr const char *WidgetRecommendedId = "widget_recommended";
    inline constexpr const char *WidgetRecommendedName = "Recommended Zones";
    inline constexpr const char *WidgetQuickNotesId = "widget_quick_notes";
    inline constexpr const char *WidgetQuickNotesName = "Quick Notes";
    inline constexpr const char *WidgetQuickControlsId = "widget_quick_controls";
    inline constexpr const char *WidgetQuickControlsName = "Quick Controls";
    inline constexpr const char *WidgetZoneHeaderId = "widget_zone_header";
    inline constexpr const char *WidgetZoneHeaderName = "Zone Header";
    inline constexpr const char *WidgetRouteArrowId = "widget_route_arrow";
    inline constexpr const char *WidgetRouteArrowName = "Route Arrow";
    inline constexpr const char *WidgetWvwId = "widget_wvw";
    inline constexpr const char *WidgetWvwName = "WvW Map Control";

    // Dashboard
    inline constexpr const char *DashboardProfilesId = "dashboard_profiles";
    inline constexpr const char *DashboardProfilesName = "Profiles";
    inline constexpr const char *DashboardPanelId = "dashboard_panel";
    inline constexpr const char *DashboardPanelName = "Panel Behavior";
    inline constexpr const char *DashboardWidgetsId = "dashboard_widgets";
    inline constexpr const char *DashboardWidgetsName = "Widget Layout";

    // HUD + Info Panel scalar rows are ungrouped; their custom section pages place them explicitly.

    // Diagnostics
    inline constexpr const char *DiagTogglesId = "diag_toggles";
    inline constexpr const char *DiagTogglesName = "Debug Toggles";
    inline constexpr const char *DiagLiveId = "diag_live";
    inline constexpr const char *DiagLiveName = "Live Diagnostics";
}

std::vector<Setting> Settings(App &app);                    // the full settings table (rebuilt by value each call)
bool DrawSetting(const Setting &s, float labelCol = 250.f); // render one control + its hover tooltip; true the frame it changes (labelCol = the page's shared, auto-sized control column)
bool NormalizeKeybinds(App &app, const char *preferredKey = nullptr); // enforce one action per non-empty keybind
// Render a whole section's model rows as grouped cards (used by the HUD / Info Panel section pages so their
// scalar settings look/behave like every other section). groupId != null renders just that subsection.
void DrawSettingSection(App &app, int section, const char *groupId = nullptr);
bool SettingMatches(const Setting &s, const char *qLower); // case-insensitive name/keyword search match
// The matcher behind it: every whitespace-separated term of `qLower` must appear in `hayLower` (already
// lowercase). Use this for anything else the Options search surfaces -- an ACTION that is not a model row
// still has to match the way rows do, or the same query finds the rows and misses it.
bool KeywordsMatch(const char *hayLower, const char *qLower);
// SaveSettings / LoadSettings are declared in ui/SettingsWindow.h (the lifecycle facade) and defined here.
