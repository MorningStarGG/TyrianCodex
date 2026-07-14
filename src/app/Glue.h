#pragma once
class App;

// Functions DEFINED in entry.cpp that the UI translation units call back into. They stay in entry.cpp because
// they are Nexus / lifecycle glue (the keybind re-registration references the file-static On* InputBind
// handlers, which are C callbacks). Declared here so ui/SettingsWindow.cpp can trigger a re-register when a
// keybind setting changes; the App is passed in explicitly (the bind strings are read from app.config).
void ApplyKeybinds(App& app);
