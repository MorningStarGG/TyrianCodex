#include "ui/tabs/OptionsTab.h"
#include "ui/SettingsWindow.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "Shared.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/Gw2Ui.h"
#include "ui/ApiScopes.h"
#include "ui/dashboard/Dashboard.h"
#include "ui/hud/Hud.h"
#include "ui/infopanel/InfoPanel.h"
#include "ui/infopanel/InfoData.h"
#include "ui/profiles/ConfigProfiles.h"
#include "ui/profiles/ProfileBar.h"
#include "ui/profiles/Loadouts.h"
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "render/glyphs/Glyphs.h"
#include "util/Textures.h"
#include "util/ImageCache.h"
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
#include "ui/tabs/SettingsModel.h"
#include "ui/tabs/DiagnosticsSection.h"
#include "ui/tabs/SettingsCommon.h"

static int g_optSection = 0;
static std::string g_optGroup;
static char g_settingSearch[64] = "";

// Jump the Options tab to a section (+ optional subsection). Used by the HUD right-click "settings".
void OpenOptionsSection(int section, const char *group)
{
    if (section >= 0 && section < SEC_COUNT)
        g_optSection = section;
    g_optGroup = group ? group : "";
    g_settingSearch[0] = '\0';
}

namespace
{
    struct GroupInfo
    {
        const char *id = nullptr;
        const char *name = nullptr;
    };

    bool Streq(const char *a, const char *b)
    {
        if (!a || !b)
            return !a && !b;
        return std::strcmp(a, b) == 0;
    }

    bool GroupSelected(const char *id)
    {
        return id && !g_optGroup.empty() && g_optGroup == id;
    }

    bool HasGroup(const std::vector<GroupInfo> &groups, const char *id)
    {
        if (!id)
            return false;
        for (const GroupInfo &g : groups)
            if (Streq(g.id, id))
                return true;
        return false;
    }

    void AddGroup(std::vector<GroupInfo> &groups, const char *id, const char *name)
    {
        if (!id || !name || HasGroup(groups, id))
            return;
        groups.push_back(GroupInfo{id, name});
    }

    std::vector<GroupInfo> GroupsForSection(const std::vector<Setting> &settings, int section)
    {
        std::vector<GroupInfo> groups;
        if (section == SEC_DASHBOARD)
        {
            AddGroup(groups, SettingGroups::DashboardProfilesId, SettingGroups::DashboardProfilesName);
            AddGroup(groups, SettingGroups::DashboardPanelId, SettingGroups::DashboardPanelName);
            AddGroup(groups, SettingGroups::DashboardWidgetsId, SettingGroups::DashboardWidgetsName);
            return groups;
        }
        if (section == SEC_DIAG)
        {
            AddGroup(groups, SettingGroups::DiagTogglesId, SettingGroups::DiagTogglesName);
            AddGroup(groups, SettingGroups::DiagLiveId, SettingGroups::DiagLiveName);
            return groups;
        }
        for (const Setting &s : settings)
            if (s.section == section)
                AddGroup(groups, s.groupId, s.groupName);
        return groups;
    }

    const char *GroupNameForId(const std::vector<GroupInfo> &groups, const std::string &id)
    {
        if (id.empty())
            return nullptr;
        for (const GroupInfo &g : groups)
            if (g.id && id == g.id)
                return g.name;
        return nullptr;
    }

    bool DrawSubsectionMenuItem(const char *label, bool selected, int rowIndex)
    {
        const float ui = Gw2Ui::GlobalScale();
        const float h = 28.f * ui;
        const Gw2Ui::RowHotspot row = Gw2Ui::Row(label, rowIndex, h, 0.f, false, selected);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImU32 col = selected ? Gw2Ui::kGold : IM_COL32(190, 182, 162, 245);
        Gw2Ui::RowLabel(dl, row, 28.f * ui, 6.f * ui, label,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, col, false, nullptr, SettingsText::Hint);
        return row.clicked;
    }

