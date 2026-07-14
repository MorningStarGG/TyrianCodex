#include "ui/tabs/SessionTab.h"
#include "ui/SettingsWindow.h"
#include "app/App.h"
#include "app/AccountData.h"
#include "app/SessionTracker.h"
#include "app/SessionHistory.h"
#include "app/AccountLevels.h"
#include "app/StaticData.h"
#include "ui/Gw2Ui.h"
#include "ui/SessionFormat.h"
#include "ui/dashboard/widgets/ApiWidgetUtil.h"   // DashApi::Gate / Freshness / Gold
#include "util/Textures.h"                         // Tex::GetTextureFromURL (currency icons)
#include "render/glyphs/Glyphs.h"                  // Render::DrawGlyph (stat-tile icons)
#include "guide/CurrentChar.h"                     // CurrentCharName
#include "api/core/Permissions.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace
{
    // Scope-chip selection (0 Session / 1 Currencies / 2 Levels / 3 Objectives). File-scope so the scope-aware
    // opener below can jump straight to a scope; DrawSessionContent uses this same var.
    int g_sessScope = 0;
}

void OpenSessionTab(App& app, int scope)
{
    if (scope >= 0) g_sessScope = scope;
    OpenSettingsTab(app, SettingsTabSessions);
}

namespace
{
    const ImU32 kPos = IM_COL32(150, 200, 120, 235);   // gains (green)
    const ImU32 kNeg = IM_COL32(214, 130, 110, 235);   // losses (red)

    ImU32 DeltaColor(long long v) { return v > 0 ? kPos : v < 0 ? kNeg : Gw2Ui::kTextDim; }

    std::string DateStr(long long epoch)
    {
        std::time_t t = (std::time_t)epoch; std::tm tm{};
        localtime_s(&tm, &t);
        char b[32]; std::strftime(b, sizeof(b), "%b %d, %H:%M", &tm);
        return b;
    }
    std::string DurStr(int sec)
    {
        char b[24];
        if (sec >= 3600) std::snprintf(b, sizeof(b), "%dh %dm", sec / 3600, (sec % 3600) / 60);
        else if (sec >= 60) std::snprintf(b, sizeof(b), "%dm", sec / 60);
        else std::snprintf(b, sizeof(b), "%ds", sec);
        return b;
    }
    std::string PlaytimeStr(long long sec)   // account total /v2/account "age" (seconds played)
    {
        const long long h = sec / 3600;
        if (h >= 48) return std::to_string(h / 24) + "d " + std::to_string(h % 24) + "h";
        return std::to_string(h) + "h " + std::to_string((sec % 3600) / 60) + "m";
    }

    const ImU32 kVal  = IM_COL32(255, 230, 170, 255);   // neutral stat-tile value (warm cream)
    const ImU32 kGoldAccent = IM_COL32(214, 178, 96, 255);
    const ImU32 kDimAccent  = IM_COL32(150, 140, 110, 255);
    using TileIcon = std::function<void(ImDrawList*, ImVec2, float)>;

