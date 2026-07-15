# Changelog

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
