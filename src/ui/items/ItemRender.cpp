#include "ui/items/ItemRender.h"

#include "app/StaticData.h"     // MaterialCategoryName (material tooltip line)
#include "app/TpEligibility.h"
#include "app/TpPrices.h"       // live Trading Post buy/sell prices (tooltip)
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"     // SetClipboard + ViewerAlert (right-click copy)
#include "ui/wiki/WikiReader.h" // Wiki::RequestOpen (deferred "Open in Wiki")
#include "util/Textures.h"      // Tex::GetTextureFromURL
#include "util/Trading.h"       // TP fee math (flip margin)

#include <cctype>
#include <cstdio>
#include <cstring>

namespace ItemUI
{
    ImU32 RarityColor(const std::string& rarity)
    {
        if (rarity == "Junk")      return IM_COL32(150, 150, 150, 220);
        if (rarity == "Fine")      return IM_COL32(96, 156, 255, 235);
        if (rarity == "Masterwork")return IM_COL32(80, 210, 120, 235);
        if (rarity == "Rare")      return IM_COL32(255, 220, 90, 240);
        if (rarity == "Exotic")    return IM_COL32(255, 150, 45, 245);
        if (rarity == "Ascended")  return IM_COL32(245, 88, 185, 245);
        if (rarity == "Legendary") return IM_COL32(160, 92, 255, 245);
        return IM_COL32(145, 132, 105, 190);
    }

    bool HasString(const std::vector<std::string>& values, const char* needle)
    {
        for (const std::string& v : values)
            if (v == needle) return true;
        return false;
    }

    std::string Humanize(std::string s)
    {
        if (s == "CraftingMaterial") return "Crafting Material";
        if (s == "MiniPet") return "Miniature";
        if (s == "UpgradeComponent") return "Upgrade Component";
        if (s == "Trinket") return "Trinket";
        std::string out;
        out.reserve(s.size() + 4);
        for (size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            if (i > 0 && std::isupper((unsigned char)c) && std::islower((unsigned char)s[i - 1]))
                out.push_back(' ');
            out.push_back(c == '_' ? ' ' : c);
        }
        return out;
    }

