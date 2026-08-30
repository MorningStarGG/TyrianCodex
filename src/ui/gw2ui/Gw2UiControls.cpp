// Gw2Ui :: GW2-skinned controls -- checkbox, buttons, textboxes, dropdown, context menus, sliders.
#include "ui/Gw2Ui.h"
#include "ui/gw2ui/Gw2UiInternal.h"
#include "ui/InputProbe.h"   // opt-in keyboard diagnostic; text boxes report their queue/ActiveId state
#include "Shared.h"
#include "render/glyphs/Glyphs.h"
#include "util/Textures.h"
#include "ui/Effect.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Gw2Ui::detail;

namespace
{
    struct ActionButtonPalette
    {
        ImU32 bgIdle;
        ImU32 bgHover;
        ImU32 bgHeld;
        ImU32 borderIdle;
        ImU32 borderHover;
        ImU32 inner;
        ImU32 textIdle;
        ImU32 textHover;
        ImU32 textDisabled;
        ImU32 sheen;
    };
    ActionButtonPalette ActionPalette(Gw2Ui::ActionButtonVariant variant)
    {
        if (variant == Gw2Ui::ActionButtonVariant::Primary)
            return {
                IM_COL32(14, 12, 8, 190),
                IM_COL32(72, 54, 25, 198),
                IM_COL32(84, 58, 22, 218),
                IM_COL32(172, 132, 60, 170),
                IM_COL32(236, 178, 82, 245),
                IM_COL32(255, 226, 150, 24),
                IM_COL32(238, 222, 188, 255),
                IM_COL32(255, 238, 196, 255),
                IM_COL32(132, 122, 104, 210),
                IM_COL32(255, 219, 126, 30)};
        if (variant == Gw2Ui::ActionButtonVariant::Danger)
            return {
                IM_COL32(9, 10, 9, 178),
                IM_COL32(72, 54, 25, 190),
                IM_COL32(84, 58, 22, 218),
                IM_COL32(148, 104, 76, 150),
                IM_COL32(232, 142, 112, 225),
                IM_COL32(255, 226, 150, 16),
                IM_COL32(226, 198, 176, 248),
                IM_COL32(255, 218, 196, 255),
                IM_COL32(132, 110, 104, 210),
                IM_COL32(255, 180, 140, 22)};
        return {
            IM_COL32(9, 10, 9, 178),
            IM_COL32(72, 54, 25, 190),
            IM_COL32(84, 58, 22, 218),
            IM_COL32(130, 104, 54, 145),
            IM_COL32(226, 166, 70, 230),
            IM_COL32(255, 226, 150, 22),
            IM_COL32(225, 209, 176, 248),
            IM_COL32(255, 232, 184, 255),
            IM_COL32(128, 120, 104, 210),
            IM_COL32(255, 218, 122, 36)};
    }
    // number-box edit state: which box is currently being typed into (0 = none, showing centred value).
    ImGuiID s_numEdit = 0;
    bool s_numFocus = false;
    char s_numBuf[32] = {};

    enum class ControlSizeMode
    {
        Logical,
        Pixels
    };

    static float ResolveControlWidth(float width, float sc, ControlSizeMode mode)
    {
        if (width <= 0.f)
            return ImGui::GetContentRegionAvail().x;
        return mode == ControlSizeMode::Pixels ? width : width * sc;
    }

    static float ResolveControlHeight(float height, float sc, float defaultPx, ControlSizeMode mode)
    {
        if (height <= 0.f)
            return defaultPx * sc;
        return mode == ControlSizeMode::Pixels ? height : height * sc;
    }

    static float LayoutControlWidth(float width)
    {
        return std::max(1.f, width > 0.f ? width : ImGui::GetContentRegionAvail().x);
    }

    struct InputFontMetrics
    {
        ImFont *font = nullptr;
        float requestedPx = 0.f;
        float windowScale = 1.f;
        float boxH = 0.f;
    };

    static InputFontMetrics InputMetrics(float sc)
    {
        ImFont *base = UiFont(nullptr);
        const float basePx = base ? base->FontSize : ImGui::GetFontSize();
        InputFontMetrics m;
        // Snapped to a baked rung like every other Gw2Ui text size, so ImGui::InputText renders its face at
        // scale 1.0. Unsnapped, any uiScale / PushTextScale other than 1.0 left typed text a few percent off
        // its bake -- the same aliasing as labels, in the one place you stare at individual glyphs.
        m.requestedPx = SnapPx(basePx * sc);
        m.font = ResolveFace(base, m.requestedPx);
        if (!m.font)
            m.font = base ? base : ImGui::GetFont();

        // ImGui::InputText renders through the current window font scale and any external ImGui
        // font-global scaling. Our placeholders are drawn with explicit GW2 UI pixel sizes, so
        // compensate here to keep typed text and placeholders on the same scale path.
        const float ioScale = std::max(0.001f, ImGui::GetIO().FontGlobalScale);
        const float facePx = std::max(0.001f, m.font ? m.font->FontSize : basePx);
        const float faceScale = std::max(0.001f, m.font ? m.font->Scale : 1.f);
        float parentWindowScale = 1.f;
        if (ImGuiWindow *w = ImGui::GetCurrentWindow())
            if (w->ParentWindow)
                parentWindowScale = std::max(0.001f, w->ParentWindow->FontWindowScale);
        m.windowScale = m.requestedPx / std::max(0.001f, ioScale * facePx * faceScale * parentWindowScale);
        m.boxH = m.requestedPx + 10.f * sc;
        return m;
    }

    // Diagnostics hook (Options > Diagnostics > "Input probe"; no-ops when off). ImGui's InputText DRAINS
    // io.InputQueueCharacters when it holds ActiveId, so the pending count has to be sampled BEFORE the call --
    // afterwards it always reads zero. Recording it per box, in submission order, shows which box actually got
    // the characters and which one found the queue already emptied.
    struct ProbedInputText
    {
        const char *label = nullptr;
        ImGuiID     id = 0;
        bool        active = false;
        int         queue = 0;

        explicit ProbedInputText(const char *l) : label(l)
        {
            if (!InputProbe::Enabled())
                return;
            id = ImGui::GetID(l);   // same ID stack the InputText below will hash against
            const ImGuiContext *g = ImGui::GetCurrentContext();
            active = g && g->ActiveId == id;
            queue = ImGui::GetIO().InputQueueCharacters.Size;
        }
        void Done(bool changed) const
        {
            if (InputProbe::Enabled())
                InputProbe::NoteTextBox(label, (unsigned)id, active, queue, changed);
        }
    };

    // ---- Text-field editing affordances (ONE implementation for every Gw2Ui text control) ----------------
    // ImGui 1.80's InputText has no context menu of its own, and its only paste is Ctrl+V. That leaves anyone
    // who expects a right-click menu without one (we give one on every OTHER surface), and it strands players
    // whose platform uses a different chord -- on macOS under Wine the Cmd+letter is eaten as a menu shortcut,
    // so Ctrl+V is the only chord that reaches ImGui at all. A menu works whatever the chord is.

    // The clipboard, inserted into the field that currently holds focus. Goes through ImGui's character queue so
    // the active InputText inserts it AT THE CURSOR (replacing any selection) exactly as if typed -- writing the
    // caller's buffer directly would be silently discarded, since an active InputText owns its own copy.
    bool PasteIntoField(ImGuiID fieldId, char *buf, size_t bufSize)
    {
        const char *clip = ImGui::GetClipboardText();
        if (!clip || !*clip)
            return false;
        ImGuiContext *g = ImGui::GetCurrentContext();
        if (g && g->ActiveId == fieldId)
        {
            ImGui::GetIO().AddInputCharactersUTF8(clip);
            return false; // the insert lands next frame; InputText reports the change itself
        }
        std::snprintf(buf, bufSize, "%s", clip); // not focused -> the buffer is ours to set
        return true;
    }

    // The field's selection as UTF-8, or the whole buffer when nothing is selected (these are short single-line
    // fields, so "copy with no selection" meaning "copy the value" is what people expect).
    std::string FieldTextForCopy(ImGuiID fieldId, const char *buf)
    {
        ImGuiContext *g = ImGui::GetCurrentContext();
        if (g && g->ActiveId == fieldId)
        {
            const ImGuiInputTextState &st = g->InputTextState;
            if (st.ID == fieldId && st.CurLenW > 0)
            {
                int a = st.Stb.select_start, b = st.Stb.select_end;
                if (a > b) std::swap(a, b);
                a = std::clamp(a, 0, st.CurLenW);
                b = std::clamp(b, 0, st.CurLenW);
                if (a < b)
                {
                    const ImWchar *from = st.TextW.Data + a, *to = st.TextW.Data + b;
                    const int bytes = ImTextCountUtf8BytesFromStr(from, to);
                    std::vector<char> tmp((size_t)bytes + 1, '\0');
                    ImTextStrToUtf8(tmp.data(), (int)tmp.size(), from, to);
                    return std::string(tmp.data());
                }
            }
        }
        return buf ? std::string(buf) : std::string();
    }

