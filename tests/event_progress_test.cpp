#include "guide/EventProgress.h"
#include "guide/EventList.h"
#include "guide/Progress.h"
#include "model/EventAreas.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace
{
    void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    struct Temp
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path() /
            ("tc-event-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Temp() { std::filesystem::create_directory(Root); }
        ~Temp() { std::error_code error; std::filesystem::remove_all(Root, error); }
        std::string Path(const char* name) const { return (Root / name).string(); }
        void Write(const char* name, const nlohmann::json& json) const { std::ofstream(Path(name)) << json.dump(); }
    };
    nlohmann::json Event(const char* id, const char* name, bool located = true, int map = 15)
    {
        nlohmann::json event = {{"eventId",id},{"name",name},{"mapId",map},{"level",8},
                                {"category","dynamic_event"},{"viewerEligible",true}};
        if (located) { event["world"] = {0,0}; event["continent"] = {100,100}; }
        return event;
    }

    void History()
    {
        Temp temp;
        EventProgressStore history;
        history.Load(temp.Path("event-progress.json"));
        Check(!history.SetComplete("default", "A", true) && !history.SetComplete("", "A", true), "Pre-login must not create event marks");
        auto version = history.Version();
        Check(history.SetMany("Alice", {"A","B","A",""}, true), "Bulk marking must work");
        Check(history.Version() == version + 1 && history.CompletedIds("Alice").size() == 2, "Bulk changes deduplicate and persist once");
        Check(!history.IsComplete("Bob", "A"), "Character histories must stay independent");
        version = history.Version();
        Check(!history.SetComplete("Alice", "A", true) && history.Version() == version, "An unchanged mark is not a mutation");
        EventProgressStore loaded;
        loaded.Load(temp.Path("event-progress.json"));
        Check(loaded.IsComplete("Alice", "A") && loaded.IsComplete("Alice", "B"), "History must survive addon reloads");
        loaded.SetComplete("Renamed", "C", true);
        loaded.RenameChar("Alice", "Renamed");
        Check(loaded.CompletedIds("Alice").empty() && loaded.CompletedIds("Renamed").size() == 3, "Rename must union marks");
        loaded.SetComplete("Renamed", "A", false);
        Check(!loaded.IsComplete("Renamed", "A") && loaded.IsComplete("Renamed", "B"), "Unticking clears just the selected event");
        loaded.PurgeChar("Renamed");
        Check(loaded.CompletedIds("Renamed").empty(), "Explicit character cleanup must include events");
        temp.Write("legacy.json", {{"Alice",{"A","A",42,"","removed-from-catalog"}}});
        loaded.Load(temp.Path("legacy.json"));
        Check(loaded.CompletedIds("Alice").size() == 2 && loaded.IsComplete("Alice", "removed-from-catalog"), "Catalog omissions must not discard history");
        std::ofstream(temp.Path("broken.json")) << "{invalid";
        loaded.Load(temp.Path("broken.json"));
        Check(loaded.CompletedIds("Alice").empty(), "Bad files must fail safely");
    }

    void ObjectiveCompatibility()
    {
        Temp temp;
        temp.Write("progress.json", {{"Alice",{"15:poi:1","15:waypoint:2"}}});
        temp.Write("confirmed.json", {{"Alice",{"15:waypoint:2"}}});
        ProgressStore progress;
        progress.Load(temp.Path("progress.json"));
        EventProgressStore history;
        history.Load(temp.Path("event-progress.json"));
        history.SetComplete("Alice", "event-a", true);
        Check(progress.TotalCompleted() == 2 && progress.CompletedIds("Alice").size() == 2, "Event marks must not affect objective/session totals");
        progress.ResetMany("Alice", {"15:poi:1","15:waypoint:2"});
        Check(progress.TotalCompleted() == 0 && progress.IsConfirmed("Alice", "15:waypoint:2") && history.IsComplete("Alice", "event-a"), "Map resets preserve events and confirmed waypoints");
        progress.MarkComplete("Alice", "15:waypoint:3");
        progress.SeedConfirmedFromCompleted({"15:waypoint:3"});
        progress.RenameChar("Alice", "Renamed");
        ProgressStore restored;
        restored.Load(temp.Path("progress.json"));
        Check(restored.IsComplete("Renamed", "15:waypoint:3") && restored.IsConfirmed("Renamed", "15:waypoint:2") &&
              restored.IsConfirmed("Renamed", "15:waypoint:3"), "Existing waypoint migration, renames and file formats must remain compatible");
    }

    void CatalogAndVisibility()
    {
        Temp temp;
        auto a = Event("A", "Defeat the bandit leader");
        auto b = Event("B", "Defeat the bandit leader");
        auto c = Event("C", "Escort the scholar", false);
        c["world"] = {1.e100,0}; // Unusable geometry must not discard the event's identity.
        auto d = Event("D", "Later event");
        auto excluded = Event("E", "Hero challenge"); excluded["viewerEligible"] = false;
        temp.Write("events.json", {{"schemaVersion",1},{"events",{a,b,c,d,excluded,Event("F","Other map",true,34)}}});
        EventAreaData catalog;
        Check(catalog.Load(temp.Path("events.json")), "Schema 1 event data must remain readable");
        Check(catalog.EventsForMap(15).size() == 4 && catalog.ById("C") && !catalog.ById("C")->HasWorldCoord, "Metadata-only events belong in the full checklist");
        const auto nearby = catalog.NearbyForMap(15,0,0);
        Check(nearby.size() == 3, "Nearby events require positions, including valid zero coordinates");
        EventProgressStore history;
        history.SetComplete("Alice", "A", true);
        auto visible = BuildEventList(nearby, history, "Alice",1,false);
        Check(visible.Remaining.size() == 1 && visible.Remaining.front().Area->EventId == "B" && visible.Completed.empty(), "Completion filtering must precede limits and preserve identical names");
        visible = BuildEventList(nearby, history, "Alice",1,true);
        Check(visible.Completed.size() == 1 && visible.Completed.front().Area->EventId == "A" && visible.RemainingCount == 2, "Show completed restores marked events alongside remaining ones");
        visible = BuildEventList(nearby, history, "Bob",8,false);
        Check(visible.Remaining.size() == 3, "Changing characters must change visible completion immediately");
        visible = BuildEventList(nearby, history, "Alice",8,true,[](const auto& event){ return event.EventId == "D"; });
        Check(visible.Remaining.size() == 1 && visible.Completed.empty(), "Search/filter predicates apply to both groups");
        history.SetMany("Alice", {"B","D"}, true);
        visible = BuildEventList(nearby,history,"Alice",8,true);
        Check(visible.Remaining.empty() && visible.Completed.size() == 3, "All-marked maps must still offer completed events");
        const auto version = catalog.Version();
        temp.Write("events.json", {{"events",{c}}});
        catalog.Load(temp.Path("events.json"));
        Check(catalog.Version() > version && history.IsComplete("Alice", "A"), "Catalog reloads invalidate views without pruning marks");
        temp.Write("duplicate.json", {{"events",{a,a}}});
        Check(!catalog.Load(temp.Path("duplicate.json")) && catalog.Areas().empty(), "Conflicting duplicate identities cannot produce inflated counts");
    }
}

int main(int argc, char** argv)
{
    try
    {
        History(); ObjectiveCompatibility(); CatalogAndVisibility();
        if (argc > 1)
        {
            EventAreaData catalog;
            Check(catalog.Load((std::filesystem::path(argv[1]) / "event_areas.json").string()), "The shipped event catalog must load");
            Check(catalog.EventsForMap(15).size() > 50, "Queensdale must have its full event list, not the nearby page");
        }
        std::cout << "Event history, catalog and visibility checks passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
