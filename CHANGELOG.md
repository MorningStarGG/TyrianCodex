# Changelog

## 1.1.0.4

### Added

- Added **sorting** to the Favorite Waypoints widget, with **Manual**, **A-Z**, **Zone** and **Nearest** modes. Manual is the default and is the order you arrange yourself; the other modes are a view over it, so switching back to Manual restores your arrangement exactly as you left it.
- Added **manual re-ordering** of favorite waypoints, either by dragging a row in the widget or with the up/down arrows in the new Favorite Waypoints list under **Options -> Widgets**. The list there also lets you un-star an entry without going back to the map.
- Added an optional **Group by zone** for favorite waypoints, which puts a small heading above each zone's entries.
- Added an option to **hide favorites the current character has not reached**, for accounts where a long shared list would otherwise show a lot of entries a new character cannot travel to yet. The widget notes how many it is hiding.
- Added an **Input probe** under **Options -> Diagnostics** (off by default) for tracking down typing or click problems. It follows a keystroke from the raw Windows message through to ImGui's text queue, lists every text box on screen with the one holding focus marked, and shows each on-screen panel's input rectangle with a flag when one overlaps the dashboard.

### Changed

- Changed favorite waypoints to be stored **for the whole account** rather than per character, so a waypoint you star is starred everywhere. Existing per-character lists are merged automatically on first run, keeping one copy of each waypoint. Whether a waypoint is unlocked is still judged per character, so entries the character you are playing has not reached are shown dimmed rather than dropped.
- Changed dashboard widgets with a hidden header so that hovering **slides the widget down** to reveal its header in real space, instead of overlaying it on top of the widget's own content. At rest nothing is reserved, so widgets stay exactly as compact as before, and the panel no longer resizes as you sweep across it.

### Fixed

- Fixed dragging a dashboard widget not scrolling the panel when you reached its top or bottom edge, which made it impossible to drop a widget anywhere that was scrolled out of view.
- Fixed a widget's hidden-header controls stealing clicks meant for the widget underneath. The drag grip and the disable button sat on top of whatever the widget drew first, which made the dashboard **Search** box in particular refuse clicks depending on how long you had hovered. A click during the reveal animation can no longer land on those buttons either.
- Fixed keyboard shortcuts firing while you were typing in a text field. Any shortcut bound with **Ctrl** or **Alt** ran its action and swallowed the keystroke, so **Ctrl+A**, **Ctrl+C** and **Ctrl+V** did not work in any of the addon's text boxes for players who use modifier shortcuts.
- Fixed search boxes drawn inside a card overrunning the card's right-hand padding, which pushed the clear button under the card edge.

## 1.1.0.3

### Added

- Added a **favorite** star and a **reminder** bell to every row in the Timers tab. The star pins an event to the top of the list and to a new **Favorites** filter; the bell asks to be told before it starts. Both are saved for the whole account, so an event you care about stays marked on every character.
- Added staged event reminders at **10 minutes, 5 minutes, 1 minute, 30 seconds and 10 seconds** before a belled event starts, each one switchable on its own under **Options -> Notifications -> Event reminders**. 10 minutes and 1 minute are on to begin with.
- Added a **Combine events starting together** option, so a busy slot where several belled events begin at once produces one notification listing them instead of one per event.
- Added a click action to event reminders: clicking the notification sets it as your target and opens the usual Travel / Show target / Not now prompt. A combined reminder opens the Timers tab instead, since there is no single place to send you.
- Added a **Track** button to each Story Journal episode that pins the "next story" suggestion to that release, for players working through the story out of release order. The guide viewer's card shows what it is tracking and offers **Stop tracking**, and the pin releases itself once that release is finished.
- Added a **Hide in WvW** setting to the HUD bar, covering the match maps (Eternal Battlegrounds, the borderlands, Obsidian Sanctum, Edge of the Mists). The WvW Lounge is not affected.
- Added a **Reset position** button to the HUD settings, which returns the bar to its docked edge.

### Changed

