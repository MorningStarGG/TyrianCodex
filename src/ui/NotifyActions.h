#pragma once
#include "ui/dashboard/Notify.h"
#include <string>

class App;

// ONE dispatcher for a clicked notification, shared by the toast stack and the dashboard log so a click means
// the same thing in both. Notify itself deliberately owns no App&; this is the UI-side half.
namespace NotifyActions
{
    void Dispatch(App& app, Notify::Action action, const std::string& payload);
}
