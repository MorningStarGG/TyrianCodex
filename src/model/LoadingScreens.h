#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct LoadingScreen
{
    uint32_t    MapId = 0;
    std::vector<uint32_t> MapIds;
    std::string Kind;
    std::string Name;
    std::string Mode;
    std::string RelPath;
    std::string WikiPage;
    std::string WikiUrl;
    std::string FileTitle;
};

class LoadingScreenData
{
public:
    bool Load(const std::string& path);
    bool Loaded() const { return loaded_; }

    const LoadingScreen* ForMap(uint32_t mapId) const;
    const std::vector<LoadingScreen>& Screens() const { return screens_; }

private:
    bool loaded_ = false;
    std::vector<LoadingScreen> screens_;
    std::map<uint32_t, size_t> byMap_;
};
