#pragma once

#include <string>

class App;

namespace Wiki
{
    void RenderReader(App& app);
    void OpenReader(App& app);
    void OpenWiki(App& app, const std::string& titleOrUrl);

    // Deferred, App-free open: queues a page to open + raises the reader on the next RenderReader frame.
    // For callers that have no App& in scope (e.g. item cells). Safe to call from any render-thread code.
    void RequestOpen(const std::string& titleOrUrl);
}
