#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "imgui.h" // ImVec2 / ImGuiID for the reusable row primitive

struct Texture_t; // Nexus texture (Width / Height / Resource) -- for ImageCard

// A small GW2-skinned widget framework for the Nexus addon
//  It composites the GW2 textures we use (window frame dat-156006, the title-bar
// strips, the protruding tab sidebar + window-tab-active overlay, the exit button, the bottom corner)
// Built once here, reused by every window. UI-2 = the window + tab sidebar;
// panels / menu / skinned controls follow.
namespace Gw2Ui
{
    // The GW2 accent/header gold -- ONE token instead of the literal IM_COL32(255,221,130,...) repeated across
    // the UI. Alpha() returns the same RGB at a different opacity (for the faded variants); it is also the one
    // implementation behind the viewer's WithAlpha().
    inline constexpr ImU32 kGold = IM_COL32(255, 221, 130, 255);
    inline ImU32 Alpha(ImU32 col, int a) { return (col & 0x00FFFFFFu) | ((ImU32)(a < 0 ? 0 : (a > 255 ? 255 : a)) << 24); }

    // Semantic text colors -- ONE source instead of the same literals repeated across the UI. kTextSub = the
    // default secondary/subtitle gray; kTextDim = the dimmer idle/placeholder gray; kTextSelected = the bright
    // gold "selected / active / header" emphasis text.
    inline constexpr ImU32 kTextSub = IM_COL32(200, 188, 160, 255);
    inline constexpr ImU32 kTextDim = IM_COL32(150, 142, 124, 255);
    inline constexpr ImU32 kTextSelected = IM_COL32(255, 232, 150, 255);

    // Pill/badge palette -- the small rounded "Lv 20" / "120 m" / status capsules drawn by Pill*(). ONE source
    // so the level/distance badges across the viewer, dashboard, Atlas + zone rows stay identical and recolor in
    // one edit (they had drifted to 105/110 fills, 110/120/130 borders, two text grays). The "Good" trio is the
    // green positive-status variant (the zone-row "Active" pill + the Atlas "Recommended" pill).
    inline constexpr ImU32 kPillFill = IM_COL32(0, 0, 0, 108);
    inline constexpr ImU32 kPillBorder = IM_COL32(160, 132, 76, 118);
    inline constexpr ImU32 kPillText = IM_COL32(208, 196, 164, 255);
    inline constexpr ImU32 kPillGoodFill = IM_COL32(20, 58, 30, 165);
    inline constexpr ImU32 kPillGoodBorder = IM_COL32(120, 220, 130, 178);
    inline constexpr ImU32 kPillGoodText = IM_COL32(160, 235, 170, 255);

    // Status colours -- ONE source for the recurring positive/gain green, negative/loss red, and the gold "chip
    // text" the viewer's distance/ETA chips repeated verbatim in 8 places. These are the EXACT values that were
    // duplicated; off-shade status greens/reds elsewhere are intentional context variants and stay as-is.
    inline constexpr ImU32 kSuccessGreen = IM_COL32(150, 220, 150, 255);
    inline constexpr ImU32 kLossRed = IM_COL32(220, 150, 150, 255);
    inline constexpr ImU32 kChipText = IM_COL32(255, 238, 190, 255);

    struct Tab
    {
        uint32_t icon;
        const char *name;
        int glyph = -1;
        const char *file = nullptr;
    }; // file (a data/ texture path) > glyph (>=0) > icon (gw2dat asset id)

    // A GW2 dye for the ColorBox / ColorPicker. name points into the
    // caller's stable storage; r/g/b is the dye's CLOTH rgb.
    struct Dye
    {
        int id;
        const char *name;
        unsigned char r, g, b;
        int item = 0;
    }; // item = dye-unlock item id (for a chat link)

    // Text alignment within a Label rect
    enum class HAlign
    {
        Left,
        Center,
        Right
    };
    enum class VAlign
    {
        Top,
        Middle,
        Bottom
    };

    // GW2 text: the shared GW2 font with optional StrokeText --
    // the 8-direction black outline drawn so text stays readable on
    // busy backgrounds. `font` null = the shared GW2 UI font. This is the universal text primitive; every
    // control's text and every list-row label routes through it, so font + stroke match the game.
    //   Label   : flows at the cursor like ImGui::Text (left aligned), advancing by the measured size.
    //   LabelIn : draws into an explicit rect [a,b] with H/V alignment (row columns, headers, captions).
    // fontSize: 0 = the GW2 font's native size; otherwise render scaled to that pixel size, so controls
    // can match DefaultFont14 / 16 / 18 exactly (e.g. button text = 14, menu rows = 16).
    // weight: faux-bold strength (the GW2 TTF renders thin, so text is drawn 2-3x sub-pixel offset). -1 =
    // the default body weight; pass a larger value (~1.6) for genuinely BOLD headers,
    // Menomonia-Bold section titles vs its regular body.
    void Label(const char *text, ImU32 color = IM_COL32(255, 255, 255, 255), bool stroke = false,
               ImFont *font = nullptr, float fontSize = 0.f, float weight = -1.f);
    // wrapWidth > 0 wraps the text at that pixel width (the caller sizes its rect/row to fit via
    // MeasureWrappedHeight). 0 = single line (clips at the rect).
    void LabelIn(ImVec2 a, ImVec2 b, const char *text, HAlign h = HAlign::Left, VAlign v = VAlign::Middle,
                 ImU32 color = IM_COL32(255, 255, 255, 255), bool stroke = false, ImFont *font = nullptr,
                 float fontSize = 0.f, float wrapWidth = 0.f, float weight = -1.f);
    // Like LabelIn but draws into an EXPLICIT draw list (e.g. ImGui::GetForegroundDrawList() for HUD overlays
    // such as the notification toasts that live outside any window). Same alignment/stroke/weight semantics.
    void LabelDL(ImDrawList *dl, ImVec2 a, ImVec2 b, const char *text, HAlign h = HAlign::Left, VAlign v = VAlign::Middle,
                 ImU32 color = IM_COL32(255, 255, 255, 255), bool stroke = false, ImFont *font = nullptr,
                 float fontSize = 0.f, float wrapWidth = 0.f, float weight = -1.f);
    // The resolved GW2 UI font (Menomonia when enabled, else Nexus's shared font) and the Nexus stock font, so
    // overlays can pick a face (the toasts' Menomonia/stock setting). Either may be null very early in startup.
    ImFont *UiFontResolved();
    ImFont *StockFont();

