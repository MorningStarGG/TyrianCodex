#include "ui/tabs/JournalTab.h"
#include "ui/SettingsWindow.h"
#include "ui/UiCommon.h"
#include "app/App.h"
#include "app/AccountData.h" // reuse the account-wide character list (no separate /v2/characters/names fetch)
#include "app/Glue.h"
#include "Shared.h"
#include "util/Draw.h"
#include "util/Json.h"
#include "guide/Completion.h"
#include "guide/CurrentChar.h"
#include "app/CharRegistry.h" // record each character's created-id (rename detection)
#include "app/CharData.h"     // OnCharRename: merge a renamed character's data across every store
#include "guide/RouteMode.h"
#include "guide/Regions.h"
#include "ui/Gw2Ui.h"
#include "ui/ApiReminder.h"
#include "render/glyphs/Glyphs.h" // Render::DrawGlyph (season expand caret)
#include "ui/viewer/ViewerLayout.h"
#include "ui/ZoneRow.h"
#include "ui/Effect.h"
#include "util/Textures.h"
#include "util/ImageCache.h"
#include "util/Dyes.h"
#include "util/Coords.h"
#include "model/ObjectiveTypes.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ui/tabs/MapThumbnail.h"
#include "ui/tabs/SettingsCommon.h"

// Cumulative chapters (personal story) form a run: ticking chapter N marks 1..N done, unticking N clears
// N..end - a contiguous "done up to here" prefix. (GuideModule.Stories.cs ApplyChapterCascade.)
static void ApplyChapterCascade(App &app, const std::string &rel, const StoryEpisode &clicked, bool done, const std::string &scope)
{
    const std::vector<StoryEpisode> *eps = app.stories.Episodes(rel);
    if (!eps)
    {
        app.storyStore.Set(rel + ":" + clicked.name, done, scope);
        return;
    }
    std::vector<const StoryEpisode *> chapters;
    for (const StoryEpisode &e : *eps)
        if (e.cumulative)
            chapters.push_back(&e);
    std::stable_sort(chapters.begin(), chapters.end(),
                     [](const StoryEpisode *a, const StoryEpisode *b)
                     { return a->order < b->order; });
    int idx = -1;
    for (int i = 0; i < (int)chapters.size(); ++i)
        if (chapters[i]->name == clicked.name)
        {
            idx = i;
            break;
        }
    if (idx < 0)
    {
        app.storyStore.Set(rel + ":" + clicked.name, done, scope);
        return;
    }
    if (done)
        for (int i = 0; i <= idx; ++i)
            app.storyStore.Set(rel + ":" + chapters[i]->name, true, scope);
    else
        for (int i = idx; i < (int)chapters.size(); ++i)
            app.storyStore.Set(rel + ":" + chapters[i]->name, false, scope);
}

// Living World / expansion episodes form a run just like the personal-story chapters: the in-game Story
// Journal unlocks episode N+1 only after N, so ticking N marks 1..N done and unticking N clears N..end.
// Keyed by API story id ("story:<id>", account scope) over the season's ORDERED episode list, so it is the
// same "contiguous done-up-to-here prefix" shape as ApplyChapterCascade, just a different key namespace.
// Episodes already confirmed by achievements are skipped on the way down -- no redundant manual marks.
static void ApplySeasonCascade(App &app, const std::vector<const Api::V2::Story *> &ordered, int idx, bool done);

// ===================== Story Journal tab ====================
// Collapsible season tree (left) + per-episode detail (right): a release-tinted header band + the official
// description + an Achievements & Rewards block + a manual "Mark complete" checkbox. Driven by /v2/stories
// (+ /seasons) + /v2/achievements (all anonymous). No splash art (not in the API + too big for our window) -
// the release tint + the reward icons carry the visuals.
static std::vector<Api::V2::StorySeason> g_jSeasons; // the season tree (My Story, LWS1, ...)

static std::map<int, Api::V2::Story> g_jStories; // story id -> details

// The per-season story lists (sorted by `order`) are STATIC once the API data has landed (the API ships the
// season.stories unsorted), so precompute them when the story/season sets change - not every frame. Pointers
// are into g_jStories (a std::map, node-stable), and the cache rebuilds whenever either set grows. Declared
// here (built further down) because the episode detail + the suggested-next walk both need the ordered list.
static std::map<std::string, std::vector<const Api::V2::Story *>> g_jSeasonStories;

// story id -> (its season id, its index in that season's ordered list). Built with g_jSeasonStories so the
// per-row done-check never linear-scans every season's story list (JournalDone runs for every visible row AND
// for every episode SeasonLastDone walks, so the old scan was O(seasons x stories) per call).
static std::map<int, std::pair<std::string, int>> g_jStoryPos;

static size_t g_jSeasonStoriesS = (size_t)-1, g_jSeasonStoriesT = (size_t)-1;

static std::map<int, Api::V2::Achievement> g_jAch; // achievement id -> catalog (icon/rewards)

// Reward item name/icon now live in AccountData's shared itemMeta store (see EnsureItems) -- no local copy.

static bool g_jPrefetched = false;      // bulk reward prefetch done (latched on success)
static double g_jPrefetchRetryAt = 0.0; // earliest time to RE-attempt the bulk fetch after a failure (throttle)

static bool g_jFetchStarted = false; // one-shot network refresh on first open

static std::string g_journalCachePath; // <addons>/TyrianCodex/journal-cache.json

static int g_jSelected = -1; // selected API story id (-1 = none / a chapter)

static int g_jSelChapter = 0; // selected personal-story chapter 1-8 (0 = none)

static std::map<std::string, bool> g_jExpanded; // season id -> expanded

// The personal story = 8 chapters that unlock roughly every 10 levels. The early chapters branch by race /
// profession / biography, so a single name is wrong - we label them by LEVEL; only the finale (level 80) is
// the same for every character, so it gets its real name. They map to our stories.json "core" Chapter 1..8
// for (per-character, cumulative) completion, SHARED with the Story tab's marks.
struct PersonalChapter
{
    int level;
    const char *name;
};

static const PersonalChapter kPersonal[8] = {
    {10, "Level 10"},
    {20, "Level 20"},
    {30, "Level 30"},
    {40, "Level 40"},
    {50, "Level 50"},
    {60, "Level 60"},
    {70, "Level 70"},
    {80, "Victory or Death"},
};

// ---------------------------------------------------------------------------------------------------------
// Per-character Personal Story (the REAL "My Story"): the character's biography + their actual chapters/steps,
// vs the generic kPersonal placeholder above. Reconstructed from the API - /characters/:id/{core,backstory,
// quests} + the static /v2/quests + /v2/backstory catalogs - resolved against the curated questId->chapter map
// scraped into data/personal_story.json (the chapter names aren't in the API). Additive: needs a `characters`
// key + a loaded character, else the kPersonal fallback shows. The "next" step is a best-effort branch-aware
// suggestion (the API gives the played path, not the future one).
struct PsMapEntry
{
    int story = 0;
    int chapterOrder = 99;
    int stepOrder = 0;
    std::string chapter;
    std::string choice;
    std::string order;
};
static std::map<int, PsMapEntry> g_psMap;   // questId -> chapter info (from data/personal_story.json)
static std::map<int, std::string> g_psArcs; // story id -> arc display name

struct PsStep
{
    int questId = 0;
    std::string name;
    bool done = false;
    bool isNext = false;
    std::string goalDone;
    std::string goalActive;
    int order = 0;
};
struct PsChapter
{
    std::string arc;
    std::string name;
    int number = 0;
    std::vector<PsStep> steps;
    bool allDone = false;
    bool hasNext = false;
};

// One character's reconstructed view (cached in memory + on disk for instant display). answerIds/questIds are
// the raw source (questIds doubles as the staleness signature); bioParas/chapters are the computed output.
struct PsCharView
{
    std::string race, profession;
    std::vector<std::string> answerIds; // raw (kept to recompute once the catalogs land)
    std::vector<int> questIds;          // raw + staleness signature
    std::vector<std::string> bioParas;  // computed
    std::vector<PsChapter> chapters;    // computed
    bool computed = false;
};
static std::map<std::string, PsCharView> g_psView; // character name -> its view (ALL known characters)
static uint64_t g_psViewVer = 0;                   // bumped on every g_psView write -> change token for PersonalStoryProgress
static std::map<std::string, int> g_psFetching;    // character -> outstanding fetch count (3..0); guards re-entry

// Shared static catalogs (fetched once; needed to reconstruct any character).
static std::map<int, Api::V2::Quest> g_psQuests;                    // questId -> quest (name/goals)
static std::map<std::string, Api::V2::BackstoryAnswer> g_psAnswers; // answer id -> answer (journal line)
static std::map<int, int> g_psQuestionOrder;                        // question id -> order (biography sort)
static bool g_psQuestsFetched = false, g_psQuestsInflight = false;
static bool g_psBioFetched = false, g_psBioInflight = false;
static int g_psBioPending = 0;

// The CURRENT character's view, copied into these render-facing globals by ActivateView (the render reads them).
static std::vector<PsChapter> g_psChapters;
static std::vector<std::string> g_psBioParas;
static std::string g_psRace;
static std::string g_psProfession;
static std::string g_psChar; // the character currently activated into the globals above
static bool g_psReady = false;
static bool g_psActiveDirty = false; // the active char's view recomputed -> re-activate next frame
static int g_psSel = -2;             // -2 none, -1 biography, >= 0 chapter index
static std::string g_psCachePath;    // <addons>/TyrianCodex/personal-story-cache.json
static bool g_psCacheDirty = false;  // a view changed -> persist
static constexpr int kPersonalStoryCacheSchema = 3;

void LoadPersonalStory(const std::string &path)
{
    std::ifstream f(path);
    if (!f)
        return;
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return;
    }
    if (j.contains("arcs") && j["arcs"].is_object())
        for (auto it = j["arcs"].begin(); it != j["arcs"].end(); ++it)
            try
            {
                g_psArcs[std::stoi(it.key())] = it.value().get<std::string>();
            }
            catch (...)
            {
            }
    if (j.contains("quests") && j["quests"].is_object())
        for (auto it = j["quests"].begin(); it != j["quests"].end(); ++it)
        {
            const auto &v = it.value();
            PsMapEntry e;
            e.story = v.value("story", 0);
            e.chapterOrder = v.value("chapterOrder", 99);
            e.stepOrder = v.value("stepOrder", 0);
            e.chapter = v.value("chapter", std::string());
            e.choice = v.value("choice", std::string());
            e.order = v.value("order", std::string());
            try
            {
                g_psMap[std::stoi(it.key())] = std::move(e);
            }
            catch (...)
            {
            }
        }
}

static int PsRaceToStory(const std::string &race)
{
    if (race == "Charr")
        return 1;
    if (race == "Norn")
        return 2;
    if (race == "Sylvari")
        return 7;
    if (race == "Asura")
        return 8;
    return 3; // Human (and the default)
}