- Changed event countdowns to tick in seconds during the last minute instead of sitting on "1m" for a full minute.
- Changed clicking an event in the Timers tab to open the Travel / Show target / Not now prompt as well as pointing the guide arrow at it, so a row click and a reminder click do the same thing. It previously retargeted the arrow silently.
- Changed story completion to be detected per episode instead of per expansion act, so 123 of 143 story episodes now tick themselves off from your achievements where only 27 did before. Heart of Thorns, Path of Fire, End of Dragons, Secrets of the Obscure, Janthir Wilds, and Visions of Eternity previously had no automatic detection at all.
- Changed marking a story episode complete to also mark the earlier episodes of that season, matching how the personal story chapters already behaved.
- Changed the story suggestion to skip releases your account cannot play, so it no longer sends you to an expansion you do not own. It never hides anything when expansion ownership is unknown.
- Changed the HUD bar so it can be dragged by **any** part of it rather than only by the clock, and so its position is stored as a proportion of the screen. The bar now keeps its place after a resolution or monitor change, and picking a different dock edge re-seats it there.

### Fixed

- Fixed the guide viewer's "Next story step" card reading "Chapter 1" and "Story 0/8" for every player regardless of their actual progress. It now follows the real per-character personal story.
- Fixed the Story Journal's "Suggested next" banner parking permanently on an episode that cannot be detected automatically, such as any Living World Season 1 episode, and never advancing past it.
- Fixed story episodes being listed in reverse order within a release, which made several seasons suggest their finale first.
- Fixed Living World Season 1's "The Tower of Nightmares" being missing from the story list entirely.
- Fixed **Travel** and **Show on map** driving the world map into its top-left corner, and suggesting waypoints from the wrong side of the world, for any zone not already visited that session.
- Fixed a cross-zone **Show on map** from the Atlas stalling partway to its destination, often out in open water, instead of reaching the location.
- Fixed **Show on map** for a distant target giving up before it arrived and leaving the world map floating in empty ocean. How far the map is allowed to scroll now depends on how far it has to go, and it tells you when it genuinely cannot reach somewhere instead of stopping silently.
- Fixed four events whose waypoint had been matched to a location on a completely different map, which sent **Travel** somewhere unrelated. Events are now checked against their own map, and one with no trustworthy location falls back to routing you to the zone.
- Fixed the HUD bar becoming unresponsive to dragging after being dragged past a screen edge, where it would ignore movement until the same distance had been dragged back.
- Fixed the HUD bar being strandable off screen with no way to recover it, which could happen by dragging it wide and then hiding the clock.

## 1.1.0.2

### Added

- Added a per-bar Text shadow setting to the Info Panel, with a None / Directional / Drop mode and an adjustable 0-100% strength, so data text stays readable with the background bar dimmed down or turned off entirely. Drop shadow version and opacity slider inspired by Netu.

### Fixed

- Fixed setting sliders being inconsistently sized (some auto-stretched to fill the row, others fixed-width) and able to overflow past the settings panel edge at narrower window widths or higher UI scale.

## 1.1.0.1

### Changed

- Changed the addon's automatic data download to always pull from the latest GitHub release instead of a tag built from the internal data-version number.

### Fixed

- Fixed the addon's automatic data download failing on startup ("Core data download failed", stuck retrying) when a published release didn't match the internal data-version tag it was requesting.

## 1.1.0.0

### Added

- Added route variants for map-completion routing: **On Foot (Tekkit)**, **Mount (Tekkit)**, **On Foot (Lady Elyssa)**, and **With Mounts (Lady Elyssa)**.
- Added route fallback when the preferred route variant is missing on the current map, without changing the user's saved preference.
- Added Lady Elyssa authored waypoint teleports from markers that contain waypoint chat links.
- Segmented Lady Elyssa routes so her trails preserve waypoint gaps instead of being forced into one continuous path.
- Added mount/glider route markers and a bundled universal mount marker icon set for raptor, springer, skimmer, jackal, griffon, skyscale, beetle, warclaw, and glider.
- Added active route diagnostics showing the requested route preference and the actual active route when fallback is used.
- Added shared/global profiles for General, Dashboard, Info Panel, and HUD profile families.
- Added shared/global loadouts that can be used by every character and can reference shared/global family profiles.
- Added **Make global** and **Copy to this character** actions to profile/loadout management.
- Added Tyrian Codex window and guide-panel-only keybinds to **Options -> Keybinds**.
- Added Farming coverage for three Secrets of the Obscure gathering materials: Static Charge (Skywatch Archipelago), Pinch of Stardust (Amnytas), and Calcified Gasp (Inner Nayos).