    // Global screen-space UI scale. This is set once per frame from Config::uiScale and applies to Tyrian Codex
    // UI only; it does not touch ImGui's shared global font/style state.
    void SetGlobalScale(float s);
    float GlobalScale();
    float Scaled(float px);
    float Unscaled(float px);

    // Text-scale multiplier: every Label/LabelIn/LabelDL fontSize AND MeasureWrappedHeight/MeasureWidth result
    // is multiplied by GlobalScale() * the current local stack value, so a caller can enlarge a region's text
    // uniformly and the measured row heights grow with it. Push/Pop are balanced (a stack); default local 1.0.
    // PushTextScale(1.0f) neutralizes ambient/local scale but still honors the global UI scale.
    void PushTextScale(float s);
    void PopTextScale();
    float TextScale();

    // A consistent GW2-styled hover tooltip (Menomonia, auto-sized -- never clips). Call when an item is
    // hovered. Use this for every icon button so tooltips look + size the same everywhere.
    void Tooltip(const char *text);

    // Rich tooltip builder -- the SAME styling/font as Tooltip() but multi-line. Wrap content between
    // TooltipBegin()/TooltipEnd() (text scale is pinned to 1.0 so a dashboard hover doesn't blow it up), and
    // emit lines with the helpers. So EVERY tooltip (plain or rich: titles, bullets, rules) goes through Gw2Ui:
    //   if (Gw2Ui::TooltipBegin()) { Gw2Ui::TooltipTitle("Collect"); Gw2Ui::TooltipSeparator();
    //                                Gw2Ui::TooltipBullet("Apples"); Gw2Ui::TooltipEnd(); }
    void SetTooltipFontSize(float px); // base tooltip text px (the app pushes the user's setting in); rich lines offset from it
    bool TooltipBegin();               // open the styled tooltip window; always returns true (for the if-guard idiom)
    void TooltipEnd();
    void TooltipTitle(const char *text);                                                                // emphasized header line (gold, faux-bold)
    void TooltipText(const char *text);                                                                 // wrapped body line (default tooltip colour)
    void TooltipMuted(const char *text);                                                                // dimmed line (categories / "not available" notes)
    void TooltipColored(const char *text, ImU32 col);                                                   // a body line in a caller colour (e.g. Free green / Premium purple)
    void TooltipBullet(const char *text);                                                               // "- " bullet + wrapped text
    void TooltipSeparator();                                                                            // a faded rule between sections
    void TooltipHeroImage(const Texture_t *img, float regionW, float maxH);                             // capped/centered card "hero" image
    void TooltipHeroImageFrame(const Texture_t *sheet, float regionW, float maxH, int cols, int frame); // one sprite-sheet frame's UV slice (animated render)

    // A small procedural chrome icon-button (no art asset): one InvisibleButton + a glyph drawn in the GW2
    // accent on hover. ONE primitive for the dashboard's grip / trash / arrows / + / chrome-mode / width
    // toggles (the toggles pick their glyph by kind). Returns true on left-click; `activeAccent` forces the
    // accent (e.g. a grip while dragging); `tip` is an optional hover tooltip. For the GRIP, query
    // ImGui::IsItemActivated()/IsItemClicked(Right) right AFTER the call (the InvisibleButton is the last item)
    // to wire a drag / context menu.
    enum class IconBtn
    {
        Grip,
        Trash,
        ArrowUp,
        ArrowDown,
        Plus,
        Minus,
        Pencil,
        Duplicate,
        ChromeBar,
        ChromeHover,
        WidthFull,
        WidthHalf
    };
    bool SmallIconButton(const char *id, ImVec2 at, float size, IconBtn kind, bool activeAccent = false, const char *tip = nullptr);
    // Draw-only glyph (no input) -- so an inline overlay can submit input first, then paint the glyph on top.
    void SmallIconGlyph(ImDrawList *dl, ImVec2 at, float size, IconBtn kind, bool on);

    // The shared "open guide" book glyph (the icon the viewer action buttons use), drawn into `dl` centred at
    // `c`. Centralized so the viewer + the dashboard header button draw the same icon.
    void GuideGlyph(ImDrawList *dl, ImVec2 c, ImU32 col);

    // Height (px) that `text` needs when wrapped to wrapWidth at fontSize - to size a row/rect before LabelIn.
    float MeasureWrappedHeight(const char *text, float fontSize, float wrapWidth, ImFont *font = nullptr);
    // Pixel width of single-line `text` at fontSize - to reserve a fixed column (e.g. a level badge).
    float MeasureWidth(const char *text, float fontSize, ImFont *font = nullptr);

    // Truncate `text` with a trailing "..." (ASCII -- Menomonia has no real ellipsis glyph) so it fits `maxWidth`
    // at `fontSize` (UTF-8-aware). Returns `text` unchanged if it already fits, or "" if even "..." won't fit.
    // The ONE shared eliding helper for fixed-width name columns / titles (was duplicated per-surface).
    std::string Ellipsize(const std::string &text, float fontSize, float maxWidth, ImFont *font = nullptr);

