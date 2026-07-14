#include "Characters.h"

#include "../core/Connection.h"
#include "../core/Json.h"

namespace Api::V2
{
    // ---- parsers --------------------------------------------------------------------------------------------

    Character ParseCharacter(const nlohmann::json& j)
    {
        Character c;
        c.name       = Json::Str(j, "name");
        c.race       = Json::Str(j, "race");
        c.gender     = Json::Str(j, "gender");
        c.profession = Json::Str(j, "profession");
        c.level      = Json::Int(j, "level");
        c.age        = Json::Int(j, "age");
        c.deaths     = Json::Int(j, "deaths");
        c.raw        = j;
        return c;
    }

    static std::vector<std::string> ParseStrArrayBare(const nlohmann::json& j)
    {
        std::vector<std::string> out;
        if (j.is_array()) for (const auto& e : j) if (e.is_string()) out.push_back(e.get<std::string>());
        return out;
    }
    static std::vector<int> ParseIntArrayBare(const nlohmann::json& j)
    {
        std::vector<int> out;
        if (j.is_array()) for (const auto& e : j) if (e.is_number_integer()) out.push_back(e.get<int>());
        return out;
    }

    static CharacterCore ParseCharacterCore(const nlohmann::json& j)
    {
        CharacterCore c;
        c.name = Json::Str(j, "name"); c.race = Json::Str(j, "race"); c.gender = Json::Str(j, "gender");
        c.profession = Json::Str(j, "profession"); c.level = Json::Int(j, "level"); c.guild = Json::Str(j, "guild");
        c.age = Json::Int(j, "age"); c.created = Json::Str(j, "created"); c.title = Json::Str(j, "title");
        c.deaths = Json::Int(j, "deaths"); c.raw = j;
        return c;
    }

    // /v2/characters/:id/backstory -> { "backstory": ["7-54", ...] }
    static std::vector<std::string> ParseBackstory(const nlohmann::json& j) { return Json::StrArray(j, "backstory"); }

    // /v2/characters/:id/crafting -> { "crafting": [{discipline, rating, active}] }
    static std::vector<CharacterCraft> ParseCrafting(const nlohmann::json& j)
    {
        std::vector<CharacterCraft> out;
        for (const auto& e : Json::Node(j, "crafting"))
            if (e.is_object()) { CharacterCraft c; c.discipline = Json::Str(e, "discipline"); c.rating = Json::Int(e, "rating"); c.active = Json::Bool(e, "active"); c.raw = e; out.push_back(std::move(c)); }
        return out;
    }

    // /v2/characters/:id/equipment -> { "equipment": [slot...] } (newer) or a bare array (older).
    static std::vector<ItemSlot> ParseEquipment(const nlohmann::json& j)
    {
        std::vector<ItemSlot> out;
        const nlohmann::json& arr = j.is_array() ? j : Json::Node(j, "equipment");
        if (arr.is_array()) { out.reserve(arr.size()); for (const auto& e : arr) out.push_back(ParseItemSlot(e)); }
        return out;
    }

    // /v2/characters/:id/equipmenttabs?tabs=all -> [{tab, name, is_active, equipment:[...]}]
    static std::vector<EquipmentTab> ParseEquipmentTabs(const nlohmann::json& j)
    {
        std::vector<EquipmentTab> out;
        if (j.is_array()) for (const auto& e : j)
        {
            EquipmentTab t; t.tab = Json::Int(e, "tab"); t.name = Json::Str(e, "name"); t.isActive = Json::Bool(e, "is_active");
            for (const auto& s : Json::Node(e, "equipment")) t.equipment.push_back(ParseItemSlot(s));
            t.raw = e; out.push_back(std::move(t));
        }
        return out;
    }

    // /v2/characters/:id/recipes -> { "recipes": [ids] } (or a bare array).
    static std::vector<int> ParseCharacterRecipes(const nlohmann::json& j)
    {
        if (j.is_array()) return ParseIntArrayBare(j);
        return Json::IntArray(j, "recipes");
    }

    // /v2/characters/:id/sab -> { zones:[...], unlocks:[{id,name}], songs:[{id,name}] }
    static SabProgress ParseSab(const nlohmann::json& j)
    {
        SabProgress s;
        for (const auto& u : Json::Node(j, "unlocks")) if (u.is_object()) s.unlocks.push_back(Json::Int(u, "id"));
        for (const auto& g : Json::Node(j, "songs"))   if (g.is_object()) s.songs.push_back(Json::Int(g, "id"));
        s.raw = j;   // the per-world `zones` (mode/world) tree stays here
        return s;
    }