    // Right-click menu, drawn through the SAME Gw2Ui::ContextMenuTree every other surface uses. `secret` drops
    // Cut and Copy: ImGui refuses both for password fields on purpose, and this must not become a way around it.
    // Returns true when `buf` was changed directly (the caller reports it as an edit).
    bool TextFieldMenu(const char *id, char *buf, size_t bufSize, bool secret, bool openNow)
    {
        char menuId[96];
        std::snprintf(menuId, sizeof(menuId), "%s_ctx", id);
        const ImGuiID fieldId = ImGui::GetID(id);

        enum { kCut = 1, kCopy, kPaste, kSelectAll };
        std::vector<Gw2Ui::MenuNode> nodes;
        if (!secret)
        {
            nodes.push_back({ "Cut", kCut });
            nodes.push_back({ "Copy", kCopy });
        }
        nodes.push_back({ "Paste", kPaste });
        nodes.push_back({ "Select all", kSelectAll });

        switch (Gw2Ui::ContextMenuTree(menuId, nodes, openNow))
        {
        case kCopy:
            if (const std::string t = FieldTextForCopy(fieldId, buf); !t.empty())
                ImGui::SetClipboardText(t.c_str());
            return false;
        case kCut:
        {
            const std::string t = FieldTextForCopy(fieldId, buf);
            if (!t.empty())
                ImGui::SetClipboardText(t.c_str());
            // Hand the buffer back before editing it: an ACTIVE InputText owns its own copy and would overwrite
            // whatever we wrote. Dropping focus first makes the write stick (the field re-reads buf next frame).
            ImGui::ClearActiveID();
            if (buf && bufSize) buf[0] = '\0';
            return true;
        }
        case kPaste:
            return PasteIntoField(fieldId, buf, bufSize);
        case kSelectAll:
        {
            ImGuiContext *g = ImGui::GetCurrentContext();
            if (g && g->ActiveId == fieldId && g->InputTextState.ID == fieldId)
            {
                g->InputTextState.SelectAll();
                g->InputTextState.CursorFollow = true;
            }
            return false;
        }
        default: return false;
        }
    }

    // Everything a Gw2Ui text control needs after its InputText. Call immediately after ImGui::InputText, while
    // the item and the ID scope are still current.
    bool TextFieldExtras(const char *id, char *buf, size_t bufSize, bool secret = false)
    {
        const bool openNow = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        return TextFieldMenu(id, buf, bufSize, secret, openNow);
    }

    struct ScopedInputFont
    {
        float previousWindowScale = 1.f;

        explicit ScopedInputFont(const InputFontMetrics &metrics)
        {
            if (ImGuiWindow *w = ImGui::GetCurrentWindow())
                previousWindowScale = w->FontWindowScale;
            ImGui::PushFont(metrics.font ? metrics.font : ImGui::GetFont());
            ImGui::SetWindowFontScale(metrics.windowScale);
        }

        ~ScopedInputFont()
        {
            ImGui::SetWindowFontScale(previousWindowScale);
            ImGui::PopFont();
        }
    };
}

bool Gw2Ui::Checkbox(const char *label, bool *v)
{
    const float sc = TextScale();
    const float box = 32.f * sc;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    // A "##"-prefixed (or empty) label is id-only: draw just the box (the caller labels the row itself).
    const bool showLabel = label && label[0] && !(label[0] == '#' && label[1] == '#');
    const float lblW = showLabel ? MeasureWidth(label, 0.f) : 0.f;
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(box + (showLabel ? 4.f * sc + lblW : 0.f), box));
    const bool hov = ImGui::IsItemHovered();
    if (clicked && v)
        *v = !*v;

    char path[112];
    std::snprintf(path, sizeof(path), "data\\textures\\ui\\checkbox\\cb-%s%s.png",
                  (v && *v) ? "checked" : "unchecked", hov ? "-active" : "");
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (const T cb = File(path); cb.srv)
        Img(dl, cb, p, ImVec2(p.x + box, p.y + box));
    // Checkbox label: GW2 font, no stroke, vertically centered against the box.
    if (showLabel)
        LabelIn(ImVec2(p.x + box + 4.f * sc, p.y), ImVec2(p.x + box + 4.f * sc + lblW, p.y + box), label,
                HAlign::Left, VAlign::Middle, IM_COL32(255, 255, 255, 255), /*stroke*/ false);
    return clicked;
}

namespace
{
Gw2Ui::ActionButtonResult ActionButtonFrameImpl(const char *id, ImVec2 size, Gw2Ui::ActionButtonVariant variant,
                                                bool disabled, const char *tooltip, ControlSizeMode sizeMode)
{
    const float sc = Gw2Ui::GlobalScale();
    size.x = ResolveControlWidth(size.x, sc, sizeMode);
    size.y = ResolveControlHeight(size.y, sc, 26.f, sizeMode);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool rawClicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = !disabled && ImGui::IsItemActive();
    const ImVec2 b(p.x + size.x, p.y + size.y);
    const ActionButtonPalette pal = ActionPalette(variant);

    const bool lit = !disabled && (hovered || held);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 bg = disabled ? IM_COL32(8, 8, 7, 118) : (held ? pal.bgHeld : (hovered ? pal.bgHover : pal.bgIdle));
    const ImU32 br = disabled ? IM_COL32(86, 76, 58, 105) : (lit ? pal.borderHover : pal.borderIdle);
    const float rounding = 3.f * sc;

    dl->AddRectFilled(p, b, bg, rounding);

    if (!disabled)
    {
        const float topH = std::max(7.f, size.y * 0.48f);
        dl->AddRectFilledMultiColor(p, ImVec2(b.x, p.y + topH),
                                    pal.sheen, pal.sheen,
                                    IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    }

    dl->AddRect(p, b, br, rounding, 0, held ? 1.55f : (hovered && !disabled ? 1.35f : 1.f));
    dl->AddRect(ImVec2(p.x + sc, p.y + sc), ImVec2(b.x - sc, b.y - sc),
                disabled ? IM_COL32(255, 230, 170, 7) : pal.inner, std::max(0.f, rounding - sc), 0, sc);

    if (hovered && tooltip && *tooltip)
        Gw2Ui::Tooltip(tooltip);

    return Gw2Ui::ActionButtonResult{rawClicked && !disabled, hovered, held, p, b};
}
}

// A GW2-styled action-button frame (per-variant background / hover / press art + optional disabled dim +
// tooltip). Width auto-fills the content region when size.x <= 0; height defaults to 26px. Returns hover/click.
Gw2Ui::ActionButtonResult Gw2Ui::ActionButtonFrame(const char *id, ImVec2 size, ActionButtonVariant variant,
                                                   bool disabled, const char *tooltip)
{
    return ActionButtonFrameImpl(id, size, variant, disabled, tooltip, ControlSizeMode::Logical);
}

Gw2Ui::ActionButtonResult Gw2Ui::ActionButtonFramePx(const char *id, ImVec2 sizePx, ActionButtonVariant variant,
                                                     bool disabled, const char *tooltip)
{
    return ActionButtonFrameImpl(id, sizePx, variant, disabled, tooltip, ControlSizeMode::Pixels);
}

bool Gw2Ui::ActionButton(const char *label, float width, float height, ActionButtonVariant variant,
                         const char *tooltip, bool disabled)
{
    const ActionButtonResult r = ActionButtonFrame(label, ImVec2(width, height), variant, disabled, tooltip);
    const ActionButtonPalette pal = ActionPalette(variant);
    const ImU32 col = disabled ? pal.textDisabled : ((r.hovered || r.held) ? pal.textHover : pal.textIdle);

    PushTextScale(1.f); // action button labels have a stable control size, even inside scaled dashboard text
    LabelIn(ImVec2(r.min.x + 8.f * GlobalScale(), r.min.y), ImVec2(r.max.x - 8.f * GlobalScale(), r.max.y), label,
            HAlign::Center, VAlign::Middle, col, true, nullptr, 16.f);
    PopTextScale();
    return r.clicked;
}

bool Gw2Ui::ActionButtonPx(const char *label, float widthPx, float heightPx, ActionButtonVariant variant,
                           const char *tooltip, bool disabled)
{
    const ActionButtonResult r = ActionButtonFramePx(label, ImVec2(widthPx, heightPx), variant, disabled, tooltip);
    const ActionButtonPalette pal = ActionPalette(variant);
    const ImU32 col = disabled ? pal.textDisabled : ((r.hovered || r.held) ? pal.textHover : pal.textIdle);

    PushTextScale(1.f);
    LabelIn(ImVec2(r.min.x + 8.f * GlobalScale(), r.min.y), ImVec2(r.max.x - 8.f * GlobalScale(), r.max.y), label,
            HAlign::Center, VAlign::Middle, col, true, nullptr, 16.f);
    PopTextScale();
    return r.clicked;
}

// GW2 StandardButton: animated 9-frame button-states atlas + 4-edge button-border + black text.
namespace
{
bool ButtonImpl(const char *label, float width, float height, ControlSizeMode sizeMode)
{
    const float sc = Gw2Ui::GlobalScale();
    width = ResolveControlWidth(width, sc, sizeMode);
    height = ResolveControlHeight(height, sc, 26.f, sizeMode);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(width, height));
    const bool hov = ImGui::IsItemHovered();

    // Animate the atlas frame 0..8 over ANIM_FRAME_TIME (0.25s) on hover in / out.
    static std::unordered_map<ImGuiID, float> s_anim;
    const ImGuiID animId = ImGui::GetID(label);
    float &a = s_anim[animId];
    const float step = ImGui::GetIO().DeltaTime / 0.25f * 8.f;
    if (hov)
        a = (a + step < 8.f) ? a + step : 8.f;
    else
        a = (a - step > 0.f) ? a - step : 0.f;
    const int frame = (int)(a + 0.5f);
    if (a <= 0.f && !hov)
        s_anim.erase(animId); // drop idle buttons so the anim map can't grow unbounded