    // Responsive-toolbar sizing (so chip/button/dropdown rows fit any window width instead of overflowing at the
    // minimum size). FillWidth = the per-control width when `count` equal controls share `availW` with `gap`
    // between them, floored (so sub-pixel rounding never overflows the row) and never below `minW`. The
    // fixedTail overload first reserves `fixedTail` px (a trailing fixed cluster like Asc/Desc + Grid/List) and
    // shares the rest. Callers still draw their own SelectableChip/Button/SearchBox/TextBox at the returned width
    // + SameLine(0, gap); for Dropdown the returned width acts as a MAX cap (it auto-fits + shrinks to fit).
    float FillWidth(float availW, int count, float gap = 6.f, float minW = 0.f);
    float FillWidth(float availW, int count, float fixedTail, float gap, float minW);
    // Advance the cursor so a fixed-width right cluster of width `clusterW` ends at the content's right edge --
    // but ONLY if it fits (avail > clusterW + gap); otherwise leave the cursor put (the caller should have moved
    // the cluster to its own row). The guarded replacement for the buggy `SetCursorPosX(start + max(0, avail-W))`
    // idiom, which stops left-overlap but lets the cluster slide off the RIGHT edge when the window is narrow.
    void SameLineRightCluster(float clusterW, float gap = 6.f);

    // A GW2-style section divider: a thin horizontal rule brightest in the middle and fading to transparent at
    // both ends (the game's subtle section rules). Drawn at the cursor across `width` (0 = content width) and
    // advances the cursor past it (rule + a little breathing room above/below).
    void Divider(float width = 0.f, ImU32 color = IM_COL32(196, 176, 128, 95));

    // A draggable vertical splitter -- the ONE reusable resize primitive for rails AND table columns. Draws a
    // thin grab handle at screen (x, y) spanning `height`, shows the ResizeEW cursor on hover, and on drag
    // adjusts *width by the mouse delta, clamped to [minW, maxW]. Returns true the frame the width changed (the
    // caller persists it + marks settingsDirty). `id` must be unique. A column divider is just a VSplitter on
    // that column's width; keep the sort/click target separate from this handle so a drag never triggers it.
    bool VSplitter(const char *id, float x, float y, float height, float *width, float minW, float maxW);

    // A draggable border BETWEEN two table columns (the column analog of VSplitter). The drag grows *leftW and
    // shrinks *rightW by the same amount so the pair's combined width is preserved -> the table total stays put
    // and the handle follows the cursor. Either pointer may be null when that side is the table's flex/fill
    // column (then only the non-null side moves and the flex column absorbs). Each adjusted side is clamped to
    // its [min,max]. Returns true the frame a width changed. `id` unique; submit AFTER the header sort buttons
    // so this ~8px handle wins the overlap and a resize drag never triggers a sort.
    bool ColumnBorder(const char *id, float x, float y, float height,
                      float *leftW, float minL, float maxL, float *rightW, float minR, float maxR);

    // A centered "empty / no results" placeholder for a list area: a faded procedural magnifier glyph above a
    // larger `title`, with an optional dimmer `hint` line below. Centered both ways in the remaining content
    // region so it reads as a real empty state, not flat text in the corner. Call inside the list's BeginChild
    // (it centers within GetContentRegionAvail and draws via the window draw list -- no cursor advance).
    void EmptyState(const char *title, const char *hint = nullptr);

    // Lightweight HUD primitives used by the guide viewer. These are procedural first, with bundled texture
    // accents where available, so the viewer can evolve without requiring a custom art pack.
    void PanelChrome(ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, ImU32 border, bool ornate);
    // Where the "label + %" text sits relative to the bar. Above (default) = the universal caption-above look
    // (every existing caller); OnBar = overlaid centered on the fill (stroked); Below = the same caption row
    // under the bar; None = bar only, no numbers. Keeping Above the default leaves all current callers identical.
    enum class BarText
    {
        Above,
        OnBar,
        Below,
        None
    };
    // textFs > 0 overrides the caption/percent/on-bar text size (so a caller with a larger header can keep the
    // bar's numbers in proportion instead of the small fixed default). 0 = the legacy 15/13 px caption.
    void ProgressBar(float fraction, const char *label = nullptr, float width = 0.f, float height = 12.f,
                     ImU32 fill = IM_COL32(215, 168, 62, 255), uint32_t iconAssetId = 0,
                     BarText placement = BarText::Above, float textFs = 0.f);

    // ProgressBreakdown: a grouped completion view -- `segs` (e.g. Free, Premium) as equal-width labeled bars on a
    // top row, then a full-width total bar below. Each bar draws "label  have / total" + a right-aligned "P%" above
    // its track. The total bar uses kGold; each segment uses its own `fill`. width 0 = fill the content region.
    struct ProgressSeg
    {
        const char *label;
        int have;
        int total;
        ImU32 fill;
    };
    void ProgressBreakdown(const ProgressSeg *segs, int segCount,
                           const char *totalLabel, int totalHave, int totalTotal, float width = 0.f);
    float Chip(const char *text, ImU32 textCol = IM_COL32(238, 232, 212, 255),
               ImU32 accent = Gw2Ui::kGold, float fontSize = 14.f);
    // A plain rounded capsule badge (NO accent dot -- that's Chip's job): fill + border + centered text, drawn
    // at an explicit screen box via the draw list, because these badges right-align inside rows alongside other
    // content (they don't flow with the cursor like Chip). The capsule sizes ITSELF from the font -- width =
    // text + PillWidth pad, height = PillHeight (fontSize+8, same vertical padding as Chip) -- so every pill,
    // everywhere, has identical breathing room; callers only choose the top-left + colors. radius = height/2.
    // Use PillWidth/PillHeight to right-align / vertically-center before drawing. Both scale with TextScale.
    // padX/padY/minH default to the standard breathing room; a cramped caller (e.g. a half-width banner) may
    // pass tighter values for a smaller, less-padded pill. Width/height stay derived from the font + TextScale.
    float PillWidth(const char *text, float fontSize, float padX = 24.f);
    float PillHeight(float fontSize, float padY = 8.f, float minH = 19.f);
    void PillAt(ImDrawList *dl, ImVec2 topLeft, const char *text, float fontSize,
                ImU32 border = Gw2Ui::kPillBorder, ImU32 textCol = Gw2Ui::kPillText, ImU32 fill = Gw2Ui::kPillFill,
                float padX = 24.f, float padY = 8.f, float minH = 19.f);
    // A small CLICKABLE rounded "done" pill: a green checkmark + optional text, hover-lit, with a tooltip.
    // Height matches Chip() so it sits on a chip row. text=nullptr -> checkmark only. Returns true on click.
    bool CheckPill(const char *id, const char *text, float fontSize = 14.f, const char *tip = "Mark complete");
    // The ONE gold-selected toggle "chip": rounded frame with idle/hover/selected states + optional tooltip,
    // showing EITHER centered text (label) OR an icon. Used by the Atlas filter chips, the viewer objective-
    // type filter, and the compact [Objectives|Events] tabs. Returns true on click. The icon variant passes
    // drawIcon (so the framework stays free of the viewer's objective-type icon layer): the chip computes the
    // centered square and calls back to paint it. label=nullptr + no drawIcon = an empty chip.
    bool SelectableChip(const char *id, bool selected, ImVec2 size, const char *label, float fontSize,
                        const char *tooltip = nullptr,
                        const std::function<void(ImDrawList *, ImVec2 center, float iconSize)> &drawIcon = {});