    // One shared control column for every settings row on the CURRENT page, auto-sized to that page's widest label
    // (set by DrawSettingsForSection / the search view just before the rows draw). Keeps all controls in ONE
    // aligned column while guaranteeing a long label is never overlapped by its control -- universal, and no
    // per-row jaggedness (every row on the page shares the same column). Clamped so one outlier can't blow it out.
    float g_settingsLabelCol = 250.f;
    float SettingsColForRows(const std::vector<Setting> &settings, int section, const char *groupId)
    {
        float maxW = 0.f;
        for (const Setting &s : settings)
        {
            if (s.section != section)
                continue;
            if (groupId && !Streq(s.groupId, groupId))
                continue;
            if (s.kind == SKind::Bool || s.kind == SKind::Keybind)
                continue; // these draw their label inline (no column)
            maxW = std::max(maxW, Gw2Ui::MeasureWidth(s.name, 0.f));
        }
        const float ui = Gw2Ui::GlobalScale();
        return std::clamp(maxW + 16.f * ui, 250.f * ui, 360.f * ui);
    }

    bool DrawSettingAndApply(App &app, const Setting &s)
    {
        if (!DrawSetting(s, g_settingsLabelCol))
            return false;
        app.settingsDirty = true;
        if (s.kind == SKind::Keybind)
            NormalizeKeybinds(app, s.key);
        if (s.ve == &app.config.pathType)
            app.zoneManager.ApplyPathType();
        if (s.vb == &app.config.useGw2Font)
            ApplyFontChoice(app);
        return true;
    }