    // Tile-icon factories: a procedural glyph, or a real currency-icon texture (drawn centered at `c`, `s` px).
    TileIcon GlyphIcon(Render::Glyph g, ImU32 col)
    { return [g, col](ImDrawList* dl, ImVec2 c, float s) { Render::DrawGlyph(dl, c, s, g, col); }; }
    TileIcon CurIcon(int id)
    {
        return [id](ImDrawList* dl, ImVec2 c, float s) {
            const char* ic = StaticData::CurrencyIcon(id);
            if (!ic || !ic[0]) return;
            char t[40]; std::snprintf(t, sizeof(t), "TC_CURRENCY_%d", id);
            if (void* tex = Tex::GetTextureFromURL(t, ic))
                dl->AddImage((ImTextureID)tex, ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f));
        };
    }

    // Stat tiles use the ONE shared framework -- Gw2Ui::Stat (the tile data) + Gw2Ui::StatGrid (the grid). This
    // tab is the original source of that widget; Instances + Fishing reuse the SAME one, so they all match.
    using Stat = Gw2Ui::Stat;

    // Generic per-session bar graph: valueOf extracts the plotted number for each record (records where it's 0 are
    // skipped so e.g. the Levels graph only shows level-gaining sessions); fmt formats a value for the hover.
    void DrawGraph(App& app, const std::string& ch,
                   const std::function<long long(const SessionRecord&)>& valueOf,
                   const std::function<std::string(long long)>& fmt, float w)
    {
        const std::vector<SessionRecord>& all = app.sessionHistory.Sessions(ch);
        std::vector<std::pair<long long, const SessionRecord*>> pts;
        for (const SessionRecord& r : all) { const long long v = valueOf(r); if (v != 0) pts.push_back({ v, &r }); }
        const int kMax = 24;
        const int start = (int)pts.size() > kMax ? (int)pts.size() - kMax : 0;
        const int n = (int)pts.size() - start;

        const float H = 120.f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, H));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (n == 0)
        {
            Gw2Ui::LabelIn(p, ImVec2(p.x + w, p.y + H), "Nothing recorded for this yet.",
                           Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, Gw2Ui::kTextDim, false, nullptr, 14.f);
            return;
        }
        long long mn = 0, mx = 0;
        for (int i = start; i < (int)pts.size(); ++i) { mn = std::min(mn, pts[i].first); mx = std::max(mx, pts[i].first); }

        const float pad = 8.f, top = p.y + pad, bot = p.y + H - pad;
        auto yOf = [&](long long v) -> float {
            if (mx == mn) return (top + bot) * 0.5f;
            const float t = (float)(v - mn) / (float)(mx - mn);
            return bot - t * (bot - top);
        };
        const float zeroY = yOf(0);
        dl->AddLine(ImVec2(p.x, zeroY), ImVec2(p.x + w, zeroY), IM_COL32(150, 140, 110, 110), 1.f);

        const float slot = w / (float)n;
        const float bw = std::min(slot * 0.6f, 22.f);
        const bool winHov = ImGui::IsWindowHovered();
        int hoverIdx = -1;
        for (int i = 0; i < n; ++i)
        {
            const long long v = pts[start + i].first;
            const float cx = p.x + slot * (i + 0.5f);
            const float y = yOf(v);
            dl->AddRectFilled(ImVec2(cx - bw * 0.5f, std::min(y, zeroY)), ImVec2(cx + bw * 0.5f, std::max(y, zeroY)),
                              v >= 0 ? kPos : kNeg, 1.f);
            if (winHov && ImGui::IsMouseHoveringRect(ImVec2(cx - slot * 0.5f, top), ImVec2(cx + slot * 0.5f, bot)))
                hoverIdx = start + i;
        }
        if (hoverIdx >= 0)
        {
            const SessionRecord& r = *pts[hoverIdx].second;
            if (Gw2Ui::TooltipBegin())
            {
                Gw2Ui::TooltipTitle(DateStr(r.startEpoch).c_str());
                Gw2Ui::TooltipText(fmt(pts[hoverIdx].first).c_str());
                Gw2Ui::TooltipMuted(("Played " + DurStr(r.durationSec)).c_str());
                Gw2Ui::TooltipEnd();
            }
        }
    }

    // Generic saved-session history list: one row per record where include(r) is true (date + duration + a
    // right-aligned value), with a per-row Delete pill and a Wipe-all button. Reused by every scope.
    void DrawHistoryList(App& app, const std::string& ch, float w,
                         const std::function<bool(const SessionRecord&)>& include,
                         const std::function<std::string(const SessionRecord&)>& rightText,
                         const std::function<ImU32(const SessionRecord&)>& rightCol)
    {
        const std::vector<SessionRecord>& hist = app.sessionHistory.Sessions(ch);
        int inc = 0; for (const SessionRecord& r : hist) if (include(r)) ++inc;
        if (inc == 0) { Gw2Ui::Label("No saved sessions yet for this character.", Gw2Ui::kTextDim, false, nullptr, 14.f); return; }

        int toDelete = -1;
        const float rowH = 40.f, pillFs = 14.f;
        const float pillH = Gw2Ui::PillHeight(pillFs);
        const float delW = Gw2Ui::PillWidth("Delete", pillFs);
        for (int i = (int)hist.size() - 1; i >= 0; --i)   // newest first
        {
            const SessionRecord& r = hist[i];
            if (!include(r)) continue;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(w, rowH));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool rowHov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + rowH));
            ImGui::PushID(i);
            Gw2Ui::RowBackground(p, ImVec2(p.x + w, p.y + rowH), rowHov, false, ImGui::GetID("histrow"), i);
            ImGui::PopID();

            const float delX = p.x + w - delW;
            const ImVec2 delMin(delX, p.y + (rowH - pillH) * 0.5f);
            const bool delHov = rowHov && ImGui::IsMouseHoveringRect(delMin, ImVec2(delX + delW, delMin.y + pillH));
            Gw2Ui::PillAt(dl, delMin, "Delete", pillFs,
                          delHov ? kNeg : IM_COL32(150, 120, 110, 150),
                          delHov ? IM_COL32(255, 230, 220, 255) : IM_COL32(200, 170, 160, 220),
                          IM_COL32(34, 22, 20, 150));
            if (delHov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) toDelete = i; }

            const float netRight = delX - 10.f;
            Gw2Ui::LabelIn(ImVec2(p.x + 6.f, p.y + 4.f), ImVec2(netRight, p.y + 22.f), DateStr(r.startEpoch).c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextSub, false, nullptr, 16.f);
            Gw2Ui::LabelIn(ImVec2(p.x + 6.f, p.y + 21.f), ImVec2(netRight, p.y + rowH - 3.f), ("Played " + DurStr(r.durationSec)).c_str(),
                           Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextDim, false, nullptr, 14.f);
            Gw2Ui::LabelIn(ImVec2(p.x + w * 0.4f, p.y), ImVec2(netRight, p.y + rowH), rightText(r).c_str(),
                           Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, rightCol(r), false, nullptr, 16.f);
        }
        if (toDelete >= 0) app.sessionHistory.Erase(ch, toDelete);
        ImGui::Spacing();
        if (Gw2Ui::ActionButton("Wipe history", 150.f, 24.f, Gw2Ui::ActionButtonVariant::Danger, "Delete ALL saved sessions for this character"))
            app.sessionHistory.Clear(ch);
    }

    const char* SessChar(const App& app)
    {
        static std::string s;
        const SessionEarnings& se = app.state.sessionEarnings;
        s = !se.character.empty() ? se.character : CurrentCharName();
        return s.c_str();
    }
}

