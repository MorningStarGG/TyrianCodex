#include "model/Dataset.h"
#include "model/EventTimers.h"
#include "app/ContentData.h"
#include "app/Config.h"
#include "guide/Progress.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <set>

using nlohmann::json;
namespace fs = std::filesystem;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const fs::path& path, const json& data) { fs::create_directories(path.parent_path()); std::ofstream(path) << data.dump(); }
std::time_t Utc(int year, int month, int day, int hour = 0, int minute = 0, int second = 0)
{
    std::tm date{}; date.tm_year = year - 1900; date.tm_mon = month - 1; date.tm_mday = day;
    date.tm_hour = hour; date.tm_min = minute; date.tm_sec = second;
    return _mkgmtime(&date);
}
void CheckBundle(const fs::path& data)
{
    std::map<uint32_t, Zone> zones;
    Check(LoadAllZones((data / "zones").string(), zones) >= 79, "Refreshed zone bundle must load in the runtime");
    for (uint32_t mapId : {1155u, 1483u})
        Check(zones.at(mapId).Loaded && !zones.at(mapId).Steps.empty() && !zones.at(mapId).CanGuide(),
              "Metadata-only hubs must expose objectives without enabling navigation");
    for (const auto& expected : std::map<uint32_t, int>{{62,28},{1165,7},{1195,22}})
    {
        int pois = 0;
        for (const Step& step : zones.at(expected.first).Steps) if (step.Type == "poi") ++pois;
        Check(pois == expected.second, "Restored map POI count must survive runtime loading");
    }
    const HeartTaskData* lattice = nullptr;
    for (const Step& step : zones.at(54).Steps) if (step.StepId == "54:heart:308") lattice = step.HeartTasks.get();
    Check(lattice && !lattice->Details.empty() && lattice->Details.front().Rows.size() == 20,
          "All configurator answers must reach runtime details");
    int verified = 0;
    for (const auto& anchor : lattice->Anchors) if (!anchor.HeightEstimated) ++verified;
    Check(verified == 10, "All verified configurator positions must reach runtime anchors");
    const HeartTaskData* headstones = nullptr;
    for (const Step& step : zones.at(26).Steps) if (step.StepId == "26:heart:5") headstones = step.HeartTasks.get();
    Check(headstones && !headstones->Details.empty(), "Headstone notes must reach the runtime");
    const auto& notes = headstones->Details.front();
    Check(notes.Rows.size() == 10, "All ten headstone answers must load");
    bool merin = false;
    for (const auto& row : notes.Rows)
        if (row.size() >= 2 && row[0] == "Merin Trollsbane" && row[1] == "Second line.") merin = true;
    Check(merin, "Merin Trollsbane must offer the second-line answer");
    int headstoneAnswers = 0;
    for (const auto& anchor : headstones->Anchors)
        if (!anchor.HeightEstimated && anchor.DetailIds == std::vector<std::string>{notes.Id}) ++headstoneAnswers;
    Check(headstoneAnswers == 10, "Every verified headstone must reference the shared answer list");
    Zone& ley = zones.at(1633);
    Check(ley.Steps.size() == 51 && ley.Release == "voe", "Leyspring objectives/release must load");
    Check(LoadZoneTrail((data / "zones").string(), ley) && ley.CanGuide(), "Leyspring authored geometry must load");
    std::set<std::string> identities;
    for (const Step& step : ley.Steps) identities.insert(step.StepId);
    for (int preference = 0; preference < 4; ++preference)
    {
        ley.Activate(preference);
        Check(ley.CanGuide() && !ley.TrailSections.empty() && ley.ActiveRouteAuthor == "lady", "Leyspring route preferences retain authored sections and author");
        std::set<std::string> active;
        for (const Step& step : ley.Steps) active.insert(step.StepId);
        Check(active == identities, "Route activation must preserve every objective identity");
    }
    std::vector<MapInfo> places;
    Check(LoadMapIndex((data / "maps/index.json").string(), places) > 0, "Atlas catalog must load");
    int convergences = 0;
    for (const auto& place : places) if (place.MapId == 1627 || place.MapId == 1629)
    {
        Check(place.Kind == "Convergence" && place.TravelMapId == 1633 && !place.ChatLink.empty(), "Convergences must travel to their Leyspring entrance");
        Check(place.HasUnavailableEncounterTracking(), "Unavailable Convergence tracking must survive runtime loading");
        ++convergences;
    }
    Check(convergences == 2, "Both Convergence map modes must remain catalogued");
}

