#pragma once
#include "Common.h"

// GET /v1/skin_details.json?skin_id= (anonymous, locale): one skin, fully typed. Like item_details, the
// type-specific block (armor/weapon/back) is keyed by the lowercased `type`; the per-type dye slots stay in a
// typed json field. Wiki: API:1/skin_details.
namespace Api::V1
{
    struct SkinDetails { std::string type; std::string weightClass; nlohmann::json dyeSlots; nlohmann::json raw; };
    struct Skin
    {
        int skinId = 0; std::string name; std::string type; std::vector<std::string> flags; std::vector<std::string> restrictions;
        std::string rarity; int iconFileId = 0; std::string iconFileSignature; SkinDetails details; nlohmann::json raw;
    };

    inline SkinDetails ParseSkinDetails(const nlohmann::json& d)
    {
        SkinDetails x;
        if (!d.is_object()) return x;
        x.type = Json::Str(d, "type"); x.weightClass = Json::Str(d, "weight_class"); x.dyeSlots = Json::Node(d, "dye_slots"); x.raw = d;
        return x;
    }
    inline Skin ParseSkin(const nlohmann::json& j)
    {
        Skin s;
        s.skinId = IntS(j, "skin_id"); s.name = Json::Str(j, "name"); s.type = Json::Str(j, "type");
        s.flags = Json::StrArray(j, "flags"); s.restrictions = Json::StrArray(j, "restrictions"); s.rarity = Json::Str(j, "rarity");
        s.iconFileId = IntS(j, "icon_file_id"); s.iconFileSignature = Json::Str(j, "icon_file_signature");
        if (!s.type.empty()) s.details = ParseSkinDetails(Json::Node(j, Lower(s.type).c_str()));
        s.raw = j;
        return s;
    }

    class SkinDetailsEndpoint
    {
    public:
        explicit SkinDetailsEndpoint(Connection* c) : _c(c) {}
        void ById(int skinId, std::function<void(Result<Skin>)> cb) const { FetchQ<Skin>(_c, "/v1/skin_details.json", "skin_id", std::to_string(skinId), kV1StaticTtl, &ParseSkin, std::move(cb)); }
    private:
        Connection* _c;
    };
}