// ---- Session (overview) ------------------------------------------------------------------------------------
static void DrawOverviewScope(App& app)
{
    const SessionEarnings& se = app.state.sessionEarnings;
    if (Gw2Ui::BeginCard("##sess_overview"))
    {
        Gw2Ui::SectionHeader("This Session",
                             se.active ? SessionFmt::Clock(SessionTracker::SessionDurationSec(app)).c_str() : nullptr);
        const float cw = Gw2Ui::CardInnerWidth();
        if (!se.active) Gw2Ui::Label("Waiting for a character to enter the world.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        else
        {
            Gw2Ui::Label(se.character.c_str(), Gw2Ui::kGold, true, nullptr, 18.f);   // character header
            ImGui::Spacing();
            const ImU32 teal = IM_COL32(120, 200, 210, 255), blue = IM_COL32(150, 170, 210, 255);
            std::vector<Stat> tiles;
            tiles.push_back({ "Session time", SessionFmt::Clock(SessionTracker::SessionDurationSec(app)), kVal, kGoldAccent, GlyphIcon(Render::Glyph::Clock, kGoldAccent) });
            const AccountData::Model& m = AccountData::Get();
            if (m.haveAccount) tiles.push_back({ "Account playtime", PlaytimeStr(m.age), kVal, kDimAccent, GlyphIcon(Render::Glyph::Clock, kDimAccent) });
            const int lv = SessionTracker::LevelsGained(app);
            tiles.push_back({ "Levels gained", std::to_string(lv), lv > 0 ? kPos : kVal, lv > 0 ? kPos : kDimAccent, GlyphIcon(Render::Glyph::ArrowUp, lv > 0 ? kPos : kDimAccent) });
            const int ob = SessionTracker::ObjectivesGained(app);
            tiles.push_back({ "Objectives done", std::to_string(ob), ob > 0 ? kPos : kVal, ob > 0 ? kPos : kDimAccent, GlyphIcon(Render::Glyph::Check, ob > 0 ? kPos : kDimAccent) });
            const long long gold = SessionTracker::CurrentDelta(app, 1);
            tiles.push_back({ "Gold", SessionFmt::Signed(1, gold), DeltaColor(gold), kGoldAccent, CurIcon(1) });
            tiles.push_back({ "Currencies changed", std::to_string((int)SessionTracker::CurrentDeltas(app).size()), kVal, teal, GlyphIcon(Render::Glyph::Coins, teal) });
            tiles.push_back({ "Saved sessions", std::to_string((int)app.sessionHistory.Sessions(se.character).size()), kVal, blue, GlyphIcon(Render::Glyph::Chart, blue) });
            Gw2Ui::StatGrid(cw, tiles);
        }
        Gw2Ui::EndCard();
    }
    ImGui::Spacing();
    Gw2Ui::Label("Switch scopes above for Currencies, Levels, and Objectives detail (with graphs + history).",
                 Gw2Ui::kTextDim, false, nullptr, 14.f);
}

// ---- Currencies (earnings + balances) ----------------------------------------------------------------------
static void DrawCurrenciesScope(App& app)
{
    if (!DashApi::Gate(app, Api::TokenPermission::Wallet, "wallet")) return;   // currency needs the wallet scope
    const SessionEarnings& se = app.state.sessionEarnings;
    const std::string ch = SessChar(app);

    // Current session earnings + Reset/Save.
    if (Gw2Ui::BeginCard("##sess_cur"))
    {
        Gw2Ui::SectionHeader("Earned This Session",
                             se.active ? SessionFmt::Clock(SessionTracker::DurationSec(app)).c_str() : nullptr);
        const float cw = Gw2Ui::CardInnerWidth();
        if (!se.active)        Gw2Ui::Label("Waiting for a character to enter the world.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        else if (!se.haveBase) Gw2Ui::Label("Baselining your wallet (waiting for a fresh fetch)...", Gw2Ui::kTextDim, false, nullptr, 14.f);
        else
        {
            const std::map<int, long long> deltas = SessionTracker::CurrentDeltas(app);
            std::vector<Stat> earned;
            { auto g = deltas.find(1); const long long gv = g != deltas.end() ? g->second : 0;
              earned.push_back({ "Gold", SessionFmt::Signed(1, gv), DeltaColor(gv), DeltaColor(gv), CurIcon(1) }); }
            for (const StaticData::CurrencyRef& c : StaticData::Currencies())
            {
                if (c.id == 1) continue;
                auto it = deltas.find(c.id);
                if (it == deltas.end()) continue;
                earned.push_back({ SessionFmt::Label(c.id), SessionFmt::Signed(c.id, it->second), DeltaColor(it->second), DeltaColor(it->second), CurIcon(c.id) });
            }
            Gw2Ui::StatGrid(cw, earned);
            if (deltas.empty())
                DashApi::Note("Nothing yet -- this tracks CHANGES, not balances, so a currency appears the moment "
                              "you gain or spend it. (Reset/Save here only affect currency; levels + objectives keep counting.)",
                              Gw2Ui::kTextDim, 13.f);
            ImGui::Spacing();
            if (Gw2Ui::ActionButton("Reset", 120.f, 24.f, Gw2Ui::ActionButtonVariant::Normal, "Discard the current currency count + re-baseline now"))
                SessionTracker::Reset(app);
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::ActionButton("Save run", 130.f, 24.f, Gw2Ui::ActionButtonVariant::Primary, "Bank this currency run to history + start a fresh one"))
                SessionTracker::Save(app);
            ImGui::Spacing();
            DashApi::Freshness(AccountData::Get().walletAt);
        }
        Gw2Ui::EndCard();
    }
    ImGui::Spacing();

    // Earnings graph (per-currency picker).
    if (Gw2Ui::BeginCard("##sess_graph"))
    {
        Gw2Ui::SectionHeader("Earnings");
        const float cw = Gw2Ui::CardInnerWidth();
        static int s_graphCur = 1;
        const std::vector<SessionRecord>& hist = app.sessionHistory.Sessions(ch);
        std::map<int, int> ord; for (const StaticData::CurrencyRef& c : StaticData::Currencies()) ord[c.id] = c.order;
        std::vector<int> ids; ids.push_back(1);
        for (const SessionRecord& r : hist)
            for (const auto& d : r.deltas)
                if (std::find(ids.begin(), ids.end(), d.first) == ids.end()) ids.push_back(d.first);
        std::sort(ids.begin(), ids.end(), [&](int a, int b) { return ord[a] != ord[b] ? ord[a] < ord[b] : a < b; });
        std::vector<std::string> labels; for (int id : ids) labels.push_back(SessionFmt::Label(id));
        std::vector<const char*> cstrs; for (const std::string& s : labels) cstrs.push_back(s.c_str());
        int sel = 0; for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == s_graphCur) sel = (int)i;
        if (Gw2Ui::Dropdown("##sess_graph_cur", cstrs.data(), (int)cstrs.size(), &sel, 220.f))
            s_graphCur = ids[std::clamp(sel, 0, (int)ids.size() - 1)];
        ImGui::Spacing();
        const int cur = s_graphCur;
        DrawGraph(app, ch,
                  [cur](const SessionRecord& r) { auto it = r.deltas.find(cur); return it != r.deltas.end() ? it->second : 0LL; },
                  [cur](long long v) { return SessionFmt::Label(cur) + std::string(": ") + SessionFmt::Signed(cur, v); }, cw);
        Gw2Ui::EndCard();
    }
    ImGui::Spacing();

    // Full balances.
    if (Gw2Ui::BeginCard("##sess_bal"))
    {
        const AccountData::Model& m = AccountData::Get();
        std::map<int, long long> have; for (const auto& w : m.wallet) if (w.value > 0) have[w.id] = w.value;
        char cnt[24]; std::snprintf(cnt, sizeof(cnt), "%d", (int)have.size());
        Gw2Ui::SectionHeader("Balances", cnt);
        const float cw = Gw2Ui::CardInnerWidth();
        std::vector<Stat> tiles;
        const std::vector<StaticData::CurrencyRef>& cat = StaticData::Currencies();
        if (!cat.empty())
            for (const StaticData::CurrencyRef& c : cat) { auto it = have.find(c.id); if (it != have.end()) tiles.push_back({ SessionFmt::Label(c.id), c.id == 1 ? SessionFmt::Coins(it->second) : std::to_string(it->second), kVal, kGoldAccent, CurIcon(c.id) }); }
        else
            for (const auto& kv : have) tiles.push_back({ SessionFmt::Label(kv.first), kv.first == 1 ? SessionFmt::Coins(kv.second) : std::to_string(kv.second), kVal, kGoldAccent, CurIcon(kv.first) });
        if (tiles.empty()) Gw2Ui::Label("No currencies.", Gw2Ui::kTextDim, false, nullptr, 14.f);
        else Gw2Ui::StatGrid(cw, tiles);
        Gw2Ui::EndCard();
    }
    ImGui::Spacing();

    // Currency history.
    if (Gw2Ui::BeginCard("##sess_curhist"))
    {
        Gw2Ui::SectionHeader("History");
        DrawHistoryList(app, ch, Gw2Ui::CardInnerWidth(),
            [](const SessionRecord& r) { return !r.deltas.empty(); },
            [](const SessionRecord& r) {
                auto g = r.deltas.find(1); const long long gv = g != r.deltas.end() ? g->second : 0;
                const int others = (int)r.deltas.size() - (g != r.deltas.end() ? 1 : 0);
                if (g != r.deltas.end()) return SessionFmt::Signed(1, gv) + (others > 0 ? "  +" + std::to_string(others) + " more" : "");
                return std::to_string((int)r.deltas.size()) + (r.deltas.size() == 1 ? " currency" : " currencies");
            },
            [](const SessionRecord& r) { auto g = r.deltas.find(1); return g != r.deltas.end() ? DeltaColor(g->second) : kPos; });
        Gw2Ui::EndCard();
    }
}