    // A RADIO ROW of SelectableChips bound to *selected (the chosen index) -- the shared "scope / view / filter
    // chip bar" used across the tabs (Collections/Items/Sessions/Homestead/Dungeons/Fishing/Wardrobe/Inventory),
    // folding the hand-rolled FillWidth+loop. Each ChipItem may carry a tooltip, an icon painter, and a fixed
    // width; width==0 chips SHARE the remaining row equally (FillWidth), so a plain text row is just an array of
    // {label}. Returns true the frame *selected changed. (On/off switches use ChipToggle, not this.)
    struct ChipItem
    {
        const char *label = nullptr;                                               // centered text (null + drawIcon = an icon-only chip)
        const char *tooltip = nullptr;                                             // optional hover tooltip
        std::function<void(ImDrawList *, ImVec2 center, float iconSize)> drawIcon; // optional icon painter
        float width = 0.f;                                                         // 0 = share the row equally (ChipRow) / measure the label (ChipFlow); > 0 = fixed
        bool disabled = false;                                                     // drawn but inert (a click never selects it) -- e.g. an unavailable option + a reason tooltip
    };
    bool ChipRow(const char *id, const ChipItem *items, int count, int *selected,
                 float height = 32.f, float fontSize = 18.f, float gap = 6.f, float minShareW = 56.f,
                 bool wrap = false, int minPerRow = 0, int maxPerRow = 0);
    // wrap: fit ~minShareW-wide chips per row (equal-share), reflow to more rows; minPerRow/maxPerRow
    // clamp the per-row count (0 = unbounded) -- e.g. (3,5) keeps a 15-chip bar at 3..5 per row
    // A single on/off chip bound to *on (a switch, not a radio member). Returns true on click (caller flips).
    bool ChipToggle(const char *id, const char *label, bool on, ImVec2 size, float fontSize, const char *tooltip = nullptr);

    // FLOW chip rows -- each chip sized to its CONTENT (ChipItem.width, or the measured label width if 0; an
    // icon-only chip is `height`-square), laid left-to-right and WRAPPED to a new row on overflow. For the
    // variable/content-width chip bars (Atlas kind + objective chips, Farming category + material chips) that the
    // equal-share ChipRow can't reproduce. ChipFlow is RADIO (*selected index; the caller maps a non-contiguous /
    // compound selection to/from an index). ChipFlowMulti is MULTI-SELECT -- each chip independently on/off via
    // isOn(i)/onToggle(i) (e.g. the Farming material chips). Both return true the frame a chip was clicked.
    bool ChipFlow(const char *id, const ChipItem *items, int count, int *selected,
                  float height = 30.f, float fontSize = 18.f, float gap = 4.f);
    bool ChipFlowMulti(const char *id, const ChipItem *items, int count,
                       const std::function<bool(int)> &isOn, const std::function<void(int)> &onToggle,
                       float height = 30.f, float fontSize = 18.f, float gap = 4.f);
    // banded = the default shaded gradient strip behind the title (the section look used in the viewer /
    // Diagnostics). Pass banded=false for a lighter "title + divider only, no fill" header -- use it when a
    // section sits directly under another banded header (e.g. a panel title) so the two bands don't stack.
    void SectionHeader(const char *title, const char *rightText = nullptr, float fontSize = 16.f,
                       ImU32 titleCol = Gw2Ui::kGold, bool banded = true);
    void TypeMedallion(uint32_t iconAssetId, ImU32 accent, float size);

    // Custom GW2 UI font: the real Menomonia TTF (loaded by the host via Nexus Fonts_AddFromFile). When set,
    // every label here renders with it instead of Nexus's generic shared font, matching the game.
    // Pass (nullptr, nullptr) to revert to the Nexus font (the in-game "Use GW2 font" toggle). Gw2Italic()
    // returns the italic face (or null) for labels that want it (e.g. the Story tab's italic sub-headers);
    // a null italic just falls back to the regular face.
    //
    // CRISPNESS: ImGui v1.80 has no dynamic rasterizer -- drawing a font at a px != its baked size BILINEARLY
    // STRETCHES the baked glyph bitmaps, which shimmers/aliases at non-clean ratios. The fix is to bake the
    // SAME TTF at several native sizes (a "ladder") and, per text run, pick the bake NEAREST the requested px
    // so the residual scale stays ~1.0 (crisp). SetGw2FontLadder registers that ladder; the text core picks
    // the nearest rung for BOTH measure and draw. SetGw2Font(r,i) is the single-rung shorthand (back-compat /
    // the (nullptr,nullptr) revert). One ladder rung MUST be 24px (the default-size + Zone-Display anchor).
    struct FontBake
    {
        float px = 0.f;
        ImFont *regular = nullptr;
        ImFont *italic = nullptr;
    };
    void SetGw2Font(ImFont *regular, ImFont *italic);
    void SetGw2FontLadder(const FontBake *bakes, int count); // bakes need not be sorted; we sort ascending
    ImFont *Gw2Italic();
    int FontRevision();