// Strip the GW2 API's inline markup (<br>, <c=@...>...</c>, ...) to plain readable text + collapse whitespace.
static std::string PsCleanText(const std::string &in)
{
    std::string s;
    s.reserve(in.size());
    for (size_t i = 0; i < in.size();)
    {
        if (in[i] == '<')
        {
            size_t e = in.find('>', i);
            if (e == std::string::npos)
                break;
            s += ' ';
            i = e + 1;
        }
        else
        {
            s += in[i];
            ++i;
        }
    }
    std::string out;
    bool sp = false;
    for (char c : s)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (!sp && !out.empty())
                out += ' ';
            sp = true;
        }
        else
        {
            out += c;
            sp = false;
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// Reconstruct ONE character's view (biography + chapters) from its raw source + the catalogs/map, and store it
// in g_psView[name]. Pure of the render globals (so it can run for any character, e.g. the login preload).
static void RebuildViewFor(const std::string &name, const std::string &race, const std::string &profession,
                           const std::vector<std::string> &answerIds, const std::vector<int> &questIds)
{
    PsCharView v;
    v.race = race;
    v.profession = profession;
    v.answerIds = answerIds;
    v.questIds = questIds;
    v.computed = true;
    auto has = [](const std::vector<int> &vec, int x)
    { return std::find(vec.begin(), vec.end(), x) != vec.end(); };

    // Biography: each played answer's cleaned journal line as its OWN paragraph, ordered by the question's order.
    std::vector<std::pair<int, std::string>> bio;
    for (const std::string &aid : answerIds)
    {
        auto it = g_psAnswers.find(aid);
        if (it == g_psAnswers.end())
            continue;
        std::string j = PsCleanText(it->second.journal);
        if (j.empty())
            continue;
        auto qo = g_psQuestionOrder.find(it->second.question);
        bio.emplace_back(qo != g_psQuestionOrder.end() ? qo->second : 0, std::move(j));
    }
    std::sort(bio.begin(), bio.end(), [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b)
              { return a.first < b.first; });
    for (auto &b : bio)
        v.bioParas.push_back(std::move(b.second));
    for (size_t i = 0; i + 1 < v.bioParas.size(); ++i) // float "This is my story." to the end (where the game puts it)
        if (v.bioParas[i] == "This is my story.")
        {
            std::string t = v.bioParas[i];
            v.bioParas.erase(v.bioParas.begin() + (long)i);
            v.bioParas.push_back(t);
            break;
        }

    // The character's arcs, in play order: their race story, then Orders, then Zhaitan.
    const int arcOrder[3] = {PsRaceToStory(race), 9, 10};
    auto arcIndex = [&](int story)
    { for (int i = 0; i < 3; ++i) if (arcOrder[i] == story) return i; return -1; };

    // The character's branch tags: biography answer TITLES match step `choice`; completed steps can also reveal
    // later `choice` branches and the order path (Vigil / Durmand Priory / Order of Whispers).
    std::vector<std::string> choices;
    std::vector<std::string> orders;
    auto addChoice = [&](const std::string &c)
    { if (!c.empty() && std::find(choices.begin(), choices.end(), c) == choices.end()) choices.push_back(c); };
    auto addOrder = [&](const std::string &o)
    { if (!o.empty() && std::find(orders.begin(), orders.end(), o) == orders.end()) orders.push_back(o); };
    for (const std::string &aid : answerIds)
    {
        auto a = g_psAnswers.find(aid);
        if (a != g_psAnswers.end())
            addChoice(a->second.title);
    }
    for (int qid : questIds)
    {
        auto m = g_psMap.find(qid);
        if (m != g_psMap.end())
        {
            addChoice(m->second.choice);
            addOrder(m->second.order);
        }
    }
    auto choiceOk = [&](const std::string &c)
    { return c.empty() || std::find(choices.begin(), choices.end(), c) != choices.end(); };
    auto orderOk = [&](const std::string &o)
    { return o.empty() || std::find(orders.begin(), orders.end(), o) != orders.end(); };

    // The (arc, chapter, step) positions the character has PLAYED. A not-done step sharing a position is a
    // sub-fork they skipped (e.g. they defended the orphanage, not the hospital), so it is NOT their "next".
    auto posKey = [](int ai, int co, int so)
    { return ai * 1000000 + co * 1000 + so; };
    std::vector<int> donePos;
    for (int qid : questIds)
    {
        auto m = g_psMap.find(qid);
        if (m != g_psMap.end())
        {
            int ai = arcIndex(m->second.story);
            if (ai >= 0)
                donePos.push_back(posKey(ai, m->second.chapterOrder, m->second.stepOrder));
        }
    }
    auto isSkippedFork = [&](int ai, int co, int so)
    { return std::find(donePos.begin(), donePos.end(), posKey(ai, co, so)) != donePos.end(); };

    // The best-effort "next": earliest (arc, chapter, step) not-done step on the character's branch that isn't a
    // skipped sub-fork.
    int nextQid = -1, bestA = 99, bestC = 999, bestS = 99999;
    for (const auto &kv : g_psMap)
    {
        const PsMapEntry &m = kv.second;
        const int ai = arcIndex(m.story);
        if (ai < 0 || has(questIds, kv.first) || !choiceOk(m.choice) || !orderOk(m.order) || isSkippedFork(ai, m.chapterOrder, m.stepOrder))
            continue;
        if (ai < bestA || (ai == bestA && (m.chapterOrder < bestC || (m.chapterOrder == bestC && m.stepOrder < bestS))))
        {
            bestA = ai;
            bestC = m.chapterOrder;
            bestS = m.stepOrder;
            nextQid = kv.first;
        }
    }

    // Group the played steps (+ the next step) into chapters, keyed by (arc index, chapter order) so a std::map
    // iterates them in arc-then-chapter order.
    std::map<std::pair<int, int>, PsChapter> chmap;
    std::vector<int> added;
    auto addStep = [&](int qid, bool isNext)
    {
        if (std::find(added.begin(), added.end(), qid) != added.end())
            return; // the API may list a quest id twice
        auto m = g_psMap.find(qid);
        if (m == g_psMap.end())
            return;
        const int ai = arcIndex(m->second.story);
        if (ai < 0)
            return;
        added.push_back(qid);
        PsChapter &ch = chmap[std::make_pair(ai, m->second.chapterOrder)];
        if (ch.name.empty())
        {
            ch.arc = g_psArcs.count(m->second.story) ? g_psArcs[m->second.story] : std::string();
            ch.name = m->second.chapter.empty() ? ch.arc : m->second.chapter;
        }
        PsStep s;
        s.questId = qid;
        s.order = m->second.stepOrder;
        s.done = has(questIds, qid);
        s.isNext = isNext;
        auto q = g_psQuests.find(qid);
        if (q != g_psQuests.end())
        {
            s.name = q->second.name;
            if (!q->second.goals.empty())
            {
                s.goalDone = PsCleanText(q->second.goals.front().complete);
                s.goalActive = PsCleanText(q->second.goals.front().active);
            }
        }
        if (s.name.empty())
            s.name = "(story step)";
        ch.steps.push_back(std::move(s));
    };
    for (int qid : questIds)
        addStep(qid, false);
    if (nextQid >= 0)
        addStep(nextQid, true);

    for (auto &kv : chmap)
    {
        PsChapter ch = std::move(kv.second);
        ch.number = kv.first.second;
        std::sort(ch.steps.begin(), ch.steps.end(), [](const PsStep &a, const PsStep &b)
                  { return a.order < b.order; });
        ch.allDone = true;
        for (const PsStep &s : ch.steps)
        {
            if (s.isNext)
                ch.hasNext = true;
            if (!s.done)
                ch.allDone = false;
        }
        v.chapters.push_back(std::move(ch));
    }

    g_psView[name] = std::move(v);
    ++g_psViewVer;
    g_psCacheDirty = true;
    if (name == g_psChar)
        g_psActiveDirty = true; // the displayed character's view changed -> re-activate
}

// Copy a character's cached view into the render-facing globals (the render reads those). resetSel on a real
// character switch (so the pane defaults back to the biography).
static void ActivateView(const std::string &cur, bool resetSel)
{
    g_psChar = cur;
    auto it = g_psView.find(cur);
    if (it != g_psView.end() && it->second.computed)
    {
        g_psRace = it->second.race;
        g_psProfession = it->second.profession;
        g_psBioParas = it->second.bioParas;
        g_psChapters = it->second.chapters;
        g_psReady = !g_psChapters.empty() || !g_psBioParas.empty();
    }
    else
    {
        g_psRace.clear();
        g_psProfession.clear();
        g_psBioParas.clear();
        g_psChapters.clear();
        g_psReady = false;
    }
    if (resetSel || g_psSel == -2)
        g_psSel = g_psReady ? -1 : -2;
}

// The current character's reconstructed personal-story progress (for the dashboard widget + suggested-next).
// Computed from the LOADED view (chapters + race + raw answers/quests) so it works whether the view came from a
// fresh API fetch OR the disk cache -- the cache path never runs RebuildViewFor, so we must not depend on it.
PsProgress PersonalStoryProgress(App &app)
{
    // Memoized per (character, view generation): the `total` pass below scans the whole ~590-entry quest map
    // into a std::set, and this is called EVERY frame by the dashboard widget, the Journal's suggested-next
    // banner and StoryRecommend (viewer card + release groups). g_psViewVer bumps whenever any view is rebuilt
    // or re-activated, so a real change still recomputes immediately.
    static std::string s_char = "\x01"; // sentinel: force the first build
    static uint64_t s_ver = (uint64_t)-1;
    static PsProgress s_cached;
    if (s_char == app.state.currentChar && s_ver == g_psViewVer)
        return s_cached;

    PsProgress p;
    auto it = g_psView.find(app.state.currentChar);
    if (it == g_psView.end() || !it->second.computed)
    {
        s_char = app.state.currentChar;
        s_ver = g_psViewVer;
        s_cached = p;
        return p;
    }
    const PsCharView &v = it->second;
    p.ready = true;

    // done + the real next chapter name + all-done, straight off the reconstructed chapters (the "My Story" tree).
    // nextName uses ch.hasNext (the chapter that renders the green "current" star -- a field the disk cache keeps),
    // not the per-step isNext, so it survives a cache load.
    for (const PsChapter &ch : v.chapters)
    {
        for (const PsStep &s : ch.steps)
            if (s.done)
                ++p.done;
        if (ch.hasNext && p.nextName.empty())
            p.nextName = ch.name;
    }
    p.allDone = !v.chapters.empty() && p.nextName.empty();

    // total = DISTINCT step positions on THIS character's branch (race arc -> Order -> Zhaitan), filtered to the
    // forks they actually chose. Mirrors RebuildViewFor's branch filter so the count matches the tree.
    const int arcOrder[3] = {PsRaceToStory(v.race), 9, 10};
    std::set<std::string> choices;
    std::set<std::string> orders;
    for (const std::string &aid : v.answerIds)
    {
        auto a = g_psAnswers.find(aid);
        if (a != g_psAnswers.end() && !a->second.title.empty())
            choices.insert(a->second.title);
    }
    for (int qid : v.questIds)
    {
        auto m = g_psMap.find(qid);
        if (m != g_psMap.end() && !m->second.choice.empty())
            choices.insert(m->second.choice);
        if (m != g_psMap.end() && !m->second.order.empty())
            orders.insert(m->second.order);
    }
    std::set<int> pos;
    for (const auto &kv : g_psMap)
    {
        const PsMapEntry &m = kv.second;
        int ai = -1;
        for (int i = 0; i < 3; ++i)
            if (arcOrder[i] == m.story)
            {
                ai = i;
                break;
            }
        if (ai < 0)
            continue;
        if (!m.choice.empty() && !choices.count(m.choice))
            continue; // a fork this character didn't take
        if (!m.order.empty() && !orders.count(m.order))
            continue; // an order path this character has not revealed through completed quests
        pos.insert(ai * 1000000 + m.chapterOrder * 1000 + m.stepOrder);
    }
    p.total = (int)pos.size();
    if (p.total < p.done)
        p.total = p.done; // safety (shouldn't happen)
    s_char = app.state.currentChar;
    s_ver = g_psViewVer;
    s_cached = p;
    return p;
}

// Once the static catalogs land, (re)compute any view that has raw source but wasn't computed yet (a fetch that
// finished before the catalogs).
static void OnPsCatalogsReady()
{
    for (auto &kv : g_psView)
        if (!kv.second.computed && !kv.second.questIds.empty())
            RebuildViewFor(kv.first, kv.second.race, kv.second.profession, kv.second.answerIds, kv.second.questIds);
}

static bool PsCatalogsReady() { return g_psQuestsFetched && g_psBioFetched; }
static std::map<std::string, PsCharView> g_psAcc; // per-character fetch accumulator (core+backstory+quests)

// Fetch the shared static catalogs (/v2/quests + /v2/backstory) once; on completion, compute any pending view.
static void EnsurePsCatalogs(App &app)
{
    if (!app.api.HasPermission(Api::TokenPermission::Characters))
        return;
    if (!g_psQuestsFetched && !g_psQuestsInflight)
    {
        g_psQuestsInflight = true;
        app.api.V2().Quests().GetAll([](Api::Result<std::vector<Api::V2::Quest>> r)
                                     {
            g_psQuestsInflight = false;
            if (!r.ok) return;
            for (auto& q : r.value) g_psQuests[q.id] = std::move(q);
            g_psQuestsFetched = true; if (PsCatalogsReady()) OnPsCatalogsReady(); });
    }
    if (!g_psBioFetched && !g_psBioInflight)
    {
        g_psBioInflight = true;
        g_psBioPending = 2;
        auto done = []
        { if (--g_psBioPending == 0) { g_psBioInflight = false; g_psBioFetched = true; if (PsCatalogsReady()) OnPsCatalogsReady(); } };
        app.api.V2().Backstory().Questions().GetAll([done](Api::Result<std::vector<Api::V2::BackstoryQuestion>> r)
                                                    {
            if (r.ok) for (auto& q : r.value) g_psQuestionOrder[q.id] = q.order;
            done(); });
        app.api.V2().Backstory().Answers().GetAll([done](Api::Result<std::vector<Api::V2::BackstoryAnswer>> r)
                                                  {
            if (r.ok) for (auto& a : r.value) g_psAnswers[a.id] = std::move(a);
            done(); });
    }
}

// When all three of a character's fetches land: rebuild its view only if new/changed (the played quest-id set
// is the staleness signature), reusing the cached view otherwise. Defers the rebuild if the catalogs aren't in.
static void PsCharFetchDone(const std::string &name)
{
    auto fit = g_psFetching.find(name);
    if (fit == g_psFetching.end() || --fit->second != 0)
        return;
    g_psFetching.erase(fit);
    auto ait = g_psAcc.find(name);
    if (ait == g_psAcc.end())
        return;
    PsCharView acc = std::move(ait->second);
    g_psAcc.erase(ait);

    auto vit = g_psView.find(name);
    const bool changed = vit == g_psView.end() || !vit->second.computed || vit->second.questIds != acc.questIds;
    if (!changed)
        return; // cache-then-refresh: nothing new, keep the cached view (instant, no re-save)
    if (PsCatalogsReady())
        RebuildViewFor(name, acc.race, acc.profession, acc.answerIds, acc.questIds);
    else
    {
        acc.computed = false;
        g_psView[name] = std::move(acc);
        ++g_psViewVer;
    } // stash raw; OnPsCatalogsReady() will build
}

// Background-fetch one character's core/backstory/quests (idempotent per character).
static void FetchCharStory(App &app, const std::string &name)
{
    if (name.empty() || name == "default" || g_psFetching.count(name))
        return;
    if (!app.api.HasPermission(Api::TokenPermission::Characters))
        return;
    g_psFetching[name] = 3;
    g_psAcc[name] = PsCharView{};
    app.api.V2().Characters().Core(name, [&app, name](Api::Result<Api::V2::CharacterCore> r)
                                   {
        if (r.ok)
        {
            auto a = g_psAcc.find(name); if (a != g_psAcc.end()) { a->second.race = r.value.race; a->second.profession = r.value.profession; }
            app.characterLevel.SetCachedLevel(app.state, name, r.value.level);   // warm the level cache for every character (no extra fetch)
            // Record this character's stable created-id. If it was last seen under a different name, that's a rename
            // -> merge the old name's per-character data into this one (settings/progress/sessions/levels/...).
            CharRegistry::Observe(name, r.value.created,
                                  [&app](const std::string& from, const std::string& to) { CharData::OnCharRename(app, from, to); });
        }
        PsCharFetchDone(name); });
    app.api.V2().Characters().Backstory(name, [name](Api::Result<std::vector<std::string>> r)
                                        {
        auto a = g_psAcc.find(name); if (a != g_psAcc.end() && r.ok) a->second.answerIds = std::move(r.value);
        PsCharFetchDone(name); });
    app.api.V2().Characters().Quests(name, [name](Api::Result<std::vector<int>> r)
                                     {
        auto a = g_psAcc.find(name); if (a != g_psAcc.end() && r.ok) a->second.questIds = std::move(r.value);
        PsCharFetchDone(name); });
}

// Persist the computed views so a later session paints instantly (cache-then-refresh, like journal-cache.json).
static void SavePersonalStoryCache()
{
    g_psCacheDirty = false;
    if (g_psCachePath.empty())
        return;
    nlohmann::json j;
    j["schema"] = kPersonalStoryCacheSchema;
    j["characters"] = nlohmann::json::object();
    for (const auto &kv : g_psView)
    {
        if (!kv.second.computed)
            continue;
        nlohmann::json c;
        c["race"] = kv.second.race;
        c["profession"] = kv.second.profession;
        c["answerIds"] = kv.second.answerIds;
        c["questIds"] = kv.second.questIds;
        c["bio"] = kv.second.bioParas;
        c["chapters"] = nlohmann::json::array();
        for (const auto &ch : kv.second.chapters)
        {
            nlohmann::json jc;
            jc["number"] = ch.number;
            jc["name"] = ch.name;
            jc["arc"] = ch.arc;
            jc["allDone"] = ch.allDone;
            jc["hasNext"] = ch.hasNext;
            jc["steps"] = nlohmann::json::array();
            for (const auto &s : ch.steps)
                jc["steps"].push_back({{"questId", s.questId}, {"name", s.name}, {"done", s.done}, {"next", s.isNext}, {"goalDone", s.goalDone}, {"goalActive", s.goalActive}, {"order", s.order}});
            c["chapters"].push_back(std::move(jc));
        }
        j["characters"][kv.first] = std::move(c);
    }
    try
    {
        Json::WriteAtomic(g_psCachePath, j.dump());
    }
    catch (...)
    {
    }
}

void LoadPersonalStoryCache(const std::string &path)
{
    g_psCachePath = path;
    std::ifstream f(path);
    if (!f)
        return;
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return;
    }
    if (j.value("schema", 0) != kPersonalStoryCacheSchema)
        return;
    if (!j.contains("characters") || !j["characters"].is_object())
        return;
    for (auto it = j["characters"].begin(); it != j["characters"].end(); ++it)
    {
        const auto &c = it.value();
        PsCharView v;
        bool valid = true;
        v.computed = true;
        v.race = c.value("race", std::string());
        v.profession = c.value("profession", std::string());
        if (c.contains("answerIds") && c["answerIds"].is_array())
            for (const auto &a : c["answerIds"])
                if (a.is_string())
                    v.answerIds.push_back(a.get<std::string>());
        if (c.contains("questIds") && c["questIds"].is_array())
            for (const auto &q : c["questIds"])
                if (q.is_number_integer())
                    v.questIds.push_back(q.get<int>());
        if (c.contains("bio") && c["bio"].is_array())
            for (const auto &b : c["bio"])
                if (b.is_string())
                    v.bioParas.push_back(b.get<std::string>());
        if (c.contains("chapters") && c["chapters"].is_array())
            for (const auto &jc : c["chapters"])
            {
                PsChapter ch;
                ch.number = jc.value("number", 0);
                ch.name = jc.value("name", std::string());
                ch.arc = jc.value("arc", std::string());
                ch.allDone = jc.value("allDone", false);
                ch.hasNext = jc.value("hasNext", false);
                if (jc.contains("steps") && jc["steps"].is_array())
                    for (const auto &js : jc["steps"])
                    {
                        PsStep s;
                        s.questId = js.value("questId", 0);
                        if (s.questId <= 0)
                            valid = false;
                        s.name = js.value("name", std::string());
                        s.done = js.value("done", false);
                        s.isNext = js.value("next", false);
                        s.goalDone = js.value("goalDone", std::string());
                        s.goalActive = js.value("goalActive", std::string());
                        s.order = js.value("order", 0);
                        ch.steps.push_back(std::move(s));
                    }
                v.chapters.push_back(std::move(ch));
            }
        if (valid)
        {
            g_psView[it.key()] = std::move(v);
            ++g_psViewVer;
        }
    }
}