// ---- A simple count scope (Levels / Objectives) ------------------------------------------------------------
static void DrawCountScope(App& app, const char* title, const char* unit, int live, long long accountTotal,
                           Render::Glyph icon, const std::function<int(const SessionRecord&)>& valueOf)
{
    const std::string ch = SessChar(app);
    if (Gw2Ui::BeginCard("##sess_count_cur"))
    {
        Gw2Ui::SectionHeader(title);
        long long best = 0;   // THIS character's best single session (matches the history + graph below)
        for (const SessionRecord& r : app.sessionHistory.Sessions(ch)) best = std::max(best, (long long)valueOf(r));
        std::vector<Stat> tiles = {
            { "This session",  std::to_string(live) + " " + unit, live > 0 ? kPos : kVal, live > 0 ? kPos : kDimAccent, GlyphIcon(icon, live > 0 ? kPos : kDimAccent) },
            { "Account total", std::to_string(accountTotal) + " " + unit, kVal, kGoldAccent, GlyphIcon(icon, kGoldAccent) },
            { "Best session",  std::to_string(best) + " " + unit, kVal, kGoldAccent, GlyphIcon(Render::Glyph::Star, kGoldAccent) },
        };
        Gw2Ui::StatGrid(Gw2Ui::CardInnerWidth(), tiles);
        Gw2Ui::EndCard();
    }
    ImGui::Spacing();
    if (Gw2Ui::BeginCard("##sess_count_graph"))
    {
        Gw2Ui::SectionHeader("Per session");
        DrawGraph(app, ch,
                  [valueOf](const SessionRecord& r) { return (long long)valueOf(r); },
                  [unit](long long val) { return "+" + std::to_string(val) + " " + unit; },
                  Gw2Ui::CardInnerWidth());
        Gw2Ui::EndCard();
    }
    ImGui::Spacing();
    if (Gw2Ui::BeginCard("##sess_count_hist"))
    {
        Gw2Ui::SectionHeader("History");
        DrawHistoryList(app, ch, Gw2Ui::CardInnerWidth(),
            [valueOf](const SessionRecord& r) { return valueOf(r) > 0; },
            [valueOf, unit](const SessionRecord& r) { return "+" + std::to_string(valueOf(r)) + " " + unit; },
            [](const SessionRecord&) { return kPos; });
        Gw2Ui::EndCard();
    }
}