    // Begin a GW2-framed tabbed window: draws frame + title bar + left tab sidebar + exit button, applies
    // the GW2 theme, then opens a child for the active tab's content. Returns true when open - the caller
    // then renders the active tab's content widgets and calls EndWindow(). Sets *open=false on close and
    // updates *activeTab when a tab is clicked.
    // ioW/ioH (optional): when supplied, the window is USER-RESIZABLE (aspect-locked from the current size up
    // to 200%, clamped to the monitor work area) and the live size is written back through them each frame so
    // the caller can persist it. When null, the window stays the fixed default size (the legacy behaviour).
    // restoreSize: force the window to *ioW/*ioH THIS frame (pass true on the open transition). When false the
    // window keeps its live size so a drag-resize works -- without this latch ImGui auto-fits to content on a
    // hot-reload (Cond_Appearing does not fire) and balloons to the max.
    bool BeginWindow(const char *id, bool *open, const char *title, const char *subtitle,
                     const Tab *tabs, int tabCount, int *activeTab,
                     float *ioW = nullptr, float *ioH = nullptr, bool restoreSize = false);
    void EndWindow();

    // Escape-to-close plumbing. A Gw2Ui window can't just read Escape itself: the press must also be CONSUMED
    // so it doesn't reach the game (Escape menu) or Nexus (closing its own window). So the Nexus WndProc hook
    // (entry.cpp) consults these: while a Gw2Ui window is focused, WindowEatsEscape() is true and the WndProc
    // consumes Escape + calls RequestEscapeClose(); BeginWindow actions the close. Call NewFrameEscReset() once
    // per frame (before any window draws) so the "focused" flag is fresh.
    void NewFrameEscReset();
    bool WindowEatsEscape();
    bool KeybindCaptureActive();
    void RequestEscapeClose();

    // A GW2 bordered panel: header bar (1032325) + title, a corner-accent flourish (1002144), a subtle dark tint,
    // then a content child inset size: 0 on an axis = fill the available space. Call EndPanel() after the content.
    bool BeginPanel(const char *idAndTitle, float width, float height, const char *title);
    void EndPanel();

    // A GW2 bordered "card" that AUTO-SIZES to its content (no height math): a rounded, tinted, bordered box
    // with the content inset by padding. Lighter than BeginPanel (no textured title bar) - for list cards like
    // the Dungeons tab's per-dungeon panels. `width` 0 = fill the available width; `bg`/`border` 0 = the
    // default dark card theme (pass a colour to highlight a card, e.g. the dungeon you're currently in). Draw
    // widgets between BeginCard/EndCard (wrap long text yourself via LabelIn - the card spans `width`, content
    // is left-aligned). Do NOT nest cards (one draw-list channel split). EndCard leaves the cursor below it.
    bool BeginCard(const char *id, float width = 0.f, ImU32 bg = 0, ImU32 border = 0);
    bool BeginAccentCard(const char *id, float width = 0.f, ImU32 accent = 0, ImU32 bg = 0, ImU32 border = 0);
    void EndCard();
    // Inner content width of the CURRENT card (card width minus padding + accent inset). Use this for any
    // FULL-WIDTH content inside a card: a card is a group, so GetContentRegionAvail() over-extends to the
    // window edge and would overrun the card's right border (and ignore its auto right margin).
    float CardInnerWidth();
    // Universal width resolver for row/layout helpers. Pass an explicit width for dashboard/table/custom-frame
    // contexts. Pass 0 inside normal content/cards: inside a card this returns CardInnerWidth(), otherwise the
    // current content region width. This keeps scrollbar/card-padding policy centralized in BeginCard.
    float ContentWidth(float explicitWidth = 0.f);

    // ---- Image cards + grids (the Content tab's fractal / strike / dungeon cards): a framed card with an IMAGE on
    // top (a map render / portrait / loading screen, or a dark placeholder when null), a centered WRAPPING name, an
    // optional caption, and an optional custom BODY drawn before the bottom (e.g. a path dropdown). Built on BeginCard.
    // FiligreeHeading is the centered gold section title (with the Zone-Display filigree rule) above a grid. CardGrid
    // lays `count` cards out in a responsive wrapping grid (columns from targetW + gap), calling drawCell(i, cardW).
    // ImageCard returns true when the card is hovered -- attach a right-click copy menu via the return value. ----
    void FiligreeHeading(const char *title, float width = 0.f);
    bool ImageCard(const char *id, const Texture_t *tex, const char *name, const char *caption = nullptr,
                   float cardW = 0.f, const std::function<void(float)> &body = {}, ImU32 bg = 0, ImU32 border = 0);
    void CardGrid(int count, float targetW, float gap, const std::function<void(int, float)> &drawCell);

    // IconGrid is the VIRTUALIZED square-cell gallery layout (cols from the available width + an ImGuiListClipper)
    // behind the wardrobe/homestead galleries. For each VISIBLE cell it calls drawCell(index, cellMin, cellSize); the
    // callback owns the cell content + an InvisibleButton(cellSize) for hover/click (PushID a STABLE id, not the
    // index). Call inside a BeginChild. Returns the column count used.
    int IconGrid(int count, float cell, float gap, const std::function<void(int idx, ImVec2 cellMin, float cellSize)> &drawCell);

    // ---- Stat tiles: a responsive grid (1/2/3 columns by width) of bordered rounded tiles -- an optional icon, a
    // small dim label, a big bold value, and a colored left accent. The standard "not flat" metric layout used
    // across the data tabs (Sessions, Instances). Reserves its own height; draw inside a card. `icon` is a free
    // function so callers draw a glyph or a real texture without Gw2Ui taking those dependencies.
    using TileIcon = std::function<void(ImDrawList *dl, ImVec2 center, float px)>;
    struct Stat
    {
        std::string label;
        std::string value;
        ImU32 col = kTextSelected;
        ImU32 accent = kGold;
        TileIcon icon;
    };
    // The ONE shared stat-tile grid (Sessions / Instances / Fishing Collections all use this). Renders at a FIXED
    // size pinned to text-scale 1.0, so it looks identical regardless of the caller's ambient PushTextScale. Long
    // labels wrap to ~2 lines; the label+value block is vertically centred. 1/2/3 columns by width.
    void StatGrid(float width, const std::vector<Stat> &tiles, float tileH = 76.f);