// Login warm-up (from WarmCaches + the first scoped journal frame, idempotent): fetch the catalogs + EVERY
// character in the background, so opening the journal / switching characters is instant. Cheap incremental work
// on top of the disk cache loaded at startup. The character list is REUSED from AccountData (the account-wide
// hub already fetches /v2/characters?ids=all + caches it to disk) instead of a separate /v2/characters/names
// call -- so the names are available from the disk cache instantly and refresh with the rest of the account.
static bool g_psPreloadDone = false;
static void PreloadPersonalStory(App &app)
{
    if (g_psPreloadDone || !app.api.HasPermission(Api::TokenPermission::Characters))
        return;
    EnsurePsCatalogs(app);
    const auto &chars = AccountData::Get().characters;
    if (chars.empty())
        return; // hub not warmed yet -> retry on the next (per-frame) call
    g_psPreloadDone = true;
    for (const auto &ch : chars)
        if (!ch.name.empty())
            FetchCharStory(app, ch.name);
}

// Per-frame (Journal tab): ensure the catalogs + current character are fetched, activate the current view, and
// flush the cache if a view changed. Self-gates on the characters scope.
static void EnsurePersonalStoryData(App &app)
{
    if (!app.api.HasPermission(Api::TokenPermission::Characters))
        return;
    EnsurePsCatalogs(app);
    PreloadPersonalStory(app); // start the all-character preload here too, in case the key wasn't scoped at login
    const std::string cur = app.state.currentChar;
    if (!cur.empty() && cur != "default")
    {
        if (!g_psView.count(cur) && !g_psFetching.count(cur))
            FetchCharStory(app, cur); // not preloaded -> fetch now
        if (cur != g_psChar || g_psActiveDirty)
        {
            ActivateView(cur, cur != g_psChar);
            g_psActiveDirty = false;
        }
    }
    if (g_psCacheDirty)
        SavePersonalStoryCache();
}

// ArenaNet leaves the /v2/stories `description` (and sometimes `timeline`) EMPTY for many of the newest
// episodes (about half of Janthir Wilds / Visions of Eternity). No clean structured source exists to fetch
// live (the wiki episode pages are dialogue transcripts), so we ship curated blurbs in
// data/journal_overrides.json (see builder/docs/journal-overrides.md for the method + how to add more). The LIVE API
// ALWAYS WINS: an override only fills a blank, so a real description silently supersedes ours when published.
struct JournalOverride
{
    std::string timeline;
    std::string description;
};

static std::map<int, JournalOverride> g_journalOverrides; // story id -> bundled fallback blurb

void LoadJournalOverrides(const std::string &path)
{
    Json::LoadStoriesKeyed(path, [](int id, const nlohmann::json &e)
                           {
        JournalOverride o;
        o.timeline    = e.value("timeline", std::string());
        o.description = e.value("description", std::string());
        g_journalOverrides[id] = std::move(o); });
}

static const JournalOverride *FindJournalOverride(int id)
{
    auto it = g_journalOverrides.find(id);
    return it != g_journalOverrides.end() ? &it->second : nullptr;
}

// Per-episode in-world location(s), scraped from the wiki (the API has no map link) into
// data/journal_locations.json by builder/build_journal_locations.py. An episode can span MULTIPLE maps (Living
// World world-events, Icebrood DRM compilations): each gets a continent-1 PUBLIC map crop and the infobox
// ROTATES through them (hover pauses, click enlarges). Single-map episodes are a 1-element list (no rotation);
// an empty list means only the location text shows.
struct MapCrop
{
    int cont = 1;
    float rect[4] = {0, 0, 0, 0};
    std::string name;
}; // one map's crop + its display name
struct JournalLocation
{
    std::string location;
    std::vector<MapCrop> maps;
};

static std::map<int, JournalLocation> g_journalLocations;

void LoadJournalLocations(const std::string &path)
{
    Json::LoadStoriesKeyed(path, [](int id, const nlohmann::json &e)
                           {
        JournalLocation o;
        o.location = e.value("location", std::string());
        auto addCrop = [&](const nlohmann::json& src, const std::string& nm) {
            if (src.contains("contRect") && src["contRect"].is_array() && src["contRect"].size() == 4)
            {
                MapCrop mc; mc.cont = src.value("continentId", 1); mc.name = nm;
                for (int i = 0; i < 4; ++i) mc.rect[i] = src["contRect"][i].get<float>();
                o.maps.push_back(std::move(mc));
            }
        };
        if (e.contains("maps") && e["maps"].is_array())          // multi-map: a list of { mapName, continentId, contRect }
            for (const auto& m : e["maps"]) addCrop(m, m.value("mapName", std::string()));
        else                                                     // single-map (back-compat): the top-level contRect
            addCrop(e, o.location);
        g_journalLocations[id] = std::move(o); });
}

// Bundled story completion-time ESTIMATES (minutes) from gw2storytimes.com (builder/build_story_times.py). Keyed
// to GW2 ids so they drop straight onto the Journal: per GW2 story id (the per-episode/Act pill), per season guid
// (the season-header total, authoritative even for the newest releases), and per quest id (== gw2storytimes
// mission id, for the per-character My Story per-step estimate). storyMissing flags a partial per-episode sum
// where the GW2 API hasn't linked all of a story's quests yet (newest content).
static std::map<int, float> g_storyMins;          // GW2 story id -> minutes
static std::map<int, int> g_storyMissing;         // GW2 story id -> quests still lacking a time (partial)
static std::map<std::string, float> g_seasonMins; // season guid  -> minutes
static std::map<int, float> g_missionMins;        // GW2 quest id -> minutes

void LoadStoryTimes(const std::string &path)
{
    try
    {
        std::ifstream f(path);
        if (!f)
            return;
        nlohmann::json j;
        f >> j;
        auto intMap = [](const nlohmann::json &o, std::map<int, float> &out)
        {
            if (!o.is_object())
                return;
            for (auto it = o.begin(); it != o.end(); ++it)
                if (it.value().is_number())
                {
                    try
                    {
                        out[std::stoi(it.key())] = it.value().get<float>();
                    }
                    catch (...)
                    {
                    }
                }
        };
        if (j.contains("storyMins"))
            intMap(j["storyMins"], g_storyMins);
        if (j.contains("missionMins"))
            intMap(j["missionMins"], g_missionMins);
        if (j.contains("seasonMins") && j["seasonMins"].is_object())
            for (auto it = j["seasonMins"].begin(); it != j["seasonMins"].end(); ++it)
                if (it.value().is_number())
                    g_seasonMins[it.key()] = it.value().get<float>();
        if (j.contains("storyMissing") && j["storyMissing"].is_object())
            for (auto it = j["storyMissing"].begin(); it != j["storyMissing"].end(); ++it)
                if (it.value().is_number_integer())
                {
                    try
                    {
                        g_storyMissing[std::stoi(it.key())] = it.value().get<int>();
                    }
                    catch (...)
                    {
                    }
                }
    }
    catch (...)
    { /* best effort -- the time estimates are optional decoration */
    }
}

// Minutes for a GW2 story id / season guid / quest id, or -1 when we have no estimate.
static float StoryTimeMins(int storyId)
{
    auto it = g_storyMins.find(storyId);
    return it != g_storyMins.end() ? it->second : -1.f;
}
static float SeasonTimeMins(const std::string &guid)
{
    auto it = g_seasonMins.find(guid);
    return it != g_seasonMins.end() ? it->second : -1.f;
}
static float MissionTimeMins(int questId)
{
    auto it = g_missionMins.find(questId);
    return it != g_missionMins.end() ? it->second : -1.f;
}
static bool StoryTimePartial(int storyId) { return g_storyMissing.count(storyId) != 0; }

static const JournalLocation *FindJournalLocation(int id)
{
    auto it = g_journalLocations.find(id);
    return it != g_journalLocations.end() ? &it->second : nullptr;
}

// Persist the fetched journal data so a later session shows the tree INSTANTLY from disk, then refreshes from
// the API in the background ("cache then refresh"). We store the raw API json so re-parsing is exact.
static void SaveJournalCache()
{
    if (g_journalCachePath.empty() || g_jSeasons.empty() || g_jStories.empty())
        return;
    nlohmann::json j;
    j["seasons"] = nlohmann::json::array();
    for (const auto &s : g_jSeasons)
        j["seasons"].push_back(s.raw);
    j["stories"] = nlohmann::json::array();
    for (const auto &kv : g_jStories)
        j["stories"].push_back(kv.second.raw);
    try
    {
        Json::WriteAtomic(g_journalCachePath, j.dump());
    }
    catch (...)
    { /* best effort */
    }
}

void LoadJournalCache(const std::string &cachePath)
{
    g_journalCachePath = cachePath;
    if (g_journalCachePath.empty())
        return;
    std::ifstream f(g_journalCachePath);
    if (!f)
        return;
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (...)
    {
        return;
    }
    if (j.contains("seasons") && j["seasons"].is_array())
    {
        std::vector<Api::V2::StorySeason> seasons;
        for (const auto &e : j["seasons"])
            seasons.push_back(Api::V2::ParseStorySeason(e));
        std::sort(seasons.begin(), seasons.end(),
                  [](const Api::V2::StorySeason &a, const Api::V2::StorySeason &b)
                  { return a.order < b.order; });
        g_jSeasons = std::move(seasons);
        if (!g_jSeasons.empty())
            g_jExpanded[g_jSeasons.front().id] = true;
    }
    if (j.contains("stories") && j["stories"].is_array())
        for (const auto &e : j["stories"])
        {
            Api::V2::Story s = Api::V2::ParseStory(e);
            g_jStories[s.id] = std::move(s);
        }
}

