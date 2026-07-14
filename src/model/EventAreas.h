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

class EventAreaData
{
public:
    bool Load(const std::string& path);
    bool Loaded() const { return loaded_; }

    const std::vector<EventArea>& Areas() const { return areas_; }
    std::vector<const EventArea*> AreasForMap(uint32_t mapId) const;

private:
    bool loaded_ = false;
    std::vector<EventArea> areas_;
    std::map<uint32_t, std::vector<size_t>> areasByMap_;
};