    static SkillSet ParseSkillSet(const nlohmann::json& j)
    {
        SkillSet s; s.heal = Json::Int(j, "heal"); s.utilities = Json::IntArray(j, "utilities"); s.elite = Json::Int(j, "elite");
        s.legends = Json::StrArray(j, "legends");   // revenant heal/utility/elite legend ids (strings)
        s.raw = j; return s;
    }
    // /v2/characters/:id/skills -> { "skills": { pve:{...}, pvp:{...}, wvw:{...} } }
    static CharacterSkills ParseSkills(const nlohmann::json& j)
    {
        CharacterSkills s; const nlohmann::json& sk = Json::Node(j, "skills");
        s.pve = ParseSkillSet(Json::Node(sk, "pve")); s.pvp = ParseSkillSet(Json::Node(sk, "pvp")); s.wvw = ParseSkillSet(Json::Node(sk, "wvw"));
        s.raw = j; return s;
    }

    static std::vector<SpecLine> ParseSpecLines(const nlohmann::json& arr)
    {
        std::vector<SpecLine> out;
        if (arr.is_array()) for (const auto& e : arr)
            if (e.is_object()) { SpecLine l; l.id = Json::Int(e, "id"); l.traits = Json::IntArray(e, "traits"); l.raw = e; out.push_back(std::move(l)); }
        return out;
    }
    // /v2/characters/:id/specializations -> { "specializations": { pve:[...], pvp:[...], wvw:[...] } }
    static CharacterSpecializations ParseSpecializations(const nlohmann::json& j)
    {
        CharacterSpecializations s; const nlohmann::json& sp = Json::Node(j, "specializations");
        s.pve = ParseSpecLines(Json::Node(sp, "pve")); s.pvp = ParseSpecLines(Json::Node(sp, "pvp")); s.wvw = ParseSpecLines(Json::Node(sp, "wvw"));
        s.raw = j; return s;
    }

    // /v2/characters/:id/training -> { "training": [{id, spent, done}] }
    static std::vector<TrainingTrack> ParseTraining(const nlohmann::json& j)
    {
        std::vector<TrainingTrack> out;
        for (const auto& e : Json::Node(j, "training"))
            if (e.is_object()) { TrainingTrack t; t.id = Json::Int(e, "id"); t.spent = Json::Int(e, "spent"); t.done = Json::Bool(e, "done"); t.raw = e; out.push_back(std::move(t)); }
        return out;
    }

    // /v2/characters/:id/inventory -> { "bags": [{id, size, inventory:[slot|null]}|null] }
    static std::vector<CharacterBag> ParseInventory(const nlohmann::json& j)
    {
        std::vector<CharacterBag> out;
        for (const auto& b : Json::Node(j, "bags"))
        {
            CharacterBag bag;
            if (b.is_object())
            {
                bag.id = Json::Int(b, "id"); bag.size = Json::Int(b, "size");
                for (const auto& s : Json::Node(b, "inventory")) bag.inventory.push_back(ParseItemSlot(s));
                bag.raw = b;
            }
            out.push_back(std::move(bag));   // null bag slot -> default (id 0), keeps positions aligned
        }
        return out;
    }

    // /v2/characters/:id/buildtabs?tabs=all -> [{tab, is_active, build:{...}}]
    static std::vector<CharacterBuildTab> ParseBuildTabs(const nlohmann::json& j)
    {
        std::vector<CharacterBuildTab> out;
        if (j.is_array()) for (const auto& e : j) { CharacterBuildTab t; t.tab = Json::Int(e, "tab"); t.isActive = Json::Bool(e, "is_active"); t.raw = e; out.push_back(std::move(t)); }
        return out;
    }

    // ---- request plumbing -----------------------------------------------------------------------------------

    Request CharactersEndpoint::MakeReq(const std::string& name, const char* sub, TokenPermission scope) const
    {
        Request req;
        req.path = "/v2/characters/" + UrlEncode(name) + "/" + sub;
        req.auth = true; req.hasScope = true; req.scope = scope;
        req.cacheTtlSec = 30;
        return req;
    }

    template <typename T>
    void CharactersEndpoint::GetSub(const std::string& name, const char* sub, TokenPermission scope, T (*parse)(const nlohmann::json&), std::function<void(Result<T>)> cb) const
    {
        _c->Get<T>(MakeReq(name, sub, scope), parse, std::move(cb));
    }

