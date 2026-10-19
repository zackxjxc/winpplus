#include "popup_surface.h"

#include <dwmapi.h>

namespace popup_surface {
bool ApplyNativeFrame(HWND menu) {
    // Retain a DWM frame for the system shadow. WM_NCCALCSIZE gives the
    // complete rectangle to our client; there is no shadow bitmap or region.
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    const HRESULT rendering = DwmSetWindowAttribute(menu, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    const MARGINS margins{1, 1, 1, 1};
    const HRESULT frame = DwmExtendFrameIntoClientArea(menu, &margins);
    const BOOL dark = FALSE;
    DwmSetWindowAttribute(menu, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // Capability detection selects Windows 11 native corners. Unsupported
    // attributes leave Windows 10 with its native square frame and shadow.
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    if (SUCCEEDED(DwmSetWindowAttribute(menu, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners)))) {
        const COLORREF border = DWMWA_COLOR_NONE;
        DwmSetWindowAttribute(menu, DWMWA_BORDER_COLOR, &border, sizeof(border));
    }
    return SUCCEEDED(rendering) && SUCCEEDED(frame);
}
}
