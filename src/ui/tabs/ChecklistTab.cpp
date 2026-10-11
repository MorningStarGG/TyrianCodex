#include "ui/tabs/ChecklistTab.h"
#include "ui/SettingsWindow.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"
#include "ui/EventUi.h"
#include "ui/ApiReminder.h"
#include "ui/tabs/MapThumbnail.h"
#include "app/App.h"
#include "guide/CurrentChar.h"
#include "guide/Regions.h"
#include "model/ObjectiveTypes.h"
#include "render/LockIcon.h"
#include "render/glyphs/Glyphs.h"
#include "util/Textures.h"
#include "Shared.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>

namespace
{
    enum Scope { Objectives, Events };
    int scope_ = Objectives;
    using Node = Gw2Ui::ChecklistNode;
    struct MapPreview
    {
        bool Available = false;
        int Continent = 1, MinLevel = 0, MaxLevel = 0;
        float Rect[4] = {};
        std::vector<MapMark> Pins;
    };
    struct ChecklistView
    {
        Gw2Ui::ChecklistState State;
        std::vector<Node> Roots;
        std::map<std::string, MapPreview> Previews;
        size_t ZoneCount = (size_t)-1;
        uint64_t CatalogVersion = (uint64_t)-1, ProgressVersion = (uint64_t)-1;
        std::string Character, Search;
        Gw2Ui::ChecklistFilter Filter = Gw2Ui::ChecklistFilter::All;
    };
    std::array<ChecklistView, 2> views_;

    std::string ZoneKey(const Zone& zone)
    {
        return "rel|" + (zone.Release.empty() ? std::string("core") : zone.Release) +
               "|reg|" + std::to_string(zone.RegionId) + "|z|" + std::to_string(zone.MapId);
    }

    void AccumulateMap(MapPreview& preview, const Zone& zone, bool crop)
    {
        if (!zone.HasRects || zone.ContinentId != 1) return;
        float rect[4];
        if (crop) ZoneMapCrop(&zone, rect);
        else std::copy(zone.ContRect, zone.ContRect + 4, rect);
        if (!preview.Available)
        {
            preview.Available = true;
            preview.Continent = zone.ContinentId;
            preview.MinLevel = zone.MinLevel;
            preview.MaxLevel = zone.MaxLevel;
            std::copy(rect, rect + 4, preview.Rect);
        }
        else
        {
            preview.Rect[0] = std::min(preview.Rect[0], rect[0]);
            preview.Rect[1] = std::min(preview.Rect[1], rect[1]);
            preview.Rect[2] = std::max(preview.Rect[2], rect[2]);
            preview.Rect[3] = std::max(preview.Rect[3], rect[3]);
            preview.MinLevel = std::min(preview.MinLevel, zone.MinLevel);
            preview.MaxLevel = std::max(preview.MaxLevel, zone.MaxLevel);
        }
    }

    std::vector<Node> ItemsForMap(App& app, const Zone& zone, Scope scope, MapPreview& preview)
    {
        std::vector<Node> nodes;
        const auto context = zone.Name + "  -  " + Regions::Label(zone);
        if (scope == Events)
        {
            auto events = app.eventAreas.EventsForMap(zone.MapId);
            std::sort(events.begin(), events.end(), [](const auto* a, const auto* b) {
                return a->Name != b->Name ? a->Name < b->Name : a->EventId < b->EventId;
            });
            std::map<std::string, int> names;
            for (const auto* event : events) ++names[event->Name];
            for (const auto* event : events)
            {
                Node leaf;
                leaf.Id = "event|" + event->EventId;
                leaf.ItemId = event->EventId;
                leaf.Label = event->Name;
                const auto location = EventUi::LocationText(*event);
                if (names[event->Name] > 1 && !location.empty()) leaf.Label += "  -  " + location;
                leaf.Context = zone.Name;
                leaf.SearchText = context + " " + location + " " + EventUi::CategoryLabel(event->Category);
                nodes.push_back(std::move(leaf));
                if (event->HasContinentCoord) preview.Pins.push_back({event->ContinentX, event->ContinentY, 0});
            }
            return nodes;
        }
        for (const char* type : {"heart", "vista", "poi", "hero", "waypoint"})
        {
            std::vector<const Step*> steps;
            for (const auto& step : zone.Steps) if (step.Type == type) steps.push_back(&step);
            if (steps.empty()) continue;
            std::sort(steps.begin(), steps.end(), [](const auto* a, const auto* b) { return a->Order < b->Order; });
            const auto* info = Objective::Get(type);
            Node group;
            group.Id = ZoneKey(zone) + "|t|" + type;
            group.Label = info ? info->GroupName : type;
            group.Height = 36.f;
            for (const auto* step : steps)
            {
                Node leaf;
                leaf.Id = leaf.ItemId = step->StepId;
                leaf.Label = step->Name.empty() ? step->Type : step->Name;
                leaf.Context = zone.Name;
                leaf.SearchText = context + " " + (info ? info->Name : type);
                group.Children.push_back(std::move(leaf));
                uint32_t asset = 0;
                Objective::TryIconAssetId(type, asset);
                preview.Pins.push_back({step->CX, step->CY, asset});
            }
            nodes.push_back(std::move(group));
        }
        return nodes;
    }

