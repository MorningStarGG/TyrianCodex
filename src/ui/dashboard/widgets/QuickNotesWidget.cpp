#include "Widgets.h"
#include "app/App.h"
#include "ui/Gw2Ui.h"
#include "ui/GuideViewer.h"       // SetClipboard, ViewerAlert
#include "util/Draw.h"            // kFontSizePx / kFontSizeCount
#include "WidgetUtil.h"           // DashUtil::Narrow
#include "Shared.h"               // APIDefs (addon directory)
#include "util/Json.h"            // Json::WriteAtomic (atomic, corruption-safe saves)

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Account-wide multi-note Markdown scratchpad persisted to <addon>/dashboard-notes.json.
namespace
{
    enum { EditorAuto = 0, EditorPopupOnly = 1 };

    struct Note
    {
        std::string id;
        std::string title;
        std::string body;
        long long created = 0;
        long long updated = 0;
    };

    struct ParsedLine
    {
        int heading = 0;
        bool bullet = false;
        bool numbered = false;
        bool checkbox = false;
        bool checked = false;
        size_t checkboxAbs = 0;
        std::string marker;
        std::string text;
    };

    struct Token
    {
        std::string text;
        std::string copy;
        bool code = false;
        bool link = false;
    };

    std::vector<Note> g_notes;
    std::string g_activeId;
    std::string g_editingId;
    bool g_loaded = false;
    bool g_popupRequest = false;
    bool g_popupWasOpen = false;
    bool g_focusInline = false;
    bool g_focusPopup = false;
    bool g_renameRequest = false;
    bool g_deleteRequest = false;
    bool g_previewClickConsumed = false;
    char g_renameBuf[128] = "";
    int g_idCounter = 1;

    long long NowSeconds()
    {
        using namespace std::chrono;
        return (long long)duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    }

    std::string NotesPath()
    {
        const char* d = AddonDir();
        return d ? std::string(d) + "\\dashboard-notes.json" : std::string();
    }

    std::string NewNoteId()
    {
        char b[64];
        std::snprintf(b, sizeof(b), "note-%lld-%d", NowSeconds(), g_idCounter++);
        return b;
    }

    Note MakeDefaultNote()
    {
        Note n;
        n.id = NewNoteId();
        n.title = "Quick Note";
        n.created = n.updated = NowSeconds();
        return n;
    }

    int ActiveIndex()
    {
        for (int i = 0; i < (int)g_notes.size(); ++i)
            if (g_notes[i].id == g_activeId) return i;
        return g_notes.empty() ? -1 : 0;
    }

    Note& ActiveNote()
    {
        int i = ActiveIndex();
        if (i < 0)
        {
            g_notes.push_back(MakeDefaultNote());
            g_activeId = g_notes[0].id;
            i = 0;
        }
        return g_notes[i];
    }

    void EnsureNote()
    {
        if (g_notes.empty())
            g_notes.push_back(MakeDefaultNote());
        if (ActiveIndex() < 0)
            g_activeId = g_notes.front().id;
    }

    void SaveNotes()
    {
        const std::string path = NotesPath();
        if (path.empty()) return;

        nlohmann::json j = nlohmann::json::object();
        j["version"] = 1;
        j["activeId"] = g_activeId;
        j["notes"] = nlohmann::json::array();
        for (const Note& n : g_notes)
        {
            j["notes"].push_back({
                { "id", n.id },
                { "title", n.title },
                { "body", n.body },
                { "created", n.created },
                { "updated", n.updated },
            });
        }
        Json::WriteAtomic(path, j.dump(2));
    }

    void LoadNotes()
    {
        if (g_loaded) return;
        g_loaded = true;
        g_notes.clear();
        g_activeId.clear();

        const std::string path = NotesPath();
        std::ifstream f(path, std::ios::binary);
        if (f)
        {
            try
            {
                nlohmann::json j;
                f >> j;
                if (j.is_object())
                {
                    if (auto it = j.find("activeId"); it != j.end() && it->is_string())
                        g_activeId = it->get<std::string>();
                    if (auto it = j.find("notes"); it != j.end() && it->is_array())
                    {
                        for (const auto& e : *it)
                        {
                            if (!e.is_object()) continue;
                            Note n;
                            n.id = e.value("id", std::string());
                            n.title = e.value("title", std::string("Quick Note"));
                            n.body = e.value("body", std::string());
                            n.created = e.value("created", 0LL);
                            n.updated = e.value("updated", 0LL);
                            if (n.id.empty()) continue;
                            if (n.title.empty()) n.title = "Untitled";
                            if (n.created <= 0) n.created = NowSeconds();
                            if (n.updated <= 0) n.updated = n.created;
                            g_notes.push_back(std::move(n));
                        }
                    }
                }
            }
            catch (...) { g_notes.clear(); g_activeId.clear(); }
        }

        EnsureNote();
        SaveNotes();
    }