    // --- Time-series LINE chart (price history + any future chart) -------------------------------------
    // A reusable multi-series line chart. Reserves `size` at the cursor (size.x<=0 => content-avail; size.y<=0 =>
    // a default height). All series share one auto-fit axis (min..max of every point, +pad); each point is (x, y)
    // -- x any monotonic key (e.g. a day number), y the value. `fmtY` formats a y value for the left-gutter axis
    // labels (e.g. coins). On hover it draws a vertical crosshair at the nearest data x + a dot per series and
    // calls `onHover(nearestX, atScreen)` so the caller builds its own tooltip from its own data. Draws nothing
    // with < 2 points total (the caller shows its own empty/loading state).
    struct ChartSeries
    {
        const ImVec2 *points = nullptr;
        int count = 0;
        ImU32 color = IM_COL32_WHITE;
        const char *name = nullptr;
    };
    void LineChart(const char *id, ImVec2 size, const ChartSeries *series, int seriesCount,
                   const std::function<std::string(float)> &fmtY,
                   const std::function<void(float nearestX, ImVec2 atScreen)> &onHover = {});

    // --- Reorder-list rows (shared by the HUD buttons + Info Panel data-texts settings) ----------------
    // One row of a multi-group reorder list: a striped background, an up / down / disable button cluster on
    // the right, an optional L/C/R-style group toggle, and an optional left glyph + label (+ a dimmed right
    // note). Draw inside a card at [x, x+width], `height` tall; `id` must be unique per row. Returns the
    // action the user clicked this frame -- the CALLER applies the one mutation (swap / move group / hide).
    struct ReorderRowDesc
    {
        const char *label = "";
        const char *note = nullptr;           // dimmed right note (e.g. "needs API key"); null = none
        int glyph = -1;                       // (int)Render::Glyph for a left icon, or -1 for none
        int group = 0;                        // current group (its toggle letter is highlighted)
        const char *const *letters = nullptr; // group-toggle letters, e.g. {"L","R"} / {"L","C","R"}
        int groupCount = 0;                   // size of `letters` (0 = no letter-toggle column)
        bool canUp = false, canDown = false;
        float fontSize = 16.f;                // label size (the note draws at fontSize - 2)
        IconBtn disableIcon = IconBtn::Minus; // the disable button's icon (Dashboard uses Trash)
        const char *disableTip = nullptr;
        bool border = false; // draw a 1px cell border (Dashboard cells)
        // Up to two trailing icon toggles drawn just left of the up button (Dashboard: chrome + width), in
        // place of the letter group. extra[0] is rightmost. Clicking one returns Extra0 / Extra1.
        struct Extra
        {
            IconBtn icon = IconBtn::Minus;
            bool show = false;
            bool on = false;
            const char *tip = nullptr;
        };
        Extra extra[2];
    };
    struct ReorderResult
    {
        enum Act
        {
            None,
            Up,
            Down,
            Disable,
            SetGroup,
            Extra0,
            Extra1
        };
        Act act = None;
        int toGroup = -1;
    };
    // `rowTop` < 0 (default) = own-row mode (reserves a row + advances the cursor). >= 0 = cell mode: draws at
    // that rowTop and does NOT advance, so the caller can place two half-cells on one row (Dashboard pairing).
    ReorderResult ReorderRow(const char *id, float x, float width, float height, const ReorderRowDesc &d, float rowTop = -1.f);
    // A "+ enable" row for the disabled list: [+] [glyph] label. Returns true the frame + is clicked.
    bool EnableRow(const char *id, float x, float width, float height, const char *label, int glyph = -1, float fontSize = 16.f);

    // GW2-skinned controls. Drop-in for the ImGui equivalents.
    // Each returns true when the value changed this frame.
    bool Checkbox(const char *label, bool *v);
    bool Slider(const char *label, float *v, float vmin, float vmax, const char *fmt = "%.0f");
    // trackWidth > 0 caps the slider track to that pixel width (else it fills the row); use it where the slider
    // shouldn't stretch the full content region (e.g. the Info Panel per-text options page).
    bool SliderInt(const char *label, int *v, int vmin, int vmax, float trackWidth = 0.f);
    // A two-handle integer RANGE slider (drag either end). Shows "Lv lo - hi" + the track with both nubs and the
    // selected band lit. Clamps lo<=hi within [vmin,vmax]. Returns true on change. Same track/nub art as SliderInt.
    bool RangeSliderInt(const char *id, int *lo, int *hi, int vmin, int vmax);

    // Tyrian Codex action button: the shared non-settings button frame. Use ActionButton() for plain text
    // actions; use ActionButtonFrame() when a caller needs to draw custom contents such as icons inside the
    // same hover/border/fill treatment. The normal variants take logical design units and apply GlobalScale().
    // Use the *Px variants when the caller already has a screen-pixel width/height from GetContentRegionAvail(),
    // FillWidth(), split columns, or dashboard/widget body layout. That avoids double-scaling responsive rows.
    enum class ActionButtonVariant
    {
        Normal,
        Primary,
        Danger
    };
    struct ActionButtonResult
    {
        bool clicked = false;
        bool hovered = false;
        bool held = false;
        ImVec2 min = ImVec2(0.f, 0.f);
        ImVec2 max = ImVec2(0.f, 0.f);
    };
    ActionButtonResult ActionButtonFrame(const char *id, ImVec2 size, ActionButtonVariant variant = ActionButtonVariant::Normal,
                                         bool disabled = false, const char *tooltip = nullptr);
    ActionButtonResult ActionButtonFramePx(const char *id, ImVec2 sizePx, ActionButtonVariant variant = ActionButtonVariant::Normal,
                                           bool disabled = false, const char *tooltip = nullptr);
    bool ActionButton(const char *label, float width = 128.f, float height = 26.f,
                      ActionButtonVariant variant = ActionButtonVariant::Normal,
                      const char *tooltip = nullptr, bool disabled = false);
    bool ActionButtonPx(const char *label, float widthPx, float heightPx,
                        ActionButtonVariant variant = ActionButtonVariant::Normal,
                        const char *tooltip = nullptr, bool disabled = false);