    void BuildTree(App& app, Scope scope)
    {
        auto& view = views_[scope];
        if (view.ZoneCount == app.zones.size() && view.CatalogVersion == app.eventAreas.Version()) return;
        view.Roots.clear();
        view.Previews.clear();
        std::map<int, MapPreview> regions;
        std::map<std::string, std::vector<const Zone*>> releases;
        for (const auto& entry : app.zones)
        {
            const auto& zone = entry.second;
            AccumulateMap(regions[zone.RegionId], zone, false);
            if (scope == Events ? !app.eventAreas.EventsForMap(zone.MapId).empty() : !zone.Steps.empty())
                releases[zone.Release.empty() ? "core" : zone.Release].push_back(&zone);
        }
        auto order = StoryData::ReleaseOrder();
        for (const auto& entry : releases)
            if (std::find(order.begin(), order.end(), entry.first) == order.end()) order.push_back(entry.first);
        for (const auto& release : order)
        {
            auto found = releases.find(release);
            if (found == releases.end()) continue;
            Node root;
            root.Id = "rel|" + release;
            root.Label = StoryData::ReleaseName(release);
            root.Bold = true;
            root.Height = 42.f;
            std::map<int, std::vector<const Zone*>> byRegion;
            for (const auto* zone : found->second) byRegion[zone->RegionId].push_back(zone);
            std::vector<std::pair<int, std::vector<const Zone*>>> sorted(byRegion.begin(), byRegion.end());
            std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                const int aLevel = Regions::MinLevel(a.second), bLevel = Regions::MinLevel(b.second);
                return aLevel != bLevel ? aLevel < bLevel : Regions::Label(a.second) < Regions::Label(b.second);
            });
            for (auto& region : sorted)
            {
                Node regionNode;
                regionNode.Id = root.Id + "|reg|" + std::to_string(region.first);
                regionNode.Label = Regions::Label(region.second);
                view.Previews[regionNode.Id] = regions[region.first];
                std::sort(region.second.begin(), region.second.end(), [](const auto* a, const auto* b) {
                    return a->MinLevel != b->MinLevel ? a->MinLevel < b->MinLevel : a->Name < b->Name;
                });
                for (const auto* zone : region.second)
                {
                    Node zoneNode;
                    zoneNode.Id = ZoneKey(*zone);
                    zoneNode.Label = zone->Name;
                    auto& preview = view.Previews[zoneNode.Id];
                    AccumulateMap(preview, *zone, true);
                    zoneNode.Children = ItemsForMap(app, *zone, scope, preview);
                    if (!zoneNode.Children.empty()) regionNode.Children.push_back(std::move(zoneNode));
                }
                if (!regionNode.Children.empty()) root.Children.push_back(std::move(regionNode));
            }
            if (!root.Children.empty()) view.Roots.push_back(std::move(root));
        }
        view.ZoneCount = app.zones.size();
        view.CatalogVersion = app.eventAreas.Version();
        view.ProgressVersion = (uint64_t)-1;
    }

    void RefreshCounts(App& app, Scope scope)
    {
        auto& view = views_[scope];
        const auto version = scope == Events ? app.eventProgress.Version() : app.progress.Version();
        if (view.ProgressVersion == version && view.Character == app.state.currentChar &&
            view.Search == view.State.Search && view.Filter == view.State.Filter) return;
        const auto& stored = scope == Events ? app.eventProgress.CompletedIds(app.state.currentChar) : app.progress.CompletedIds(app.state.currentChar);
        std::set<std::string> marked(stored.begin(), stored.end());
        if (scope == Objectives)
        {
            const auto& confirmed = app.progress.ConfirmedIds(app.state.currentChar);
            marked.insert(confirmed.begin(), confirmed.end());
        }
        Gw2Ui::UpdateChecklist(view.Roots, view.State, [&](const auto& id) { return marked.count(id) != 0; });
        view.ProgressVersion = version;
        view.Character = app.state.currentChar;
        view.Search = view.State.Search;
        view.Filter = view.State.Filter;
    }

    void ApplyGroup(App& app, Scope scope, const Node& group, bool on)
    {
        std::vector<std::string> ids;
        Gw2Ui::VisitChecklistItems(group, [&](const auto& leaf) { ids.push_back(leaf.ItemId); });
        if (scope == Events) { app.eventProgress.SetMany(app.state.currentChar, ids, on); return; }
        std::vector<std::string> objectives, waypoints;
        for (const auto& id : ids)
        {
            const auto* step = FindStepById(app, id);
            if (!step) continue;
            if (on)
            {
                objectives.push_back(id);
                if (step->Type == "waypoint") waypoints.push_back(id);
            }
            else if (!app.progress.IsConfirmed(app.state.currentChar, id)) objectives.push_back(id);
        }
        app.progress.SetMany(app.state.currentChar, objectives, on);
        if (on) app.progress.SetConfirmedMany(app.state.currentChar, waypoints, true);
    }

    void DrawItem(App& app, Scope scope, const Node& node, int depth, int row)
    {
        ImGui::PushID(node.Id.c_str());
        auto& view = views_[scope];
        const auto label = node.Label + (view.State.Search[0] ? "  -  " + node.Context : "");
        Gw2Ui::ChecklistItemSpec spec;
        spec.Id = "##item";
        spec.Label = label.c_str();
        spec.Marked = node.Done != 0;
        if (scope == Events)
        {
            const auto* event = app.eventAreas.ById(node.ItemId);
            if (!event) { ImGui::PopID(); return; }
            const auto kind = EventUi::CategoryLabel(event->Category);
            spec.Detail = kind.c_str();
            spec.Color = EventUi::CategoryColor(event->Category);
            spec.CanMark = EventProgressStore::CanMark(app.state.currentChar);
            spec.DrawIcon = [event, marked = spec.Marked](auto* list, auto center, auto size) {
                Render::DrawGlyph(list, center, size, Render::Glyph::Event, marked ? Gw2Ui::kTextDim : EventUi::CategoryColor(event->Category), {false,false,false});
            };
            const auto hit = Gw2Ui::ChecklistItem(spec, depth, row);
            if (hit.CheckboxClicked) app.eventProgress.SetComplete(app.state.currentChar, event->EventId, !spec.Marked);
            if (hit.BodyClicked) EventUi::Travel(app, *event);
            if (hit.Row.hovered) EventUi::Tooltip(*event);
            EventUi::Menu(app, *event, hit.Row.hovered);
        }
        else
        {
            const Zone* zone = nullptr;
            const auto* step = FindStepById(app, node.ItemId, &zone);
            if (!step) { ImGui::PopID(); return; }
            const bool waypoint = step->Type == "waypoint";
            const bool confirmed = waypoint && app.progress.IsConfirmed(app.state.currentChar, step->StepId);
            const auto* info = Objective::Get(step->Type);
            spec.Detail = info ? info->Name : "";
            spec.Color = Objective::ColorOf(step->Type, 255);
            spec.CanMark = !confirmed;
            spec.DisabledTip = "Unlock this waypoint before clearing it";
            spec.Selected = step->StepId == app.state.manualTarget;
            spec.TrailingWidth = waypoint ? 32.f * Gw2Ui::TextScale() : 0.f;
            spec.DrawIcon = [step](auto* list, auto center, auto size) {
                uint32_t asset = 0;
                if (Objective::TryIconAssetId(step->Type, asset))
                    if (const auto* texture = Tex::GetAssetTex(asset); texture && texture->Resource)
                        list->AddImage((ImTextureID)texture->Resource, ImVec2(center.x-size/2,center.y-size/2), ImVec2(center.x+size/2,center.y+size/2));
            };
            const auto hit = Gw2Ui::ChecklistItem(spec, depth, row);
            if (waypoint && hit.Row.rowMax.x > hit.Row.rowMin.x)
                Render::DrawLock(ImGui::GetWindowDrawList(), ImVec2(hit.Row.rowMax.x - 30.f * Gw2Ui::TextScale(),
                                  (hit.Row.rowMin.y + hit.Row.rowMax.y - 24.f * Gw2Ui::TextScale()) / 2.f), 24.f * Gw2Ui::TextScale(), confirmed);
            if (hit.CheckboxClicked)
            {
                app.progress.SetComplete(app.state.currentChar, step->StepId, !spec.Marked);
                if (!spec.Marked && waypoint) app.progress.SetConfirmed(app.state.currentChar, step->StepId, true);
            }
            else if (hit.BodyClicked)
            {
                if (waypoint && ImGui::GetIO().MousePos.x >= hit.Row.rowMax.x - spec.TrailingWidth)
                    app.progress.SetConfirmed(app.state.currentChar, step->StepId, !confirmed);
                else if (!spec.Marked) app.state.manualTarget = step->StepId;
            }
            if (hit.Row.hovered) ObjectiveTooltip(step->Name.c_str(), step->Type.c_str(), step, zone, step->CX, step->CY, true);
        }
        ImGui::PopID();
    }

    void JumpTo(App& app, Scope scope, uint32_t mapId)
    {
        const auto found = app.zones.find(mapId);
        if (found == app.zones.end()) return;
        auto& view = views_[scope];
        view.State.Search[0] = '\0';
        view.State.Filter = Gw2Ui::ChecklistFilter::All;
        Gw2Ui::ExpandChecklistTo(view.Roots, view.State, ZoneKey(found->second));
    }
}