static void EnsureJournalData(App &app)
{
    if (g_jFetchStarted)
        return;
    g_jFetchStarted = true;
    // Background refresh: replace the (possibly cached) data with fresh API data, then re-save the cache.
    app.api.V2().Stories().Seasons().GetAll([](Api::Result<std::vector<Api::V2::StorySeason>> r)
                                            {
        if (!r.ok) { if (g_jSeasons.empty()) g_jFetchStarted = false; return; }   // retry next open only if we had nothing
        std::sort(r.value.begin(), r.value.end(),
                  [](const Api::V2::StorySeason& a, const Api::V2::StorySeason& b) { return a.order < b.order; });
        g_jSeasons = std::move(r.value);
        if (!g_jSeasons.empty()) g_jExpanded[g_jSeasons.front().id] = true;
        SaveJournalCache(); });
    app.api.V2().Stories().GetAll([](Api::Result<std::vector<Api::V2::Story>> r)
                                  {
        if (!r.ok) return;
        for (auto& s : r.value) g_jStories[s.id] = std::move(s);
        SaveJournalCache(); });
    app.storyCompletion.RefreshAchievements(false); // account progress for auto-done (if the key is scoped)
}

// A release/season accent colour (matches the game's release brand) for the tree headers + detail band.
static ImU32 SeasonTint(const std::string &name)
{
    auto has = [&](const char *s)
    { return name.find(s) != std::string::npos; };
    if (has("My Story") || has("Personal"))
        return IM_COL32(80, 115, 170, 255); // personal story blue
    // Living World seasons: each is distinct but colour-tied to the expansion era it neighbours in the timeline.
    if (has("Season 1"))
        return IM_COL32(180, 62, 88, 255); // Scarlet's War: rose-crimson (red era, pinker than PoF)
    if (has("Season 2"))
        return IM_COL32(135, 175, 70, 255); // Dry Top/Silverwastes -> HoT: golden-lime (HoT green, but yellower)
    if (has("Heart of Thorns"))
        return IM_COL32(70, 150, 80, 255); // jungle green
    if (has("Season 3"))
        return IM_COL32(50, 135, 115, 255); // post-HoT toward Cantha: emerald-teal (darker/greener than EoD)
    if (has("Path of Fire"))
        return IM_COL32(180, 70, 60, 255); // crimson red
    if (has("Season 4"))
        return IM_COL32(150, 90, 185, 255); // Aurene/crystal: amethyst (PoF-red + Icebrood-blue blend)
    if (has("Icebrood"))
        return IM_COL32(90, 145, 210, 255); // Jormag ice: glacier blue (colder/bluer than EoD jade)
    if (has("End of Dragons"))
        return IM_COL32(55, 155, 165, 255); // cantha teal
    if (has("Secrets of the Obscure"))
        return IM_COL32(195, 160, 85, 255); // sky gold
    if (has("Janthir"))
        return IM_COL32(22, 68, 113, 255); // janthir blue (#164471, sampled from game)
    if (has("Visions of Eternity"))
        return IM_COL32(175, 58, 14, 255); // visions of eternity orange-red (#AF3A0E, sampled)
    return IM_COL32(160, 140, 90, 255);
}

// The personal-story band/tab tint = the character's RACE colour (the personal story is race-based). Uses the
// GW2 wiki Navigator hues, DARKENED to a band-appropriate intensity so the gold title + light caption read (the
// bright wiki hexes washed the text out). Falls back to the My Story blue before the race is known.
static ImU32 PsRaceTint()
{
    const std::string &r = g_psRace;
    if (r == "Asura")
        return IM_COL32(76, 50, 128, 255); // #9966FF purple
    if (r == "Charr")
        return IM_COL32(118, 52, 60, 255); // #D25D6B rose-red
    if (r == "Human")
        return IM_COL32(112, 90, 26, 255); // #FFCC33 gold (darker, so the gold title still reads)
    if (r == "Norn")
        return IM_COL32(50, 100, 128, 255); // #66CCFF blue
    if (r == "Sylvari")
        return IM_COL32(34, 104, 38, 255); // #33CC33 green
    return SeasonTint("My Story");
}

// The game's own collapsible-header background textures (gw2dat asset ids, 512x32 strips loaded from the CDN).
// Seasons without one fall back to the solid tint. Janthir Wilds + Visions of Eternity are NOT here because
// their strips aren't on gw2dat yet (the two newest releases - community extraction lags live); when they
// appear (id > 3333335, 512x32), add: Janthir -> ?, Visions of Eternity -> ?. Living World / My Story stay on
// the tint to match the game's subdued non-expansion headers.
static uint32_t SeasonBgAsset(const std::string &name)
{
    if (name.find("Heart of Thorns") != std::string::npos)
        return 1234874;
    if (name.find("Path of Fire") != std::string::npos)
        return 1827409;
    if (name.find("Icebrood") != std::string::npos)
        return 2630935;
    if (name.find("End of Dragons") != std::string::npos)
        return 2630936;
    if (name.find("Secrets of the Obscure") != std::string::npos)
        return 3333335;
    return 0;
}

// Scale a colour's RGB by `f` (clamped). Used to brighten a tint so a MULTIPLY over the mid-toned neutral
// strip lands back on the target hue (multiply only darkens, so the tint must be pre-brightened).
static ImU32 BrightenColor(ImU32 c, float f)
{
    auto ch = [&](int sh)
    { return (ImU32)std::min(255, (int)(((c >> sh) & 0xFFu) * f)); };
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), 255);
}

// Paint a release header band [a,b], matching how the GAME draws it. Real CDN strips (HoT/PoF/EoD/SotO) are
// RGBA with a baked alpha GRADIENT (opaque left -> transparent right) that the game blends over its dark
// journal panel, so the colour fades into the panel on the right. We replicate that: a dark base (the
// "panel"), then the FULL strip stretched across it respecting the strip's own alpha (NO cover-crop - the
// strips are designed to stretch into a header bar; cropping would throw away the gradient). Everything
// without CDN art (Janthir, VoE, Living World, My Story) tints our bundled neutral strip instead. Shared by
// the tree headers + the detail band.
static void DrawSeasonHeaderBg(ImDrawList *dl, ImVec2 a, ImVec2 b, const std::string &name, ImU32 tint)
{
    dl->AddRectFilled(a, b, IM_COL32(20, 21, 25, 255)); // dark "journal panel" the strip's alpha fades into

    const uint32_t bgId = SeasonBgAsset(name);
    const Texture_t *bg = bgId ? Tex::GetAssetTex(bgId) : nullptr;

    if (bg && bg->Resource && bg->Width > 0 && bg->Height > 0)
    {
        // Full strip (uv 0..1) stretched across the bar; its own alpha gradient blends over the dark base, so
        // it fades colour->panel on the right exactly like the game.
        dl->AddImage((ImTextureID)bg->Resource, a, b, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(255, 255, 255, 255));
    }
    else
    {
        // Everything without real CDN art (Janthir, VoE, EVERY Living World season, My Story): tint our bundled
        // neutral strip to the release hue, so they all carry the same painterly texture as the real strips.
        // The plain multiply only looked dark because of the source strip's baked ALPHA GRADIENT - its BRIGHT
        // pixels are at its TRANSPARENT (right) end, fading into the dark base. JournalNeutralTexB = the strip
        // made opaque (bright pixels show), with the texture the source carries in its ALPHA channel on the
        // mid/right (where the RGB goes flat) BAKED into luminance - each region keeps its OWN texture, no
        // cloning. A MODEST tint (x1.4, well below clipping -> no browning) lands the bright pixels on the
        // in-game peak with the texture intact. (tintAlpha unused now - kept for signature compatibility.)
        const ImU32 t = BrightenColor(tint, 1.40f);
        if (const Texture_t *gs = Tex::GetFileTex("data\\textures\\ui\\JournalNeutralTexB.png"); gs && gs->Resource)
            dl->AddImage((ImTextureID)gs->Resource, a, b, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), t);
        else
            dl->AddRectFilled(a, b, t); // not loaded yet: flat tint so the bar still shows the release colour
    }

    dl->AddRectFilled(a, ImVec2(a.x + 4.f, b.y), tint); // bright accent edge
}

static std::string JournalSeasonId(int storyId)
{
    auto it = g_jStoryPos.find(storyId);
    return it != g_jStoryPos.end() ? it->second.first : std::string();
}

static std::string JournalSeasonName(int storyId)
{
    const std::string id = JournalSeasonId(storyId);
    if (!id.empty())
        for (const auto &s : g_jSeasons)
            if (s.id == id)
                return s.name;
    return "";
}

static bool JournalPerCharacter(const std::string &seasonName)
{
    return seasonName.find("My Story") != std::string::npos || seasonName.find("Personal") != std::string::npos;
}

// The achievement ids an API story unlocks, looked up by /v2/stories id -- stories.json is keyed by the same
// `storyId`, so this is a direct hit with no name matching (the old normalized-name match could never pair an
// expansion's ACT entries with the Journal's EPISODE rows, leaving all of HoT/PoF/EoD/SotO/JW/VoE undetectable).
// Built once into a flat index: the spine never changes after load.
static const std::vector<int> &StoryAchievementIds(App &app, const Api::V2::Story &st)
{
    static std::map<int, std::vector<int>> index;
    static size_t builtFor = (size_t)-1;
    if (builtFor != app.stories.ByRelease().size())
    {
        index.clear();
        for (const auto &kv : app.stories.ByRelease())
            for (const StoryEpisode &ep : kv.second)
                if (ep.storyId > 0)
                    index[ep.storyId] = ep.achievementIds;
        builtFor = app.stories.ByRelease().size();
    }
    static const std::vector<int> kNone;
    auto it = index.find(st.id);
    return it != index.end() ? it->second : kNone;
}

static bool JournalAutoDone(App &app, const std::vector<int> &achIds)
{
    if (achIds.empty())
        return false;
    for (int id : achIds)
        if (!app.storyCompletion.AchievementDone(id))
            return false;
    return true;
}

static bool JournalDone(App &app, const Api::V2::Story &st)
{
    const std::vector<int> ach = StoryAchievementIds(app, st);
    if (JournalAutoDone(app, ach))
        return true;
    const std::string scope = JournalPerCharacter(JournalSeasonName(st.id)) ? app.state.currentChar : std::string();
    return app.storyStore.IsDone("story:" + std::to_string(st.id), scope);
}

static void ApplySeasonCascade(App &app, const std::vector<const Api::V2::Story *> &ordered, int idx, bool done)
{
    if (idx < 0 || idx >= (int)ordered.size())
        return;
    const std::string scope = JournalPerCharacter(JournalSeasonName(ordered[idx]->id)) ? app.state.currentChar : std::string();
    const auto set = [&](const Api::V2::Story &st, bool v)
    { app.storyStore.Set("story:" + std::to_string(st.id), v, scope); };

    if (done)
    {
        for (int i = 0; i <= idx; ++i)
            if (!JournalAutoDone(app, StoryAchievementIds(app, *ordered[i]))) // already proven -- don't store a duplicate
                set(*ordered[i], true);
    }
    else
    {
        for (int i = idx; i < (int)ordered.size(); ++i)
            set(*ordered[i], false);
    }
}

// The highest index in an ordered season that is known-done, or -1. The suggestion uses it to SUPERSEDE the
// episodes below: story is strictly sequential within a season, so anything before a confirmed completion has
// been played even when we cannot prove it (LWS1 and a handful of side episodes carry no completion
// achievement at all, and without this the "Suggested next" banner parks on the first of them forever).
// Read-only -- it never marks anything done, so the dots and the completion counts stay honest.
static int SeasonLastDone(App &app, const std::vector<const Api::V2::Story *> &ordered)
{
    for (int i = (int)ordered.size() - 1; i >= 0; --i)
        if (JournalDone(app, *ordered[i]))
            return i;
    return -1;
}

// Is this the personal-story ("My Story") season? Its API stories are branched per race, so we replace them
// with the 8 level-chapters above.
static bool IsPersonalSeason(const std::string &name)
{
    return name.find("My Story") != std::string::npos || name.find("Personal") != std::string::npos;
}

// Our stories.json "core" Chapter n (1..8), used for personal-story completion (per-character, cumulative).
static const StoryEpisode *CoreChapterEpisode(App &app, int n)
{
    const std::vector<StoryEpisode> *eps = app.stories.Episodes("core");
    if (!eps || n < 1 || n > (int)eps->size())
        return nullptr;
    return &(*eps)[n - 1];
}

static bool PersonalChapterDone(App &app, int n)
{
    const StoryEpisode *ep = CoreChapterEpisode(app, n);
    return ep && app.storyStore.IsDone("core:" + ep->name, app.state.currentChar);
}

// Fetch the reward Item icons/names we don't have yet (the achievement reward "chest"). Deduped + lazy.
// Routed through AccountData's shared item-metadata store (the inventory browser's `itemMeta` + the SAME
// `TC_ITEM_%d` texture/icon-cache scheme) so reward items and inventory items share one fetch + one cache.
static void EnsureItems(App &app, const std::vector<int> &ids)
{
    (void)app;
    AccountData::EnsureItemMetadata(ids);
}

// Prefetch ALL story achievements + their reward items in a couple of bulk calls (once), so selecting any
// episode shows its rewards immediately instead of fetching on click. Uses our stories.json achievementIds.
// DIAGNOSTIC (temporary): outcome of the last Journal reward (achievement) fetch, shown in the "Loading
// rewards..." line so a stuck episode tells us WHY (network / rate-limited / forbidden / parse / http code).
static std::string g_jRewardDiag = "(no fetch yet)";
static std::string RewardErr(const Api::ApiError &e)
{
    const char *k =
        e.kind == Api::ErrorKind::Network ? "Network" : e.kind == Api::ErrorKind::RateLimited ? "RateLimited"
                                                    : e.kind == Api::ErrorKind::Forbidden     ? "Forbidden"
                                                    : e.kind == Api::ErrorKind::Unauthorized  ? "Unauthorized"
                                                    : e.kind == Api::ErrorKind::Parse         ? "Parse"
                                                    : e.kind == Api::ErrorKind::NotFound      ? "NotFound"
                                                    : e.kind == Api::ErrorKind::ServiceDown   ? "ServiceDown"
                                                    : e.kind == Api::ErrorKind::BadRequest    ? "BadRequest"
                                                                                              : "Other";
    return std::string(k) + " http=" + std::to_string(e.status) + (e.text.empty() ? "" : " \"" + e.text + "\"");
}