    // Legacy/settings GW2 StandardButton: the 9-frame button-states atlas (hover animates frame 0->8
    // over 0.25s) inside the 4-edge button-border, with black centered text. Returns true when clicked.
    bool Button(const char *label, float width = 128.f, float height = 26.f);
    bool ButtonPx(const char *label, float widthPx, float heightPx);

    // GW2 TextBox: the GW2 input-box texture skinning an ImGui::InputText (which supplies the cursor /
    // selection / keyboard) in the GW2 font. Returns true while the text changes. `buf` is edited in place.
    bool TextBox(const char *id, char *buf, size_t bufSize, float width);
    // The height of a TextBox / SearchBox (font size + padding) -- so a Dropdown / button on the same row can match.
    float InputBoxHeight();
    // A TextBox for SECRETS (e.g. the API key): masks the text as password dots unless *revealed, with an eye
    // toggle in a right-hand column that flips *revealed. Default-hidden so a key never shows on stream.
    bool TextBoxSecret(const char *id, char *buf, size_t bufSize, float width, bool *revealed);
    // A TextBox for FILTER fields: placeholder/hint text + a magnifier (empty) / clickable X (clears) icon.
    // The one reusable search widget - use it for every search/filter box. Returns true when the text changed.
    bool SearchBox(const char *id, char *buf, size_t bufSize, float width, const char *hint = "Search...");

    // GW2 Dropdown: the input-box box + dd-arrow chevron + selected text, and a popup list with
    // solid-black background, a dark-brown (45,37,25) fill on the hovered row, and
    // Chardonnay highlight text (NOT the menu scrolling-highlight
    // `items` is itemCount C-strings; *selected is the chosen index. Returns true when the selection changes.
    // Optional section dividers: before the item at sectionAt[k], a thin rule + dim sectionLabel[k] header is drawn
    // (purely visual -- *selected still indexes `items` 1:1). sectionCount 0 = a flat list as before.
    bool Dropdown(const char *id, const char *const *items, int itemCount, int *selected, float width = 250.f,
                  const int *sectionAt = nullptr, const char *const *sectionLabel = nullptr, int sectionCount = 0,
                  float height = 27.f); // height: the box height, raise it to line up with a SearchBox row
    bool DropdownPx(const char *id, const char *const *items, int itemCount, int *selected, float widthPx,
                    const int *sectionAt = nullptr, const char *const *sectionLabel = nullptr, int sectionCount = 0,
                    float heightPx = 0.f);

    // GW2 KeybindingAssigner: a name panel + a hotkey panel + a Clear panel. Click the hotkey region to
    // capture a new combo (Esc cancels). Clear writes "(null)" internally and displays it as "Not set".
    // Returns true when the bind changes; the new combo string is written to bindBuf.
    bool KeybindAssigner(const char *label, char *bindBuf, size_t bufSize, float width = 380.f, float nameWidth = 220.f);

    // GW2 ColorBox: a dye swatch (cp-clr-dc tinted by *rgb, 0xRRGGBB) that opens the GW2 dye
    // palette -- a scrollable grid of 24px swatches (cp-clr-v1..v4 + hover/active),
    // Click a dye to pick it. `dyes` is the /v2/colors palette. Returns true when *rgb changes.
    bool ColorBox(const char *id, unsigned int *rgb, const Dye *dyes, int dyeCount, float size = 32.f);

    // GW2 ContextMenuStrip: a right-click popup -- a dark (33,32,33) panel + scrollbar-track left
    // edge, with rows that reuse the scrolling highlight (RowBackground) + a bullet + shadowed text.
    // Open it with ImGui::OpenPopup(id) (e.g. on a right-click), then call this. Returns the clicked item
    // index this frame, else -1.
    int ContextMenu(const char *id, const char *const *items, int itemCount);

    // A GW2-styled context menu WITH submenus (same panel/row look as ContextMenu). Build a tree of MenuNode:
    // a leaf carries an `id` returned when clicked; a node with `children` is a submenu (its `id` ignored); a
    // `separator` node draws a faint divider. Call EVERY frame; pass openNow=true the frame the trigger fired
    // (it OpenPopups for you). Returns the clicked leaf id, or -1.
    struct MenuNode
    {
        std::string label;
        int id = -1; // returned when this LEAF is clicked
        bool selected = false;
        bool separator = false;         // a divider row (label/id ignored)
        std::vector<MenuNode> children; // non-empty => submenu
    };
    int ContextMenuTree(const char *id, const std::vector<MenuNode> &nodes, bool openNow);

    // The GW2 list-row background -- alternating striping (the 156044 fade at Color.Black*0.7
    // over even rows) + the scrolling highlight (the real menuitem.fx shader; roller animates 0->1 over
    // 0.5s on hover, =1 when selected). The universal building block for ANY row: call it for a row of
    // bounds [a,b], then draw your own icons / labels / columns on top. `id` keys the per-row highlight
    // animation (e.g. ImGui::GetID(key)); rowIndex < 0 disables striping. MenuItem is built on this, and
    // the story / zone / checklist rows use it too -- so every row matches the game by construction.
    void RowBackground(ImVec2 a, ImVec2 b, bool hovered, bool selected, ImGuiID id, int rowIndex = -1);

    // A GW2 menu/list row: RowBackground + a left-padded label. Returns true when clicked. Pass the
    // row's index for alternating dark striping (even rows); rowIndex < 0 = no striping.
    bool MenuItem(const char *label, bool selected, float height = 32.f, int rowIndex = -1);