### Changed

- Replaced the old binary `On Foot` / `Mount` path selector with the new route variant selector.
- Preserved old route settings by migrating old foot/mount values to Tekkit foot/mount preferences.
- Updated route JSON loading to support optional `routeVariants`, `routeMarkers`, and `routeTransitions` while keeping old route JSON compatible.
- Updated route activation so trail points, trail sections, objective order, route markers, and authored waypoint teleports come from the same selected route variant.
- Updated Lady Elyssa route ordering and fallback checks to use objective coverage instead of treating authored waypoint gaps as missing route coverage.
- Updated trail rendering, map trail rendering, and trail following to respect route sections and avoid drawing/walking lines across Lady Elyssa waypoint teleports.
- Updated travel prompts for Lady Elyssa-authored waypoint teleports so they describe the waypoint use clearly and continue using Guild Wars 2's normal teleport confirmation.
- Updated Tekkit and Lady Elyssa marker handling so marker points stay attached to their exact source route branch instead of being copied by map ID or between authors.
- Updated loadouts to store scoped profile references so shared loadouts can point at shared profiles instead of relying on same-name per-character profiles.
- Updated loadout settings, dashboard loadout widget, and Info Panel loadout menus to label shared entries with `[global]`.
- Updated Tyrian Codex keybinds to be handled by Tyrian Codex settings instead of trying to mirror Nexus's saved keybind UI.
- Kept one no-default Nexus QuickAccess bridge bind for tray-icon left-click behavior; user-configurable addon keybinds now live in Tyrian Codex settings.

### Fixed

- Fixed selected Lady Elyssa routes being misclassified as partial because waypoint teleports create intentional trail gaps.
- Fixed trail rendering/localization stopping or reconnecting incorrectly at authored route section boundaries.
- Fixed typed text in shared search, password, text, and slider-number fields using a different scale path than GW2-drawn placeholders under Nexus/GW2 font scaling.

## 1.0.0.3

### Added

- Added support for up to five independent Info Panel bars. Bar 1 keeps the existing migrated setup; Bars 2-5 start disabled and empty.
- Added per-bar Info Panel settings for enabled state, dock edge, width, offsets, opacity, text size, bar height, combat/map hiding, data-text layout, and per-data-text options.
- Added an Info Panel bar-height setting from `18 px` to `64 px`, with `Auto` preserving the existing text-size based height.

### Changed

- Migrated old single-bar Info Panel profile data into Bar 1 and changed new profile saves to write the new `infoBars` shape.
- Updated Info Panel rendering, context menus, API-domain polling, and WvW live polling to scan all enabled bars independently.
- Updated Info Panel height calculation so fixed bar heights use a compact clamp while still avoiding obvious text clipping.

## 1.0.0.2

### Added

- Added Info Panel width, horizontal offset, and edge offset settings so the data bar can be a shorter positioned strip instead of always full-width.

### Changed

- Changed Tyrian Codex source-code licensing from GPLv3 to LGPLv3.
- Updated the Info Panel renderer to use one bounded panel rectangle for background, hit targets, clipping, and left/center/right zone anchors.

## 1.0.0.1

### Added

- Added a global Tyrian Codex UI scale setting saved as `uiScale`, defaulting to `1.00`.
- Added the `Global UI scale` control under General -> Text & Fonts with 5% steps from 75% to 200%.
- Added GW2 UI framework helpers for addon-only scaling: `SetGlobalScale`, `GlobalScale`, `Scaled`, and `Unscaled`.
- Added pixel-sized button/dropdown variants for layouts that already work in screen pixels.
- Added global-scale support to the main Tyrian Codex window, wiki window, dashboard, HUD, info panel, guide viewer, GPS arrow, zone display, notifications, tooltips, and shared controls.
- Added real LiteHTML article content scaling for the in-addon wiki, with scale-aware article cache rebuilding.
- Added SZG5 credit text in the README and About credits.
- Added an AI assistance disclosure to the README.

