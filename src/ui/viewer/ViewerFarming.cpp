#include "ui/viewer/ViewerFarming.h"
#include "ui/viewer/ViewerLayout.h"   // ViewerScale (uniform viewer zoom)
#include "ui/Gw2Ui.h"
#include "guide/FarmCategories.h"
#include "util/Draw.h"        // kFontSizePx / kFontSizeCount
#include "util/Textures.h"    // Tex::GetBundledTexture
#include "Shared.h"           // MumbleLink (player position for nearest distance)
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{
    void* MatIcon(const std::string& icon)
    {
        return icon.empty() ? nullptr : Tex::GetBundledTexture(("data\\textures\\markers\\gather\\" + icon + ".png").c_str());
    }

    ImU32 CatDot(const std::string& cat, int a)
    {
        if (cat == "ore" || cat == "special") return IM_COL32(176, 156, 120, a);
        if (cat == "wood")                     return IM_COL32(156, 116, 74, a);
        if (cat == "plant")                    return IM_COL32(118, 186, 96, a);
        if (cat == "seafood")                  return IM_COL32(96, 166, 196, a);
        return IM_COL32(206, 176, 96, a);
    }

    struct MatGroup
    {
        std::string mat, icon, category;
        int   total = 0, done = 0;
        float nearest = FLT_MAX;
    };
}

