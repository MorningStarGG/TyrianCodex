#include "Widgets.h"
#include "ui/Gw2Ui.h"
#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <string>

// A simple four-function calculator (+ - x /), GW2-styled with our buttons. Immediate-execution (no operator
// precedence), like a basic pocket calculator. Transient state (resets on reload) -- a scratch tool.
namespace
{
    std::string g_entry;        // the number currently being typed
    double      g_acc   = 0.0;  // running accumulator
    char        g_op    = 0;    // pending operator: 0 / '+' '-' '*' '/'
    bool        g_fresh = true; // next digit starts a new entry (after = or an operator)
    bool        g_err   = false;

    std::string Fmt(double v) { char b[48]; std::snprintf(b, sizeof(b), "%.10g", v); return b; }

    void Digit(char c) { if (g_fresh) { g_entry.clear(); g_fresh = false; g_err = false; } g_entry += c; }
    void Dot()         { if (g_fresh) { g_entry = "0"; g_fresh = false; g_err = false; } if (g_entry.find('.') == std::string::npos) g_entry += '.'; }
    void Clear()       { g_entry.clear(); g_acc = 0.0; g_op = 0; g_fresh = true; g_err = false; }
    void Back()        { if (!g_fresh && !g_entry.empty()) g_entry.pop_back(); }

    double Apply(double a, char op, double b)
    {
        switch (op) { case '+': return a + b; case '-': return a - b; case '*': return a * b;
                      case '/': if (b == 0.0) { g_err = true; return 0.0; } return a / b; default: return b; }
    }
    void Operator(char c)
    {
        const double cur = g_entry.empty() ? g_acc : std::atof(g_entry.c_str());
        g_acc = (g_op && !g_entry.empty()) ? Apply(g_acc, g_op, cur) : cur;
        g_op = c; g_entry.clear(); g_fresh = true;
    }
    void Equals()
    {
        if (g_op && !g_entry.empty()) { g_acc = Apply(g_acc, g_op, std::atof(g_entry.c_str())); g_op = 0; }
        else if (!g_entry.empty())    { g_acc = std::atof(g_entry.c_str()); }
        g_entry.clear(); g_fresh = true;
    }
}

void DashW::Calculator(App& /*app*/, float w)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // display (right-aligned, with the pending op shown faintly on the left)
    const float dispH = 32.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, dispH));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + dispH), IM_COL32(6, 8, 7, 224), 4.f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + dispH), IM_COL32(150, 124, 70, 160), 4.f);
    if (g_op) { char o[2] = { g_op == '*' ? 'x' : g_op, 0 }; Gw2Ui::LabelDL(dl, ImVec2(p.x + 8.f, p.y), ImVec2(p.x + 24.f, p.y + dispH), o, Gw2Ui::HAlign::Left, Gw2Ui::VAlign::Middle, IM_COL32(180, 150, 100, 220), false, nullptr, 16.f); }
    const std::string disp = g_err ? "Error" : (!g_entry.empty() ? g_entry : Fmt(g_acc));
    Gw2Ui::LabelDL(dl, ImVec2(p.x + 26.f, p.y), ImVec2(p.x + w - 8.f, p.y + dispH), disp.c_str(),
                   Gw2Ui::HAlign::Right, Gw2Ui::VAlign::Middle, IM_COL32(238, 232, 212, 255), false, nullptr, 20.f);
    ImGui::Spacing();

    const float gap = 5.f;
    const float bw = (w - 3.f * gap) / 4.f;
    const float bh = 27.f;
    auto B = [&](const char* lbl, Gw2Ui::ActionButtonVariant v = Gw2Ui::ActionButtonVariant::Normal) { return Gw2Ui::ActionButton(lbl, bw, bh, v); };

    // Row 1: C  DEL  /  x
    if (B("C"))   Clear();      ImGui::SameLine(0.f, gap);
    if (B("DEL")) Back();       ImGui::SameLine(0.f, gap);
    if (B("/"))   Operator('/'); ImGui::SameLine(0.f, gap);
    if (B("x"))   Operator('*');
    // Row 2: 7 8 9 -
    if (B("7")) Digit('7'); ImGui::SameLine(0.f, gap);
    if (B("8")) Digit('8'); ImGui::SameLine(0.f, gap);
    if (B("9")) Digit('9'); ImGui::SameLine(0.f, gap);
    if (B("-")) Operator('-');
    // Row 3: 4 5 6 +
    if (B("4")) Digit('4'); ImGui::SameLine(0.f, gap);
    if (B("5")) Digit('5'); ImGui::SameLine(0.f, gap);
    if (B("6")) Digit('6'); ImGui::SameLine(0.f, gap);
    if (B("+")) Operator('+');
    // Row 4: 1 2 3 =
    if (B("1")) Digit('1'); ImGui::SameLine(0.f, gap);
    if (B("2")) Digit('2'); ImGui::SameLine(0.f, gap);
    if (B("3")) Digit('3'); ImGui::SameLine(0.f, gap);
    if (B("=", Gw2Ui::ActionButtonVariant::Primary)) Equals();
    // Row 5: 0 (wide) .
    if (Gw2Ui::ActionButton("0", bw * 2.f + gap, bh)) Digit('0'); ImGui::SameLine(0.f, gap);
    if (B(".")) Dot();
}