    // Background fill: the current frame's cream band. Samples src (frame*350, 0, 350, 20) -- the
    // TOP 20px of the 3150x76 atlas, which is the tan button fill that brightens tan->white on hover
    // (frame 0..8); the dark band lower in the source is NOT part of the button. Drawn inset (3,3,W-6,H-5).
    if (const T bs = File("data\\textures\\ui\\button-states.png"); bs.srv)
    {
        const float vCap = 20.f / 76.f; // only the top 20px band of the 76px source
        dl->AddImage((ImTextureID)bs.srv, ImVec2(p.x + 3.f * sc, p.y + 3.f * sc), ImVec2(p.x + width - 3.f * sc, p.y + height - 2.f * sc),
                     ImVec2(frame / 9.f, 0.f), ImVec2((frame + 1) / 9.f, vCap));
    }

    // Border: 4 edges from the 4x4 button-border texture.
    if (const T bb = File("data\\textures\\ui\\button-border.png"); bb.srv)
    {
        const ImTextureID t = (ImTextureID)bb.srv;
        const float W = width, H = height, q = 0.25f;
        dl->AddImage(t, ImVec2(p.x + 2.f * sc, p.y), ImVec2(p.x + W - 3.f * sc, p.y + 4.f * sc), ImVec2(0, 0), ImVec2(q, 1));             // top  src(0,0,1,4)
        dl->AddImage(t, ImVec2(p.x + W - 4.f * sc, p.y + 2.f * sc), ImVec2(p.x + W, p.y + H - sc), ImVec2(0, q), ImVec2(1, 2 * q)); // right src(0,1,4,1)
        dl->AddImage(t, ImVec2(p.x + 3.f * sc, p.y + H - 4.f * sc), ImVec2(p.x + W - 3.f * sc, p.y + H), ImVec2(q, 0), ImVec2(2 * q, 1)); // bot  src(1,0,1,4)
        dl->AddImage(t, ImVec2(p.x, p.y + 2.f * sc), ImVec2(p.x + 4.f * sc, p.y + H - sc), ImVec2(0, 3 * q), ImVec2(1, 4 * q));     // left src(0,3,4,1)
    }

    // StandardButton text: DefaultFont14, black, centered.
    Gw2Ui::LabelIn(p, ImVec2(p.x + width, p.y + height), label, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                   IM_COL32(0, 0, 0, 255), false, nullptr, 18.f);
    return clicked;
}
}

bool Gw2Ui::Button(const char *label, float width, float height)
{
    return ButtonImpl(label, width, height, ControlSizeMode::Logical);
}

bool Gw2Ui::ButtonPx(const char *label, float widthPx, float heightPx)
{
    return ButtonImpl(label, widthPx, heightPx, ControlSizeMode::Pixels);
}

// GW2 TextBox: the input-box texture (3-slice) skinning a transparent ImGui::InputText in the GW2 font.
bool Gw2Ui::TextBox(const char *id, char *buf, size_t bufSize, float width)
{
    const float sc = TextScale();
    width = LayoutControlWidth(width);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const InputFontMetrics im = InputMetrics(sc);
    const float h = im.boxH;

    if (const T ib = File("data\\textures\\ui\\input-box.png"); ib.srv)
        Img3H(dl, ib, p, ImVec2(p.x + width, p.y + h), 2.f * sc);

    bool changed = false;
    {
        ScopedInputFont inputFont(im);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // texture supplies the box
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * sc, (h - im.requestedPx) * 0.5f));
        ImGui::SetNextItemWidth(width);
        const ProbedInputText probe(id);
        changed = ImGui::InputText(id, buf, bufSize);
        probe.Done(changed);
        changed |= TextFieldExtras(id, buf, bufSize);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    return changed;
}

float Gw2Ui::InputBoxHeight() { return InputMetrics(TextScale()).boxH; } // matches TextBox/SearchBox box height

bool Gw2Ui::TextBoxSecret(const char *id, char *buf, size_t bufSize, float width, bool *revealed)
{
    const float sc = TextScale();
    width = LayoutControlWidth(width);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const InputFontMetrics im = InputMetrics(sc);
    const float h = im.boxH;
    const float iconW = h; // right-hand icon columns: paste, then the eye toggle
    const bool reveal = revealed && *revealed;

    if (const T ib = File("data\\textures\\ui\\input-box.png"); ib.srv)
        Img3H(dl, ib, p, ImVec2(p.x + width, p.y + h), 2.f * sc);

    bool changed = false;
    {
        ScopedInputFont inputFont(im);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // texture supplies the box
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f * sc, (h - im.requestedPx) * 0.5f));
        ImGui::SetNextItemWidth(std::max(1.f, width - iconW * 2.f));
        const ProbedInputText probe(id);
        changed = ImGui::InputText(id, buf, bufSize, reveal ? 0 : ImGuiInputTextFlags_Password);
        probe.Done(changed);
        changed |= TextFieldExtras(id, buf, bufSize, /*secret*/ true);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    // Paste button. A secret field is the one place a failed paste is INVISIBLE (the text is masked) and the
    // value -- a 72-character API key -- is not something anyone retypes. So it gets an explicit button that
    // works whatever the platform's paste chord is. Lives here so every secret field has it (the Welcome
    // window used to carry its own copy of this button; the settings field never did).
    {
        char pid[80];
        std::snprintf(pid, sizeof(pid), "%s_paste", id);
        ImGui::SetCursorScreenPos(ImVec2(p.x + width - iconW * 2.f, p.y));
        const bool pclick = ImGui::InvisibleButton(pid, ImVec2(iconW, h));
        const bool phov = ImGui::IsItemHovered();
        if (phov)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            Tooltip("Paste from clipboard");
        }
        Render::DrawGlyph(dl, ImVec2(p.x + width - iconW * 1.5f, p.y + h * 0.5f), 16.f * sc,
                          Render::Glyph::Copy, phov ? Gw2Ui::kGold : IM_COL32(190, 178, 150, 220), {false, false, false});
        if (pclick)
            if (const char *clip = ImGui::GetClipboardText(); clip && *clip)
            {
                std::snprintf(buf, bufSize, "%s", clip);
                changed = true;
            }
    }

    // eye toggle: an outline eye + pupil, with a slash when hidden. Click flips *revealed.
    const ImVec2 c(p.x + width - iconW * 0.5f, p.y + h * 0.5f);
    char eid[80];
    std::snprintf(eid, sizeof(eid), "%s_eye", id);
    ImGui::SetCursorScreenPos(ImVec2(p.x + width - iconW, p.y));
    const bool click = ImGui::InvisibleButton(eid, ImVec2(iconW, h));
    const bool hov = ImGui::IsItemHovered();
    if (hov)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        Tooltip(reveal ? "Hide key" : "Show key");
    }
    const ImU32 col = hov ? Gw2Ui::kGold : IM_COL32(190, 178, 150, 220);
    Render::DrawGlyph(dl, c, 16.f * sc, reveal ? Render::Glyph::Eye : Render::Glyph::EyeOff, col, {false, false, false});
    if (click && revealed)
        *revealed = !*revealed;

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h)); // leave the cursor below the box (the eye moved it)
    return changed;
}

// GW2 SearchBox: a TextBox with placeholder/hint text + a right-hand icon column - a magnifier when empty, a
// clickable X (clears the field) when there's text. ONE reusable filter widget so every search box (settings /
// journal / checklist / dye picker / ...) looks + behaves the same. Returns true when the text changed (incl. a clear).
bool Gw2Ui::SearchBox(const char *id, char *buf, size_t bufSize, float width, const char *hint)
{
    const float sc = TextScale();
    width = LayoutControlWidth(width);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const InputFontMetrics im = InputMetrics(sc);
    const float h = im.boxH;
    const float iconW = h; // right-hand icon column (magnifier when empty / X when filled)

    if (const T ib = File("data\\textures\\ui\\input-box.png"); ib.srv)
        Img3H(dl, ib, p, ImVec2(p.x + width, p.y + h), 2.f * sc);

    if (!buf[0])
    {
        const float padX = 8.f * sc;
        const float textRight = std::max(p.x + padX, p.x + width - iconW - 4.f * sc);
        const char *placeholder = (hint && hint[0]) ? hint : "Search...";
        dl->PushClipRect(ImVec2(p.x + padX, p.y), ImVec2(textRight, p.y + h), true);
        LabelDL(dl, ImVec2(p.x + padX, p.y), ImVec2(textRight, p.y + h), placeholder,
                HAlign::Left, VAlign::Middle, IM_COL32(150, 142, 122, 190), false, nullptr,
                UiFont(nullptr) ? UiFont(nullptr)->FontSize : ImGui::GetFontSize());
        dl->PopClipRect();
    }

    bool changed = false;
    {
        ScopedInputFont inputFont(im);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // texture supplies the box
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f * sc, (h - im.requestedPx) * 0.5f));
        ImGui::SetNextItemWidth(std::max(1.f, width - iconW));
        const ProbedInputText probe(id);
        changed = ImGui::InputText(id, buf, bufSize);
        probe.Done(changed);
        changed |= TextFieldExtras(id, buf, bufSize);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    const ImVec2 c(p.x + width - iconW * 0.5f, p.y + h * 0.5f); // centre of the right icon column
    if (buf[0])                                                 // clear (X) button
    {
        char xid[80];
        std::snprintf(xid, sizeof(xid), "%s_clr", id);
        ImGui::SetCursorScreenPos(ImVec2(p.x + width - iconW, p.y));
        const bool xc = ImGui::InvisibleButton(xid, ImVec2(iconW, h));
        const ImU32 col = ImGui::IsItemHovered() ? Gw2Ui::kTextSelected : IM_COL32(200, 190, 170, 220);
        const float r = 4.f * sc;
        dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.8f * sc);
        dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.8f * sc);
        if (xc)
        {
            buf[0] = '\0';
            changed = true;
        }
    }
    else // magnifier affordance
    {
        const ImU32 col = IM_COL32(160, 150, 130, 150);
        dl->AddCircle(ImVec2(c.x - 1.5f * sc, c.y - 1.5f * sc), 3.6f * sc, col, 0, 1.6f * sc);
        dl->AddLine(ImVec2(c.x + 1.0f * sc, c.y + 1.0f * sc), ImVec2(c.x + 4.6f * sc, c.y + 4.6f * sc), col, 1.8f * sc);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h)); // leave the cursor below the box (the X moved it)
    return changed;
}