void DrawChecklistContent(App& app)
{
    UpdateCurrentChar(app.state);
    static const Gw2Ui::ChipItem scopes[] = {{"Map completion"}, {"Events"}};
    Gw2Ui::ChipRow("##checklist_scope", scopes, 2, &scope_, 32.f, 18.f);
    Gw2Ui::Divider();
    const auto scope = (Scope)scope_;
    auto& view = views_[scope];
    BuildTree(app, scope);
    if (scope == Objectives && !app.api.HasKey())
        ApiReminder::Card(app, "checklist", "level-based zone recommendations", false);
    const auto help = scope == Events ?
        "Tick events you have seen or completed on this character. Marks are permanent until you clear them. Counts cover listed events; some maps have incomplete event coverage." :
        "Tick off what you have already done to sync the guide. Ticking a waypoint unlocks it for travel; click its gold lock before clearing it.";
    Gw2Ui::LabelWrapped(help, Gw2Ui::kTextSub, false, nullptr, 16.f);
    if (scope == Events)
    {
        const auto character = "Character: " + (EventProgressStore::CanMark(app.state.currentChar) ? app.state.currentChar : std::string("enter the game to mark events"));
        Gw2Ui::Label(character.c_str(), Gw2Ui::kTextSub, false, nullptr, 16.f);
    }
    Gw2Ui::SearchToolbar("##checklist_search", view.State.Search, sizeof(view.State.Search),
        scope == Events ? "Search events, zones or regions..." : "Search objectives...", [&] {
            static const Gw2Ui::ChipItem filters[] = {{"All"}, {"Unmarked"}, {"Marked"}};
            int filter = (int)view.State.Filter;
            Gw2Ui::ChipRow("##checklist_filter", filters, 3, &filter, 28.f, 16.f);
            view.State.Filter = (Gw2Ui::ChecklistFilter)filter;
            const auto found = app.zones.find(CurrentMapId());
            const bool haveZone = found != app.zones.end() && (scope == Events ? !app.eventAreas.EventsForMap(found->first).empty() : !found->second.Steps.empty());
            if (Gw2Ui::ActionButton("Current zone", 132.f, 26.f, Gw2Ui::ActionButtonVariant::Normal,
                                    haveZone ? "Expand and scroll to the current zone" : "No listed entries for the current zone", !haveZone))
                JumpTo(app, scope, CurrentMapId());
        });
    RefreshCounts(app, scope);
    Gw2Ui::ChecklistCallbacks callbacks;
    callbacks.CanMark = scope == Objectives || EventProgressStore::CanMark(app.state.currentChar);
    callbacks.DrawLeaf = [&](const auto& leaf, int depth, int row) { DrawItem(app, scope, leaf, depth, row); };
    callbacks.MarkGroup = [&](const auto& group, bool marked) { ApplyGroup(app, scope, group, marked); };
    callbacks.HoverGroup = [&](const auto& group) {
        const auto preview = view.Previews.find(group.Id);
        if (preview == view.Previews.end() || !preview->second.Available) return;
        const auto& map = preview->second;
        GroupTooltip(group.Label.c_str(), map.MinLevel, map.MaxLevel, group.Done, group.Total, map.Continent, map.Rect,
                     map.Pins.data(), (int)map.Pins.size(), true);
    };
    ImGui::BeginChild("##checklist_body", ImVec2(0.f, 0.f), false);
    if (!Gw2Ui::DrawChecklist(view.Roots, view.State, callbacks))
        Gw2Ui::EmptyState("No matching entries.", scope == Events ? "Try another filter or zone. Event coverage varies by map." : "Try another search or filter.");
    ImGui::EndChild();
}

void OpenEventChecklist(App& app, unsigned mapId)
{
    scope_ = Events;
    BuildTree(app, Events);
    JumpTo(app, Events, mapId);
    OpenSettingsTab(app, SettingsTabChecklist);
}

void WarmChecklist(App& app)
{
    BuildTree(app, Objectives);
    for (const auto& preview : views_[Objectives].Previews)
    {
        const auto& map = preview.second;
        if (map.Available) PrefetchRectTiles(map.Continent, map.Rect[0], map.Rect[1], map.Rect[2], map.Rect[3]);
    }
}