    // One group of settings rendered as a GW2 card (controls inside, small gaps between rows) -- the shared
    // building block for both the sectioned view and the search results, so each subsection reads as a tidy
    // card instead of a flat run of rows. Leaves a gap below for the next card. `cardId` must be unique.
    void DrawSettingRowsCarded(App &app, const std::vector<const Setting *> &rows, const char *cardId)
    {
        if (rows.empty())
            return;
        Gw2Ui::BeginCard(cardId);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (i)
                ImGui::Dummy(ImVec2(0.f, 3.f)); // breathing room between rows
            DrawSettingAndApply(app, *rows[i]);
        }
        Gw2Ui::EndCard();
        ImGui::Dummy(ImVec2(0.f, 6.f)); // gap before the next card / header
    }

    void DrawSettingsForSection(App &app, const std::vector<Setting> &settings, int section, const char *groupId)
    {
        g_settingsLabelCol = SettingsColForRows(settings, section, groupId); // one aligned column for this page
        // Accumulate rows per group, then flush each group as a card with its header above it (grouped
        // sections); ungrouped sections (General/Arrow/Keybinds, the API key row) flush as ONE header-less
        // card -- the panel title already names them. A specific-group page (groupId set) is one card, no
        // header (the panel title shows "Section / Group").
        std::vector<const Setting *> rows;
        const char *curGroup = nullptr, *curGroupName = nullptr;
        bool any = false, started = false;
        char cardId[80];

        auto flush = [&]()
        {
            if (rows.empty())
                return;
            if (!groupId && curGroupName)
                Gw2Ui::SectionHeader(curGroupName, nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            std::snprintf(cardId, sizeof(cardId), "opt-%d-%s", section, curGroup ? curGroup : "flat");
            DrawSettingRowsCarded(app, rows, cardId);
            rows.clear();
        };

        for (const Setting &s : settings)
        {
            if (s.section != section)
                continue;
            if (groupId && !Streq(s.groupId, groupId))
                continue;
            if (started && !groupId && !Streq(s.groupId, curGroup))
                flush(); // group boundary -> close the card
            curGroup = s.groupId;
            curGroupName = s.groupName;
            rows.push_back(&s);
            any = true;
            started = true;
        }
        flush();
        if (!any)
            Gw2Ui::Label("No settings in this subsection.", IM_COL32(180, 170, 150, 255), false, nullptr, SettingsText::Body);
    }

    // Zone Display: the text-color block (preset swatches + a hex/RGB picker) -- custom UI the scalar settings
    // table can't express. Persisted via the internal zdColR/G/B keys (Save/LoadSettings), not a table row.
    void DrawZoneDisplayColor(App &app)
    {
        Config &c = app.config;
        Gw2Ui::SectionHeader("Text color", nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
        Gw2Ui::BeginCard("zd-color");

        struct Sw
        {
            const char *name;
            int r, g, b;
        };
        static const Sw kSw[] = {
            {"Gold", 223, 194, 149},        // BrightGold (the default)
            {"Bright gold", 255, 221, 130}, // the kGold accent
            {"Parchment", 238, 225, 190},
            {"White", 255, 255, 255},
            {"Silver", 198, 205, 220},
            {"Crimson", 208, 84, 72},
            {"Jade", 120, 205, 160},
        };
        const int n = (int)(sizeof(kSw) / sizeof(kSw[0]));
        const float swW = 26.f;
        const float swGap = ImGui::GetStyle().ItemSpacing.x;
        const float rowW = ImGui::GetContentRegionAvail().x; // wrap the swatch grid at the card's right edge
        float lineW = 0.f;
        for (int i = 0; i < n; ++i)
        {
            const Sw &s = kSw[i];
            if (lineW > 0.f)
            {
                if (lineW + swGap + swW > rowW)
                    lineW = 0.f; // next swatch won't fit -> new line
                else
                {
                    ImGui::SameLine(0.f, swGap);
                    lineW += swGap;
                }
            }
            ImGui::PushID(i);
            const ImVec4 cv(s.r / 255.f, s.g / 255.f, s.b / 255.f, 1.f);
            const bool active = ((int)c.zdColR == s.r && (int)c.zdColG == s.g && (int)c.zdColB == s.b);
            if (ImGui::ColorButton(s.name, cv,
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha | (active ? 0 : ImGuiColorEditFlags_NoBorder),
                                   ImVec2(swW, swW)))
            {
                c.zdColR = (unsigned)s.r;
                c.zdColG = (unsigned)s.g;
                c.zdColB = (unsigned)s.b;
                app.settingsDirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", s.name);
            ImGui::PopID();
            lineW += swW;
        }

        float col[3] = {c.zdColR / 255.f, c.zdColG / 255.f, c.zdColB / 255.f};
        ImGui::SetNextItemWidth(std::min(220.f, ImGui::GetContentRegionAvail().x - 150.f));
        if (ImGui::ColorEdit3("##zdHex", col,
                              ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoLabel))
        {
            c.zdColR = (unsigned)(col[0] * 255.f + 0.5f);
            c.zdColG = (unsigned)(col[1] * 255.f + 0.5f);
            c.zdColB = (unsigned)(col[2] * 255.f + 0.5f);
            app.settingsDirty = true;
        }
        ImGui::SameLine();
        Gw2Ui::Label("Custom (hex / RGB)", Gw2Ui::kTextSub, false, nullptr, SettingsText::Body);
        Gw2Ui::EndCard();
    }

    // Zone Display: a live "Preview banner" button (mirrors the Diagnostics test buttons) -- fires the banner
    // for the player's current map / sector so settings can be tuned without re-zoning.
    void DrawZoneDisplayPreview(App &app)
    {
        ImGui::Dummy(ImVec2(0.f, 6.f));
        Gw2Ui::Divider(0.f);
        Gw2Ui::SectionHeader("Preview", nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
        Gw2Ui::BeginCard("zd-preview");
        const float bw = Gw2Ui::FillWidth(ImGui::GetContentRegionAvail().x - 8.f, 2, ImGui::GetStyle().ItemSpacing.x, 120.f);
        if (ImGui::Button("Preview banner", ImVec2(bw, 0.f)))
            app.zoneDisplay.Reannounce(app);
        if (ImGui::IsItemHovered())
            Gw2Ui::Tooltip("Play the full map-entry banner (region / zone / area) for your current location.");
        ImGui::SameLine();
        if (ImGui::Button("Preview area", ImVec2(bw, 0.f)))
            app.zoneDisplay.ReannounceSector(app); // the area announcement (small line or big banner, per "Area style")
        if (ImGui::IsItemHovered())
            Gw2Ui::Tooltip("Play the area-crossing announcement (one line or big banner, per Area > Area style).");
        Gw2Ui::EndCard();
    }

    bool SearchMatchesDiagnosticsLive(const char *qLower)
    {
        return qLower && *qLower &&
               KeywordsMatch("diagnostics live debug cache map tiles icons clear reset progress mumble trail "
                             "gameplay status level",
                             qLower);
    }
}

// Options tab: a sectioned settings view - a left "Settings" section menu + a right content
// panel (both bordered), exactly like GuideModule.BuildOptionsTab.
// The Account / API section's live status under the key field: whether a key is set + whether its scopes
// resolved (via /v2/tokeninfo), then a card per scope the guide uses with a plain-language note on what it
// powers and a granted/missing mark. Anonymous features (map names + tiles) work with no key.
namespace
{
    void DrawScopeRow(App &app, const ApiScopes::Scope &sc, bool keyValid)
    {
        const bool granted = keyValid && app.api.HasPermission(sc.p);
        const float fs = SettingsText::Body, hint = SettingsText::Hint;
        const float availW = Gw2Ui::ContentWidth(); // card-aware (GetContentRegionAvail overran the card's right padding)
        const float indent = 24.f, rightPad = 6.f;  // keep wrapped text clear of the card border
        const float descW = (availW - indent - rightPad) < 80.f ? 80.f : (availW - indent - rightPad);
        const float nameH = fs + 4.f;
        const float descH = Gw2Ui::MeasureWrappedHeight(sc.desc, hint, descW);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        const ImU32 ok = IM_COL32(150, 210, 140, 255), missIcon = IM_COL32(206, 120, 104, 235);
        const ImU32 dimTxt = IM_COL32(150, 142, 122, 205);
        Render::DrawGlyph(dl, ImVec2(p.x + 8.f, p.y + fs * 0.5f), 15.f,
                          granted ? Render::Glyph::Check : Render::Glyph::Cross, granted ? ok : missIcon, {false, false, false});
        Gw2Ui::LabelIn(ImVec2(p.x + indent, p.y), ImVec2(p.x + indent + 220.f, p.y + nameH), sc.name,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, granted ? Gw2Ui::kGold : dimTxt, false, nullptr, fs);
        if (!granted)
        {
            const float nameW = Gw2Ui::MeasureWidth(sc.name, fs);
            Gw2Ui::LabelIn(ImVec2(p.x + indent + nameW + 8.f, p.y), ImVec2(p.x + availW, p.y + nameH),
                           keyValid ? "(not in this key)" : "(needs a key)", Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                           IM_COL32(180, 120, 108, 210), false, nullptr, hint);
        }
        Gw2Ui::LabelIn(ImVec2(p.x + indent, p.y + nameH), ImVec2(p.x + indent + descW, p.y + nameH + descH), sc.desc,
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, granted ? IM_COL32(196, 188, 168, 235) : dimTxt,
                       false, nullptr, hint, descW);
        ImGui::Dummy(ImVec2(availW, nameH + descH + 7.f));
    }
}

static void DrawApiKeyStatus(App &app)
{
    ImGui::Spacing();
    const bool hasKey = app.api.HasKey();
    const bool resolving = hasKey && app.api.ScopesResolving();
    const Api::TokenInfo ti = hasKey ? app.api.ScopeInfo() : Api::TokenInfo{};
    const bool valid = hasKey && !resolving && ti.valid;

    // --- key status (colour-coded card) ---
    if (!hasKey)
    {
        Gw2Ui::BeginAccentCard("api-st", 0.f, IM_COL32(150, 124, 70, 220), IM_COL32(0, 0, 0, 30), IM_COL32(120, 100, 60, 120));
        Gw2Ui::Label("No API key set", IM_COL32(220, 210, 185, 255), false, nullptr, SettingsText::Header);
        SettingsParagraph("Paste a GW2 API key in the box above to unlock the features below. Create one at "
                          "account.arena.net -> Applications. The key is stored locally in settings.json (Nexus has "
                          "no key vault), so use a least-scope key with only the boxes you want.",
                          Gw2Ui::kTextSub);
        Gw2Ui::EndCard();
    }
    else if (resolving)
    {
        Gw2Ui::BeginCard("api-st");
        Gw2Ui::Label("Checking API key...", Gw2Ui::kGold, false, nullptr, SettingsText::Body);
        Gw2Ui::EndCard();
    }
    else if (!valid)
    {
        Gw2Ui::BeginAccentCard("api-st", 0.f, IM_COL32(228, 110, 90, 235), IM_COL32(30, 10, 8, 90), IM_COL32(150, 70, 55, 150));
        Gw2Ui::Label("Key not recognized", IM_COL32(240, 150, 130, 255), false, nullptr, SettingsText::Header);
        SettingsParagraph("The key was rejected, or it couldn't be checked (offline?). Map names + tile imagery "
                          "still work with no key; everything below needs a valid key.",
                          Gw2Ui::kTextSub);
        Gw2Ui::EndCard();
    }
    else
    {
        Gw2Ui::BeginAccentCard("api-st", 0.f, IM_COL32(120, 200, 110, 235), IM_COL32(8, 22, 8, 80), IM_COL32(80, 130, 70, 140));
        char hdr[160];
        std::snprintf(hdr, sizeof(hdr), "Key \"%s\" accepted", ti.name.c_str());
        Gw2Ui::Label(hdr, IM_COL32(170, 225, 160, 255), false, nullptr, SettingsText::Header);
        SettingsParagraph("Granted scopes are checked below. The guide only ever READS account data -- it never "
                          "changes anything.",
                          Gw2Ui::kTextSub);
        Gw2Ui::EndCard();
    }

    // --- per-scope breakdown: what each permission is for + whether this key has it ---
    ImGui::Spacing();
    Gw2Ui::Label("Permissions & what they unlock", IM_COL32(190, 178, 150, 255), false, nullptr, SettingsText::Header);
    SettingsParagraph(valid
                          ? "What each scope powers in the guide. Grant only the ones you want -- a missing scope just turns its "
                            "feature off, nothing else breaks."
                          : "What each scope the guide can use is for. Add a key above with the ones you want.",
                      Gw2Ui::kTextSub);
    ImGui::Spacing();
    Gw2Ui::BeginCard("api-scopes");
    for (const ApiScopes::Scope &sc : ApiScopes::kScopes)
        DrawScopeRow(app, sc, valid);
    Gw2Ui::EndCard();

    SettingsParagraph("Map names and tile imagery work with no key at all.", IM_COL32(150, 142, 122, 205));
}

void DrawOptionsContent(App &app)
{
    // Two side-by-side bordered panels positioned both at top y=5 so
    // their headers line up. menuW=235; content at menuW+12, filling the rest with a 17px right margin.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float w = avail.x, h = avail.y;
    // Draggable, persisted menu/body split (shared Gw2Ui::VSplitter).
    const float ui = Gw2Ui::GlobalScale();
    const float bodyMin = std::min(300.f * ui, std::max(180.f, w * 0.58f));
    const float maxByBody = std::max(120.f * ui, w - bodyMin);
    const float optMinMenu = std::min(180.f * ui, maxByBody);
    const float optMaxMenu = std::max(optMinMenu, std::min(380.f * ui, maxByBody));
    const float panelPad = 5.f * ui;
    const float panelGap = 7.f * ui;
    float &menuW = app.config.PaneW("opt.menu", 235.f);
    menuW = std::clamp(menuW, optMinMenu, optMaxMenu);
    const auto &settings = Settings(app);
    const bool searching = g_settingSearch[0] != '\0';

    char qLower[64];
    {
        size_t i = 0;
        for (; g_settingSearch[i] && i < sizeof(qLower) - 1; ++i)
            qLower[i] = (char)std::tolower((unsigned char)g_settingSearch[i]);
        qLower[i] = '\0';
    }

    // LEFT: a "Search settings" box pinned at the top -- it REPLACES the old "Settings" header band (redundant:
    // the wrench tab + the right panel's title already say where we are) and, living in the panel's fixed top
    // OUTSIDE the inner scroll child, it's always reachable without scrolling past the categories. The category
    // list scrolls in its own child below it.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + panelPad, origin.y + panelPad));
    if (Gw2Ui::BeginPanel("##optmenu", menuW, h - panelPad * 2.f, nullptr))
    {
        Gw2Ui::SearchBox("##setsearch", g_settingSearch, sizeof(g_settingSearch),
                         ImGui::GetContentRegionAvail().x, "Search settings...");
        ImGui::Dummy(ImVec2(0.f, 6.f));
        ImGui::BeginChild("##sectabs", ImGui::GetContentRegionAvail(), false);
        for (int i = 0; i < SEC_COUNT; ++i)
        {
            if (Gw2Ui::MenuItem(kSections[i], !searching && g_optSection == i, 40.f * ui, i))
            {
                g_optSection = i;
                g_optGroup.clear();
                g_settingSearch[0] = '\0';
            }

            const std::vector<GroupInfo> groups = GroupsForSection(settings, i);
            if (!groups.empty() && !searching && g_optSection == i)
            {
                for (int gi = 0; gi < (int)groups.size(); ++gi)
                {
                    if (DrawSubsectionMenuItem(groups[gi].name, GroupSelected(groups[gi].id), gi))
                    {
                        g_optSection = i;
                        g_optGroup = groups[gi].id ? groups[gi].id : "";
                        g_settingSearch[0] = '\0';
                    }
                }
            }
            // Info Panel: a "Layout & texts" page + one subsection per data text that has options (the texts
            // have no settings-table rows, so we build this submenu from the HudData catalog).
            if (i == SEC_INFO && !searching && g_optSection == i)
            {
                if (DrawSubsectionMenuItem("Layout & texts", g_optGroup.empty(), 0))
                {
                    g_optSection = i;
                    g_optGroup.clear();
                    g_settingSearch[0] = '\0';
                }
                int gi = 1;
                for (const InfoData::HudText &t : InfoData::HudTexts())
                {
                    if (InfoData::OptionsFor(t.key).empty())
                        continue;
                    if (DrawSubsectionMenuItem(t.title, g_optGroup == t.key, gi++))
                    {
                        g_optSection = i;
                        g_optGroup = t.key;
                        g_settingSearch[0] = '\0';
                    }
                }
            }
        }
        ImGui::EndChild();
        Gw2Ui::EndPanel();
    }

    // RIGHT: the selected section's settings, or the cross-section search results.
    const float bodyX = origin.x + panelPad + menuW + panelGap;
    const float bodyW = std::max(120.f * ui, w - panelPad * 2.f - panelGap - menuW);
    ImGui::SetCursorScreenPos(ImVec2(bodyX, origin.y + panelPad));
    const std::vector<GroupInfo> activeGroups = GroupsForSection(settings, g_optSection);
    const char *activeGroupName = GroupNameForId(activeGroups, g_optGroup);
    if (!activeGroupName && g_optSection == SEC_INFO && !g_optGroup.empty())
    {
        const InfoData::HudText *t = InfoData::FindText(g_optGroup.c_str());
        if (t)
            activeGroupName = t->title;
    }
    char panelTitle[128];
    if (searching)
        std::snprintf(panelTitle, sizeof(panelTitle), "Search results");
    else if (activeGroupName)
        std::snprintf(panelTitle, sizeof(panelTitle), "%s / %s", kSections[g_optSection], activeGroupName);
    else
        std::snprintf(panelTitle, sizeof(panelTitle), "%s", kSections[g_optSection]);
    if (!Gw2Ui::BeginPanel("##optbody", bodyW, h - panelPad * 2.f,
                           panelTitle))
        return;

    if (searching)
    {
        { // one aligned control column across every search hit (same rule as a section page)
            float maxW = 0.f;
            for (const Setting &s : settings)
                if (s.kind != SKind::Bool && s.kind != SKind::Keybind && SettingMatches(s, qLower))
                    maxW = std::max(maxW, Gw2Ui::MeasureWidth(s.name, 0.f));
            g_settingsLabelCol = std::clamp(maxW + 16.f * ui, 250.f * ui, 360.f * ui);
        }
        int hits = 0, cardN = 0;
        std::string lastHeader;
        std::vector<const Setting *> rows;
        auto flushSearch = [&]()
        {
            if (rows.empty())
                return;
            char cid[48];
            std::snprintf(cid, sizeof(cid), "opt-srch-%d", cardN++);
            DrawSettingRowsCarded(app, rows, cid);
            rows.clear();
        };
        for (int sec = 0; sec < SEC_COUNT; ++sec)
        {
            for (const Setting &s : settings)
            {
                if (s.section != sec || !SettingMatches(s, qLower))
                    continue;
                std::string header = kSections[sec];
                if (s.groupName && *s.groupName)
                    header += std::string(" / ") + s.groupName;
                if (header != lastHeader)
                {
                    flushSearch(); // close the previous header's card before starting a new one
                    Gw2Ui::SectionHeader(header.c_str(), nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
                    lastHeader = header;
                }
                rows.push_back(&s);
                ++hits;
            }
        }
        flushSearch();
        // The Diagnostics section is a live readout + actions (not toggle settings), so surface it in search
        // when the query matches its keywords - keeps it findable like everything else in Options.
        if (SearchMatchesDiagnosticsLive(qLower))
        {
            if (hits)
                ImGui::Dummy(ImVec2(0.f, 6.f));
            Gw2Ui::SectionHeader("Diagnostics / Live Diagnostics", nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            DrawDiagnosticsSection(app);
            ++hits;
        }
        // The HUD panel's ACTIONS (reset position + the button-layout editor) are not model rows either, so
        // surface them on a keyword match for the same reason - otherwise "reset position" finds nothing.
        if (HUD::SearchMatchesActions(qLower))
        {
            if (hits)
                ImGui::Dummy(ImVec2(0.f, 6.f));
            Gw2Ui::SectionHeader("HUD / Bar Actions", nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            HUD::DrawResetPosition(app);
            ImGui::Dummy(ImVec2(0.f, 6.f));
            HUD::DrawButtonLayout(app);
            ++hits;
        }
        if (!hits)
            Gw2Ui::Label("No settings match your search.", IM_COL32(180, 170, 150, 255), false, nullptr, SettingsText::Body);
    }
    else if (g_optSection == SEC_DIAG)
    {
        if (g_optGroup.empty() || g_optGroup == SettingGroups::DiagTogglesId)
        {
            if (g_optGroup.empty())
                Gw2Ui::SectionHeader(SettingGroups::DiagTogglesName, nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            DrawSettingsForSection(app, settings, SEC_DIAG, g_optGroup.empty() ? SettingGroups::DiagTogglesId : g_optGroup.c_str());
        }
        if (g_optGroup.empty())
        {
            ImGui::Dummy(ImVec2(0.f, 6.f));
            Gw2Ui::Divider(0.f);
        }
        if (g_optGroup.empty() || g_optGroup == SettingGroups::DiagLiveId)
        {
            if (g_optGroup.empty())
                Gw2Ui::SectionHeader(SettingGroups::DiagLiveName, nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            DrawDiagnosticsSection(app);
        }
    }
    else if (g_optSection == SEC_DASHBOARD)
    {
        if (g_optGroup.empty() || g_optGroup == SettingGroups::DashboardProfilesId)
        {
            if (g_optGroup.empty())
                Gw2Ui::SectionHeader(SettingGroups::DashboardProfilesName, nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            Dashboard::DrawProfileSettings(app);
        }
        if (g_optGroup.empty())
        {
            ImGui::Dummy(ImVec2(0.f, 6.f));
            Gw2Ui::Divider(0.f);
        }
        if (g_optGroup.empty() || g_optGroup == SettingGroups::DashboardPanelId)
        {
            if (g_optGroup.empty())
                Gw2Ui::SectionHeader(SettingGroups::DashboardPanelName, nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            DrawSettingsForSection(app, settings, SEC_DASHBOARD, SettingGroups::DashboardPanelId);
        }
        if (g_optGroup.empty())
        {
            ImGui::Dummy(ImVec2(0.f, 6.f));
            Gw2Ui::Divider(0.f);
        }
        if (g_optGroup.empty() || g_optGroup == SettingGroups::DashboardWidgetsId)
        {
            if (g_optGroup.empty())
                Gw2Ui::SectionHeader(SettingGroups::DashboardWidgetsName, nullptr, SettingsText::Header, Gw2Ui::kGold, /*banded*/ false);
            Dashboard::DrawWidgetSettings(app);
        }
    }
    else if (g_optSection == SEC_HUD)
    {
        HUD::DrawSettings(app); // one page: Profiles -> Placement -> Clock -> Buttons (all per-character profile)
    }
    else if (g_optSection == SEC_INFO)
    {
        InfoPanel::DrawSettings(app, g_optGroup.c_str()); // "" = Layout & texts; a text key = that text's options subsection
    }
    else if (g_optSection == SEC_LOADOUTS)
    {
        Loadouts::DrawSettings(app); // loadout bar + per-family active-profile pickers
    }
    else if (g_optSection == SEC_ZONEDISPLAY)
    {
        // Auto rows (grouped Behaviour / Appearance / Animation / Sector), then the custom color block (Appearance) + Preview buttons (Behaviour).
        DrawSettingsForSection(app, settings, SEC_ZONEDISPLAY, g_optGroup.empty() ? nullptr : g_optGroup.c_str());
        if (g_optGroup.empty() || g_optGroup == SettingGroups::ZdAppearanceId)
        {
            ImGui::Dummy(ImVec2(0.f, 6.f));
            DrawZoneDisplayColor(app);
        }
        if (g_optGroup.empty() || g_optGroup == SettingGroups::ZdBehaviorId)
            DrawZoneDisplayPreview(app);
    }
    else
    {
        if (g_optSection == SEC_GENERAL && g_optGroup.empty()) // per-character general-settings profiles at the top
        {
            Profiles::DrawProfileBar(app, ConfigProfiles::Host(app, ConfigProfiles::Owner::General), "genprof",
                                     "These general settings (guide, arrow, panel, routing, trails, markers, notifications, keybinds) are "
                                     "saved per character in this profile -- the API key + Diagnostics stay global, and the Dashboard / HUD / "
                                     "Info Panel keep their own profiles. Import one from another character below.");
            ImGui::Dummy(ImVec2(0.f, 6.f));
            Gw2Ui::Divider(0.f);
        }
        DrawSettingsForSection(app, settings, g_optSection, g_optGroup.empty() ? nullptr : g_optGroup.c_str());
        if (g_optSection == SEC_API)
            DrawApiKeyStatus(app);
    }

    // Keep dependent ranges valid (fade-end must stay past fade-start).
    if (app.config.fadeGone < app.config.fadeFull + 1.f)
        app.config.fadeGone = app.config.fadeFull + 1.f;
    if (app.config.markerFadeFar < app.config.markerFadeNear + 1.f)
        app.config.markerFadeFar = app.config.markerFadeNear + 1.f;

    Gw2Ui::EndPanel();

    // Splitter handle in the gap between the menu and body panels.
    if (Gw2Ui::VSplitter("##opt_menu_split", origin.x + panelPad + menuW + panelGap * 0.5f, origin.y + panelPad,
                         h - panelPad * 2.f, &menuW, optMinMenu, optMaxMenu))
        app.settingsDirty = true;
}

// Render one section's model rows as grouped cards -- the HUD / Info Panel section pages call this so their
// scalar settings look + behave exactly like every other Options section. Declared in ui/tabs/SettingsModel.h.
void DrawSettingSection(App &app, int section, const char *groupId)
{
    std::vector<Setting> settings = Settings(app);
    DrawSettingsForSection(app, settings, section, groupId);
}
