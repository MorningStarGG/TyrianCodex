#pragma once
#include <cstdint>
#include <vector>
class App;

// -----------------------------------------------------------------------------------------------------
// DashWidget: the data-driven dashboard widget interface (mirrors the Settings() table style). Each widget
// lives in its own .cpp under widgets/ and exposes ONE body-draw function; the registry (WidgetRegistry.cpp)
// lists them all in one place. The Dashboard draws the card chrome (header + drag grip + background) and
// hands the body the width it should lay out to -- a widget may render a single responsive layout, or branch
// internally to a purpose-built compact form when `width` is small (half-width). canHalf gates whether the
// dashboard ever pairs it two-per-row.
// -----------------------------------------------------------------------------------------------------
struct DashWidget
{
    const char* key;        // STABLE id (persistence + settings + layout); never change once shipped
    const char* title;      // header text
    uint32_t    icon;       // gw2dat asset id for the header (0 = none)
    bool        canHalf;    // may render at half width (two-per-row)
    bool        defaultOn;    // shown by default (reconcile uses this for newly-seen widgets)
    int         defaultSpan;  // default width when placed: 1 = full row, 2 = half (paired two-per-row)
    int         defaultOrder; // order among default-on widgets (0 = unspecified / not-default)
    void      (*draw)(App&, float width);   // body ONLY (the Dashboard supplies the control strip; for normal
                                            // widgets it also supplies the card frame)
    bool        selfFramed = false;  // widget draws its OWN card(s)/banner (e.g. reuses a viewer panel that
                                     // BeginCards). The Dashboard then must NOT wrap it in a card (that would
                                     // nest channel-splits and corrupt the draw list) -- it only adds the
                                     // control strip above. Defaulted so plain widgets are unchanged.
    bool        defaultHoverChrome = false;  // default chrome mode for a NEW widget: false = pinned header bar,
                                             // true = controls hidden until hover (e.g. the Zone Header banner)
};

const std::vector<DashWidget>& DashWidgets();          // the ONE registry, built once
const DashWidget*              FindDashWidget(const char* key);