static void PrefetchJournalRewards(App &app)
{
    if (g_jPrefetched)
        return;
    std::set<int> achSet;
    for (const auto &kv : app.stories.ByRelease())
        for (const StoryEpisode &ep : kv.second)
            for (int id : ep.achievementIds)
                achSet.insert(id);
    if (achSet.empty())
        return; // spine not loaded yet; retry next frame
    if (ImGui::GetTime() < g_jPrefetchRetryAt)
        return;                                  // a prior attempt failed recently; wait before retrying
    g_jPrefetchRetryAt = ImGui::GetTime() + 5.0; // if THIS attempt fails, don't retry for 5s
    g_jPrefetched = true;
    std::vector<int> achIds(achSet.begin(), achSet.end());
    g_jRewardDiag = "bulk: fetching " + std::to_string(achIds.size()) + " ach...";
    app.api.V2().Achievements().Get(achIds, [&app](Api::Result<std::vector<Api::V2::Achievement>> r)
                                    {
        if (!r.ok) { g_jPrefetched = false; g_jRewardDiag = "bulk FAIL: " + RewardErr(r.error); return; }
        g_jRewardDiag = "bulk ok: " + std::to_string(r.value.size());
        std::vector<int> itemIds;
        for (auto& a : r.value)
        {
            for (const auto& rw : a.rewards) if (rw.type == "Item") itemIds.push_back(rw.id);
            if (!a.icon.empty()) { char tid[40]; std::snprintf(tid, sizeof(tid), "TC_ACH_%d", a.id); ImageCache::PrefetchUrl(tid, a.icon.c_str()); }
            g_jAch[a.id] = std::move(a);
        }
        EnsureItems(app, itemIds); });
}

// A region accent colour for the mastery-insight reward gem (matches the game's regional mastery-point hues).
static ImU32 MasteryRegionColor(const std::string &region)
{
    if (region == "Tyria")
        return IM_COL32(95, 205, 205, 255); // central tyria teal
    if (region == "Maguuma")
        return IM_COL32(120, 205, 90, 255); // jungle green
    if (region == "Desert")
        return IM_COL32(228, 150, 60, 255); // crystal desert orange
    if (region == "Tundra")
        return IM_COL32(120, 185, 235, 255); // icebrood ice blue
    if (region == "Jade")
        return IM_COL32(80, 205, 150, 255); // cantha jade
    if (region == "Sky")
        return IM_COL32(225, 195, 110, 255); // soto sky gold
    if (region == "Wild")
        return IM_COL32(135, 195, 110, 255); // janthir green
    if (region == "Magic")
        return IM_COL32(185, 130, 225, 255); // magic purple
    return IM_COL32(185, 200, 210, 255);
}

static void SelectJournalStory(App &app, int id)
{
    g_jSelected = id;
    g_jSelChapter = 0; // an API story supersedes a personal-chapter selection
    auto it = g_jStories.find(id);
    if (it == g_jStories.end())
        return;
    std::vector<int> need;
    for (int aid : StoryAchievementIds(app, it->second))
        if (!g_jAch.count(aid))
            need.push_back(aid);
    if (!need.empty())
    {
        g_jRewardDiag = "sel: fetching " + std::to_string(need.size()) + " ach...";
        app.api.V2().Achievements().Get(need, [](Api::Result<std::vector<Api::V2::Achievement>> r)
                                        {
            if (r.ok) { for (auto& a : r.value) g_jAch[a.id] = std::move(a); g_jRewardDiag = "sel ok: " + std::to_string(r.value.size()); }
            else g_jRewardDiag = "sel FAIL: " + RewardErr(r.error); });
    }
    else
        g_jRewardDiag = "all cached";
}

// A framed completion control for the journal detail panel (replaces a bare checkbox): a rounded panel that
// turns green when done, with the GW2 checkbox + a state label. `locked` = auto-completed from achievements
// (shown ticked + non-interactive). Returns true when the user toggled it (caller persists *checked).
static bool JournalCompleteRow(const char *label, bool *checked, bool locked)
{
    const float sc = Gw2Ui::TextScale();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float h = 50.f * sc, box = 32.f * sc, lblFs = 18.f;
    const float padX = 14.f * sc, gap = 10.f * sc;
    const float labelW = Gw2Ui::MeasureWidth(label, lblFs);
    const float w = std::min(avail, padX + box + gap + labelW + 18.f * sc); // fit content: pad+box+gap+label+pad
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    const ImVec2 p(cur.x + (avail - w) * 0.5f, cur.y); // centre the panel in the detail pane
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const bool done = checked && *checked;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), done ? IM_COL32(30, 52, 33, 190) : IM_COL32(26, 26, 30, 150), 5.f * sc);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), done ? IM_COL32(96, 165, 104, 210) : IM_COL32(96, 90, 80, 200), 5.f * sc, 0, sc);
    const ImVec2 boxP(p.x + padX, p.y + (h - box) * 0.5f);
    ImGui::SetCursorScreenPos(boxP);
    bool toggled = false;
    if (locked)
        Gw2Ui::PaintCheckbox(boxP, box, 1, false); // ticked, non-interactive
    else
        toggled = Gw2Ui::Checkbox("##jdone", checked);
    Gw2Ui::LabelIn(ImVec2(boxP.x + box + gap, p.y), ImVec2(p.x + w - 8.f * sc, p.y + h), label,
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                   done ? IM_COL32(175, 224, 181, 255) : IM_COL32(235, 230, 215, 255), false, nullptr, lblFs);
    ImGui::SetCursorScreenPos(ImVec2(cur.x, p.y + h + 6.f * sc));
    return toggled;
}

// A small rounded "Lv N" badge in the detail band, ending at rightX, vertically centred on cy. AMBER when the
// character is below the required level (the game's "will become playable" cue), muted green-gold otherwise.
// Returns its left x so the caller can lay the title out up to it.
static float DrawLevelPill(ImDrawList *dl, float rightX, float cy, int level, bool locked)
{
    char txt[16];
    std::snprintf(txt, sizeof(txt), "Lv %d", level);
    const float fs = 16.f, ph = 23.f, padX = 9.f;
    const float pw = Gw2Ui::MeasureWidth(txt, fs) + padX * 2.f;
    const ImVec2 a(rightX - pw, cy - ph * 0.5f), b(rightX, cy + ph * 0.5f);
    dl->AddRectFilled(a, b, locked ? IM_COL32(78, 52, 18, 235) : IM_COL32(36, 44, 39, 235), ph * 0.5f);
    dl->AddRect(a, b, locked ? IM_COL32(205, 150, 60, 225) : IM_COL32(150, 178, 150, 205), ph * 0.5f);
    Gw2Ui::LabelIn(a, b, txt, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                   locked ? IM_COL32(255, 206, 120, 255) : IM_COL32(222, 236, 222, 255), false, nullptr, fs);
    return a.x;
}

// Defined later (with the Zones/ImageCache code); forward-declared for the Journal's location map.

static MapCrop g_jMapZoomCrop;      // the crop shown enlarged in the click popup
static std::string g_jMapZoomTitle; // its caption

