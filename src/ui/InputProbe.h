#pragma once

// Keyboard diagnostic behind config.inputProbe (Diagnostics > "Input probe"). It answers the one question
// static reading cannot: when a text field is focused but nothing types, WHERE does the keystroke stop?
//
// Two levels:
//   * FRAME level  -- messages our WndProc hook is handed vs characters that reach ImGui's queue.
//   * PER-BOX level -- every Gw2Ui text box reports, as it is submitted, whether it holds ActiveId and how
//     many characters were in the queue AT THAT MOMENT. ImGui's InputText DRAINS the queue when it is active
//     (imgui_widgets.cpp: `io.InputQueueCharacters.resize(0)`), so submission order matters: this shows which
//     box saw the characters and which one found the queue already empty.
//
// Everything no-ops unless SetEnabled(true). Implemented in ui/tabs/DiagnosticsSection.cpp (with the readout).
namespace InputProbe
{
    void SetEnabled(bool on);   // once per frame from the composition root; gates all collection
    bool Enabled();

    // From the Nexus WndProc hook, for EVERY message, before we consume anything.
    void NoteMessage(unsigned msg);

    // Once per frame at the TOP of Render, before any UI is submitted -- ImGui's character queue is drained by
    // whichever InputText consumes it, so sampling later would read zero and prove nothing.
    void SampleFrame();

    // From a Gw2Ui text box, immediately AFTER its ImGui::InputText call. `queueOnEntry` must be sampled
    // BEFORE the InputText (that call is what drains it).
    void NoteTextBox(const char* id, unsigned imguiId, bool active, int queueOnEntry, bool changed);
}