    void Mutated(Note& n)
    {
        n.updated = NowSeconds();
        SaveNotes();
    }

    std::string Trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    bool IsContinuation(char c)
    {
        return (((unsigned char)c) & 0xC0u) == 0x80u;
    }

    size_t NextChar(const std::string& s, size_t pos)
    {
        if (pos >= s.size()) return s.size();
        ++pos;
        while (pos < s.size() && IsContinuation(s[pos])) ++pos;
        return pos;
    }

    bool StartsWith(const std::string& s, size_t p, const char* lit)
    {
        const size_t n = std::strlen(lit);
        return p + n <= s.size() && s.compare(p, n, lit) == 0;
    }

    std::string UniqueTitle(const char* base)
    {
        std::string root = (base && *base) ? base : "Quick Note";
        auto exists = [&](const std::string& t) {
            for (const Note& n : g_notes) if (n.title == t) return true;
            return false;
        };
        if (!exists(root)) return root;
        for (int i = 2; ; ++i)
        {
            std::string c = root + " " + std::to_string(i);
            if (!exists(c)) return c;
        }
    }

    int ResizeTextCallback(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            std::string* text = (std::string*)data->UserData;
            text->resize((size_t)data->BufTextLen);
            data->Buf = (char*)text->c_str();
        }
        return 0;
    }

    bool InputTextMultilineString(const char* id, std::string& text, ImVec2 size, float fs, bool focusNow)
    {
        text.reserve(text.size() + 256);
        const float desiredPx = std::max(10.f, fs * Gw2Ui::TextScale());
        const float basePx = std::max(1.f, ImGui::GetFontSize());
        const float fontScale = std::max(0.65f, desiredPx / basePx);

        if (focusNow) ImGui::SetKeyboardFocusHere();
        ImGui::SetWindowFontScale(fontScale);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(24, 24, 28, 235));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(28, 28, 34, 245));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(30, 30, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_Border,         IM_COL32(118, 95, 50, 180));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.f, 6.f));
        const bool changed = ImGui::InputTextMultiline(id, (char*)text.c_str(), text.capacity() + 1, size,
            ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput,
            ResizeTextCallback, &text);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::SetWindowFontScale(1.f);
        return changed;
    }

    std::vector<Token> InlineTokens(const std::string& s)
    {
        std::vector<Token> out;
        for (size_t i = 0; i < s.size(); )
        {
            if (s[i] == '`')
            {
                const size_t e = s.find('`', i + 1);
                if (e != std::string::npos)
                {
                    out.push_back(Token{ s.substr(i + 1, e - i - 1), "", true, false });
                    i = e + 1;
                    continue;
                }
            }
            if (StartsWith(s, i, "http://") || StartsWith(s, i, "https://"))
            {
                size_t e = i;
                while (e < s.size() && !std::isspace((unsigned char)s[e])) ++e;
                const std::string link = s.substr(i, e - i);
                out.push_back(Token{ link, link, false, true });
                i = e;
                continue;
            }
            if (StartsWith(s, i, "[&"))
            {
                const size_t e = s.find(']', i + 2);
                if (e != std::string::npos)
                {
                    const std::string link = s.substr(i, e - i + 1);
                    out.push_back(Token{ link, link, false, true });
                    i = e + 1;
                    continue;
                }
            }
            if (std::isspace((unsigned char)s[i]))
            {
                size_t e = i;
                while (e < s.size() && std::isspace((unsigned char)s[e])) ++e;
                out.push_back(Token{ " ", "", false, false });
                i = e;
                continue;
            }
            size_t e = i;
            while (e < s.size() && !std::isspace((unsigned char)s[e]) && s[e] != '`' &&
                   !StartsWith(s, e, "http://") && !StartsWith(s, e, "https://") && !StartsWith(s, e, "[&"))
                e = NextChar(s, e);
            if (e == i)
                e = NextChar(s, i);
            out.push_back(Token{ s.substr(i, e - i), "", false, false });
            i = e;
        }
        return out;
    }

    void DrawToken(ImDrawList* dl, const Token& tok, const std::string& text, ImVec2 p, float w, float lineH, float fs, int id)
    {
        ImU32 col = tok.link ? IM_COL32(150, 220, 255, 255)
                  : tok.code ? IM_COL32(235, 216, 170, 255)
                             : IM_COL32(234, 228, 210, 255);
        if (tok.code)
            dl->AddRectFilled(ImVec2(p.x - 2.f, p.y + 1.f), ImVec2(p.x + w + 2.f, p.y + lineH - 2.f), IM_COL32(255, 255, 255, 18), 2.f);
        Gw2Ui::LabelDL(dl, p, ImVec2(p.x + w + 3.f, p.y + lineH), text.c_str(),
                       Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, col, false, nullptr, fs);
        if (tok.link)
        {
            dl->AddLine(ImVec2(p.x, p.y + lineH - 3.f), ImVec2(p.x + w, p.y + lineH - 3.f), col, 1.f);
            ImGui::PushID(id);
            ImGui::SetCursorScreenPos(p);
            if (ImGui::InvisibleButton("##lnk", ImVec2(std::max(4.f, w), lineH)))
            {
                SetClipboard(tok.copy);
                ViewerAlert("Copied link.");
                g_previewClickConsumed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        }
    }

    float LayoutInlineWrapped(const std::string& text, ImVec2 origin, float width, float fs, float lineH, int& idSeed, bool draw)
    {
        const std::vector<Token> toks = InlineTokens(text);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        width = std::max(4.f, width);
        float x = origin.x;
        float y = origin.y;
        const float right = origin.x + width;
        bool any = false;

        auto newline = [&]() {
            x = origin.x;
            y += lineH;
        };

        for (const Token& tok : toks)
        {
            if (tok.text == " " && x == origin.x) continue;
            size_t pos = 0;
            while (pos < tok.text.size())
            {
                size_t end = tok.text.size();
                std::string part = tok.text.substr(pos, end - pos);
                float tw = Gw2Ui::MeasureWidth(part.c_str(), fs);
                if (x > origin.x && x + tw > right) newline();
                if (tw > width)
                {
                    end = pos;
                    size_t best = pos;
                    while (end < tok.text.size())
                    {
                        const size_t n = NextChar(tok.text, end);
                        const float cw = Gw2Ui::MeasureWidth(tok.text.substr(pos, n - pos).c_str(), fs);
                        if (cw <= width || best == pos) { best = n; end = n; }
                        else break;
                    }
                    end = best;
                    part = tok.text.substr(pos, end - pos);
                    tw = Gw2Ui::MeasureWidth(part.c_str(), fs);
                }
                if (part != " ")
                {
                    if (draw)
                        DrawToken(dl, tok, part, ImVec2(x, y), tw, lineH, fs, idSeed++);
                    else
                        ++idSeed;
                    any = true;
                }
                x += tw;
                pos = end;
                if (pos < tok.text.size()) newline();
            }
        }
        return any ? (y - origin.y + lineH) : lineH;
    }

    float DrawInlineWrapped(const std::string& text, ImVec2 origin, float width, float fs, float lineH, int& idSeed)
    {
        return LayoutInlineWrapped(text, origin, width, fs, lineH, idSeed, true);
    }

    float EstimateWrappedHeight(const std::string& text, float width, float fs, float lineH)
    {
        const float h = Gw2Ui::MeasureWrappedHeight(text.c_str(), fs, std::max(4.f, width));
        return std::max(lineH, h);
    }

    ParsedLine ParseLine(const std::string& raw, size_t absStart)
    {
        ParsedLine p;
        size_t i = 0;
        while (i < raw.size() && raw[i] == ' ') ++i;

        int hashes = 0;
        while (i + hashes < raw.size() && raw[i + hashes] == '#') ++hashes;
        if (hashes >= 1 && hashes <= 3 && i + hashes < raw.size() && raw[i + hashes] == ' ')
        {
            p.heading = hashes;
            p.text = Trim(raw.substr(i + hashes + 1));
            return p;
        }

        if (i + 5 <= raw.size() && (raw[i] == '-' || raw[i] == '*' || raw[i] == '+') && raw[i + 1] == ' ' &&
            raw[i + 2] == '[' && raw[i + 4] == ']')
        {
            const char c = (char)std::tolower((unsigned char)raw[i + 3]);
            if (c == ' ' || c == 'x')
            {
                p.checkbox = true;
                p.checked = c == 'x';
                p.checkboxAbs = absStart + i + 3;
                p.text = (i + 5 < raw.size() && raw[i + 5] == ' ') ? raw.substr(i + 6) : raw.substr(i + 5);
                return p;
            }
        }

        if (i + 1 < raw.size() && (raw[i] == '-' || raw[i] == '*' || raw[i] == '+') && raw[i + 1] == ' ')
        {
            p.bullet = true;
            p.marker = "-";
            p.text = raw.substr(i + 2);
            return p;
        }

        size_t d = i;
        while (d < raw.size() && std::isdigit((unsigned char)raw[d])) ++d;
        if (d > i && d + 1 < raw.size() && raw[d] == '.' && raw[d + 1] == ' ')
        {
            p.numbered = true;
            p.marker = raw.substr(i, d - i + 1);
            p.text = raw.substr(d + 2);
            return p;
        }

        p.text = raw;
        return p;
    }

    bool ToggleCheckboxAt(Note& n, size_t pos)
    {
        if (pos >= n.body.size()) return false;
        n.body[pos] = (std::tolower((unsigned char)n.body[pos]) == 'x') ? ' ' : 'x';
        Mutated(n);
        return true;
    }

    bool DrawMarkdownPreview(Note& n, float width, float height, float fs)
    {
        g_previewClickConsumed = false;
        ImGui::BeginChild("##preview", ImVec2(width, height), true);
        const bool hovered = ImGui::IsWindowHovered();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 clipMin = ImGui::GetWindowPos();
        const ImVec2 clipMax(clipMin.x + ImGui::GetWindowSize().x, clipMin.y + ImGui::GetWindowSize().y);
        const float pad = 5.f * Gw2Ui::TextScale();
        const float innerW = std::max(20.f, ImGui::GetContentRegionAvail().x - pad * 2.f);
        const float fontPx = fs * Gw2Ui::TextScale();
        const float lineH = std::max(fontPx + 5.f, Gw2Ui::MeasureWrappedHeight("Ag", fs, innerW) + 4.f);
        float y = start.y + pad;
        size_t abs = 0;
        int idSeed = 1;
        auto visible = [&](float top, float h) {
            return top + h >= clipMin.y - lineH && top <= clipMax.y + lineH;
        };

        if (n.body.empty())
        {
            if (visible(y, lineH))
                Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), ImVec2(start.x + pad, y), ImVec2(start.x + pad + innerW, y + lineH),
                               "Empty note", Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kTextDim, false, nullptr, fs);
            y += lineH;
        }
        else
        {
            while (abs <= n.body.size())
            {
                size_t e = n.body.find('\n', abs);
                const bool hasNewline = e != std::string::npos;
                if (!hasNewline) e = n.body.size();
                const std::string raw = n.body.substr(abs, e - abs);
                ParsedLine p = ParseLine(raw, abs);
                float bodyX = start.x + pad;
                float bodyW = innerW;
                float lfs = fs;
                if (p.heading)
                {
                    lfs = fs + (p.heading == 1 ? 5.f : p.heading == 2 ? 3.f : 1.f);
                    const float headingLineH = lineH + (p.heading == 1 ? 5.f : 2.f);
                    const float h = visible(y, headingLineH * 2.f)
                        ? DrawInlineWrapped(p.text, ImVec2(bodyX, y), bodyW, lfs, headingLineH, idSeed)
                        : EstimateWrappedHeight(p.text, bodyW, lfs, headingLineH);
                    y += h + 2.f;
                }
                else
                {
                    if (p.checkbox)
                    {
                        const float cb = std::min(18.f * Gw2Ui::TextScale(), lineH - 2.f);
                        if (visible(y, lineH))
                        {
                            ImGui::PushID((int)p.checkboxAbs);
                            ImGui::SetCursorScreenPos(ImVec2(bodyX, y + (lineH - cb) * 0.5f));
                            if (ImGui::InvisibleButton("##cb", ImVec2(cb, cb)))
                                g_previewClickConsumed = ToggleCheckboxAt(n, p.checkboxAbs);
                            const bool hov = ImGui::IsItemHovered();
                            Gw2Ui::PaintCheckbox(ImVec2(bodyX, y + (lineH - cb) * 0.5f), cb, p.checked ? 1 : 0, hov);
                            ImGui::PopID();
                        }
                        bodyX += cb + 7.f;
                        bodyW -= cb + 7.f;
                    }
                    else if (p.bullet || p.numbered)
                    {
                        if (visible(y, lineH))
                            Gw2Ui::LabelDL(ImGui::GetWindowDrawList(), ImVec2(bodyX, y), ImVec2(bodyX + 24.f * Gw2Ui::TextScale(), y + lineH),
                                           p.marker.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Top, Gw2Ui::kGold, false, nullptr, fs);
                        bodyX += 24.f * Gw2Ui::TextScale();
                        bodyW -= 24.f * Gw2Ui::TextScale();
                    }
                    const float h = visible(y, lineH * 2.f)
                        ? DrawInlineWrapped(p.text, ImVec2(bodyX, y), bodyW, lfs, lineH, idSeed)
                        : EstimateWrappedHeight(p.text, bodyW, lfs, lineH);
                    y += std::max(lineH, h);
                }
                if (!hasNewline) break;
                abs = e + 1;
                if (raw.empty()) y += lineH * 0.7f;
            }
        }

        ImGui::SetCursorScreenPos(start);
        ImGui::Dummy(ImVec2(innerW, std::max(height, y - start.y + pad)));
        const bool bodyClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_previewClickConsumed;
        ImGui::EndChild();
        return bodyClicked;
    }

    void StopEditing()
    {
        g_editingId.clear();
        g_popupRequest = false;
        g_popupWasOpen = false;
        g_focusInline = false;
        g_focusPopup = false;
        SaveNotes();
    }

    void StartEditing(Note& n, bool popup)
    {
        g_editingId = n.id;
        if (popup)
        {
            g_popupRequest = true;
            g_focusPopup = true;
        }
        else
        {
            g_focusInline = true;
        }
    }

    void CreateNote(bool popup)
    {
        Note n = MakeDefaultNote();
        n.title = UniqueTitle("Quick Note");
        g_activeId = n.id;
        g_notes.push_back(std::move(n));
        SaveNotes();
        StartEditing(ActiveNote(), popup);
    }

    void DuplicateNote()
    {
        StopEditing();
        Note src = ActiveNote();
        src.id = NewNoteId();
        src.title = UniqueTitle((src.title + " copy").c_str());
        src.created = src.updated = NowSeconds();
        g_activeId = src.id;
        g_notes.push_back(std::move(src));
        SaveNotes();
    }

    void DeleteActiveNote()
    {
        const int idx = ActiveIndex();
        if (idx >= 0 && idx < (int)g_notes.size())
            g_notes.erase(g_notes.begin() + idx);
        if (g_notes.empty()) g_notes.push_back(MakeDefaultNote());
        const int nextIdx = std::clamp(idx, 0, (int)g_notes.size() - 1);
        g_activeId = g_notes[nextIdx].id;
        StopEditing();
        SaveNotes();
    }

    bool DrawToolbar(float width, bool newNotePopup)
    {
        bool hovered = false;
        std::vector<const char*> names;
        names.reserve(g_notes.size());
        for (const Note& n : g_notes) names.push_back(n.title.c_str());

        const float sc = Gw2Ui::TextScale();
        const float btn = 18.f;
        const float gap = 5.f * sc;
        const float actionsW = btn * 4.f + gap * 4.f;
        const float ddW = std::max(72.f, width - actionsW);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        int sel = std::max(0, ActiveIndex());
        if (Gw2Ui::Dropdown("##qnsel", names.data(), (int)names.size(), &sel, ddW))
        {
            sel = std::clamp(sel, 0, (int)g_notes.size() - 1);
            g_activeId = g_notes[sel].id;
            StopEditing();
            SaveNotes();
        }
        hovered = hovered || ImGui::IsItemHovered();
        const float rowH = ImGui::GetItemRectSize().y;
        float x = p.x + ddW + gap;
        const float y = p.y + (rowH - btn) * 0.5f;
        if (Gw2Ui::SmallIconButton("##qnadd", ImVec2(x, y), btn, Gw2Ui::IconBtn::Plus, false, "Create note")) CreateNote(newNotePopup);
        hovered = hovered || ImGui::IsItemHovered(); x += btn + gap;
        if (Gw2Ui::SmallIconButton("##qnren", ImVec2(x, y), btn, Gw2Ui::IconBtn::Pencil, false, "Rename note"))
        {
            std::snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", ActiveNote().title.c_str());
            g_renameRequest = true;
        }
        hovered = hovered || ImGui::IsItemHovered(); x += btn + gap;
        if (Gw2Ui::SmallIconButton("##qndup", ImVec2(x, y), btn, Gw2Ui::IconBtn::Duplicate, false, "Duplicate note")) DuplicateNote();
        hovered = hovered || ImGui::IsItemHovered(); x += btn + gap;
        if (Gw2Ui::SmallIconButton("##qndel", ImVec2(x, y), btn, Gw2Ui::IconBtn::Trash, false, "Delete note")) g_deleteRequest = true;
        hovered = hovered || ImGui::IsItemHovered();

        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + 5.f * sc));

        if (g_renameRequest) { ImGui::OpenPopup("##qnrename"); g_renameRequest = false; }
        if (ImGui::BeginPopup("##qnrename"))
        {
            Gw2Ui::TextBox("##qnrname", g_renameBuf, sizeof(g_renameBuf), 230.f);
            const bool enter = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter), false) ||
                               ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeyPadEnter), false);
            if (Gw2Ui::ActionButton("Rename", 96.f, 26.f, Gw2Ui::ActionButtonVariant::Primary) || enter)
            {
                const std::string t = Trim(g_renameBuf);
                if (!t.empty())
                {
                    ActiveNote().title = t;
                    Mutated(ActiveNote());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::ActionButton("Cancel", 90.f, 26.f)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (g_deleteRequest) { ImGui::OpenPopup("##qndelete"); g_deleteRequest = false; }
        if (ImGui::BeginPopup("##qndelete"))
        {
            Gw2Ui::Label("Delete this note?", IM_COL32(234, 228, 210, 255), false, nullptr, 18.f);
            ImGui::Spacing();
            if (Gw2Ui::ActionButton("Delete", 96.f, 26.f, Gw2Ui::ActionButtonVariant::Danger)) { DeleteActiveNote(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(0.f, 8.f);
            if (Gw2Ui::ActionButton("Cancel", 90.f, 26.f)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return hovered;
    }

    bool DrawNativeEditor(const char* id, Note& n, ImVec2 size, float fs, bool& focusFlag)
    {
        const bool changed = InputTextMultilineString(id, n.body, size, fs, focusFlag);
        focusFlag = false;
        if (changed) Mutated(n);
        return changed;
    }

    bool DrawEditorCloseButton(ImDrawList* dl, ImVec2 at, float size)
    {
        ImGui::SetCursorScreenPos(at);
        ImGui::InvisibleButton("##qnclose", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        const ImU32 col = hovered ? IM_COL32(255, 216, 176, 255) : IM_COL32(210, 196, 160, 235);
        const float p = 5.f;
        dl->AddLine(ImVec2(at.x + p, at.y + p), ImVec2(at.x + size - p, at.y + size - p), col, 1.7f);
        dl->AddLine(ImVec2(at.x + size - p, at.y + p), ImVec2(at.x + p, at.y + size - p), col, 1.7f);
        if (hovered)
            dl->AddRect(at, ImVec2(at.x + size, at.y + size), IM_COL32(170, 126, 64, 130), 2.f);
        return clicked;
    }

    void DrawPopupEditor(App& app, Note& n, float fs)
    {
        if (g_popupRequest)
        {
            g_popupWasOpen = true;
            g_popupRequest = false;
        }
        if (!g_popupWasOpen || g_editingId != n.id)
            return;

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 defaultSize(720.f, 520.f);
        const ImVec2 savedSize(
            std::clamp(app.config.quickNotesEditorW, 360.f, std::max(360.f, io.DisplaySize.x)),
            std::clamp(app.config.quickNotesEditorH, 260.f, std::max(260.f, io.DisplaySize.y)));
        ImGui::SetNextWindowSize(app.config.quickNotesRememberEditorSize ? savedSize : defaultSize, ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(360.f, 260.f), ImVec2(io.DisplaySize.x, io.DisplaySize.y));
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        bool open = true;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
        if (ImGui::Begin("##qnpopupedit", &open, flags))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 ws = ImGui::GetWindowSize();
            if (app.config.quickNotesRememberEditorSize)
            {
                const float newW = std::clamp(ws.x, 360.f, std::max(360.f, io.DisplaySize.x));
                const float newH = std::clamp(ws.y, 260.f, std::max(260.f, io.DisplaySize.y));
                if (std::fabs(newW - app.config.quickNotesEditorW) > 0.5f || std::fabs(newH - app.config.quickNotesEditorH) > 0.5f)
                {
                    app.config.quickNotesEditorW = newW;
                    app.config.quickNotesEditorH = newH;
                    app.settingsDirty = true;
                }
            }
            const float headerH = 42.f;
            const float pad = 12.f;
            const float footerH = 38.f;
            const ImVec2 br(wp.x + ws.x, wp.y + ws.y);

            dl->AddRectFilled(wp, br, IM_COL32(8, 8, 7, 238), 5.f);
            dl->AddRectFilledMultiColor(wp, ImVec2(br.x, wp.y + headerH),
                IM_COL32(48, 38, 20, 245), IM_COL32(18, 15, 10, 235),
                IM_COL32(7, 7, 6, 225), IM_COL32(22, 18, 11, 235));
            dl->AddRect(wp, br, IM_COL32(133, 103, 48, 205), 5.f, 0, 1.3f);
            dl->AddLine(ImVec2(wp.x + pad, wp.y + headerH), ImVec2(br.x - pad, wp.y + headerH), IM_COL32(176, 135, 62, 150), 1.f);

            ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y));
            ImGui::InvisibleButton("##qndrag", ImVec2(std::max(1.f, ws.x - 34.f), headerH));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const ImVec2 d = ImGui::GetIO().MouseDelta;
                ImGui::SetWindowPos(ImVec2(wp.x + d.x, wp.y + d.y));
            }

            Gw2Ui::LabelDL(dl, ImVec2(wp.x + pad, wp.y + 2.f), ImVec2(br.x - 38.f, wp.y + headerH),
                           n.title.c_str(), Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle,
                           Gw2Ui::kGold, true, nullptr, 20.f, 0.f, 1.25f);
            if (DrawEditorCloseButton(dl, ImVec2(br.x - 29.f, wp.y + 10.f), 18.f))
                open = false;

            const ImVec2 editorPos(wp.x + pad, wp.y + headerH + pad);
            const ImVec2 editorSize(std::max(80.f, ws.x - pad * 2.f), std::max(80.f, ws.y - headerH - footerH - pad * 2.f));
            ImGui::SetCursorScreenPos(editorPos);
            DrawNativeEditor("##qnpopupeditor", n, editorSize, fs, g_focusPopup);

            const ImVec2 donePos(wp.x + pad, br.y - footerH + 6.f);
            ImGui::SetCursorScreenPos(donePos);
            if (Gw2Ui::ActionButton("Done", 96.f, 26.f, Gw2Ui::ActionButtonVariant::Primary))
            {
                StopEditing();
                open = false;
            }

            const ImU32 gripCol = IM_COL32(170, 140, 80, 160);
            for (int i = 0; i < 3; ++i)
            {
                const float o = 5.f + i * 4.f;
                dl->AddLine(ImVec2(br.x - o, br.y - 2.f), ImVec2(br.x - 2.f, br.y - o), gripCol, 1.4f);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        if (!open && g_editingId == n.id)
            StopEditing();
    }
}

void DashW::QuickNotes(App& app, float w)
{
    LoadNotes();
    EnsureNote();

    const float fs = kFontSizePx[std::clamp(app.config.quickNotesTextSize, 0, kFontSizeCount - 1)];
    const float sc = Gw2Ui::TextScale();
    const float bodyH = std::max(108.f, 116.f * sc);
    const bool popupForWidth = app.config.quickNotesEditorMode == EditorPopupOnly || DashUtil::Narrow(w);
    const bool toolbarHovered = DrawToolbar(w, popupForWidth);
    Note& note = ActiveNote();

    if (g_editingId == note.id && popupForWidth && !g_popupWasOpen && !g_popupRequest)
    {
        g_popupRequest = true;
        g_focusPopup = true;
    }

    if (g_editingId == note.id && !popupForWidth)
    {
        DrawNativeEditor("##qninlineeditor", note, ImVec2(w, bodyH), fs, g_focusInline);
        if (ImGui::IsItemDeactivated())
            StopEditing();
    }
    else
    {
        const bool clickedBody = DrawMarkdownPreview(note, w, bodyH, fs);
        if (clickedBody && !toolbarHovered)
            StartEditing(note, popupForWidth);
    }

    DrawPopupEditor(app, note, fs);
}