void DrawFarmingTab(App& app)
{
    GuideState& st  = app.state;
    Config&     cfg = app.config;
    const float vs  = ViewerScale(app);   // uniform viewer zoom for LAYOUT px (text already scaled via PushTextScale)
    const float fs  = kFontSizePx[std::clamp(cfg.viewerFontSize, 0, kFontSizeCount - 1)];
    const float lfs = kFontSizePx[std::clamp(cfg.listFontSize,   0, kFontSizeCount - 1)];
    const uint32_t mapId = st.activeGatherMapId;

    if (st.activeGather.empty())
    {
        Gw2Ui::Label("No gathering nodes on this map.", Gw2Ui::kTextSub, false, nullptr, lfs - 2.f);
        return;
    }

    const float ax = MumbleLink ? MumbleLink->AvatarPosition.X : 0.f;
    const float az = MumbleLink ? MumbleLink->AvatarPosition.Z : 0.f;

    // Group the current map's nodes by material (icon + counts + nearest ungathered), and note which categories
    // exist on this map (so Row 1 only offers chips that have content here).
    std::map<std::string, MatGroup> mats;
    bool catPresent[Farm::kCategoryCount] = { false };
    catPresent[0] = true;   // All
    for (const auto& g : st.activeGather)
    {
        const FarmNode* nd = g.Node; if (!nd) continue;
        for (int c = 1; c < Farm::kCategoryCount; ++c)
            if (Farm::CategoryMatch(c, nd->Category)) catPresent[c] = true;
        MatGroup& m = mats[nd->Material];
        if (m.mat.empty()) { m.mat = nd->Material; m.category = nd->Category; }
        if (m.icon.empty()) m.icon = nd->Icon;
        ++m.total;
        if (st.farmGathered.count(nd)) ++m.done;
        else m.nearest = std::min(m.nearest, (float)std::hypot(g.X - ax, g.Z - az));
    }

    // Clamp a stale category (one no longer present on this map) back to All.
    if (cfg.gatherCategory < 0 || cfg.gatherCategory >= Farm::kCategoryCount || !catPresent[cfg.gatherCategory])
        cfg.gatherCategory = 0;

    // --- Row 1: category chips (single-select scope -> Config.gatherCategory). ---
    {
        const float gap = 4.f * vs, h = 28.f * vs, cw = 34.f * vs;
        // Build only the PRESENT categories; ChipRow equal-shares when one row fits, else wraps at the cw floor.
        std::vector<Gw2Ui::ChipItem> items;
        std::vector<int> presentCat;
        for (int c = 0; c < Farm::kCategoryCount; ++c)
        {
            if (!catPresent[c]) continue;
            Gw2Ui::ChipItem ci;
            ci.label   = Farm::kCategoryNames[c];
            ci.tooltip = Farm::CategoryDesc(c);
            items.push_back(ci);
            presentCat.push_back(c);
        }
        int sel = 0;
        for (int i = 0; i < (int)presentCat.size(); ++i) if (presentCat[i] == cfg.gatherCategory) { sel = i; break; }
        if (Gw2Ui::ChipRow("fcat", items.data(), (int)items.size(), &sel, h, fs - 5.f, gap, cw, true))
        { cfg.gatherCategory = presentCat[sel]; app.settingsDirty = true; }
        ImGui::Spacing();
    }

    // Materials in the selected category, ordered nearest-ungathered-first then by name.
    std::vector<const MatGroup*> inCat;
    for (auto& kv : mats)
        if (Farm::CategoryMatch(cfg.gatherCategory, kv.second.category))
            inCat.push_back(&kv.second);
    std::sort(inCat.begin(), inCat.end(), [](const MatGroup* a, const MatGroup* b) {
        const bool ad = a->done >= a->total, bd = b->done >= b->total;
        if (ad != bd) return !ad;                        // unfinished first
        if (a->nearest != b->nearest) return a->nearest < b->nearest;
        return a->mat < b->mat;
    });

    // --- Row 2: material chips (multi-toggle; lit = enabled via Config.GatherShown). Flow-wrap. ---
    {
        const float gap = 4.f * vs, cw = 38.f * vs, ch = 34.f * vs;
        std::vector<Gw2Ui::ChipItem> items;
        std::vector<std::string> matOf;
        std::vector<std::string> tips;          // own the tooltip strings for the frame
        items.reserve(inCat.size()); matOf.reserve(inCat.size()); tips.reserve(inCat.size());
        for (const MatGroup* m : inCat)
        {
            const bool lit = cfg.GatherShown(mapId, m->mat);
            void* tex = MatIcon(m->icon);
            const std::string cat = m->category;
            tips.push_back(Farm::MaterialDesc(m->mat));
            Gw2Ui::ChipItem ci;
            ci.label    = nullptr;
            ci.tooltip  = tips.back().c_str();
            ci.width    = cw;
            ci.drawIcon = [tex, lit, cat](ImDrawList* dl, ImVec2 c, float is) {
                const int a = lit ? 255 : 90;
                if (tex) dl->AddImage((ImTextureID)tex, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f),
                                      ImVec2(c.x + is * 0.5f, c.y + is * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
                                      IM_COL32(255, 255, 255, a));
                else dl->AddCircleFilled(c, is * 0.42f, CatDot(cat, a), 14);
            };
            items.push_back(std::move(ci));
            matOf.push_back(m->mat);
        }
        Gw2Ui::ChipFlowMulti("fmat", items.data(), (int)items.size(),
            [&](int i) { return cfg.GatherShown(mapId, matOf[i]); },
            [&](int i) { const bool l = cfg.GatherShown(mapId, matOf[i]); cfg.SetGatherShown(mapId, matOf[i], !l); app.settingsDirty = true; },
            ch, fs, gap);
        ImGui::Spacing();
    }

    // --- Node list: every enabled, not-yet-gathered node in the selected category, rendered as step-viewer rows
    // (row stripe/hover + material icon + name + a right-aligned distance pill), nearest-first, the nearest one
    // highlighted as the current target. Cached behind a change token (map / category / enabled-set / gathered-set
    // / >2 m move) so the per-frame path is just a clipper over the cached order -- thousands of nodes stay cheap.
    static std::vector<int> s_order;     // indices into st.activeGather
    static uint32_t s_mapId    = 0xFFFFFFFFu;
    static int      s_cat      = -1;
    static size_t   s_hidden   = (size_t)-1;
    static size_t   s_gathered = (size_t)-1;
    static float    s_px = 1e30f, s_pz = 1e30f;

    size_t hiddenN = 0;
    if (auto hit = cfg.gatherHidden.find(mapId); hit != cfg.gatherHidden.end()) hiddenN = hit->second.size();
    bool rebuild = mapId != s_mapId || cfg.gatherCategory != s_cat || hiddenN != s_hidden ||
                   st.farmGathered.size() != s_gathered;
    { const float dx = ax - s_px, dz = az - s_pz; if (dx * dx + dz * dz > 4.f) rebuild = true; }   // re-sort on >2 m move
    if (rebuild)
    {
        s_order.clear();
        for (int i = 0; i < (int)st.activeGather.size(); ++i)
        {
            const auto& g = st.activeGather[i];
            const FarmNode* nd = g.Node; if (!nd) continue;
            if (st.farmGathered.count(nd)) continue;                                   // gathered this session -> drop
            if (!cfg.GatherShown(mapId, nd->Material)) continue;                       // disabled material (Row 2)
            if (!Farm::CategoryMatch(cfg.gatherCategory, nd->Category)) continue;      // not in the selected category
            s_order.push_back(i);
        }
        std::sort(s_order.begin(), s_order.end(), [&](int a, int b) {
            const auto& ga = st.activeGather[a]; const auto& gb = st.activeGather[b];
            const float da = (ga.X - ax) * (ga.X - ax) + (ga.Z - az) * (ga.Z - az);
            const float db = (gb.X - ax) * (gb.X - ax) + (gb.Z - az) * (gb.Z - az);
            return da < db;
        });
        s_mapId = mapId; s_cat = cfg.gatherCategory; s_hidden = hiddenN; s_gathered = st.farmGathered.size();
        s_px = ax; s_pz = az;
    }

    // Header block: "Gathering"  [ Reset run ]  [ N nodes ] + a divider, with real gaps so neither the pills nor
    // the node list sit on the line. Reserved as ONE plain Dummy (normal layout flow), so the cursor advances
    // cleanly past it and the node-list clipper below starts where it leaves off. Content is painted on top via
    // the draw list; the Reset pill is hit-tested by hand (no item -> no line-flow surprises).
    {
        char hdr[24]; std::snprintf(hdr, sizeof(hdr), "%d node%s", (int)s_order.size(), s_order.size() == 1 ? "" : "s");
        // fs-2 (matches the banner pills), NOT fs-3: 19px is an ugly 0.79x downscale from the 24px atlas that
        // aliases the glyph edges ("wavy"); fs-2 = 20px (0.83x) is a clean ratio like the banner / distance pills.
        const float hfs   = std::max(11.f, fs - 2.f);
        const float pillH = Gw2Ui::PillHeight(hfs);
        const float gapTop = 7.f * vs, gapBot = 9.f * vs;          // pills -> divider, divider -> first node row
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        const float hw = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(hw, pillH + gapTop + gapBot));   // reserve the whole block; the cursor ends below it
        ImDrawList* hdl = ImGui::GetWindowDrawList();
        const float divY = hp.y + pillH + gapTop;

        // Drop the count pill on a narrow header (< ~300 px) so the Reset pill + "Gathering" label never collide.
        const bool showCount = hw >= 300.f;
        const float pillW = showCount ? Gw2Ui::PillWidth(hdr, hfs) : 0.f;
        if (showCount)
            Gw2Ui::PillAt(hdl, ImVec2(hp.x + hw - pillW, hp.y), hdr, hfs);   // count pill (right)

        const float rbW = Gw2Ui::PillWidth("Reset run", hfs);
        const float rbX = hp.x + hw - pillW - 6.f * vs - rbW;
        const ImVec2 rbMin(rbX, hp.y), rbMax(rbX + rbW, hp.y + pillH);
        const bool rhov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rbMin, rbMax);
        Gw2Ui::PillAt(hdl, rbMin, "Reset run", hfs,
                      rhov ? Gw2Ui::kGold : IM_COL32(150, 130, 80, 165),
                      rhov ? Gw2Ui::kGold : IM_COL32(214, 204, 178, 240), IM_COL32(30, 26, 15, 150));
        if (rhov)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            Gw2Ui::Tooltip("Reset the run - re-show nodes you've already passed (they respawn).");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) st.farmGathered.clear();
        }

        Gw2Ui::LabelIn(ImVec2(hp.x, hp.y), ImVec2(rbX - 6.f * vs, hp.y + pillH), "Gathering",
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, nullptr, hfs);
        hdl->AddLine(ImVec2(hp.x, divY), ImVec2(hp.x + hw, divY), IM_COL32(178, 144, 84, 60));
    }
    if (s_order.empty())
    {
        bool anyEnabled = false;   // distinguish "all gathered" (run complete) from "nothing selected"
        for (const auto& g : st.activeGather)
        {
            if (!g.Node || !cfg.GatherShown(mapId, g.Node->Material)) continue;
            if (!Farm::CategoryMatch(cfg.gatherCategory, g.Node->Category)) continue;
            anyEnabled = true; break;
        }
        ImGui::Spacing();
        if (anyEnabled)
        {
            Gw2Ui::Label("All nodes gathered for this run.", IM_COL32(150, 235, 150, 255), true, nullptr, lfs);
            Gw2Ui::Label("They respawn over time - reset to walk them again.", Gw2Ui::kTextSub, false, nullptr, lfs - 3.f);
            ImGui::Spacing();
            if (ImGui::Button("Reset run", ImVec2(130.f * vs, 0.f))) st.farmGathered.clear();
        }
        else
            Gw2Ui::Label("No materials selected - tap a chip above to include one.", Gw2Ui::kTextSub, false, nullptr, lfs - 2.f);
        return;
    }

    const float iconSize = 24.f * vs, rowRightInset = 8.f * vs, pillFs = lfs - 2.f;
    const float iconSlot = iconSize + 9.f * vs;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Per-row VARIABLE height, exactly like the step objective list: each row's height is the wrapped material-name
    // height, so a narrow row wraps to 2 lines and GROWS to fit instead of clipping. No clipper -- s_order is one
    // map's filtered, ungathered, in-category nodes (dozens), and the heavy sort is already cached above.
    for (int oi = 0; oi < (int)s_order.size(); ++oi)
    {
        const int ni = s_order[oi];
        if (ni < 0 || ni >= (int)st.activeGather.size()) continue;
        const auto& g = st.activeGather[ni];
        const FarmNode* nd = g.Node; if (!nd) continue;

        const float dist = std::sqrt((g.X - ax) * (g.X - ax) + (g.Z - az) * (g.Z - az));
        char distbuf[24]; std::snprintf(distbuf, sizeof(distbuf), "%.0f m", dist);
        const std::string name = Farm::MaterialPretty(nd->Material);
        const bool current = (oi == 0);
        const bool hasMount = !nd->Mount.empty();
        // Actual draw width (not Gw2Ui::ContentWidth(), which is card-aware -> returns the OUTER card width and
        // pushes the distance pill past the scrollbar when this panel is reused inside a dashboard widget).
        const float rw = ImGui::GetContentRegionAvail().x;
        const float distW = Gw2Ui::PillWidth(distbuf, pillFs);
        const float mntW  = hasMount ? Gw2Ui::PillWidth(nd->Mount.c_str(), pillFs) : 0.f;
        const float pillsW = distW + (hasMount ? mntW + 6.f * vs : 0.f);
        const float textRightOff = (rw - rowRightInset) - pillsW - 10.f * vs;
        const float textW = std::max(40.f * vs, textRightOff - iconSlot);
        const float wrapH = Gw2Ui::MeasureWrappedHeight(name.c_str(), lfs, textW);
        const float rowH  = std::max(34.f * vs, wrapH + 8.f * vs);

        ImGui::PushID(ni);
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##frow", oi, rowH, rw, false, current);
        const ImVec2 rp = row.min;
        if (void* tex = MatIcon(nd->Icon))
            dl->AddImage((ImTextureID)tex, ImVec2(rp.x, rp.y + (rowH - iconSize) * 0.5f),
                         ImVec2(rp.x + iconSize, rp.y + (rowH - iconSize) * 0.5f + iconSize));
        else
            dl->AddCircleFilled(ImVec2(rp.x + iconSize * 0.5f, rp.y + rowH * 0.5f), iconSize * 0.32f, CatDot(nd->Category, 235), 14);
        const ImU32 col = current ? IM_COL32(150, 255, 150, 255) : IM_COL32(222, 218, 208, 255);
        Gw2Ui::RowLabel(dl, row, iconSlot, std::max(0.f, rw - textRightOff), name.c_str(),
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, col, current, nullptr, lfs, textW);
        Gw2Ui::PillAt(dl, Gw2Ui::RowRight(row, distW, Gw2Ui::PillHeight(pillFs), rowRightInset), distbuf, pillFs);
        if (hasMount)   // a cyan "needs <mount>" pill to the left of the distance
            Gw2Ui::PillAt(dl, Gw2Ui::RowRight(row, mntW, Gw2Ui::PillHeight(pillFs), rowRightInset + distW + 6.f * vs),
                          nd->Mount.c_str(), pillFs, IM_COL32(96, 170, 200, 200), IM_COL32(196, 230, 245, 255),
                          IM_COL32(20, 44, 56, 150));
        if (row.hovered)
        {
            std::string tip = Farm::MaterialDesc(nd->Material);
            if (hasMount) tip += "\nNeeds the " + nd->Mount + " to reach.";
            Gw2Ui::Tooltip(tip.c_str());
        }
        ImGui::PopID();
    }
}