// A wiki-style infobox: a bordered panel with the location map (fit to its own aspect, centred, so there is NO
// dark letterbox) and the location/map name beneath it. An episode that spans several maps ROTATES through them
// every few seconds (hover pauses on the one under the cursor; a "(i/N)" counter + dot strip show there are
// more); clicking the map opens an enlarged popup of whatever is currently shown. Drawn at `origin`, `width`
// wide; returns its total height so the caller can lay out the column beside it.
static float DrawEpisodeInfobox(int epId, const JournalLocation &loc, ImVec2 origin, float width)
{
    if (loc.maps.empty())
        return 0.f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const int n = (int)loc.maps.size();

    // Rotate through the crops; freeze while hovered. Only one infobox is on screen at a time, so a single
    // phase accumulator suffices; reset it when the selected episode changes so each starts on its first map.
    static int s_ep = -1;
    static double s_phase = 0.0, s_lastT = 0.0;
    const double now = ImGui::GetTime();
    if (epId != s_ep)
    {
        s_ep = epId;
        s_phase = 0.0;
        s_lastT = now;
    }
    const float kRotateSec = 3.f;
    const int idx = (int)(s_phase / kRotateSec) % n;
    const MapCrop &mc = loc.maps[idx];

    const float pad = 9.f, inner = width - 2.f * pad;
    const float cw = std::max(1.f, mc.rect[2] - mc.rect[0]);
    const float ch = std::max(1.f, mc.rect[3] - mc.rect[1]);
    const float aspect = ch / cw;
    float boxW = inner, boxH = boxW * aspect; // fit the crop inside (inner x maxH) -> no letterbox
    const float maxH = 280.f;
    if (boxH > maxH)
    {
        boxH = maxH;
        boxW = boxH / aspect;
    }
    const float mapX = origin.x + pad + (inner - boxW) * 0.5f; // centre the map in the panel
    const float mapY = origin.y + pad;
    const float labelY = mapY + boxH + 9.f, labelH = 22.f;
    const float namesY = labelY + labelH;
    // caption = the current map's name (rotates), with a counter when there is more than one.
    std::string caption = mc.name.empty() ? loc.location : mc.name;
    if (n > 1)
    {
        char c[16];
        std::snprintf(c, sizeof(c), "   (%d/%d)", idx + 1, n);
        caption += c;
    }
    const float namesH = Gw2Ui::MeasureWrappedHeight(caption.c_str(), 16.f, inner);
    const float height = (namesY + namesH + pad) - origin.y;

    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(20, 22, 27, 235), 5.f);
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(120, 110, 78, 180), 5.f);

    ImGui::SetCursorScreenPos(ImVec2(mapX, mapY));
    ImGui::InvisibleButton("##jinfomap", ImVec2(boxW, boxH));
    const bool clicked = ImGui::IsItemClicked();
    const bool hov = ImGui::IsItemHovered();
    DrawMapThumbnailRect(mc.cont, mc.rect[0], mc.rect[1], mc.rect[2], mc.rect[3], ImVec2(mapX, mapY), ImVec2(boxW, boxH));
    if (hov) // signal it's clickable
    {
        dl->AddRect(ImVec2(mapX, mapY), ImVec2(mapX + boxW, mapY + boxH), Gw2Ui::Alpha(Gw2Ui::kGold, 220), 0, 0, 2.f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (clicked)
    {
        g_jMapZoomCrop = mc;
        g_jMapZoomTitle = mc.name.empty() ? loc.location : mc.name;
        ImGui::OpenPopup("##jmapzoom");
    }

    if (n > 1) // a small dot strip (bottom-centre of the map) marking which crop is showing
    {
        const float sp = 11.f, dy = mapY + boxH - 9.f;
        float dx = mapX + boxW * 0.5f - (n - 1) * sp * 0.5f;
        for (int i = 0; i < n; ++i, dx += sp)
            dl->AddCircleFilled(ImVec2(dx, dy), 3.f, i == idx ? Gw2Ui::kGold : IM_COL32(90, 86, 70, 220));
    }

    Gw2Ui::LabelIn(ImVec2(origin.x + pad, labelY), ImVec2(origin.x + width - pad, labelY + labelH), "Location",
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kGold, true, Gw2Ui::Gw2Italic(), 18.f);
    Gw2Ui::LabelIn(ImVec2(origin.x + pad, namesY), ImVec2(origin.x + width - pad, namesY + namesH), caption.c_str(),
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(218, 215, 228, 255), false, nullptr, 16.f, inner);

    if (!hov && n > 1)
        s_phase += now - s_lastT; // advance only when NOT hovered (pause the map under the cursor)
    s_lastT = now;
    return height;
}

// The enlarged-map popup, opened by clicking an infobox map. Drawn from the same scope that called OpenPopup.
static void DrawMapZoomPopup()
{
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(16, 18, 22, 252));
    if (ImGui::BeginPopup("##jmapzoom"))
    {
        const float cw = std::max(1.f, g_jMapZoomCrop.rect[2] - g_jMapZoomCrop.rect[0]);
        const float ch = std::max(1.f, g_jMapZoomCrop.rect[3] - g_jMapZoomCrop.rect[1]);
        float bw = 680.f, bh = bw * (ch / cw);
        if (bh > 680.f)
        {
            bh = 680.f;
            bw = bh * (cw / ch);
        }
        Gw2Ui::Label(g_jMapZoomTitle.c_str(), Gw2Ui::kGold, true, nullptr, 20.f);
        ImGui::Spacing();
        const ImVec2 mp = ImGui::GetCursorScreenPos();
        DrawMapThumbnailRect(g_jMapZoomCrop.cont, g_jMapZoomCrop.rect[0], g_jMapZoomCrop.rect[1], g_jMapZoomCrop.rect[2], g_jMapZoomCrop.rect[3], mp, ImVec2(bw, bh));
        ImGui::Dummy(ImVec2(bw, bh));
        Gw2Ui::Label("Click outside to close.", IM_COL32(165, 160, 145, 255), false, nullptr, 14.f);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
}

static void DrawJournalDetail(App &app, const Api::V2::Story &st)
{
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const std::string seas = JournalSeasonName(st.id);
    const ImU32 tint = SeasonTint(seas);
    const JournalOverride *ov = FindJournalOverride(st.id); // bundled fallback when the API leaves fields blank

    // Release header band: the release's background texture (or tint) + title (left) and, on the right, the
    // timeline year (bold, like the game) plus a "Lv N" pill (amber when below the character's level).
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bandH = 46.f;
    DrawSeasonHeaderBg(dl, p, ImVec2(p.x + w, p.y + bandH), seas, tint);
    const float cy = p.y + bandH * 0.5f;
    float clusterL = p.x + w - 12.f; // left edge of the right-hand (year + level pill) cluster
    const char *timeline = !st.timeline.empty() ? st.timeline.c_str() : (ov && !ov->timeline.empty() ? ov->timeline.c_str() : "");
    if (timeline[0])
    {
        const float tlw = Gw2Ui::MeasureWidth(timeline, 20.f);
        Gw2Ui::LabelIn(ImVec2(clusterL - tlw, p.y), ImVec2(clusterL, p.y + bandH), timeline,
                       Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(245, 240, 225, 255), true, nullptr, 20.f, 0.f, 1.4f);
        clusterL -= tlw + 12.f;
    }
    if (st.level > 0)
        clusterL = DrawLevelPill(dl, clusterL, cy, st.level, app.state.charLevel > 0 && app.state.charLevel < st.level) - 10.f;
    Gw2Ui::LabelIn(ImVec2(p.x + 14.f, p.y), ImVec2(std::max(p.x + 14.f, clusterL), p.y + bandH), st.name.c_str(),
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSelected, true, nullptr, 24.f, 0.f, 1.5f);
    ImGui::Dummy(ImVec2(w, bandH + 6.f));

    // Treat blank OR junk ("Summary of Chapter NN" - placeholder text the API returns for a couple of
    // episodes, e.g. Roots of Terror) as no description, so the curated override fills it instead.
    const bool apiDescOk = !st.description.empty() && st.description.rfind("Summary of Chapter", 0) != 0;
    const char *desc = apiDescOk ? st.description.c_str() : (ov && !ov->description.empty() ? ov->description.c_str() : "");
    const JournalLocation *loc = FindJournalLocation(st.id);

    if (loc && !loc->maps.empty())
    {
        // Wiki layout: the story text on the LEFT, a bordered map infobox on the RIGHT (both from the same top).
        const float infoW = std::clamp(w * 0.42f, 240.f, 320.f);
        const float gap = 14.f;
        const float leftW = std::max(160.f, w - infoW - gap);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        float descH = 0.f;
        if (desc[0])
        {
            descH = Gw2Ui::MeasureWrappedHeight(desc, 20.f, leftW);
            Gw2Ui::LabelIn(top, ImVec2(top.x + leftW, top.y + descH), desc, Gw2Ui::HAlign::Left,
                           Gw2Ui::VAlign::Top, IM_COL32(225, 222, 212, 255), false, nullptr, 20.f, leftW);
        }
        const float infoH = DrawEpisodeInfobox(st.id, *loc, ImVec2(top.x + leftW + gap, top.y), infoW);
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + std::max(descH, infoH) + 8.f));
    }
    else
    {
        if (loc && !loc->location.empty()) // located but no map image -> a full-width "Location" text line
        {
            Gw2Ui::Label("Location", Gw2Ui::kGold, true, Gw2Ui::Gw2Italic(), 18.f);
            const float lh = Gw2Ui::MeasureWrappedHeight(loc->location.c_str(), 18.f, w);
            const ImVec2 lp = ImGui::GetCursorScreenPos();
            Gw2Ui::LabelIn(lp, ImVec2(lp.x + w, lp.y + lh), loc->location.c_str(), Gw2Ui::HAlign::Left,
                           Gw2Ui::VAlign::Top, IM_COL32(215, 212, 225, 255), false, nullptr, 18.f, w);
            ImGui::Dummy(ImVec2(w, lh));
            ImGui::Spacing();
        }
        if (desc[0])
        {
            const float dh = Gw2Ui::MeasureWrappedHeight(desc, 20.f, w);
            const ImVec2 dp = ImGui::GetCursorScreenPos();
            Gw2Ui::LabelIn(dp, ImVec2(dp.x + w, dp.y + dh), desc, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                           IM_COL32(225, 222, 212, 255), false, nullptr, 20.f, w);
            ImGui::Dummy(ImVec2(w, dh));
            ImGui::Spacing();
        }
    }

    const std::vector<int> achIds = StoryAchievementIds(app, st);
    if (!achIds.empty())
    {
        Gw2Ui::Divider();
        int doneCount = 0;
        for (int id : achIds)
            if (app.storyCompletion.AchievementDone(id))
                ++doneCount;
        char rhdr[64];
        std::snprintf(rhdr, sizeof(rhdr), "Achievements and Rewards   (%d/%d)", doneCount, (int)achIds.size());
        Gw2Ui::Label(rhdr, Gw2Ui::kGold, true, Gw2Ui::Gw2Italic(), 20.f);
        ImGui::Spacing();

        std::vector<int> itemIds;
        for (int aid : achIds)
        {
            auto it = g_jAch.find(aid);
            if (it != g_jAch.end())
                for (const auto &rw : it->second.rewards)
                    if (rw.type == "Item")
                        itemIds.push_back(rw.id);
        }
        EnsureItems(app, itemIds);

        // Build a flat list of reward icons across this episode's achievements: each item "chest" + a region-
        // coloured mastery-insight gem. Drawn HORIZONTAL + wrapped, like the game's Achievements and Rewards.
        struct Rw
        {
            bool mastery;
            void *tex;
            ImU32 color;
            bool done;
            std::string tip;
        };
        std::vector<Rw> rws;
        for (int aid : achIds)
        {
            auto it = g_jAch.find(aid);
            if (it == g_jAch.end())
                continue;
            const Api::V2::Achievement &a = it->second;
            const bool aDone = app.storyCompletion.AchievementDone(a.id) != 0;
            bool any = false;
            for (const auto &rw : a.rewards)
            {
                if (rw.type == "Item")
                {
                    void *tex = nullptr;
                    std::string nm;
                    const auto &itemMeta = AccountData::Get().itemMeta;
                    auto iit = itemMeta.find(rw.id);
                    if (iit != itemMeta.end() && iit->second.have)
                    {
                        nm = iit->second.name;
                        if (!iit->second.icon.empty())
                        {
                            char tid[40];
                            std::snprintf(tid, sizeof(tid), "TC_ITEM_%d", rw.id);
                            tex = Tex::GetTextureFromURL(tid, iit->second.icon.c_str());
                        }
                    }
                    std::string tip = a.name + (nm.empty() ? "" : ("\nReward: " + nm)) + (aDone ? "\nCompleted" : "\nIn progress");
                    rws.push_back({false, tex, 0u, aDone, tip});
                    any = true;
                }
                else if (rw.type == "Mastery")
                {
                    rws.push_back({true, nullptr, MasteryRegionColor(rw.region), aDone,
                                   a.name + "\nMastery Insight (" + rw.region + ")" + (aDone ? "\nCompleted" : "\nIn progress")});
                    any = true;
                }
            }
            if (!any) // no item/mastery reward -> show the achievement's own icon, or a placeholder box if it has none
            {
                void *tex = nullptr;
                if (!a.icon.empty())
                {
                    char tid[40];
                    std::snprintf(tid, sizeof(tid), "TC_ACH_%d", a.id);
                    tex = Tex::GetTextureFromURL(tid, a.icon.c_str());
                }
                rws.push_back({false, tex, 0u, aDone, a.name + (aDone ? "\nCompleted" : "\nIn progress")});
            }
        }

        const float iconSz = 44.f, gap = 10.f;
        const float availW = ImGui::GetContentRegionAvail().x;
        const int perRow = std::max(1, (int)((availW + gap) / (iconSz + gap)));
        for (size_t i = 0; i < rws.size(); ++i)
        {
            if ((int)(i % perRow) != 0)
                ImGui::SameLine(0.f, gap);
            ImGui::PushID((int)i);
            const ImVec2 ip = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##rw", ImVec2(iconSz, iconSz));
            const Rw &e = rws[i];
            if (e.mastery)
            {
                const ImVec2 c(ip.x + iconSz * 0.5f, ip.y + iconSz * 0.5f);
                const float r = iconSz * 0.42f;
                dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), e.color);
                dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), IM_COL32(25, 35, 35, 220), 2.f);
                dl->AddQuadFilled(ImVec2(c.x, c.y - r * 0.5f), ImVec2(c.x + r * 0.5f, c.y), ImVec2(c.x, c.y + r * 0.5f), ImVec2(c.x - r * 0.5f, c.y), IM_COL32(255, 255, 255, 70));
            }
            else if (e.tex)
                dl->AddImage((ImTextureID)e.tex, ip, ImVec2(ip.x + iconSz, ip.y + iconSz));
            else
            {
                dl->AddRectFilled(ip, ImVec2(ip.x + iconSz, ip.y + iconSz), IM_COL32(0, 0, 0, 80));
                dl->AddRect(ip, ImVec2(ip.x + iconSz, ip.y + iconSz), IM_COL32(120, 120, 120, 160));
            }
            if (e.done)
                dl->AddCircleFilled(ImVec2(ip.x + iconSz - 7.f, ip.y + iconSz - 7.f), 6.f, IM_COL32(120, 210, 130, 255));
            if (ImGui::IsItemHovered())
                Gw2Ui::Tooltip(e.tip.c_str());
            ImGui::PopID();
        }
        if (rws.empty())
            Gw2Ui::Label(("Loading rewards..." + (app.config.debugApi ? ("   [" + g_jRewardDiag + "]") : std::string())).c_str(), IM_COL32(170, 150, 150, 255), false, nullptr, 16.f);
        ImGui::Spacing();
    }

    // The manual completion control (a framed panel; locked + green when auto-confirmed from achievements).
    Gw2Ui::Divider();
    const std::string scope = JournalPerCharacter(seas) ? app.state.currentChar : std::string();
    const std::string key = "story:" + std::to_string(st.id);
    const bool autoDone = JournalAutoDone(app, achIds);
    bool checked = autoDone || app.storyStore.IsDone(key, scope);
    const char *clbl = autoDone ? "Completed (from your achievements)"
                                : (checked ? "Completed - click to undo (also clears the later episodes)"
                                           : "Mark this episode complete (also marks the earlier ones)");
    if (JournalCompleteRow(clbl, &checked, autoDone))
    {
        // Cascade over the season run (ticking N marks 1..N), matching the personal story's chapter cascade.
        auto pos = g_jStoryPos.find(st.id);
        auto sit = pos != g_jStoryPos.end() ? g_jSeasonStories.find(pos->second.first) : g_jSeasonStories.end();
        if (sit != g_jSeasonStories.end())
            ApplySeasonCascade(app, sit->second, pos->second.second, checked);
        else
            app.storyStore.Set(key, checked, scope); // not in a season list (shouldn't happen) -- plain single mark
    }

    // Pin the story suggestion to THIS release. The suggestion otherwise walks releases chronologically, which
    // is wrong for anyone playing out of order -- and the API never reports which story the player selected
    // in-game, so an explicit choice is the only signal there is.
    const std::string trackRel = StoryData::ReleaseForSeason(seas);
    if (!trackRel.empty() && !StoryData::PerCharacter(trackRel))
    {
        ImGui::Spacing();
        const bool tracking = (app.config.storyTrack == trackRel);
        const std::string relName = StoryData::ReleaseName(trackRel);
        const std::string label = tracking ? ("Tracking " + relName + " - click to stop")
                                           : ("Track " + relName);
        const std::string tip = tracking
            ? "The story suggestion follows this release; click to go back to the suggested order"
            : ("Make the guide suggest " + relName + " next, instead of the earliest unfinished story");
        const float availW = ImGui::GetContentRegionAvail().x;
        const float bw = std::min(availW, Gw2Ui::Scaled(320.f));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - bw) * 0.5f);   // centred, like the completion panel above
        if (Gw2Ui::ActionButtonPx(label.c_str(), bw, Gw2Ui::Scaled(26.f),
                                  tracking ? Gw2Ui::ActionButtonVariant::Primary : Gw2Ui::ActionButtonVariant::Normal,
                                  tip.c_str()))
        {
            app.config.storyTrack = tracking ? std::string() : trackRel;
            app.settingsDirty = true;
        }
    }

    DrawMapZoomPopup(); // enlarged map, opened by clicking the infobox map
}

// Detail for a personal-story chapter (the My Story special-case): level-tinted band + a note that the
// personal story varies per character + a cumulative "Mark complete" (ticking N marks 1..N).
static void DrawPersonalChapterDetail(App &app, int n)
{
    if (n < 1 || n > 8)
        return;
    const PersonalChapter &pc = kPersonal[n - 1];
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 tint = PsRaceTint();

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bandH = 46.f;
    DrawSeasonHeaderBg(dl, p, ImVec2(p.x + w, p.y + bandH), "My Story", tint);
    char title[48];
    std::snprintf(title, sizeof(title), "%d. %s", n, pc.name);
    Gw2Ui::LabelIn(ImVec2(p.x + 14.f, p.y), ImVec2(p.x + w * 0.70f, p.y + bandH), title,
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSelected, true, nullptr, 24.f, 0.f, 1.5f);
    Gw2Ui::LabelIn(ImVec2(p.x + w * 0.70f, p.y), ImVec2(p.x + w - 12.f, p.y + bandH), "Personal Story",
                   Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(235, 230, 215, 255), true, nullptr, 18.f);
    ImGui::Dummy(ImVec2(w, bandH + 6.f));

    // (No separate "unlocks at level" line - the chapter title already states the level, e.g. "7. Level 70".)
    const char *desc = (n == 8)
                           ? "Victory or Death is the finale of the personal story - the assault on the Elder Dragon Zhaitan at "
                             "Arah. It is the same for every character. Play it from the in-game Story Journal."
                           : "The personal story you play depends on your character's race, profession, and the biography choices "
                             "you made at creation, so these chapters differ from character to character - only the finale "
                             "(Victory or Death, level 80) is the same for everyone. Play it from the in-game Story Journal.";
    if (n == 8) // the global finale -> show its location map (Arah, in Cursed Shore), wiki-style: text LEFT, map RIGHT
    {
        static const JournalLocation kVoD{"Arah, Ruins of Orr (Cursed Shore)",
                                          {MapCrop{1, {42880.f, 41600.f, 44928.f, 45696.f}, "Arah, Ruins of Orr (Cursed Shore)"}}};
        const float infoW = std::clamp(w * 0.42f, 240.f, 320.f), gap = 14.f, leftW = std::max(160.f, w - infoW - gap);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const float descH = Gw2Ui::MeasureWrappedHeight(desc, 18.f, leftW);
        Gw2Ui::LabelIn(top, ImVec2(top.x + leftW, top.y + descH), desc, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       IM_COL32(225, 222, 212, 255), false, nullptr, 18.f, leftW);
        const float infoH = DrawEpisodeInfobox(900008, kVoD, ImVec2(top.x + leftW + gap, top.y), infoW);
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + std::max(descH, infoH) + 8.f));
    }
    else
    {
        const float dh = Gw2Ui::MeasureWrappedHeight(desc, 18.f, w);
        const ImVec2 dp = ImGui::GetCursorScreenPos();
        Gw2Ui::LabelIn(dp, ImVec2(dp.x + w, dp.y + dh), desc, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                       IM_COL32(225, 222, 212, 255), false, nullptr, 18.f, w);
        ImGui::Dummy(ImVec2(w, dh));
    }
    Gw2Ui::Divider();

    const StoryEpisode *ep = CoreChapterEpisode(app, n);
    bool checked = ep && app.storyStore.IsDone("core:" + ep->name, app.state.currentChar);
    const char *clbl = checked ? "Completed (earlier chapters too)" : "Mark complete (also marks earlier chapters)";
    if (JournalCompleteRow(clbl, &checked, false) && ep)
        ApplyChapterCascade(app, "core", *ep, checked, app.state.currentChar);

    DrawMapZoomPopup(); // enlarged map if the finale's location infobox was clicked (no-op for chapters 1-7)
}