    // --- Shared list-row CHROME (content-agnostic) ---
    // The hit-area + stripe + hover-highlight every result/list row needs, in ONE place so the hover-roll key
    // is always row-unique (it keys off the strId in the CURRENT id scope -- the caller MUST already be inside
    // a per-row PushID so sibling rows don't collide). Returns the row bounds + click/hover; the caller paints
    // its own content into [min .. min+(width,height)] via the draw list. width <= 0 => ContentWidth(), so rows
    // are card-aware by default. Pass an explicit width for tables/dashboard widgets/custom frames.
    // Set allowOverlap when the caller layers its own hotspots (copy / favorite) on top of the row button.
    // Both ItemUI::DrawResultRow (items) and Search::DrawResultRow (locations) build on this.
    struct RowHotspot
    {
        ImVec2 min;
        float width = 0.f, height = 0.f;
        bool clicked = false, hovered = false;
    };
    RowHotspot Row(const char *strId, int rowIndex, float height, float width = 0.f, bool allowOverlap = false, bool selected = false);
    // Draw clipped text inside a Row() result. leftOffset and rightInset are row-relative, so callers don't need
    // to repeat p.x + width - ... arithmetic; RowLabel clips the text to the resolved bounds.
    void RowLabel(ImDrawList *dl, const RowHotspot &row, float leftOffset, float rightInset, const char *text,
                  HAlign h = HAlign::Left, VAlign v = VAlign::Middle,
                  ImU32 color = IM_COL32(255, 255, 255, 255), bool stroke = false,
                  ImFont *font = nullptr, float fontSize = 0.f, float wrapWidth = 0.f, float weight = -1.f);

    // A reusable sortable + resizable DATA TABLE -- the shared shell behind the All-Items + Flip-Finder tables:
    // a flex LEFT column (cols[0], widthStore null) that fills, fixed persisted columns dragged via ColumnBorder,
    // a sortable header row (^/v indicator + hover), the bottom rule, and a clipper body of Row()s. The caller
    // OWNS its sort state (onSort flips/sets it + re-sorts its rows) and renders each row's cells in drawRow from
    // the per-column rects the table computed -- so any cell content (icons, coins, colored stats, tooltips) and
    // any per-row PushID stay the caller's. Returns true if a column width changed this frame (-> settingsDirty).
    struct TableColumn
    {
        const char *header = "";
        float *widthStore = nullptr; // persisted width px (Config::PaneW); NULL = the FLEX column (must be cols[0])
        float minW = 56.f, maxW = 460.f;
        HAlign align = HAlign::Left;
        int sortId = -1; // < 0 = not sortable (no header button)
    };
    struct TableRowCols
    {
        const float *x;
        const float *w;
        int count;
        float availW;
    }; // per-column row-relative x + width
    struct DataTableDesc
    {
        const char *id = "##dt";
        const TableColumn *cols = nullptr;
        int colCount = 0;
        float availW = 0.f; // content width (caller passes GetContentRegionAvail().x / ContentWidth())
        int rowCount = 0;
        float headerH = 31.f, rowH = 40.f, headerFs = 16.f;
        float leftPad = 0.f;      // gutter before the first column (e.g. a row-icon); flex width accounts for it
        float flexMin = 120.f;    // min width of the flex column
        float rightReserve = 0.f; // px kept clear on the right (the rows-child scrollbar)
        bool bodyChild = true;    // wrap rows in their own BeginChild (own scrollbar); false = already in a child
        int activeSort = -1;      // the caller's current sort id (drives the ^/v indicator)
        bool sortAsc = true;
        std::function<void(int sortId)> onSort; // header click -> caller toggles/sets its sort + re-sorts
        std::function<void(int rowIndex, const RowHotspot &row, const TableRowCols &cols)> drawRow;
    };
    bool DataTable(const DataTableDesc &d);

    // Return the top-left for a right-aligned row element. If itemHeight > 0 it is vertically centered in row.
    ImVec2 RowRight(const RowHotspot &row, float itemWidth, float itemHeight = 0.f, float rightInset = 0.f);
    // Common two-column label/value row. Uses ContentWidth(), scales the logical row height/insets with
    // TextScale(), reserves the measured value width, clips the label, and right-aligns the value to the same
    // row edge in every card/dashboard context.
    void TextValueRow(const char *label, const char *value, float width = 0.f, float height = 18.f,
                      float fontSize = 14.f,
                      ImU32 labelColor = Gw2Ui::kTextSub,
                      ImU32 valueColor = IM_COL32(255, 230, 170, 255),
                      bool labelStroke = false, bool valueStroke = false,
                      float rightInset = 0.f, float valuePad = 8.f);

    // --- Immediate-mode TREE (reusable; the Checklist tab + any future tree build on it) ---
    // One tree row is structural chrome only: the caller PAINTS its own content (checkbox/labels/icons) into
    // the returned content area and, on a click, dispatches by `clickX` to its own hit regions (the GW2 tree
    // mixes a row-level expand toggle with in-row checkbox/lock hits, so a single row click is region-routed
    // rather than using nested widgets). NodeTree, flattened for immediate mode -
    // expand state lives in the caller (one bool per node), and the tree is rebuilt from data each frame.
    struct TreeRowResult
    {
        ImVec2 rowMin, rowMax; // full row bounds
        ImVec2 contentMin;     // x where caller content begins (past the indent + arrow column)
        bool clicked = false;  // row clicked this frame
        float clickX = -1.f;   // mouse x relative to rowMin.x at the click (route to checkbox/lock/toggle); -1 if none
        bool hovered = false;
    };
    // Reserve + paint a tree row at the cursor: indented by `depth`, an expand chevron when `arrowState` >= 0
    // (0 = collapsed, 1 = expanded; -1 = leaf, no arrow), the GW2 row highlight (rowIndex stripes + selected
    // glow), and a full-row click. Advances the cursor past the row.
    TreeRowResult TreeRow(const char *id, int depth, float height, int arrowState, bool selected, int rowIndex);

    // Paint a GW2 checkbox sprite at [topLeft, +size] - PAINT ONLY (the owning row handles the click). `state`:
    // 0 = empty, 1 = checked, 2 = partial (some-but-not-all descendants done -> a gold dash). For group
    // "check-all" boxes + tree leaves whose click is routed by the row.
    void PaintCheckbox(ImVec2 topLeft, float size, int state, bool hovered = false);

    // Pre-load a bundled (local-first) gw2dat UI asset by id at addon start, so the window's tab icons
    // and chrome are ready before its first open instead of popping in after a CDN fetch.
    void WarmAsset(uint32_t assetId);
}
