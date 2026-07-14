#pragma once
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/Permissions.h"   // TokenPermission (sub-resource scopes)
#include "../core/Request.h"       // Request (MakeReq return type)
#include "../core/Result.h"
#include "Common.h"                // ItemSlot (equipment / inventory)

// GET /v2/characters and /v2/characters/{name}/* (authenticated; scope: characters). The full character object
// drives zone recommendation (its `level`); every sub-resource is typed (no bare-raw) - backstory/quests are
// id vectors, equipment/inventory reuse ItemSlot, and the build-mode objects (specializations/skills/training)
// are small typed structs (the deep build payload kept in `raw`). Wiki: API:2/characters.
namespace Api { class Connection; }
namespace Api::V2
{
    struct Character
    {
        std::string    name;
        std::string    race;
        std::string    gender;
        std::string    profession;
        int            level = 0;
        int            age   = 0;     // seconds played
        int            deaths = 0;
        nlohmann::json raw;           // full object - reach any field not typed above
    };

    // /v2/characters/:id/core (the light identity object; its race selects the personal-story arc).
    struct CharacterCore { std::string name, race, gender, profession; int level = 0; std::string guild; int age = 0; std::string created; std::string title; int deaths = 0; nlohmann::json raw; };

    struct CharacterCraft { std::string discipline; int rating = 0; bool active = false; nlohmann::json raw; };

    struct EquipmentTab { int tab = 0; std::string name; bool isActive = false; std::vector<ItemSlot> equipment; nlohmann::json raw; };
    struct CharacterBag { int id = 0; int size = 0; std::vector<ItemSlot> inventory; nlohmann::json raw; };

    struct SpecLine { int id = 0; std::vector<int> traits; nlohmann::json raw; };
    struct CharacterSpecializations { std::vector<SpecLine> pve, pvp, wvw; nlohmann::json raw; };
    struct SkillSet  { int heal = 0; std::vector<int> utilities; int elite = 0; std::vector<std::string> legends; nlohmann::json raw; };   // revenant legends are string ids
    struct CharacterSkills { SkillSet pve, pvp, wvw; nlohmann::json raw; };
    struct TrainingTrack { int id = 0; int spent = 0; bool done = false; nlohmann::json raw; };
    struct CharacterBuildTab { int tab = 0; bool isActive = false; nlohmann::json raw; };   // the build object in raw
    struct SabProgress { std::vector<int> unlocks; std::vector<int> songs; nlohmann::json raw; };   // zones in raw

    Character ParseCharacter(const nlohmann::json& j);

    class CharactersEndpoint
    {
    public:
        explicit CharactersEndpoint(Connection* c) : _c(c) {}

        // The full character object (its level drives zone recommendations) + the account's character names.
        void Get(const std::string& name, std::function<void(Result<Character>)> cb) const;
        void Names(std::function<void(Result<std::vector<std::string>>)> cb) const;

        // Per-character sub-resources (scope: characters unless noted; inventory = inventories, buildtabs = builds).
        void HeroPoints(const std::string& name,      std::function<void(Result<std::vector<std::string>>)> cb) const;   // unlocked hero-point ids
        void Core(const std::string& name,            std::function<void(Result<CharacterCore>)> cb) const;
        void Backstory(const std::string& name,       std::function<void(Result<std::vector<std::string>>)> cb) const;   // backstory answer ids
        void Crafting(const std::string& name,        std::function<void(Result<std::vector<CharacterCraft>>)> cb) const;
        void Equipment(const std::string& name,       std::function<void(Result<std::vector<ItemSlot>>)> cb) const;
        void EquipmentTabs(const std::string& name,   std::function<void(Result<std::vector<EquipmentTab>>)> cb) const;   // ?tabs=all
        void Quests(const std::string& name,          std::function<void(Result<std::vector<int>>)> cb) const;           // selected story-step quest ids
        void Recipes(const std::string& name,         std::function<void(Result<std::vector<int>>)> cb) const;
        void Sab(const std::string& name,             std::function<void(Result<SabProgress>)> cb) const;
        void Skills(const std::string& name,          std::function<void(Result<CharacterSkills>)> cb) const;
        void Specializations(const std::string& name, std::function<void(Result<CharacterSpecializations>)> cb) const;
        void Training(const std::string& name,        std::function<void(Result<std::vector<TrainingTrack>>)> cb) const;
        void Dungeons(const std::string& name,        std::function<void(Result<std::vector<std::string>>)> cb) const;   // frequenter paths (deprecated upstream)
        void Inventory(const std::string& name,       std::function<void(Result<std::vector<CharacterBag>>)> cb) const;  // scope: inventories
        void BuildTabs(const std::string& name,       std::function<void(Result<std::vector<CharacterBuildTab>>)> cb) const; // ?tabs=all; scope: builds

    private:
        Request MakeReq(const std::string& name, const char* sub, TokenPermission scope) const;
        template <typename T>
        void GetSub(const std::string& name, const char* sub, TokenPermission scope, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb) const;
        Connection* _c;
    };
}