// GW2 Dropdown: input-box box + dd-arrow + selected text, then a popup list. DefaultFont14 == our 18px (matches the button).
namespace
{
bool DropdownImpl(const char *id, const char *const *items, int itemCount, int *selected, float width,
                  const int *sectionAt, const char *const *sectionLabel, int sectionCount, float height,
                  ControlSizeMode sizeMode)
{
    const float sc = Gw2Ui::TextScale();
    const float h = ResolveControlHeight(height, sc, 27.f, sizeMode); // Overridable to match a row
    const float arrowW = 16.f * sc;
    constexpr float kFs = 18.f;
    const ImU32 kText = IM_COL32(239, 240, 239, 255);
    const ImU32 kChard = IM_COL32(255, 204, 119, 255); // ContentService.Colors.Chardonnay

    // Fit the box (and its popup) to the LONGEST item + arrow + padding so there's never a big black gap to the
    // right of short names. A positive `width` acts as a MAX (longer content clips); width <= 0 = pure auto-fit.
    // Central, so EVERY dropdown gets it -- not a per-call-site band-aid.
    float content = 0.f;
    for (int i = 0; i < itemCount; ++i)
        content = std::max(content, Gw2Ui::MeasureWidth(items[i], kFs));
    content += 36.f * sc; // left pad + gap + arrow + right pad
    width = (width > 0.f) ? std::min(ResolveControlWidth(width, sc, sizeMode), content) : content;
    if (width < arrowW + 24.f * sc)
        width = arrowW + 24.f * sc; // floor: an empty/tiny list still shows the box + arrow

    char pop[128];
    std::snprintf(pop, sizeof(pop), "%s_ddpop", id); // UNIQUE popup id per dropdown

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hov = ImGui::IsItemHovered();

    if (const T ib = File("data\\textures\\ui\\input-box.png"); ib.srv)
        Img3H(dl, ib, p, ImVec2(p.x + width, p.y + h), 2.f);

    const char *arrowFile = (hov || ImGui::IsPopupOpen(pop))
                                ? "data\\textures\\ui\\dd-arrow-active.png"
                                : "data\\textures\\ui\\dd-arrow.png";
    if (const T ar = File(arrowFile); ar.srv)
        Img(dl, ar, ImVec2(p.x + width - arrowW - 5.f * sc, p.y + (h - 16.f * sc) * 0.5f),
            ImVec2(p.x + width - 5.f * sc, p.y + (h - 16.f * sc) * 0.5f + 16.f * sc));

    const char *sel = (*selected >= 0 && *selected < itemCount) ? items[*selected] : "";
    Gw2Ui::LabelIn(ImVec2(p.x + 5.f * sc, p.y), ImVec2(p.x + width - 10.f * sc - arrowW, p.y + h), sel,
                   Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, kText, false, nullptr, kFs);

    if (clicked)
        ImGui::OpenPopup(pop);

    int visibleSectionCount = 0;
    if (sectionAt)
        for (int s = 0; s < sectionCount; ++s)
            if (sectionAt[s] >= 0 && sectionAt[s] < itemCount)
                ++visibleSectionCount;

    const float sectionH = 23.f * sc;
    const float fullContentH = std::max(h, itemCount * h + visibleSectionCount * sectionH);
    const float desiredContentH = std::min(fullContentH, h * 10.f + sectionH);
    const float margin = 8.f * sc;
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float belowY = p.y + h - sc;
    const float spaceBelow = std::max(0.f, disp.y - margin - belowY);
    const float spaceAbove = std::max(0.f, p.y - margin);
    const bool openAbove = spaceBelow < desiredContentH && spaceAbove > spaceBelow;
    const float availableH = openAbove ? spaceAbove : spaceBelow;
    const float childH = std::min(desiredContentH, std::max(h, availableH));
    const bool needsScroll = fullContentH > childH + 0.5f;
    const float childW = width + (needsScroll ? ImGui::GetStyle().ScrollbarSize : 0.f);
    const float popupX = std::clamp(p.x, margin, std::max(margin, disp.x - margin - childW));
    const float popupY = openAbove ? std::max(margin, p.y + sc - childH) : belowY;

    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(popupX, popupY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 255));
    if (ImGui::BeginPopup(pop, ImGuiWindowFlags_NoMove))
    {
        const bool popupAppearing = ImGui::IsWindowAppearing();
        ImGui::BeginChild("##ddscroll", ImVec2(childW, childH), false);
        ImDrawList *pdl = ImGui::GetWindowDrawList();
        for (int i = 0; i < itemCount; ++i)
        {
            for (int s = 0; s < sectionCount; ++s)
                if (sectionAt && sectionAt[s] == i) // section divider before item i
                {
                    const ImVec2 hp = ImGui::GetCursorScreenPos();
                    const float hh = 23.f * sc;
                    ImGui::Dummy(ImVec2(width, hh));
                    pdl->AddLine(ImVec2(hp.x + 8.f * sc, hp.y + 5.f * sc), ImVec2(hp.x + width - 8.f * sc, hp.y + 5.f * sc), IM_COL32(92, 80, 56, 220), sc);
                    if (sectionLabel && sectionLabel[s] && sectionLabel[s][0])
                        DrawLabelCore(pdl, ImVec2(hp.x + 10.f * sc, hp.y + 5.f * sc), ImVec2(hp.x + width - 8.f * sc, hp.y + hh),
                                      sectionLabel[s], UiFont(nullptr), IM_COL32(176, 154, 112, 235), false,
                                      Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, 14.f);
                }
            ImGui::PushID(i);
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const bool rclick = ImGui::InvisibleButton("##it", ImVec2(width, h));
            const bool rhov = ImGui::IsItemHovered();
            if (popupAppearing && selected && i == *selected)
                ImGui::SetScrollHereY(0.5f);
            if (rhov) // dark-brown highlight spanning the FULL row (was inset ~28px on the right -> a black gap)
                pdl->AddRectFilled(ImVec2(rp.x + 2.f * sc, rp.y + 2.f * sc),
                                   ImVec2(rp.x + width - 2.f * sc, rp.y + h - 2.f * sc), IM_COL32(45, 37, 25, 255));
            DrawLabelCore(pdl, ImVec2(rp.x + 8.f * sc, rp.y), ImVec2(rp.x + width - 8.f * sc, rp.y + h),
                          items[i], UiFont(nullptr), rhov ? kChard : kText, false,
                          Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, kFs);
            if (rclick)
            {
                *selected = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return changed;
}
}

bool Gw2Ui::Dropdown(const char *id, const char *const *items, int itemCount, int *selected, float width,
                     const int *sectionAt, const char *const *sectionLabel, int sectionCount, float height)
{
    return DropdownImpl(id, items, itemCount, selected, width, sectionAt, sectionLabel, sectionCount, height,
                        ControlSizeMode::Logical);
}

bool Gw2Ui::DropdownPx(const char *id, const char *const *items, int itemCount, int *selected, float widthPx,
                       const int *sectionAt, const char *const *sectionLabel, int sectionCount, float heightPx)
{
    return DropdownImpl(id, items, itemCount, selected, widthPx, sectionAt, sectionLabel, sectionCount, heightPx,
                        ControlSizeMode::Pixels);
}

namespace
{
bool IsUnsetKeybind(const char *bind)
{
    return !bind || !bind[0] || std::strcmp(bind, "Not set") == 0 || std::strcmp(bind, "(null)") == 0;
}

bool NexusKeyName(int vk, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    {
        std::snprintf(out, outSize, "%c", (char)vk);
        return true;
    }
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        std::snprintf(out, outSize, "F%d", vk - VK_F1 + 1);
        return true;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        std::snprintf(out, outSize, "NUM %d", vk - VK_NUMPAD0);
        return true;
    }

    const char *name = nullptr;
    switch (vk)
    {
    case VK_SPACE: name = "SPACE"; break;
    case VK_RETURN: name = "ENTER"; break;
    case VK_TAB: name = "TAB"; break;
    case VK_BACK: name = "BACKSPACE"; break;
    case VK_INSERT: name = "INSERT"; break;
    case VK_DELETE: name = "DELETE"; break;
    case VK_HOME: name = "HOME"; break;
    case VK_END: name = "END"; break;
    case VK_PRIOR: name = "PAGEUP"; break;
    case VK_NEXT: name = "PAGEDOWN"; break;
    case VK_UP: name = "UP"; break;
    case VK_DOWN: name = "DOWN"; break;
    case VK_LEFT: name = "LEFT"; break;
    case VK_RIGHT: name = "RIGHT"; break;
    case VK_ADD: name = "NUM +"; break;
    case VK_SUBTRACT: name = "NUM -"; break;
    case VK_MULTIPLY: name = "NUM *"; break;
    case VK_DIVIDE: name = "NUM /"; break;
    case VK_DECIMAL: name = "NUM ."; break;
    case VK_OEM_1: name = ";"; break;
    case VK_OEM_PLUS: name = "="; break;
    case VK_OEM_COMMA: name = ","; break;
    case VK_OEM_MINUS: name = "-"; break;
    case VK_OEM_PERIOD: name = "."; break;
    case VK_OEM_2: name = "/"; break;
    case VK_OEM_3: name = "`"; break;
    case VK_OEM_4: name = "["; break;
    case VK_OEM_5: name = "\\"; break;
    case VK_OEM_6: name = "]"; break;
    case VK_OEM_7: name = "'"; break;
    default: break;
    }
    if (name)
    {
        std::snprintf(out, outSize, "%s", name);
        return true;
    }

    char fallback[32] = {};
    const UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if (sc == 0 || GetKeyNameTextA((LONG)(sc << 16), fallback, sizeof(fallback)) <= 0)
        return false;
    for (char &c : fallback)
        c = (char)std::toupper((unsigned char)c);
    std::snprintf(out, outSize, "%s", fallback);
    return out[0] != '\0';
}

bool IsModifierOrMouse(int vk)
{
    switch (vk)
    {
    case VK_CONTROL:
    case VK_SHIFT:
    case VK_MENU:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    default:
        return false;
    }
}
}

// GW2 KeybindingAssigner: name panel + hotkey panel (white*0.15, *0.20 on hover) + centered bind
// text. Click the hotkey region to capture; we read the live key state (Win32) and format the
// combo for Nexus, Nexus owns the real bind registration -- this is the
// control surface; wiring it to InputBinds happens when the Keybinds section is built.
bool Gw2Ui::KeybindAssigner(const char *label, char *bindBuf, size_t bufSize, float width, float nameWidth)
{
    if (!bindBuf || bufSize == 0)
        return false;
    const char *&s_capturing = s_kbCapturing; // file-scope (so BeginWindow can see an active capture)
    const float sc = TextScale();
    width = ResolveControlWidth(width, sc, ControlSizeMode::Logical);
    nameWidth *= sc;
    const float h = 20.f * sc;
    const float pad = 2.f * sc;
    const float clearW = 70.f * sc;
    const float minHotkeyW = 92.f * sc;
    if (width < nameWidth + pad + minHotkeyW + pad + clearW)
        nameWidth = std::max(82.f * sc, width - pad - minHotkeyW - pad - clearW);
    constexpr float kFs = 18.f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(label, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const float hkLeft = nameWidth + pad;
    const float clearLeft = width - clearW;
    const float hkRight = clearLeft - pad;
    const float mouseX = ImGui::GetIO().MousePos.x - p.x;
    const bool overHotkey = hovered && mouseX >= hkLeft && mouseX < hkRight;
    const bool overClear = hovered && mouseX >= clearLeft;
    const bool capturing = (s_capturing == label);

    dl->AddRectFilled(p, ImVec2(p.x + nameWidth, p.y + h), IM_COL32(255, 255, 255, 38)); // name panel white*0.15
    LabelIn(ImVec2(p.x + 4.f * sc, p.y), ImVec2(p.x + nameWidth, p.y + h), label,
            HAlign::Left, VAlign::Middle, IM_COL32(255, 255, 255, 255), false, nullptr, kFs);

    const ImU32 hkBg = (overHotkey || capturing) ? IM_COL32(255, 255, 255, 51) : IM_COL32(255, 255, 255, 38);
    dl->AddRectFilled(ImVec2(p.x + hkLeft, p.y), ImVec2(p.x + hkRight, p.y + h), hkBg);
    const char *shown = capturing ? "Press keys... (Esc)" : (IsUnsetKeybind(bindBuf) ? "Not set" : bindBuf);
    LabelIn(ImVec2(p.x + hkLeft, p.y), ImVec2(p.x + hkRight, p.y + h), shown,
            HAlign::Center, VAlign::Middle, IM_COL32(255, 255, 255, 255), false, nullptr, kFs);

    const ImU32 clearBg = overClear ? IM_COL32(255, 255, 255, 51) : IM_COL32(255, 255, 255, 38);
    dl->AddRectFilled(ImVec2(p.x + clearLeft, p.y), ImVec2(p.x + width, p.y + h), clearBg);
    LabelIn(ImVec2(p.x + clearLeft, p.y), ImVec2(p.x + width, p.y + h), "Clear",
            HAlign::Center, VAlign::Middle, IM_COL32(255, 255, 255, 245), false, nullptr, kFs);

    bool changed = false;
    if (overClear && ImGui::IsMouseClicked(0))
    {
        std::snprintf(bindBuf, bufSize, "(null)");
        s_capturing = nullptr;
        changed = true;
    }
    else if (overHotkey && ImGui::IsMouseClicked(0))
        s_capturing = label;

    if (capturing)
    {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            s_capturing = nullptr;
        }
        else
        {
            const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            for (int vk = 0x08; vk <= 0xFE; ++vk)
            {
                if (vk == VK_ESCAPE || IsModifierOrMouse(vk))
                    continue;
                if (GetAsyncKeyState(vk) & 0x8000)
                {
                    char keyName[32] = {};
                    if (NexusKeyName(vk, keyName, sizeof(keyName)))
                    {
                        std::snprintf(bindBuf, bufSize, "%s%s%s%s",
                                      ctrl ? "CTRL+" : "", shift ? "SHIFT+" : "", alt ? "ALT+" : "", keyName);
                        changed = true;
                    }
                    s_capturing = nullptr;
                    break;
                }
            }
        }
    }
    return changed;
}

// GW2 ColorBox + ColorPicker: a 32px dye swatch (cp-clr-dc tinted by the current rgb) that opens
// a scrollable grid of the full dye palette (24px cp-clr-v1..v4 swatches + hover/active overlays), each
// tinted by its cloth rgb. Picking a dye writes its rgb back.
bool Gw2Ui::ColorBox(const char *id, unsigned int *rgb, const Dye *dyes, int dyeCount, float size)
{
    const float sc = TextScale();
    size *= sc;
    auto rgbOf = [](const Dye &d) -> unsigned int
    { return ((unsigned)d.r << 16) | ((unsigned)d.g << 8) | d.b; };
    const char *kDc = "data\\textures\\ui\\colorpicker\\cp-clr-dc.png";
    const char *kHover = "data\\textures\\ui\\colorpicker\\cp-clr-hover.png";
    const char *kActive = "data\\textures\\ui\\colorpicker\\cp-clr-active.png";

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hov = ImGui::IsItemHovered();

    const ImU32 tint = IM_COL32((*rgb >> 16) & 0xFF, (*rgb >> 8) & 0xFF, *rgb & 0xFF, 255);
    if (const T sw = File(kDc); sw.srv)
        Img(dl, sw, p, ImVec2(p.x + size, p.y + size), ImVec2(0, 0), ImVec2(1, 1), tint);
    if (hov)
        if (const T hv = File(kHover); hv.srv)
            Img(dl, hv, p, ImVec2(p.x + size, p.y + size));

    if (clicked)
        ImGui::OpenPopup("##cppop");

    bool changed = false;
    const float cell = 24.f * sc, pad = 3.f * sc;
    const int perRow = 15;
    const float sb = ImGui::GetStyle().ScrollbarSize;
    const float gridDrawW = perRow * (cell + pad);   // the swatch columns
    const float childW = gridDrawW + 2.f * pad + sb; // + padding + room for the scrollbar
    ImGui::SetNextWindowSize(ImVec2(childW + 2.f * pad, 470.f * sc), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 235));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    if (ImGui::BeginPopup("##cppop"))
    {
        static char s_search[64] = "";
        SearchBox("##cpsearch", s_search, sizeof(s_search), gridDrawW, "Search dyes...");

        auto matches = [&](const char *name) -> bool
        {
            if (!s_search[0])
                return true;
            std::string n = name, q = s_search;
            for (char &c : n)
                c = (char)std::tolower((unsigned char)c);
            for (char &c : q)
                c = (char)std::tolower((unsigned char)c);
            return n.find(q) != std::string::npos;
        };

        ImGui::BeginChild("##cpgrid", ImVec2(childW, 418.f * sc), false);
        ImDrawList *pdl = ImGui::GetWindowDrawList();
        int shown = 0;
        for (int i = 0; i < dyeCount; ++i)
        {
            if (!matches(dyes[i].name))
                continue;
            ImGui::PushID(i);
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const bool cclick = ImGui::InvisibleButton("##sw", ImVec2(cell, cell));
            const bool chov = ImGui::IsItemHovered();
            const ImU32 ct = IM_COL32(dyes[i].r, dyes[i].g, dyes[i].b, 255);

            char vfile[80];
            std::snprintf(vfile, sizeof(vfile), "data\\textures\\ui\\colorpicker\\cp-clr-v%d.png", (i % 4) + 1);
            if (const T s = File(vfile); s.srv)
                Img(pdl, s, cp, ImVec2(cp.x + cell, cp.y + cell), ImVec2(0, 0), ImVec2(1, 1), ct);
            if (chov)
                if (const T hv = File(kHover); hv.srv)
                    Img(pdl, hv, cp, ImVec2(cp.x + cell, cp.y + cell));
            if (rgbOf(dyes[i]) == (*rgb & 0xFFFFFF))
                if (const T ac = File(kActive); ac.srv)
                    Img(pdl, ac, cp, ImVec2(cp.x + cell, cp.y + cell));

            if (chov)
                Tooltip(dyes[i].name);
            if (cclick)
            {
                *rgb = rgbOf(dyes[i]);
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            if ((shown % perRow) != perRow - 1)
                ImGui::SameLine(0.f, pad);
            ++shown;
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return changed;
}

// GW2 ContextMenuStrip: dark (33,32,33) panel + scrollbar-track left edge + rows that reuse the
// scrolling highlight + a bullet (155038) + shadowed text. Item pitch is ITEM_HEIGHT 22 + ITEM_VERTICALMARGIN 6.
// Box metrics for a context menu: width to the widest item, height to fit all rows.
// (In namespace Gw2Ui so the body helpers see the namespace members RowBackground / LabelIn / HAlign.)
namespace Gw2Ui
{

    static void MenuMetrics(const char *const *items, int itemCount, float &iw, float &panelW, float &panelH)
    {
        const float sc = Gw2Ui::TextScale();
        const float bp = 2.f * sc, ih = 22.f * sc, vm = 6.f * sc, pitch = ih + vm;
        constexpr float kFs = 18.f; // 18 is a native ladder rung -> exact (scale 1.0)
        float maxTextW = 0.f;
        for (int i = 0; i < itemCount; ++i)
            maxTextW = (std::max)(maxTextW, Gw2Ui::MeasureWidth(items[i], kFs)); // SAME rung the rows draw with -> measure==draw
        iw = (std::max)(135.f * sc, maxTextW + 30.f * sc + 30.f * sc);
        panelW = iw + bp * 2.f;
        panelH = bp + ih + (itemCount - 1) * pitch + bp;
    }

    // Shared body: paints the (33,32,33) panel + the scrollbar-track edge frame at content origin `o`, lays
    // out the rows (RowBackground scrolling-highlight + bullet + shadowed text), and returns the clicked index
    // (closing the current popup on click) else -1. Used by ContextMenu (opens its own popup) and
    // ContextMenuInline (renders into a popup the host already opened). The caller has already reserved space.
    static int DrawMenuBody(ImVec2 o, const char *const *items, int itemCount, float iw, float panelW, float panelH)
    {
        const float sc = Gw2Ui::TextScale();
        const float bp = 2.f * sc, ih = 22.f * sc, vm = 6.f * sc, pitch = ih + vm;
        ImDrawList *dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(ImVec2(o.x + bp, o.y + bp), ImVec2(o.x + panelW - bp, o.y + panelH - bp),
                          IM_COL32(33, 32, 33, 255));
        // left/right vertical (native), top/bottom horizontal (the strip rotated 90deg, done here via the AddImageQuad UV winding).
        if (const T edge = File("data\\textures\\ui\\scrollbar-track.png"); edge.srv)
        {
            const ImTextureID t = (ImTextureID)edge.srv;
            const float tw = edge.w * sc;                  // track width (4px)
            const ImU32 ec = IM_COL32(255, 255, 255, 204); // White * 0.8
            const float x0 = o.x, x1 = o.x + panelW, y0 = o.y, y1 = o.y + panelH;
            dl->AddImage(t, ImVec2(x0, y0), ImVec2(x0 + tw, y1), ImVec2(0, 0), ImVec2(1, 1), ec); // left
            dl->AddImage(t, ImVec2(x1 - tw, y0), ImVec2(x1, y1), ImVec2(0, 0), ImVec2(1, 1), ec); // right
            dl->AddImageQuad(t, ImVec2(x0, y0), ImVec2(x1, y0), ImVec2(x1, y0 + tw), ImVec2(x0, y0 + tw),
                             ImVec2(0, 0), ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ec); // top
            dl->AddImageQuad(t, ImVec2(x0, y1 - tw), ImVec2(x1, y1 - tw), ImVec2(x1, y1), ImVec2(x0, y1),
                             ImVec2(0, 0), ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ec); // bottom
        }
        ImGui::Dummy(ImVec2(panelW, panelH)); // reserve so the popup auto-sizes

        int result = -1;
        for (int i = 0; i < itemCount; ++i)
        {
            const ImVec2 rp(o.x + bp, o.y + bp + i * pitch);
            ImGui::SetCursorScreenPos(rp);
            ImGui::PushID(i);
            const bool rclick = ImGui::InvisibleButton("##cmi", ImVec2(iw, ih));
            const bool rhov = ImGui::IsItemHovered();
            RowBackground(rp, ImVec2(rp.x + iw, rp.y + ih), rhov, false, ImGui::GetID("##cmi"), -1);
            if (const T b = Asset(155038); b.srv)
                Img(dl, b, ImVec2(rp.x + 6.f * sc, rp.y + (ih - 18.f * sc) * 0.5f),
                    ImVec2(rp.x + 24.f * sc, rp.y + (ih - 18.f * sc) * 0.5f + 18.f * sc)); // bullet @ HORIZONTAL_PADDING, 18px
            LabelIn(ImVec2(rp.x + 30.f * sc, rp.y), ImVec2(rp.x + iw - 6.f * sc, rp.y + ih), items[i],
                    HAlign::Left, VAlign::Middle, IM_COL32(239, 240, 239, 255), false, nullptr, 18.f); // TEXT_LEFTPADDING 30
            if (rclick)
            {
                result = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        return result;
    }

    // ---- Submenu-capable variant (same panel/row look) ----------------------------------------------------
    static void LevelMetrics(const std::vector<Gw2Ui::MenuNode> &nodes, float &iw, float &panelW, float &panelH)
    {
        const float sc = Gw2Ui::TextScale();
        const float bp = 2.f * sc, ih = 22.f * sc, vm = 6.f * sc, pitch = ih + vm;
        constexpr float kFs = 18.f; // 18 is a native ladder rung -> exact (scale 1.0)
        float maxTextW = 0.f;
        bool anySub = false, anySel = false;
        for (const auto &n : nodes)
        {
            if (n.separator)
                continue;
            maxTextW = (std::max)(maxTextW, Gw2Ui::MeasureWidth(n.label.c_str(), kFs)); // SAME rung the rows draw with
            if (!n.children.empty())
                anySub = true;
            if (n.selected)
                anySel = true;
        }
        // 30 left bullet/pad; wider right pad when any row carries a submenu arrow OR a selected green check.
        iw = (std::max)(135.f * sc, maxTextW + 30.f * sc + ((anySub || anySel) ? 28.f * sc : 12.f * sc));
        panelW = iw + bp * 2.f;
        panelH = bp + ih + ((int)nodes.size() - 1) * pitch + bp; // uniform pitch (a separator occupies a row too)
    }

    // Per-submenu keep-alive (cid -> last time its row OR its subtree was hovered). A short grace after the mouse
    // leaves both keeps the submenu open while traversing diagonally toward it, so it doesn't flicker shut.
    static std::unordered_map<std::string, double> s_menuSubAlive;

    // Renders one menu level at `o`; opens child popups for submenu rows (one per stack level, ImGui-managed).
    // Returns the clicked LEAF id (propagated up so a deep click closes the whole stack), else -1. *outHovered (if
    // given) reports whether THIS level's popup OR any descendant submenu is hovered -- so a parent keeps its open
    // submenu alive while a grandchild is hovered (fixes deep-menu cascade-close).
    static int DrawMenuLevel(ImVec2 o, const std::vector<Gw2Ui::MenuNode> &nodes, const char *idPrefix, bool *outHovered = nullptr)
    {
        const float sc = Gw2Ui::TextScale();
        const float bp = 2.f * sc, ih = 22.f * sc, vm = 6.f * sc, pitch = ih + vm;
        float iw, panelW, panelH;
        LevelMetrics(nodes, iw, panelW, panelH);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        bool anyHover = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup); // mouse over this level's popup

        dl->AddRectFilled(ImVec2(o.x + bp, o.y + bp), ImVec2(o.x + panelW - bp, o.y + panelH - bp), IM_COL32(33, 32, 33, 255));
        if (const T edge = File("data\\textures\\ui\\scrollbar-track.png"); edge.srv)
        {
            const ImTextureID t = (ImTextureID)edge.srv;
            const float tw = edge.w * sc;
            const ImU32 ec = IM_COL32(255, 255, 255, 204);
            const float x0 = o.x, x1 = o.x + panelW, y0 = o.y, y1 = o.y + panelH;
            dl->AddImage(t, ImVec2(x0, y0), ImVec2(x0 + tw, y1), ImVec2(0, 0), ImVec2(1, 1), ec);
            dl->AddImage(t, ImVec2(x1 - tw, y0), ImVec2(x1, y1), ImVec2(0, 0), ImVec2(1, 1), ec);
            dl->AddImageQuad(t, ImVec2(x0, y0), ImVec2(x1, y0), ImVec2(x1, y0 + tw), ImVec2(x0, y0 + tw), ImVec2(0, 0), ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ec);
            dl->AddImageQuad(t, ImVec2(x0, y1 - tw), ImVec2(x1, y1 - tw), ImVec2(x1, y1), ImVec2(x0, y1), ImVec2(0, 0), ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ec);
        }
        ImGui::Dummy(ImVec2(panelW, panelH));

        int result = -1;
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            const Gw2Ui::MenuNode &n = nodes[i];
            const ImVec2 rp(o.x + bp, o.y + bp + i * pitch);
            if (n.separator)
            {
                dl->AddLine(ImVec2(rp.x + 8.f * sc, rp.y + ih * 0.5f), ImVec2(rp.x + iw - 8.f * sc, rp.y + ih * 0.5f), IM_COL32(120, 108, 82, 130), sc);
                continue;
            }
            ImGui::SetCursorScreenPos(rp);
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##mi", ImVec2(iw, ih));
            // AllowWhenBlockedByPopup: a row must still register hover while a sibling's submenu popup is open, so
            // hovering a DIFFERENT submenu row switches to it (else the first submenu gets "stuck" open).
            const bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
            // Hover is the ONLY row highlight (so it always reads); an enabled/current row is marked by a green check on
            // the right instead of a persistent highlight that would mask the hover.
            RowBackground(rp, ImVec2(rp.x + iw, rp.y + ih), hov, /*selected*/ false, ImGui::GetID("##mi"), -1);
            if (const T b = Asset(155038); b.srv)
                Img(dl, b, ImVec2(rp.x + 6.f * sc, rp.y + (ih - 18.f * sc) * 0.5f), ImVec2(rp.x + 24.f * sc, rp.y + (ih - 18.f * sc) * 0.5f + 18.f * sc));
            LabelIn(ImVec2(rp.x + 30.f * sc, rp.y), ImVec2(rp.x + iw - 6.f * sc, rp.y + ih), n.label.c_str(), HAlign::Left, VAlign::Middle, IM_COL32(239, 240, 239, 255), false, nullptr, 18.f);
            if (n.selected) // current/enabled -> right-side green check (a row is never both selected and a submenu)
                Render::DrawGlyph(dl, ImVec2(rp.x + iw - 12.f * sc, rp.y + ih * 0.5f), 16.f * sc, Render::Glyph::Check, IM_COL32(120, 235, 140, 255), {false, false, false});
            if (!n.children.empty())
            {
                const float ay = rp.y + ih * 0.5f; // submenu arrow
                Render::DrawGlyph(dl, ImVec2(rp.x + iw - 10.f * sc, ay), 16.f * sc, Render::Glyph::CaretRight, IM_COL32(239, 240, 239, 235), {false, false, false});
                char cid[96];
                std::snprintf(cid, sizeof(cid), "%s/%d", idPrefix, i);
                if (hov)
                    ImGui::OpenPopup(cid); // hovering a row opens its submenu (and, being a new id at this level, closes a sibling's)
                // Position the submenu beside the row, but keep it on-screen: the explicit pos overrides ImGui's
                // auto-flip, so a tall submenu near a screen edge would run off (e.g. a bottom-docked Info Panel
                // opening downward). Clamp into the viewport work area -> it opens UPWARD near the bottom, and to the
                // LEFT near the right edge.
                float ciw, cpw, cph;
                LevelMetrics(n.children, ciw, cpw, cph);
                const ImVec2 disp = ImGui::GetIO().DisplaySize; // fullscreen overlay -> work area is 0..DisplaySize
                const float vpL = 0.f, vpT = 0.f, vpR = disp.x, vpB = disp.y;
                float subX = o.x + panelW - bp - 2.f * sc, subY = rp.y - bp; // 2px overlap so moving right into it has no dead gap
                if (subX + cpw > vpR)
                    subX = o.x - cpw + bp + 2.f * sc; // off the right edge -> flip to the LEFT of the parent
                if (subX < vpL)
                    subX = vpL;
                if (subY + cph > vpB)
                    subY = vpB - cph; // off the bottom -> shift up so it fits
                if (subY < vpT)
                    subY = vpT;
                ImGui::SetNextWindowPos(ImVec2(subX, subY));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
                ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
                if (ImGui::BeginPopup(cid, ImGuiWindowFlags_NoMove))
                {
                    bool childHov = false; // this submenu OR any of ITS descendants hovered (set by the recursion)
                    const int r = DrawMenuLevel(ImGui::GetCursorScreenPos(), n.children, cid, &childHov);
                    const std::string ck(cid);
                    const double now = ImGui::GetTime();
                    if (hov || childHov)
                        s_menuSubAlive[ck] = now; // refresh keep-alive while the row or its subtree is hovered
                    if (r >= 0)
                    {
                        result = r;
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        auto it = s_menuSubAlive.find(ck);
                        const double last = (it != s_menuSubAlive.end()) ? it->second : now;
                        if (!hov && !childHov && now - last > 0.30) // off the row AND the whole subtree for a grace period -> close
                        {
                            ImGui::CloseCurrentPopup();
                            if (it != s_menuSubAlive.end())
                                s_menuSubAlive.erase(it); // evict the keep-alive entry so the map doesn't grow unbounded
                        }
                    }
                    if (childHov)
                        anyHover = true; // a descendant is hovered -> this level counts as hovered (keeps OUR parent alive)
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                // The open submenu popup SWALLOWS this row's InvisibleButton click (ImGui consumes it as a click-
                // outside-the-popup that just dismisses it), so a clickable PARENT ("tab" with a valid id) detects
                // its OWN click via hover (allowed past the popup, AllowWhenBlockedByPopup above) + the mouse-down
                // this frame -> it opens the tab's main view, while hovering still expands the submenu.
                if (n.id >= 0 && hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    result = n.id;
                    ImGui::CloseCurrentPopup();
                }
            }
            else if (clicked)
            {
                result = n.id;
                ImGui::CloseCurrentPopup();
            } // a LEAF row (no blocking submenu) -> its action
            ImGui::PopID();
        }
        if (outHovered)
            *outHovered = anyHover;
        return result;
    }

} // namespace Gw2Ui

int Gw2Ui::ContextMenu(const char *id, const char *const *items, int itemCount)
{
    PushTextScale(1.f); // stable menu chrome -- never the ambient dashboard text-scale (see ContextMenuTree)
    float iw, panelW, panelH;
    MenuMetrics(items, itemCount, iw, panelW, panelH);

    int result = -1;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0)); // we paint the panel ourselves
    if (ImGui::BeginPopup(id, ImGuiWindowFlags_NoMove))            // a context menu is anchored - never draggable
    {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        result = DrawMenuBody(o, items, itemCount, iw, panelW, panelH);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    PopTextScale();
    return result;
}

int Gw2Ui::ContextMenuTree(const char *id, const std::vector<MenuNode> &nodes, bool openNow)
{
    if (openNow)
        ImGui::OpenPopup(id);
    int result = -1;
    // A menu is chrome: it (and every nested submenu) must render at a STABLE size, not the ambient dashboard
    // text-scale of whatever surface opened it -- else LabelIn renders bigger than LevelMetrics measured and the
    // text spills past the panel's clip rect. Same neutralisation tooltips + action buttons already do.
    PushTextScale(1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0)); // we paint the panel ourselves
    if (ImGui::BeginPopup(id, ImGuiWindowFlags_NoMove))
    {
        result = DrawMenuLevel(ImGui::GetCursorScreenPos(), nodes, id);
        if (result >= 0)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    PopTextScale();
    return result;
}

// GW2 trackbar with a GW2-style editable number entry to the LEFT (type the precise
// value), then the track 154968 + 4px bumpers + the 16px nub, then the label. Click/drag the track too.
static bool TrackBar(const char *label, float *v, float vmin, float vmax, const char *fmt, bool asInt, float trackWidth = 0.f)
{
    if (!v)
        return false;
    bool changed = false;
    ImGui::PushID(label);
    const float sc = Gw2Ui::TextScale();

    // 1) Number box (left): GW2 input-box texture (3-slice, 2px caps). The value shows CENTRED; click it
    //    to type a precise value (an editable field appears, then commits on Enter / focus loss).
    const float boxW = 64.f * sc;
    const float frameH0 = std::max(ImGui::GetFrameHeight(), Gw2Ui::InputBoxHeight());
    const ImVec2 bp = ImGui::GetCursorScreenPos();
    ImDrawList *dlb = ImGui::GetWindowDrawList();
    if (const T ib = File("data\\textures\\ui\\input-box.png"); ib.srv)
        Img3H(dlb, ib, bp, ImVec2(bp.x + boxW, bp.y + frameH0), 2.f * sc);

    const ImGuiID nid = ImGui::GetID("##num");
    if (s_numEdit == nid)
    {
        if (s_numFocus)
        {
            std::snprintf(s_numBuf, sizeof(s_numBuf), fmt, *v);
            ImGui::SetKeyboardFocusHere();
            s_numFocus = false;
        }
        // Centre the text while typing by padding the frame to (boxW - textWidth)/2 each frame.
        {
            const InputFontMetrics im = InputMetrics(sc);
            ScopedInputFont inputFont(im);
            const float tw = ImGui::CalcTextSize(s_numBuf).x;
            const float padX = std::max(2.f * sc, (boxW - tw) * 0.5f - 2.f * sc);
            const float padY = std::max(0.f, (frameH0 - im.requestedPx) * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::SetCursorScreenPos(bp);
            ImGui::SetNextItemWidth(boxW);
            ImGui::InputText("##num", s_numBuf, sizeof(s_numBuf),
                             ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemDeactivated())
            {
                float parsed = *v;
                if (std::sscanf(s_numBuf, "%f", &parsed) == 1)
                {
                    float nv = std::clamp(parsed, vmin, vmax);
                    if (asInt)
                        nv = std::round(nv);
                    if (nv != *v)
                    {
                        *v = nv;
                        changed = true;
                    }
                }
                s_numEdit = 0;
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
    }
    else
    {
        ImGui::SetCursorScreenPos(bp);
        if (ImGui::InvisibleButton("##num", ImVec2(boxW, frameH0)))
        {
            s_numEdit = nid;
            s_numFocus = true;
        }
        char nb[32];
        std::snprintf(nb, sizeof(nb), fmt, *v);
        Gw2Ui::LabelIn(bp, ImVec2(bp.x + boxW, bp.y + frameH0), nb, Gw2Ui::HAlign::Center, Gw2Ui::VAlign::Middle,
                       ImGui::GetColorU32(ImGuiCol_Text), false, nullptr, 16.f);
    }
    ImGui::SameLine();

    // 2) Slider track + nub, vertically centred against the number box's frame height.
    // A label starting with "##" (or empty) is id-only -- the caller drew the name itself (settings rows
    // put the name on the LEFT at a column), so the track then fills the rest of the row.
    const bool showLabel = label && label[0] && !(label[0] == '#' && label[1] == '#');
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float Hh = 16.f * sc, nubW = 16.f * sc, BUMP = 4.f * sc;
    const float frameH = std::max(ImGui::GetFrameHeight(), Hh + 6.f * sc);
    const float lblW = showLabel ? Gw2Ui::MeasureWidth(label, 16.f) : 0.f;
    const float autoTrackW = ImGui::GetContentRegionAvail().x - lblW - 16.f * sc;
    // A caller-requested trackWidth is a PREFERENCE, not a guarantee -- clamp it to what's actually left in the
    // row too, else a fixed width wider than the current panel/scale just overflows past the row (this is what
    // made every row overflow together once they all requested the same width: none of them were ever actually
    // checked against available space, only the auto-size branch was).
    float trackW = (trackWidth > 0.f) ? std::min(trackWidth * sc, autoTrackW) : std::min(autoTrackW, 620.f);
    if (trackW < 90.f * sc)
        trackW = 90.f * sc;
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    const ImVec2 p(cur.x, cur.y + (frameH - Hh) * 0.5f);
    ImGui::InvisibleButton("##trk", ImVec2(trackW, frameH));
    if (ImGui::IsItemActive()) // click or drag -> set value from mouse x
    {
        float frac = (ImGui::GetIO().MousePos.x - p.x - BUMP * 0.5f) / (trackW - BUMP - nubW);
        frac = std::clamp(frac, 0.f, 1.f);
        float nv = vmin + frac * (vmax - vmin);
        if (asInt)
            nv = std::round(nv);
        if (nv != *v)
        {
            *v = nv;
            changed = true;
        }
    }
    if (const T tr = File("data\\textures\\ui\\trackbar\\track.png"); tr.srv)
        Img(dl, tr, p, ImVec2(p.x + trackW, p.y + Hh));
    // End bars
    dl->AddRectFilled(p, ImVec2(p.x + BUMP, p.y + Hh), IM_COL32(225, 222, 210, 255));
    dl->AddRectFilled(ImVec2(p.x + trackW - BUMP, p.y), ImVec2(p.x + trackW, p.y + Hh), IM_COL32(225, 222, 210, 255));
    const float frac = (vmax > vmin) ? std::clamp((*v - vmin) / (vmax - vmin), 0.f, 1.f) : 0.f;
    const float nubX = p.x + BUMP * 0.5f + frac * (trackW - BUMP - nubW);
    if (const T nub = File("data\\textures\\ui\\trackbar\\tb-nub.png"); nub.srv)
        Img(dl, nub, ImVec2(nubX, p.y), ImVec2(nubX + nubW, p.y + Hh));

    // 3) Label (right) - only when the caller didn't suppress it with a "##" id.
    if (showLabel)
    {
        ImGui::SameLine();
        Gw2Ui::Label(label, ImGui::GetColorU32(ImGuiCol_Text), false, nullptr, 16.f);
    }

    ImGui::PopID();
    return changed;
}

bool Gw2Ui::Slider(const char *label, float *v, float vmin, float vmax, const char *fmt, float trackWidth)
{
    return TrackBar(label, v, vmin, vmax, fmt, false, trackWidth);
}

bool Gw2Ui::SliderInt(const char *label, int *v, int vmin, int vmax, float trackWidth)
{
    float f = (float)*v;
    bool changed = TrackBar(label, &f, (float)vmin, (float)vmax, "%.0f", true, trackWidth);
    if (changed)
        *v = (int)std::lround(f);
    return changed;
}

bool Gw2Ui::RangeSliderInt(const char *id, int *lo, int *hi, int vmin, int vmax)
{
    ImGui::PushID(id);
    bool changed = false;
    const float sc = Gw2Ui::TextScale();
    const float Hh = 16.f * sc, nubW = 16.f * sc, BUMP = 4.f * sc;
    const float frameH = std::max(ImGui::GetFrameHeight(), Hh + 6.f * sc);

    char lab[32];
    std::snprintf(lab, sizeof(lab), "Lv %d - %d", *lo, *hi);
    Gw2Ui::Label(lab, ImGui::GetColorU32(ImGuiCol_Text), false, nullptr, 16.f);
    ImGui::SameLine();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    float trackW = ImGui::GetContentRegionAvail().x - 8.f * sc;
    if (trackW < 120.f * sc)
        trackW = 120.f * sc;
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    const ImVec2 p(cur.x, cur.y + (frameH - Hh) * 0.5f);
    ImGui::InvisibleButton("##rtrk", ImVec2(trackW, frameH));
    const ImGuiID trkId = ImGui::GetID("##rtrk");
    const float span = (float)(vmax - vmin);
    auto valToCx = [&](int v)
    {
        const float frac = span > 0.f ? std::clamp((float)(v - vmin) / span, 0.f, 1.f) : 0.f;
        return p.x + BUMP * 0.5f + frac * (trackW - BUMP - nubW) + nubW * 0.5f;
    };
    auto xToVal = [&](float x)
    {
        float frac = (x - p.x - BUMP * 0.5f - nubW * 0.5f) / (trackW - BUMP - nubW);
        frac = std::clamp(frac, 0.f, 1.f);
        return vmin + (int)std::lround(frac * span);
    };

    static ImGuiID s_active = 0;
    static int s_handle = 0; // which slider + which end is being dragged
    if (ImGui::IsItemActivated())
    {
        const float mx = ImGui::GetIO().MousePos.x;
        s_active = trkId;
        s_handle = (std::fabs(mx - valToCx(*lo)) <= std::fabs(mx - valToCx(*hi))) ? 0 : 1;
    }
    if (ImGui::IsItemActive() && s_active == trkId)
    {
        const int nv = std::clamp(xToVal(ImGui::GetIO().MousePos.x), vmin, vmax);
        if (s_handle == 0)
        {
            const int c = std::min(nv, *hi);
            if (c != *lo)
            {
                *lo = c;
                changed = true;
            }
        }
        else
        {
            const int c = std::max(nv, *lo);
            if (c != *hi)
            {
                *hi = c;
                changed = true;
            }
        }
    }

    if (const T tr = File("data\\textures\\ui\\trackbar\\track.png"); tr.srv)
        Img(dl, tr, p, ImVec2(p.x + trackW, p.y + Hh));
    dl->AddRectFilled(p, ImVec2(p.x + BUMP, p.y + Hh), IM_COL32(225, 222, 210, 255));
    dl->AddRectFilled(ImVec2(p.x + trackW - BUMP, p.y), ImVec2(p.x + trackW, p.y + Hh), IM_COL32(225, 222, 210, 255));
    const float xlo = valToCx(*lo), xhi = valToCx(*hi);
    dl->AddRectFilled(ImVec2(std::min(xlo, xhi), p.y + Hh * 0.5f - 2.f * sc),
                      ImVec2(std::max(xlo, xhi), p.y + Hh * 0.5f + 2.f * sc), IM_COL32(214, 170, 90, 205));
    auto drawNub = [&](float cx)
    {
        const float nx = cx - nubW * 0.5f;
        if (const T nub = File("data\\textures\\ui\\trackbar\\tb-nub.png"); nub.srv)
            Img(dl, nub, ImVec2(nx, p.y), ImVec2(nx + nubW, p.y + Hh));
    };
    drawNub(xlo);
    drawNub(xhi);

    ImGui::PopID();
    return changed;
}