    void CharactersEndpoint::Get(const std::string& name, std::function<void(Result<Character>)> cb) const
    {
        Request req;
        req.path        = "/v2/characters/" + UrlEncode(name);
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Characters;
        req.cacheTtlSec = 30;   // level changes slowly; a short TTL avoids hammering on every character switch
        _c->Get<Character>(std::move(req), &ParseCharacter, std::move(cb));
    }

    void CharactersEndpoint::Names(std::function<void(Result<std::vector<std::string>>)> cb) const
    {
        Request req;
        req.path = "/v2/characters";
        req.auth = true; req.hasScope = true; req.scope = TokenPermission::Characters;
        req.cacheTtlSec = 30;
        _c->Get<std::vector<std::string>>(std::move(req), &ParseStrArrayBare, std::move(cb));
    }

    void CharactersEndpoint::HeroPoints(const std::string& name, std::function<void(Result<std::vector<std::string>>)> cb) const { GetSub(name, "heropoints", TokenPermission::Characters, &ParseStrArrayBare, std::move(cb)); }
    void CharactersEndpoint::Core(const std::string& name,       std::function<void(Result<CharacterCore>)> cb) const            { GetSub(name, "core",      TokenPermission::Characters, &ParseCharacterCore, std::move(cb)); }
    void CharactersEndpoint::Backstory(const std::string& name,  std::function<void(Result<std::vector<std::string>>)> cb) const { GetSub(name, "backstory", TokenPermission::Characters, &ParseBackstory, std::move(cb)); }
    void CharactersEndpoint::Crafting(const std::string& name,   std::function<void(Result<std::vector<CharacterCraft>>)> cb) const { GetSub(name, "crafting", TokenPermission::Characters, &ParseCrafting, std::move(cb)); }
    void CharactersEndpoint::Equipment(const std::string& name,  std::function<void(Result<std::vector<ItemSlot>>)> cb) const     { GetSub(name, "equipment", TokenPermission::Characters, &ParseEquipment, std::move(cb)); }
    void CharactersEndpoint::Quests(const std::string& name,     std::function<void(Result<std::vector<int>>)> cb) const          { GetSub(name, "quests",    TokenPermission::Characters, &ParseIntArrayBare, std::move(cb)); }
    void CharactersEndpoint::Recipes(const std::string& name,    std::function<void(Result<std::vector<int>>)> cb) const          { GetSub(name, "recipes",   TokenPermission::Characters, &ParseCharacterRecipes, std::move(cb)); }
    void CharactersEndpoint::Sab(const std::string& name,        std::function<void(Result<SabProgress>)> cb) const               { GetSub(name, "sab",       TokenPermission::Characters, &ParseSab, std::move(cb)); }
    void CharactersEndpoint::Skills(const std::string& name,     std::function<void(Result<CharacterSkills>)> cb) const           { GetSub(name, "skills",    TokenPermission::Characters, &ParseSkills, std::move(cb)); }
    void CharactersEndpoint::Specializations(const std::string& name, std::function<void(Result<CharacterSpecializations>)> cb) const { GetSub(name, "specializations", TokenPermission::Characters, &ParseSpecializations, std::move(cb)); }
    void CharactersEndpoint::Training(const std::string& name,   std::function<void(Result<std::vector<TrainingTrack>>)> cb) const { GetSub(name, "training",  TokenPermission::Characters, &ParseTraining, std::move(cb)); }
    void CharactersEndpoint::Dungeons(const std::string& name,   std::function<void(Result<std::vector<std::string>>)> cb) const  { GetSub(name, "dungeons",  TokenPermission::Characters, &ParseStrArrayBare, std::move(cb)); }
    void CharactersEndpoint::Inventory(const std::string& name,  std::function<void(Result<std::vector<CharacterBag>>)> cb) const { GetSub(name, "inventory", TokenPermission::Inventories, &ParseInventory, std::move(cb)); }

    void CharactersEndpoint::EquipmentTabs(const std::string& name, std::function<void(Result<std::vector<EquipmentTab>>)> cb) const
    {
        Request req = MakeReq(name, "equipmenttabs", TokenPermission::Characters);
        req.query = { { "tabs", "all" } };
        _c->Get<std::vector<EquipmentTab>>(std::move(req), &ParseEquipmentTabs, std::move(cb));
    }
    void CharactersEndpoint::BuildTabs(const std::string& name, std::function<void(Result<std::vector<CharacterBuildTab>>)> cb) const
    {
        Request req = MakeReq(name, "buildtabs", TokenPermission::Builds);
        req.query = { { "tabs", "all" } };
        _c->Get<std::vector<CharacterBuildTab>>(std::move(req), &ParseBuildTabs, std::move(cb));
    }
}
