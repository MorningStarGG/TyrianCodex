#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct EventArea
{
    std::string              EventId;
    std::string              Name;
    uint32_t                 MapId = 0;
    std::string              Category;
    std::vector<std::string> Flags;
    int                      Level = 0;
    bool                     ViewerEligible = false;
    bool                     HasWorldCoord = false;
    bool                     HasContinentCoord = false;
    std::string              LocationType;
    float                    WorldX = 0.f;
    float                    WorldZ = 0.f;
    float                    ContinentX = 0.f;
    float                    ContinentY = 0.f;
    float                    RadiusMeters = -1.f;
    std::string              WikiTitle;
    std::string              WikiUrl;
    std::string              Description;
};

struct NearbyEventArea
{
    const EventArea* Area = nullptr;
    float DistanceMeters = 0.f;
    float EdgeDistanceMeters = 0.f;
};

class EventAreaData
{
public:
    bool Load(const std::string& path);
    bool Loaded() const { return loaded_; }
    uint64_t Version() const { return version_; }

    const std::vector<EventArea>& Areas() const { return areas_; }
    std::vector<const EventArea*> AreasForMap(uint32_t mapId) const;
    // Full tracked catalog, independent of proximity, coordinates and routes.
    std::vector<const EventArea*> EventsForMap(uint32_t mapId) const;
    const EventArea* ById(const std::string& id) const;
    std::vector<NearbyEventArea> NearbyForMap(uint32_t mapId, float x, float z, int minLevel = 0, int maxLevel = 80) const;

private:
    bool loaded_ = false;
    uint64_t version_ = 0;
    std::vector<EventArea> areas_;
    std::map<uint32_t, std::vector<size_t>> areasByMap_;
    std::map<std::string, size_t> areasById_;
};
