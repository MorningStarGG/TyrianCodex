#pragma once
#include "ui/Gw2Ui.h"
#include <string>
#include <vector>

class App;
struct Zone;
struct StoryEpisode;

// -----------------------------------------------------------------------------------------------------
// StoryRecommend: the max-level "what's next" model. At level 80 the level-band Recommend list collapses
// (every map is in-band), so the viewer's Recommend mode + the dashboard Recommended-Zones widget switch to
// this story-progression view: a "next story step" detail card on top, then areas grouped by release in
// Journal/release order. Pure reuse of the story spine + completion engine + zone<->release tags; this just
// derives "next episode" + the per-release union grouping and draws the shared detail card. Below 80 nothing
// here is used (the callers keep the existing level-band behavior). See the plan + [[tc-...]] notes.
// -----------------------------------------------------------------------------------------------------
namespace StoryRecommend
{
    // The next unfinished story step: the first release (in StoryData::ReleaseOrder) holding an episode that
    // is not done yet, that episode, and a best-effort entry zone for the Travel action. All pointers reference
    // app-owned data (valid for the frame). release is null when every playable episode is complete.
    // `name`/`description` are what to DRAW: they are owned copies because the personal story's next chapter
    // comes from the live API reconstruction (PersonalStoryProgress), not from an element of app.stories -- in
    // that case `episode` is null while name/release are set.
    struct NextStory
    {
        const std::string*  release   = nullptr;
        const StoryEpisode* episode   = nullptr;   // null for the API-derived personal-story chapter
        const Zone*         entryZone = nullptr;   // null when the release has no bundled open-world zone
        std::string         name;
        std::string         description;
        bool                tracked = false;   // came from the pinned release (config.storyTrack), not the walk
    };
    NextStory Next(App& app);

    // One release group for the union list: completion counts on BOTH axes + the release's bundled zones
    // (sorted by MinLevel,Name). A group is listed while it is incomplete on either axis.
    struct ReleaseGroup
    {
        std::string              release;
        int                      storyDone = 0, storyTotal = 0;
        int                      mapDone   = 0, mapTotal   = 0;
        std::vector<const Zone*> zones;
    };
    // Releases (in ReleaseOrder) incomplete on EITHER axis (story not all done OR a zone not mapped) that have
    // at least one bundled zone. Map-completion grouping is cached behind (char, progress version, zone count);
    // the cheap story counts + union filter recompute each call. Returns a stable reference (no per-frame copy
    // beyond the small filtered vector).
    const std::vector<ReleaseGroup>& Groups(App& app);

    // Next story step: release/episode, description, Journal and Travel actions. The viewer uses a standalone
    // card; the dashboard hosts the same content inline, retaining its gold accent within the widget frame.
    void DrawNextStoryCard(App& app, float w, bool compact,
                          Gw2Ui::SectionPresentation presentation = Gw2Ui::SectionPresentation::Card);

    // A release group header that fits its width: one line ("<Release>   Story d/t - Maps d/t") when it fits,
    // else the release name (wrapped) over a smaller counts sub-line -- so it never clips at half width. Used by
    // both the dashboard widget and the viewer body.
    void DrawReleaseHeader(const ReleaseGroup& g, float fontSize);
}