### Changed

- Made `uiScale` a global setting, not a character/profile setting.
- Updated the render path to apply `uiScale` once per frame before drawing Tyrian Codex UI.
- Updated the GW2 UI text-scale stack so local text scaling now stacks on top of the global UI scale.
- Updated settings rows to respond to scale and available width instead of assuming fixed unscaled control widths.
- Replaced the global-scale slider with a dropdown so changing scale does not push the control off-screen while it is being adjusted.
- Updated shared buttons, action buttons, dropdowns, text boxes, password boxes, search boxes, checkboxes, keybind assigners, color boxes, menus, track bars, and range sliders to use shared scale-aware sizing.
- Updated GW2-style window chrome, title icon, title text, tab rail, close button, resize grip, content offsets, and minimum sizing to honor global scale.
- Updated dashboard panel chrome, drag handle, header strip, profile selector, resize grip, widget cards, widget controls, progress bars, wallet rows, quick controls, and responsive card widths to scale consistently.
- Updated dashboard widgets for scaled spacing and controls, including alt equipment, calculator, characters overview, crafting cart, inventory, loadouts, notes, session earnings, and travel target widgets.
- Updated HUD icon boxes, labels, clock text, panel padding, gaps, and bar height to scale with the global setting.
- Updated info panel bar height, icon sizing, spacing, lock icons, row measurements, and text sizing to scale with the global setting.
- Updated GPS arrow image size, bob offset, objective text, type icon, and spacing so existing arrow size settings remain local adjustments on top of global scale.
- Updated zone display scaling so its local banner scale remains a fine-tune setting on top of global UI scale.
- Updated guide viewer layout so its local viewer scale remains separate while the whole viewer participates in global scale.
- Updated atlas/filter controls, journal/checklist rows, fishing, inventory, recipes, trading post, dungeon, and options tab controls to use scale-aware widths and spacing.
- Updated wiki reader action buttons and tab buttons to use pixel-sized shared controls inside manually positioned wiki layouts.
- Updated the wiki tab strip, search toolbar, search results, library popup, article rail, native infobox rail, and article/rail split to use scale-aware sizing.
- Updated the wiki reader to stack its toolbar and rail/article layout when the scaled controls cannot fit cleanly side by side.
- Updated story recommendation card buttons so they fit scaled card widths.

### Fixed

- Fixed search placeholder text rendering by drawing placeholders through the GW2 UI text path instead of ImGui's raw `InputTextWithHint`.
- Fixed placeholder clipping/alignment in search boxes at non-default global scale and tester-specific font/DPI combinations.
- Fixed dashboard progress bars and row content reaching the right edge incorrectly at high scale.
- Fixed dashboard wallet/session/account text rows overlapping by scaling the shared row height and insets.
- Fixed dashboard widget buttons and controls running past card bounds when the panel is scaled up.
- Fixed Quick Controls sizing so the grid uses the card body width instead of unscaled assumptions.
- Fixed compact guide viewer progress bars being double-scaled.
- Fixed guide viewer action buttons using already-scaled pixel dimensions as logical dimensions.
- Fixed settings controls and keybind rows clipping or drifting when global scale is increased.
- Fixed journal/checklist checkbox and row alignment by moving the affected rows to shared scale-aware sizing.
- Fixed wiki/action buttons that were double-scaled inside manually positioned UI.
- Fixed wiki search bars and toolbar buttons crowding each other at high global scale.
- Fixed wiki infobox rail labels wrapping into narrow fragments at high global scale.
- Fixed wiki article text staying at the old size after global UI scale changes.
- Fixed global scale interactions with local feature scale controls so existing fine-tune settings continue to work on top of global scale.

### Notes

- In-world values remain unchanged: route width in meters, marker world sizes, fade distances, arrival distances, and route localization math are not affected by `uiScale`.
- Map and minimap DPI correction remains separate from global UI scale.