void DrawSessionContent(App& app)
{
    // Scope toggle (like the Items tab): Session overview / Currencies / Levels / Objectives.
    static const Gw2Ui::ChipItem kScopeChips[] = { {"Session"}, {"Currencies"}, {"Levels"}, {"Objectives"} };
    Gw2Ui::ChipRow("##sessScope", kScopeChips, 4, &g_sessScope, 32.f, 18.f, 6.f, 80.f, true);
    Gw2Ui::Divider(0.f);

    Gw2Ui::PushTextScale(1.3f);   // match (a touch above) the other settings tabs; base sizes read small in the big shell
    switch (g_sessScope)
    {
        case 1: DrawCurrenciesScope(app); break;
        case 2: DrawCountScope(app, "Levels Gained", "levels", SessionTracker::LevelsGained(app),
                               AccountLevels::Total(),   // seeded from the API roster, account-wide, monotonic
                               Render::Glyph::ArrowUp, [](const SessionRecord& r) { return r.levelsGained; }); break;
        case 3:   // objectives: account total = the live stored checklist completions across all characters (reflects resets/un-checks)
            DrawCountScope(app, "Objectives Done", "objectives", SessionTracker::ObjectivesGained(app),
                           app.progress.TotalCompleted(), Render::Glyph::Check,
                           [](const SessionRecord& r) { return r.objectivesGained; }); break;
        default: DrawOverviewScope(app); break;
    }
    Gw2Ui::PopTextScale();
}