    static void ReplaceAll(std::string& s, const char* from, const char* to)
    {
        const size_t flen = std::strlen(from);
        if (flen == 0) return;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos)
        {
            s.replace(pos, flen, to);
            pos += std::strlen(to);
        }
    }

    std::string CleanDescription(std::string s)
    {
        ReplaceAll(s, "&lt;", "<");
        ReplaceAll(s, "&gt;", ">");
        ReplaceAll(s, "&amp;", "&");
        ReplaceAll(s, "&quot;", "\"");

        std::string out;
        out.reserve(s.size());
        bool inTag = false;
        for (char c : s)
        {
            if (c == '<') { inTag = true; continue; }
            if (c == '>') { inTag = false; continue; }
            if (inTag) continue;
            if (c == '\r') continue;
            out.push_back(c == '\n' ? ' ' : c);
        }
        while (!out.empty() && std::isspace((unsigned char)out.front())) out.erase(out.begin());
        while (!out.empty() && std::isspace((unsigned char)out.back())) out.pop_back();
        return out;
    }

    std::string JoinStrings(const std::vector<std::string>& values, const char* sep)
    {
        std::string out;
        for (const std::string& v : values)
        {
            if (v.empty()) continue;
            if (!out.empty()) out += sep;
            out += Humanize(v);
        }
        return out;
    }

    std::string CoinText(int copper)
    {
        if (copper <= 0) return std::string();
        const int g = copper / 10000;
        const int s = (copper / 100) % 100;
        const int c = copper % 100;
        char b[64];
        if (g > 0)      std::snprintf(b, sizeof(b), "Vendor value: %dg %ds %dc", g, s, c);
        else if (s > 0) std::snprintf(b, sizeof(b), "Vendor value: %ds %dc", s, c);
        else            std::snprintf(b, sizeof(b), "Vendor value: %dc", c);
        return b;
    }

    // A plain "Xg Ys Zc" coin string (no prefix) for the live TP price lines. Pads s/c so 1g 5s reads "1g 05s 00c".
    static std::string CoinsPlain(int copper)
    {
        if (copper <= 0) return "0c";
        const int g = copper / 10000, s = (copper / 100) % 100, c = copper % 100;
        char b[64];
        if (g > 0)      std::snprintf(b, sizeof(b), "%dg %02ds %02dc", g, s, c);
        else if (s > 0) std::snprintf(b, sizeof(b), "%ds %02dc", s, c);
        else            std::snprintf(b, sizeof(b), "%dc", c);
        return b;
    }

    // Per-frame gate set by item-cell callers (ItemsTab / Inventory / ItemsWidget) from Config.showTpPrices, so the
    // shared renderers can show live TP prices without an App& reference. Default on.
    static bool g_showTpPrices = true;
    void SetTpPrices(bool on) { g_showTpPrices = on; }

    static std::string DisplayName(int itemId, const AccountData::ItemMeta* meta, const NameFn& nameFor)
    {
        if (meta && !meta->name.empty()) return meta->name;
        if (nameFor)
        {
            std::string name = nameFor(itemId);
            if (!name.empty()) return name;
        }
        return itemId > 0 ? ("Item " + std::to_string(itemId)) : std::string();
    }

    void DrawCopyMenu(const char* popupId, bool openNow, const std::string& name, const std::string& chatLink)
    {
        if (name.empty() && chatLink.empty()) return;

        std::vector<Gw2Ui::MenuNode> nodes;
        if (!name.empty()) nodes.push_back({ "Copy name", 1 });
        if (!chatLink.empty()) nodes.push_back({ "Copy link", 2 });
        if (!name.empty()) nodes.push_back({ "Open in Wiki", 3 });

        const int action = Gw2Ui::ContextMenuTree(popupId, nodes, openNow);
        if (action == 1)
        {
            SetClipboard(name);
            ViewerAlert("Copied item name.");
        }
        else if (action == 2)
        {
            SetClipboard(chatLink);
            ViewerAlert("Copied item link.");
        }
        else if (action == 3)
        {
            Wiki::RequestOpen(name);   // deferred open (no App& in this render path)
        }
    }

    std::string DurationText(int ms)
    {
        if (ms <= 0) return std::string();
        const int sec = (ms + 999) / 1000;
        char b[48];
        if (sec >= 3600) std::snprintf(b, sizeof(b), "Duration: %dh %dm", sec / 3600, (sec / 60) % 60);
        else if (sec >= 60) std::snprintf(b, sizeof(b), "Duration: %dm %ds", sec / 60, sec % 60);
        else std::snprintf(b, sizeof(b), "Duration: %ds", sec);
        return b;
    }

    std::string DetailStr(const AccountData::ItemMeta& meta, const char* key)
    {
        if (!meta.details.is_object()) return std::string();
        auto it = meta.details.find(key);
        return (it != meta.details.end() && it->is_string()) ? it->get<std::string>() : std::string();
    }

    int DetailInt(const AccountData::ItemMeta& meta, const char* key)
    {
        if (!meta.details.is_object()) return 0;
        auto it = meta.details.find(key);
        return (it != meta.details.end() && it->is_number_integer()) ? it->get<int>() : 0;
    }

    std::string ItemTypeLine(const AccountData::ItemMeta& meta)
    {
        std::string line = meta.rarity.empty() ? "Basic" : meta.rarity;
        const std::string type = Humanize(meta.type);
        if (!type.empty()) { if (!line.empty()) line += " "; line += type; }
        return line;
    }

    std::string ItemDetailLine(const AccountData::ItemMeta& meta)
    {
        std::string detail = DetailStr(meta, "type");
        if (!detail.empty() && detail != meta.type) return Humanize(detail);
        return std::string();
    }

    static void TooltipNamedItemList(const NameFn& nameFor, const char* label, const std::vector<int>& ids)
    {
        if (ids.empty()) return;
        std::string line(label);
        line += ": ";
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (i) line += ", ";
            line += nameFor ? nameFor(ids[i]) : std::string();
        }
        Gw2Ui::TooltipMuted(line.c_str());
    }

    void DrawItemLoreBlock(const AccountData::ItemMeta& meta, const std::string& statSet, bool includeTypeLines)
    {
        if (includeTypeLines)
        {
            const std::string typeLine = ItemTypeLine(meta);
            if (!typeLine.empty()) Gw2Ui::TooltipText(typeLine.c_str());
            const std::string detailLine = ItemDetailLine(meta);
            if (!detailLine.empty()) Gw2Ui::TooltipMuted(detailLine.c_str());
            if (!statSet.empty()) { std::string s = "Stat: " + statSet; Gw2Ui::TooltipMuted(s.c_str()); }
        }

        const std::string desc = CleanDescription(!DetailStr(meta, "description").empty()
            ? DetailStr(meta, "description") : meta.description);
        if (!desc.empty())
        {
            Gw2Ui::TooltipSeparator();
            Gw2Ui::TooltipText(desc.c_str());
        }
        if (meta.level > 0)
        {
            char b[48];
            std::snprintf(b, sizeof(b), "Required level: %d", meta.level);
            Gw2Ui::TooltipMuted(b);
        }

        const int minPower = DetailInt(meta, "min_power");
        const int maxPower = DetailInt(meta, "max_power");
        const int defense = DetailInt(meta, "defense");
        const int size = DetailInt(meta, "size");
        const int charges = DetailInt(meta, "charges");
        const int duration = DetailInt(meta, "duration_ms");
        if (minPower > 0 || maxPower > 0)
        {
            char b[64];
            std::snprintf(b, sizeof(b), "Weapon strength: %d - %d", minPower, maxPower);
            Gw2Ui::TooltipMuted(b);
        }
        if (defense > 0)
        {
            char b[48];
            std::snprintf(b, sizeof(b), "Defense: %d", defense);
            Gw2Ui::TooltipMuted(b);
        }
        if (size > 0)
        {
            char b[48];
            std::snprintf(b, sizeof(b), "Slots: %d", size);
            Gw2Ui::TooltipMuted(b);
        }
        if (charges > 0)
        {
            char b[48];
            std::snprintf(b, sizeof(b), "Charges: %d", charges);
            Gw2Ui::TooltipMuted(b);
        }
        const std::string durationLine = DurationText(duration);
        if (!durationLine.empty()) Gw2Ui::TooltipMuted(durationLine.c_str());
        const std::string damageType = DetailStr(meta, "damage_type");
        if (!damageType.empty())
        {
            std::string line = "Damage: " + Humanize(damageType);
            Gw2Ui::TooltipMuted(line.c_str());
        }
        const std::string weight = DetailStr(meta, "weight_class");
        if (!weight.empty())
        {
            std::string line = "Weight: " + Humanize(weight);
            Gw2Ui::TooltipMuted(line.c_str());
        }
        if (meta.details.is_object())
        {
            auto infix = meta.details.find("infix_upgrade");
            if (infix != meta.details.end() && infix->is_object())
            {
                auto attrs = infix->find("attributes");
                if (attrs != infix->end() && attrs->is_array())
                {
                    for (const auto& a : *attrs)
                    {
                        if (!a.is_object()) continue;
                        const std::string attr = a.value("attribute", std::string());
                        const int mod = a.value("modifier", 0);
                        if (!attr.empty() && mod != 0)
                        {
                            char b[96];
                            std::snprintf(b, sizeof(b), "%+d %s", mod, Humanize(attr).c_str());
                            Gw2Ui::TooltipMuted(b);
                        }
                    }
                }
                auto buff = infix->find("buff");
                if (buff != infix->end() && buff->is_object())
                {
                    const std::string buffDesc = CleanDescription(buff->value("description", std::string()));
                    if (!buffDesc.empty()) Gw2Ui::TooltipText(buffDesc.c_str());
                }
            }
            auto bonuses = meta.details.find("bonuses");
            if (bonuses != meta.details.end() && bonuses->is_array())
                for (const auto& b : *bonuses)
                    if (b.is_string())
                    {
                        const std::string bonus = CleanDescription(b.get<std::string>());
                        if (!bonus.empty()) Gw2Ui::TooltipText(bonus.c_str());
                    }
            auto infusionFlags = meta.details.find("infusion_upgrade_flags");
            if (infusionFlags != meta.details.end() && infusionFlags->is_array())
            {
                std::vector<std::string> flags;
                for (const auto& f : *infusionFlags) if (f.is_string()) flags.push_back(f.get<std::string>());
                if (!flags.empty())
                {
                    std::string line = "Infusion slot: " + JoinStrings(flags);
                    Gw2Ui::TooltipMuted(line.c_str());
                }
            }
        }
    }

    void DrawItemTooltip(int itemId, const AccountData::ItemMeta* meta, const Instance& inst, const NameFn& nameFor,
                         const std::string& statSet, const std::function<void()>& extra)
    {
        const int count = inst.count;
        const std::string& location = inst.location;
        const Api::V2::ItemSlot* slot = inst.slot;
        const Api::V2::MaterialStack* material = inst.material;

        if (!Gw2Ui::TooltipBegin()) return;
        if (meta)
        {
            if (!meta->icon.empty())
            {
                char texId[40];
                std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", itemId);
                if (void* tex = Tex::GetTextureFromURL(texId, meta->icon.c_str()))
                {
                    const float tipIcon = std::max(46.f, 48.f * Gw2Ui::TextScale());
                    ImGui::Image((ImTextureID)tex, ImVec2(tipIcon, tipIcon));
                    ImGui::SameLine(0.f, 8.f);
                }
            }
            ImGui::BeginGroup();
            Gw2Ui::TooltipTitle(meta->name.empty() ? (nameFor ? nameFor(itemId) : std::string()).c_str() : meta->name.c_str());
            const std::string typeLine = ItemTypeLine(*meta);
            if (!typeLine.empty()) Gw2Ui::TooltipText(typeLine.c_str());
            const std::string detailLine = ItemDetailLine(*meta);
            if (!detailLine.empty()) Gw2Ui::TooltipMuted(detailLine.c_str());
            if (!statSet.empty()) { std::string s = "Stat: " + statSet; Gw2Ui::TooltipMuted(s.c_str()); }
            ImGui::EndGroup();
        }
        else
        {
            Gw2Ui::TooltipTitle((nameFor ? nameFor(itemId) : std::string()).c_str());
        }

        if (meta)
        {
            DrawItemLoreBlock(*meta, statSet, false);
        }

        Gw2Ui::TooltipSeparator();
        char cbuf[80];
        if (material)
            std::snprintf(cbuf, sizeof(cbuf), "Material Storage: %d in %s", std::max(1, material->count), StaticData::MaterialCategoryName(material->category).c_str());
        else if (count > 0)
            std::snprintf(cbuf, sizeof(cbuf), "Count: %d", std::max(1, count));
        else
            cbuf[0] = '\0';
        if (cbuf[0]) Gw2Ui::TooltipMuted(cbuf);
        if (!location.empty()) Gw2Ui::TooltipMuted(location.c_str());
        if (slot)
        {
            if (!slot->binding.empty())
            {
                std::string bind = "Binding: " + slot->binding;
                if (!slot->boundTo.empty()) bind += " to " + slot->boundTo;
                Gw2Ui::TooltipMuted(bind.c_str());
            }
            TooltipNamedItemList(nameFor, "Upgrades", slot->upgrades);
            TooltipNamedItemList(nameFor, "Infusions", slot->infusions);
        }
        if (material && !material->binding.empty())
        {
            std::string bind = "Binding: " + material->binding;
            Gw2Ui::TooltipMuted(bind.c_str());
        }
        if (meta)
        {
            if (!meta->restrictions.empty())
            {
                std::string line = "Restrictions: " + JoinStrings(meta->restrictions);
                Gw2Ui::TooltipMuted(line.c_str());
            }
            if (!meta->flags.empty())
            {
                std::vector<std::string> important;
                for (const std::string& f : meta->flags)
                    if (f != "HideSuffix" && f != "NoMysticForge" && f != "NoUnderwater")
                        important.push_back(f);
                if (!important.empty())
                {
                    std::string line = "Flags: " + JoinStrings(important);
                    Gw2Ui::TooltipMuted(line.c_str());
                }
            }
            const std::string value = CoinText(meta->vendorValue);
            if (!value.empty() && !HasString(meta->flags, "NoSell"))
                Gw2Ui::TooltipMuted(value.c_str());

            // Live Trading Post prices: Want() queues a batched fetch; Get() reads the session cache (60s TTL).
            if (g_showTpPrices && itemId > 0 && TpEligibility::IsTradeable(itemId))
            {
                TpPrices::Want(itemId);
                TpPrices::Price tp;
                if (TpPrices::Get(itemId, tp) && tp.tradeable && (tp.buyUnit > 0 || tp.sellUnit > 0))
                {
                    if (tp.sellUnit > 0) { std::string s = "TP sell: " + CoinsPlain(tp.sellUnit); Gw2Ui::TooltipMuted(s.c_str()); }
                    if (tp.buyUnit  > 0) { std::string s = "TP buy: "  + CoinsPlain(tp.buyUnit);  Gw2Ui::TooltipMuted(s.c_str()); }
                    if (tp.buyUnit > 0 && tp.sellUnit > 0)
                    {
                        const int profit = Trading::FlipProfit(tp.buyUnit, tp.sellUnit);
                        char b[96];
                        std::snprintf(b, sizeof(b), "Flip margin: %s%s (%.0f%%)", profit < 0 ? "-" : "",
                                      CoinsPlain(profit < 0 ? -profit : profit).c_str(),
                                      Trading::Roi(profit, tp.buyUnit) * 100.0);
                        Gw2Ui::TooltipMuted(b);
                    }
                }
            }
        }
        if (extra) extra();
        Gw2Ui::TooltipSeparator();
        if (meta && !meta->chatLink.empty()) Gw2Ui::TooltipMuted("Right-click for copy menu.");
        else Gw2Ui::TooltipMuted("Right-click to copy item name.");
        Gw2Ui::TooltipEnd();
    }

    bool DrawItemCell(int itemId, const AccountData::ItemMeta* meta, const Instance& inst, const NameFn& nameFor,
                      float cell, const std::string& statSet)
    {
        const int count = inst.count;
        if (g_showTpPrices && itemId > 0 && TpEligibility::IsTradeable(itemId))
            TpPrices::Want(itemId);   // prefetch visible TP cells so the hover tooltip is instant
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##itemcell", ImVec2(cell, cell));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b(p.x + cell, p.y + cell);
        const ImU32 bg = itemId > 0 ? IM_COL32(20, 18, 15, 185) : IM_COL32(95, 86, 70, 46);
        dl->AddRectFilled(p, b, bg);
        dl->AddRect(p, b, itemId > 0 && meta ? RarityColor(meta->rarity) : IM_COL32(115, 105, 88, 125), 0.f, 0, hov ? 2.0f : 1.0f);
        if (itemId > 0)
        {
            const float pad = std::max(3.f, 4.f * Gw2Ui::TextScale());
            if (meta && !meta->icon.empty())
            {
                char texId[40];
                std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", itemId);
                if (void* tex = Tex::GetTextureFromURL(texId, meta->icon.c_str()))
                    dl->AddImage((ImTextureID)tex, ImVec2(p.x + pad, p.y + pad), ImVec2(b.x - pad, b.y - pad));
            }
            else
            {
                dl->AddRectFilled(ImVec2(p.x + 8.f, p.y + 8.f), ImVec2(b.x - 8.f, b.y - 8.f), IM_COL32(60, 52, 40, 170));
            }
            if (count > 1)
            {
                char cb[16];
                std::snprintf(cb, sizeof(cb), "%d", count);
                Gw2Ui::LabelDL(dl, ImVec2(p.x + 2.f, b.y - 19.f), ImVec2(b.x - 4.f, b.y - 1.f), cb,
                               Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(255, 244, 220, 255), true, nullptr, 14.f);
            }
            if (hov)
            {
                DrawItemTooltip(itemId, meta, inst, nameFor, statSet);
            }
            DrawCopyMenu("##itemcopymenu", hov && ImGui::IsMouseClicked(ImGuiMouseButton_Right),
                         DisplayName(itemId, meta, nameFor), meta ? meta->chatLink : std::string());
        }
        return clicked;
    }

    ResultRow DrawResultRow(int rowIndex, float width, int id, const std::string& icon,
                            const std::string& name, const std::string& rarity, int count,
                            const std::string& chatLink)
    {
        const float rowH = 24.f * Gw2Ui::TextScale();
        ResultRow   out;
        ImGui::PushID(rowIndex);
        ImGui::PushID(id);                                   // per-row id scope -> Row()'s hover key is unique
        const Gw2Ui::RowHotspot h = Gw2Ui::Row("##rr", rowIndex, rowH, width);
        out.clicked = h.clicked;
        out.hovered = h.hovered;
        const ImVec2 p = h.min;
        const float rowW = h.width;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!icon.empty())
        {
            char texId[40]; std::snprintf(texId, sizeof(texId), "TC_ITEM_%d", id);
            if (void* tex = Tex::GetTextureFromURL(texId, icon.c_str()))
                dl->AddImage((ImTextureID)tex, ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(p.x + rowH - 2.f, p.y + rowH - 2.f));
        }
        dl->AddRect(ImVec2(p.x + 2.f, p.y + 2.f), ImVec2(p.x + rowH - 2.f, p.y + rowH - 2.f), RarityColor(rarity), 0.f, 0, 1.2f);

        const float ts = Gw2Ui::TextScale();   // text scales with the region (no-op at the default 1.0)
        float nameRight = p.x + rowW - 4.f;
        if (count > 0)
        {
            char cnt[16]; std::snprintf(cnt, sizeof(cnt), "x%d", count);
            const float cw = Gw2Ui::MeasureWidth(cnt, 14.f * ts) + 6.f;
            Gw2Ui::RowLabel(dl, h, rowW - cw, 2.f, cnt,
                            Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f * ts);
            nameRight = p.x + rowW - cw - 4.f;
        }
        Gw2Ui::RowLabel(dl, h, rowH + 4.f, std::max(0.f, p.x + rowW - nameRight), name.c_str(),
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, RarityColor(rarity), false, nullptr, 14.f * ts);
        if (out.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        DrawCopyMenu("##itemcopymenu", out.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right), name, chatLink);
        ImGui::PopID();
        ImGui::PopID();
        return out;
    }
}