// Right-aligned band caption (race+profession / arc). The other seasons' year reads because it sits on a
// darker/textured strip; the "My Story" band is a flat BRIGHT-blue tint, so the same light caption washes out.
// Restore the year's legibility with a dark drop-shadow under a brighter fill (plus the shared stroke+weight).
// Returns the width it consumed (so the left title can clamp to it).
static float DrawPsBandRight(ImVec2 bandP, float w, float bandH, const std::string &text)
{
    if (text.empty())
        return 0.f;
    const float rw = Gw2Ui::MeasureWidth(text.c_str(), 20.f);
    const ImVec2 a(bandP.x + w - 12.f - rw, bandP.y), b(bandP.x + w - 12.f, bandP.y + bandH);
    Gw2Ui::LabelIn(ImVec2(a.x + 1.f, a.y + 2.f), ImVec2(b.x + 1.f, b.y + 2.f), text.c_str(), // dark drop-shadow
                   Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(8, 12, 20, 240), false, nullptr, 20.f, 0.f, 1.4f);
    Gw2Ui::LabelIn(a, b, text.c_str(), // bright caption
                   Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(255, 250, 236, 255), true, nullptr, 20.f, 0.f, 1.4f);
    return rw + 16.f;
}

// The per-character Personal Story detail pane: the biography (sel == -1) or a reconstructed chapter (>= 0).
static void DrawPersonalStoryDetail(App &app, int sel)
{
    (void)app;
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 tint = PsRaceTint();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bandH = 46.f;
    std::string who = g_psRace;
    if (!g_psProfession.empty())
        who += (who.empty() ? "" : " ") + g_psProfession; // "Human Elementalist", shown on every My Story header

    if (sel == -1) // biography
    {
        DrawSeasonHeaderBg(dl, p, ImVec2(p.x + w, p.y + bandH), "My Story", tint);
        const float rw = DrawPsBandRight(p, w, bandH, who);
        Gw2Ui::LabelIn(ImVec2(p.x + 14.f, p.y), ImVec2(p.x + w - 12.f - rw, p.y + bandH), "Biography",
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSelected, true, nullptr, 24.f, 0.f, 1.5f);
        ImGui::Dummy(ImVec2(w, bandH + 12.f));

        // The game's "I'm <name>." headline, then each backstory paragraph spaced out.
        if (!g_psChar.empty() && g_psChar != "default")
        {
            const std::string lead = "I'm " + g_psChar + ".";
            const float lh = Gw2Ui::MeasureWrappedHeight(lead.c_str(), 26.f, w);
            const ImVec2 lp = ImGui::GetCursorScreenPos();
            Gw2Ui::LabelIn(lp, ImVec2(lp.x + w, lp.y + lh), lead.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(255, 240, 200, 255), true, nullptr, 26.f, w);
            ImGui::Dummy(ImVec2(w, lh + 12.f));
        }
        if (g_psBioParas.empty())
            Gw2Ui::Label("No biography found for this character.", IM_COL32(190, 180, 165, 255), false, nullptr, 18.f);
        for (const std::string &para : g_psBioParas)
        {
            const float ph = Gw2Ui::MeasureWrappedHeight(para.c_str(), 20.f, w);
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            Gw2Ui::LabelIn(pp, ImVec2(pp.x + w, pp.y + ph), para.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, IM_COL32(224, 220, 210, 255), false, nullptr, 20.f, w);
            ImGui::Dummy(ImVec2(w, ph + 11.f));
        }
        return;
    }

    if (sel < 0 || sel >= (int)g_psChapters.size())
    {
        CenteredLabel("Select a chapter on the left.", Gw2Ui::kTextSub, 18.f);
        return;
    }
    const PsChapter &ch = g_psChapters[sel];
    DrawSeasonHeaderBg(dl, p, ImVec2(p.x + w, p.y + bandH), "My Story", tint);
    char title[112];
    std::snprintf(title, sizeof(title), "%d. %s", ch.number, ch.name.c_str());
    const float rw = DrawPsBandRight(p, w, bandH, who); // race+class on every header (like the biography), not the arc
    Gw2Ui::LabelIn(ImVec2(p.x + 14.f, p.y), ImVec2(p.x + w - 12.f - rw, p.y + bandH), title,
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSelected, true, nullptr, 24.f, 0.f, 1.5f);
    ImGui::Dummy(ImVec2(w, bandH + 8.f));

    for (const PsStep &s : ch.steps)
    {
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        const float rh = 26.f;
        if (s.isNext)
        {
            if (void *star = Tex::GetTextureFromAssetId(102369))
                dl->AddImage((ImTextureID)star, ImVec2(rp.x + 2.f, rp.y + 3.f), ImVec2(rp.x + 22.f, rp.y + 23.f));
        }
        else if (s.done)
            dl->AddCircleFilled(ImVec2(rp.x + 12.f, rp.y + rh * 0.5f), 4.5f, IM_COL32(120, 210, 130, 255));
        else
            dl->AddCircle(ImVec2(rp.x + 12.f, rp.y + rh * 0.5f), 4.5f, IM_COL32(150, 140, 120, 200), 0, 1.4f);
        // Per-step estimate as a capsule pill: gw2storytimes mission id == the step's GW2 quest id (direct lookup).
        const std::string est = EstimateText(MissionTimeMins(s.questId));
        const float pillFs = 14.f;
        const float pillW = est.empty() ? 0.f : Gw2Ui::PillWidth(est.c_str(), pillFs, 11.f);
        const float pillH = Gw2Ui::PillHeight(pillFs, 3.f, 16.f);
        Gw2Ui::LabelIn(ImVec2(rp.x + 28.f, rp.y), ImVec2(rp.x + w - 10.f - pillW, rp.y + rh), s.name.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                       s.isNext ? Gw2Ui::kGold : (s.done ? IM_COL32(170, 210, 178, 255) : IM_COL32(205, 200, 220, 255)), true, nullptr, 20.f);
        if (!est.empty())
            Gw2Ui::PillAt(dl, ImVec2(rp.x + w - 6.f - pillW, rp.y + (rh - pillH) * 0.5f), est.c_str(), pillFs,
                          Gw2Ui::kPillBorder, Gw2Ui::kPillText, Gw2Ui::kPillFill, 11.f, 3.f, 16.f);
        ImGui::Dummy(ImVec2(w, rh));
        const std::string &txt = s.isNext ? s.goalActive : s.goalDone;
        if (!txt.empty())
        {
            const float th = Gw2Ui::MeasureWrappedHeight(txt.c_str(), 16.f, w - 28.f);
            const ImVec2 tp = ImGui::GetCursorScreenPos();
            Gw2Ui::LabelIn(ImVec2(tp.x + 28.f, tp.y), ImVec2(tp.x + w - 8.f, tp.y + th), txt.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top,
                           IM_COL32(165, 160, 150, 255), false, nullptr, 16.f, w - 28.f);
            ImGui::Dummy(ImVec2(w, th + 3.f));
        }
    }
    Gw2Ui::Divider();
    Gw2Ui::Label("Continue your story in the in-game Story Journal.", IM_COL32(160, 155, 145, 255), false, nullptr, 16.f);
}

static void BuildJournalSeasonStories()
{
    g_jSeasonStories.clear();
    g_jStoryPos.clear();
    for (const Api::V2::StorySeason &s : g_jSeasons)
    {
        std::vector<const Api::V2::Story *> ss;
        for (int sid : s.stories)
        {
            auto it = g_jStories.find(sid);
            if (it != g_jStories.end())
                ss.push_back(&it->second);
        }
        std::sort(ss.begin(), ss.end(), [](const Api::V2::Story *a, const Api::V2::Story *b)
                  { return a->order < b->order; });
        for (int i = 0; i < (int)ss.size(); ++i)
            g_jStoryPos[ss[i]->id] = {s.id, i};
        g_jSeasonStories[s.id] = std::move(ss);
    }
    g_jSeasonStoriesS = g_jStories.size();
    g_jSeasonStoriesT = g_jSeasons.size();
}