int main(int argc, char** argv)
{
    fs::path temporary = fs::temp_directory_path();
    if (temporary.filename().empty()) temporary = temporary.parent_path();
    const fs::path root = temporary / ("tc-content-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (fs::absolute(root).parent_path() != fs::absolute(temporary) || !fs::create_directory(root))
    { std::cerr << "Cannot create an isolated temporary test directory\n"; return 1; }
    try
    {
        // Schema 1 retains chips; schema 2 adds linked actions/details/anchors to the same steps.
        Write(root / "zones/index.json", {{"zones", {{{"slug", "test"}}}}});
        Write(root / "zones/test.json", {{"zone", {{"mapId", 42}, {"name", "Test"}}}, {"hasTrail", false},
                                        {"steps", {{{"stepId", "42:heart:1"}, {"type", "heart"}, {"name", "Test heart"}}}}});
        ProgressStore progress;
        progress.Load((root / "progress.json").string());
        progress.SetComplete("Player", "42:heart:1", true);
        for (int schema : {1, 2})
        {
            json heart = {{"chips", {{{"label", "Interact"}, {"category", "interact"}, {"count", 1}, {"examples", {"Use the object"}}, {"progress", {"5%"}}}}}};
            if (schema == 2)
            {
                heart["actions"] = {{{"id", "a"}, {"text", "Use the object"}, {"shortText", "Use object"}, {"label", "Interact"}, {"category", "interact"}, {"progress", "5%"}, {"targetIds", {"t"}}}};
                heart["details"] = {{{"id", "d"}, {"title", "Answers"}, {"headers", {"Question", "Answer"}}, {"rows", {{"Dimensions", "22.04"}, {"Scanning", "16,128"}}}}};
                heart["details"][0]["rows"] = json::array({json::array({"Dimensions", "22.04"}), json::array({"Scanning", "16,128"})});
                heart["anchors"] = {{{"id", "p"}, {"actionId", "a"}, {"targetId", "t"}, {"detailIds", {"d"}}, {"worldPos", {1., 2., 3.}}, {"heightEstimated", false}}};
            }
            Write(root / "heart_tasks.json", {{"schemaVersion", schema}, {"tasksByStepId", {{"42:heart:1", heart}}}});
            std::map<uint32_t, Zone> zones;
            Check(LoadAllZones((root / "zones").string(), zones) == 1, "Metadata-only zone must load");
            Zone& zone = zones.at(42);
            Check(zone.Loaded && !zone.CanGuide(), "Metadata must not enable route guidance");
            const Step& step = zone.Steps.front();
            ProgressStore restored;
            restored.Load((root / "progress.json").string());
            Check(restored.IsComplete("Player", step.StepId), "Saved objective completion survives schema/data regeneration");
            Check(step.TaskChips.size() == 1 && step.TaskChips.front().Progress.front() == "5%", "Existing chips/progress must survive");
            Check(step.HeartTasks != nullptr, "Shared heart data must attach to step");
            if (schema == 2)
            {
                const HeartTaskData& tasks = *step.HeartTasks;
                Check(tasks.Actions.size() == 1 && tasks.Details.front().Rows.size() == 2 && tasks.Anchors.size() == 1, "Schema 2 guidance must load");
                Check(tasks.Anchors.front().ActionId == tasks.Actions.front().Id, "Anchor must reference action");
                Check(!tasks.Anchors.front().HeightEstimated && tasks.Anchors.front().Position.y == 2.f, "Verified height must survive");
                Check(tasks.Actions.front().ShortText == "Use object", "Short world action must load");
            }
            zone.HasAuthoredRoute = true;
            Check(!LoadZoneTrail((root / "zones").string(), zone) && !zone.CanGuide(), "Missing geometry must disable navigation");
            Check(!LoadZoneTrail((root / "zones").string(), zone), "Repeated failed geometry load must remain false");
        }
        Zone route; route.Loaded = true; route.HasAuthoredRoute = true; route.GeomLoaded = true;
        route.Trail = {{0, 0, 0}, {1, 0, 1}};
        Check(route.CanGuide(), "Valid authored geometry enables guidance");
        ContentData content;
        Write(root / "content.json", {{"raidOrder", {"nexus_of_eternity"}}, {"raids", {{"nexus_of_eternity", {{"name", "Nexus of Eternity"}, {"tracking", "unavailable"}, {"wings", json::array()}}}}}});
        content.Load((root / "content.json").string(), (root / "rotations.json").string());
        Check(content.Raids().size() == 1 && !content.Raids().front().trackingAvailable, "Unsupported raid tracking must stay explicit");
        Write(root / "timers.json", {{"events", {{{"key", "test"}, {"name", "Test"}, {"scheduleMinutes", {0}}}}},
                                     {"scheduleCoverage", {{"unresolved", {{{"title", "Shackles of the Ancients"}}}}}}});
        EventTimerData timers;
        Check(timers.Load((root / "timers.json").string()) && timers.UnresolvedSchedules().size() == 1, "Unresolved timer coverage must load");
        EventTimer event; event.ScheduleMinutes = {0}; event.DurationMinutes = 1;
        Check(EventSchedule::Next(event, 86399).secondsUntil == 1, "UTC midnight event boundary");
        Check(ContentSchedule::DayOfYearIndex(Utc(2025,2,28,23,59,59)) == 58, "Non-leap Feb 28");
        Check(ContentSchedule::DayOfYearIndex(Utc(2025,3,1)) == 60, "Non-leap Mar 1 skips Feb 29 slot");
        Check(ContentSchedule::DayOfYearIndex(Utc(2024,2,29)) == 59, "Leap Feb 29 slot");
        Check(ContentSchedule::DayOfYearIndex(Utc(2024,3,1)) == 60, "Leap Mar 1 alignment");
        Check(ContentSchedule::DayOfYearIndex(Utc(2100,3,1)) == 60, "Century non-leap alignment");
        Check(ContentSchedule::DayOfYearIndex(Utc(2000,2,29)) == 59, "400-year leap alignment");
        Check(ContentSchedule::DayOfYearIndex(Utc(2026,1,1)) == 0, "UTC year boundary");
        Config config;
        Check(config.worldHeartHints && config.worldHeartAnswers, "Heart world settings default on");
        if (argc == 2) CheckBundle(argv[1]);
        fs::remove_all(root);
        std::cout << "Content runtime regression checks passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; fs::remove_all(root); return 1; }
}