static char g_jSearch[64] = ""; // Journal episode-name filter (the fixed search box below the tree)
void DrawJournalContent(App &app)
{
    UpdateCurrentChar(app.state);
    EnsureJournalData(app);       // the tab's data dependency (idempotent); reward metadata + icons warm at login
    EnsurePersonalStoryData(app); // per-character My Story (biography + real chapters); self-gates on the characters scope
    PrefetchJournalRewards(app);  // self-gates on g_jPrefetched; RETRIES (throttled) here if the login warm-up failed
    if (g_jSeasonStoriesS != g_jStories.size() || g_jSeasonStoriesT != g_jSeasons.size())
        BuildJournalSeasonStories();

    if (g_jSeasons.empty() || g_jStories.empty())
    {
        CenteredLabel("Loading Story Journal...", Gw2Ui::kTextSub, 20.f);
        return;
    }

    // No key -> the per-character "My Story" view falls back to the generic placeholder; nudge (dismissible).
    if (!app.api.HasKey())
        ApiReminder::Card(app, "journal", "your character's personal Story (My Story) and its progress", /*gated*/ false);

    const ImVec2 jOrigin = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float fullH = ImGui::GetContentRegionAvail().y;
    // Draggable, persisted tree/detail split (shared Gw2Ui::VSplitter); seeds to the old 0.42 ratio.
    const float jMaxLeft = std::clamp(fullW - 320.f, 200.f, 520.f);
    float &leftW = app.config.PaneW("journal.tree", std::max(220.f, fullW * 0.42f));
    leftW = std::clamp(leftW, 200.f, jMaxLeft);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    // One tree row (episode/chapter): a done dot OR (the suggested-next entry) a green star + name, with a
    // hover/select highlight. Returns true when clicked.
    auto storyRow = [&](const char *name, bool done, bool sel, bool suggested, float estMins = -1.f) -> bool
    {
        const float rh = 31.f;
        ImGui::PushID(name ? name : "");
        const Gw2Ui::RowHotspot row = Gw2Ui::Row("##sr", -1, rh, 0.f, false, sel);
        const ImVec2 rp = row.min;
        const float rw = row.width;
        if (suggested) // the game's green "currently playing" star on what to do next
        {
            if (void *star = Tex::GetTextureFromAssetId(102369))
                dl->AddImage((ImTextureID)star, ImVec2(rp.x + 5.f, rp.y + (rh - 20.f) * 0.5f), ImVec2(rp.x + 25.f, rp.y + (rh - 20.f) * 0.5f + 20.f));
        }
        else if (done)
            dl->AddCircleFilled(ImVec2(rp.x + 16.f, rp.y + rh * 0.5f), 4.5f, IM_COL32(120, 210, 130, 255));
        // Right-aligned completion-time estimate as a capsule pill (gw2storytimes), reserving room so the name
        // ellipsizes before it.
        const std::string est = (estMins > 0.f) ? EstimateText(estMins) : std::string();
        const float pillFs = 14.f;
        const float pillW = est.empty() ? 0.f : Gw2Ui::PillWidth(est.c_str(), pillFs, 11.f);
        const float pillH = Gw2Ui::PillHeight(pillFs, 3.f, 16.f);
        Gw2Ui::RowLabel(dl, row, 30.f, est.empty() ? 8.f : pillW + 8.f, name,
                        Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                        suggested ? Gw2Ui::kGold
                                  : (done ? IM_COL32(150, 200, 160, 255) : (sel ? Gw2Ui::kTextSelected : IM_COL32(210, 205, 225, 255))),
                        true, nullptr, 20.f, 0.f, suggested ? 1.2f : -1.f);
        if (!est.empty())
            Gw2Ui::PillAt(dl, Gw2Ui::RowRight(row, pillW, pillH, 4.f), est.c_str(), pillFs,
                          Gw2Ui::kPillBorder, Gw2Ui::kPillText, Gw2Ui::kPillFill, 11.f, 3.f, 16.f);
        ImGui::PopID();
        return row.clicked;
    };

    // Suggested-next = the first not-done entry in journal order (the game's green "currently playing" star),
    // SKIPPING episodes a later completion in the same season has superseded and releases this account cannot
    // play. Without both, the banner parks forever on the first episode we cannot auto-detect (LWS1 has no
    // completion achievements at all) or on an expansion the player does not own.
    int sugChapter = 0;
    int sugStoryId = -1;
    bool sugFound = false;
    bool sugTracked = false;
    std::string sugSeasonId, sugName;
    const std::vector<std::string> &access = AccountData::Get().access;
    // A pinned release (config.storyTrack, set by the Track button in the episode detail) is scanned FIRST, so
    // the banner follows the story the player says they are on rather than the earliest unfinished one.
    std::vector<const Api::V2::StorySeason *> order;
    for (const Api::V2::StorySeason &s : g_jSeasons)
        if (!app.config.storyTrack.empty() && StoryData::ReleaseForSeason(s.name) == app.config.storyTrack)
            order.push_back(&s);
    const size_t trackedCount = order.size();
    for (const Api::V2::StorySeason &s : g_jSeasons)
        if (trackedCount == 0 || StoryData::ReleaseForSeason(s.name) != app.config.storyTrack)
            order.push_back(&s);

    for (size_t si = 0; si < order.size(); ++si)
    {
        const Api::V2::StorySeason &s = *order[si];
        if (!IsPersonalSeason(s.name) && s.stories.empty())
            continue;
        if (!StoryData::ReleasePlayable(StoryData::ReleaseForSeason(s.name), access))
            continue; // not owned -- do not send the player somewhere they cannot go (fails open when unknown)
        if (IsPersonalSeason(s.name))
        {
            const PsProgress ps = PersonalStoryProgress(app);
            if (ps.ready) // real per-character API progress -> the ACTUAL next chapter (not the "Level 10" placeholder)
            {
                if (!ps.allDone)
                {
                    sugChapter = 1;
                    sugSeasonId = s.id;
                    sugName = ps.nextName.empty() ? std::string("Personal story") : ps.nextName;
                    sugFound = true;
                }
                // allDone -> personal story finished; fall through to the next season's suggestion
            }
            else // no key / not fetched yet -> the manual-checkbox model
                for (int n = 1; n <= 8; ++n)
                    if (!PersonalChapterDone(app, n))
                    {
                        sugChapter = n;
                        sugSeasonId = s.id;
                        sugName = kPersonal[n - 1].name;
                        sugFound = true;
                        break;
                    }
        }
        else if (auto sit = g_jSeasonStories.find(s.id); sit != g_jSeasonStories.end())
        {
            // Start past the last confirmed completion: everything below it is superseded (story is sequential
            // within a season), so an undetectable episode can no longer wall the walk.
            for (int i = SeasonLastDone(app, sit->second) + 1; i < (int)sit->second.size(); ++i)
                if (!JournalDone(app, *sit->second[i]))
                {
                    sugStoryId = sit->second[i]->id;
                    sugSeasonId = s.id;
                    sugName = sit->second[i]->name;
                    sugFound = true;
                    break;
                }
        }
        if (sugFound)
        {
            sugTracked = (si < trackedCount);
            break;
        }
    }

    // Auto-select the suggested-next episode when nothing is selected yet, so opening the Journal shows that
    // episode's detail (+ expands its season) instead of a blank "Select an episode" pane. Once the player
    // picks anything, their selection sticks.
    if (sugFound && g_jSelected < 0 && g_jSelChapter == 0)
    {
        g_jExpanded[sugSeasonId] = true;
        if (sugChapter > 0)
            g_jSelChapter = sugChapter;
        else
            SelectJournalStory(app, sugStoryId);
    }

    // LEFT column: a fixed "Suggested next" banner (the game's "Currently:" header) above the scrolling tree.
    ImGui::BeginGroup();
    {
        const ImVec2 colLeft = ImGui::GetCursorScreenPos();
        const float bh = 48.f;
        const float bw = leftW - 16.f;                               // narrower than the column so it doesn't crowd the detail banner
        const ImVec2 bp(colLeft.x + (leftW - bw) * 0.5f, colLeft.y); // centered within the rail (equal gap each side)
        ImGui::SetCursorScreenPos(bp);
        const bool bclick = ImGui::InvisibleButton("##jsug", ImVec2(bw, bh));
        dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + bh), IM_COL32(22, 26, 24, 195), 4.f);
        dl->AddRect(bp, ImVec2(bp.x + bw, bp.y + bh), IM_COL32(96, 130, 100, 170), 4.f);
        if (sugFound)
        {
            if (void *star = Tex::GetTextureFromAssetId(102369))
                dl->AddImage((ImTextureID)star, ImVec2(bp.x + 9.f, bp.y + (bh - 26.f) * 0.5f), ImVec2(bp.x + 35.f, bp.y + (bh - 26.f) * 0.5f + 26.f));
            Gw2Ui::LabelIn(ImVec2(bp.x, bp.y + 5.f), ImVec2(bp.x + bw, bp.y + 24.f),
                           sugTracked ? "Tracking" : "Suggested next",
                           Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(175, 190, 178, 255), false, nullptr, 16.f);
            Gw2Ui::LabelIn(ImVec2(bp.x, bp.y + 23.f), ImVec2(bp.x + bw, bp.y + bh - 4.f), sugName.c_str(),
                           Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(150, 225, 165, 255), true, nullptr, 20.f, 0.f, 1.3f);
            if (bclick) // jump to it: expand its season + select it
            {
                g_jExpanded[sugSeasonId] = true;
                if (sugChapter > 0)
                {
                    g_jSelChapter = sugChapter;
                    g_jSelected = -1;
                }
                else
                    SelectJournalStory(app, sugStoryId);
            }
        }
        else
            Gw2Ui::LabelIn(ImVec2(bp.x, bp.y), ImVec2(bp.x + bw, bp.y + bh), "All caught up!",
                           Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle, IM_COL32(150, 225, 165, 255), true, nullptr, 20.f);
        ImGui::SetCursorScreenPos(ImVec2(colLeft.x, colLeft.y + bh + 6.f)); // back to the column left for the tree/search below
    }

    // Episode-name search: the fixed box below the tree filters which entries show (and forces matching
    // seasons open). Case-insensitive substring match on the episode / chapter name.
    auto lc = [](std::string s)
    { for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return s; };
    const bool searching = g_jSearch[0] != '\0';
    const std::string q = searching ? lc(g_jSearch) : std::string();
    auto match = [&](const std::string &nm)
    { return !searching || lc(nm).find(q) != std::string::npos; };

    // The collapsible season tree (scrolls under the fixed banner; a fixed search box sits below it).
    ImGui::BeginChild("##jtree", ImVec2(leftW, fullH - 54.f - 36.f), false);
    dl = ImGui::GetWindowDrawList(); // BUGFIX: draw tree content (season-header bgs, triangles, stars, dots)
                                     // to the CHILD's draw list so it clips to the child frame. `dl` was
                                     // captured from the PARENT window, so scrolled headers overflowed the
                                     // child and painted over the fixed "Suggested next" banner above.
    for (const Api::V2::StorySeason &s : g_jSeasons)
    {
        // Skip empty placeholder seasons (the API ships a vestigial "Scarlet's War" with 0 stories). The
        // personal-story season is special-cased to 8 chapters, so it is never skipped.
        if (!IsPersonalSeason(s.name) && s.stories.empty())
            continue;
        if (searching) // hide seasons with no matching episode; a match forces its season open
        {
            bool any = false;
            if (IsPersonalSeason(s.name))
            {
                for (int n = 1; n <= 8 && !any; ++n)
                    any = match(kPersonal[n - 1].name);
            }
            else
            {
                auto sit = g_jSeasonStories.find(s.id);
                if (sit != g_jSeasonStories.end())
                    for (const Api::V2::Story *st : sit->second)
                        if (match(st->name))
                        {
                            any = true;
                            break;
                        }
            }
            if (!any)
                continue;
        }

        bool &expanded = g_jExpanded[s.id];
        const bool open = expanded || searching;
        ImGui::PushID(s.id.c_str());
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        const float hw = ImGui::GetContentRegionAvail().x;
        const float hh = 34.f;
        const bool hc = ImGui::InvisibleButton("##sh", ImVec2(hw, hh));
        const ImU32 tint = IsPersonalSeason(s.name) ? PsRaceTint() : SeasonTint(s.name); // My Story tab = race colour
        DrawSeasonHeaderBg(dl, hp, ImVec2(hp.x + hw, hp.y + hh), s.name, tint);
        const ImVec2 tc(hp.x + 17.f, hp.y + hh * 0.5f);
        Render::DrawGlyph(dl, tc, 17.f, open ? Render::Glyph::CaretDown : Render::Glyph::CaretRight, IM_COL32(235, 230, 215, 255), {false, false, false});
        // Season total estimate, right-aligned (skip My Story -- its total is the sum of ALL race paths, not what
        // one character plays; the per-step estimates in that view cover it instead).
        const std::string seasTot = IsPersonalSeason(s.name) ? std::string() : EstimateText(SeasonTimeMins(s.id));
        const float sFs = 14.f;
        const float seasTotW = seasTot.empty() ? 0.f : Gw2Ui::PillWidth(seasTot.c_str(), sFs, 12.f) + 6.f; // pill + right margin
        const float seasTotH = Gw2Ui::PillHeight(sFs, 3.f, 18.f);
        Gw2Ui::LabelIn(ImVec2(hp.x + 28.f, hp.y), ImVec2(hp.x + hw - 32.f - seasTotW, hp.y + hh), s.name.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, Gw2Ui::kTextSelected, true, nullptr, 22.f, 0.f, 1.4f);
        if (!seasTot.empty())
            Gw2Ui::PillAt(dl, ImVec2(hp.x + hw - seasTotW, hp.y + (hh - seasTotH) * 0.5f), seasTot.c_str(), sFs,
                          Gw2Ui::kPillBorder, Gw2Ui::kPillText, Gw2Ui::kPillFill, 12.f, 3.f, 18.f);
        if (sugFound && !expanded && sugSeasonId == s.id) // collapsed season holds the suggested entry: flag it
            if (void *star = Tex::GetTextureFromAssetId(102369))
                dl->AddImage((ImTextureID)star, ImVec2(hp.x + hw - 28.f - seasTotW, hp.y + (hh - 20.f) * 0.5f), ImVec2(hp.x + hw - 8.f - seasTotW, hp.y + (hh - 20.f) * 0.5f + 20.f));
        if (hc)
            expanded = !expanded;
        ImGui::PopID();

        if (!open)
            continue;

        if (IsPersonalSeason(s.name))
        {
            if (g_psReady && g_psChar == app.state.currentChar)
            {
                // Per-character reconstruction: a Biography row + the character's real chapters/steps.
                if (!searching || match("Biography"))
                {
                    ImGui::PushID(910000);
                    if (storyRow("Biography", false, g_psSel == -1, false))
                    {
                        g_psSel = -1;
                        g_jSelChapter = 0;
                        g_jSelected = -1;
                    }
                    ImGui::PopID();
                }
                for (int i = 0; i < (int)g_psChapters.size(); ++i)
                {
                    const PsChapter &ch = g_psChapters[i];
                    char label[112];
                    std::snprintf(label, sizeof(label), "%d. %s", ch.number, ch.name.c_str());
                    if (searching && !match(label))
                        continue;
                    ImGui::PushID(910001 + i);
                    if (storyRow(label, ch.allDone, g_psSel == i, ch.hasNext))
                    {
                        g_psSel = i;
                        g_jSelChapter = 0;
                        g_jSelected = -1;
                    }
                    ImGui::PopID();
                }
            }
            else
            {
                // Fallback (no characters key / still loading): the generic 8 level-chapters.
                for (int n = 1; n <= 8; ++n)
                {
                    if (searching && !match(kPersonal[n - 1].name))
                        continue;
                    ImGui::PushID(900000 + n); // distinct id space from API story ids
                    if (storyRow(kPersonal[n - 1].name, PersonalChapterDone(app, n), g_jSelChapter == n, sugFound && sugChapter == n))
                    {
                        g_jSelChapter = n;
                        g_jSelected = -1;
                    }
                    ImGui::PopID();
                }
            }
            continue;
        }

        // Stories sorted by `order`, precomputed once (the API ships them unsorted).
        auto sit = g_jSeasonStories.find(s.id);
        if (sit == g_jSeasonStories.end())
            continue;
        for (const Api::V2::Story *st : sit->second)
        {
            if (searching && !match(st->name))
                continue;
            ImGui::PushID(st->id);
            if (storyRow(st->name.c_str(), JournalDone(app, *st), st->id == g_jSelected, sugFound && sugStoryId == st->id, StoryTimeMins(st->id)))
            {
                SelectJournalStory(app, st->id);
                g_psSel = -2;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    Gw2Ui::SearchBox("##jsearch", g_jSearch, sizeof(g_jSearch), leftW - 8.f, "Search episodes..."); // fixed episode-name filter below the tree
    ImGui::EndGroup();

    ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x + 8.f); // a left gutter so the detail pane clears the splitter grab line

    // RIGHT: the selected episode's detail (a personal-story chapter, or an API story).
    ImGui::BeginChild("##jdetail", ImVec2(0.f, fullH), false);
    if (g_psReady && g_psChar == app.state.currentChar && g_psSel >= -1)
        DrawPersonalStoryDetail(app, g_psSel);
    else if (g_jSelChapter > 0)
        DrawPersonalChapterDetail(app, g_jSelChapter);
    else if (auto sel = g_jStories.find(g_jSelected); sel != g_jStories.end())
        DrawJournalDetail(app, sel->second);
    else
        CenteredLabel("Select an episode on the left.", Gw2Ui::kTextSub, 18.f);
    ImGui::EndChild();

    // Splitter handle in the gap between the season tree and the detail pane (drawn last so it doesn't disturb
    // the SameLine flow above).
    if (Gw2Ui::VSplitter("##journal_split", jOrigin.x + leftW + 4.f, jOrigin.y, fullH, &leftW, 200.f, jMaxLeft))
        app.settingsDirty = true;
}

// Login warm: prefetch every Story Journal location map (all crops, so the carousel stays warm), then load
// the journal tree (cache-then-refresh), the reward metadata (its callbacks disk-prefetch the icons), and
// background-fetch every character's My Story. Wraps the formerly-inline WarmCaches body.
void WarmJournal(App &app)
{
    for (const auto &kv : g_journalLocations)
        for (const MapCrop &mc : kv.second.maps)
            PrefetchRectTiles(mc.cont, mc.rect[0], mc.rect[1], mc.rect[2], mc.rect[3]);
    EnsureJournalData(app);
    PrefetchJournalRewards(app);
    PreloadPersonalStory(app);
}

const std::string &JournalRewardDiag() { return g_jRewardDiag; }
