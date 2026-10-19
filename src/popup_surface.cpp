#include "popup_surface.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace popup_surface {
namespace {
constexpr wchar_t kShadowClass[] = L"WinPPlus.MenuShadow";
constexpr int kPadding = 32, kRadius = 10;

class Bitmap {
public:
    Bitmap(int width, int height) : width(width), height(height) {
        dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (dc && bitmap) previous = SelectObject(dc, bitmap);
    }
    ~Bitmap() {
        if (previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    explicit operator bool() const { return dc && bitmap && pixels; }
    bool Present(HWND window) const {
        POINT source{};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        return UpdateLayeredWindow(window, nullptr, nullptr, &size, dc, &source, 0, &blend, ULW_ALPHA) != FALSE;
    }
    HDC dc{};
    std::uint32_t* pixels{};
    int width{}, height{};
private:
    HBITMAP bitmap{};
    HGDIOBJ previous{};
};

double Distance(double x, double y, double width, double height, double radius) {
    const double dx = std::abs(x - width / 2) - width / 2 + radius;
    const double dy = std::abs(y - height / 2) - height / 2 + radius;
    return std::hypot(std::max(dx, 0.0), std::max(dy, 0.0)) + std::min(std::max(dx, dy), 0.0) - radius;
}

void PaintShadow(HWND shadow, int bodyWidth, int bodyHeight, UINT dpi) {
    const double scale = dpi / 96.0;
    const int padding = MulDiv(kPadding, static_cast<int>(dpi), 96);
    Bitmap surface(bodyWidth + 2 * padding, bodyHeight + 2 * padding);
    if (!surface) return;
    for (int y = 0; y < surface.height; ++y) {
        for (int x = 0; x < surface.width; ++x) {
            const double distance = Distance(x + 0.5 - padding, y + 0.5 - padding - 3 * scale,
                                             bodyWidth, bodyHeight, kRadius * scale);
            // Two soft lobes give a broad ambient shadow and a lighter contact
            // edge. The alpha is generated from the same rounded outline as the card.
            const double alpha = 0.18 * 0.5 * std::erfc(distance / (std::sqrt(2.0) * 8 * scale)) +
                                 0.08 * 0.5 * std::erfc(distance / (std::sqrt(2.0) * 2 * scale));
            surface.pixels[y * surface.width + x] = static_cast<std::uint32_t>(std::lround(alpha * 255)) << 24;
        }
    }
    surface.Present(shadow);
}
}

bool RegisterShadowClass(HINSTANCE instance) {
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = kShadowClass;
    return RegisterClassW(&cls) != 0;
}

HWND CreateShadow(HINSTANCE instance, const RECT& body) {
    // The card owns its focus; this separate owner window stays underneath it
    // and passes all mouse input through to the desktop or another application.
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                           kShadowClass, L"", WS_POPUP, body.left, body.top,
                           body.right - body.left, body.bottom - body.top, nullptr, nullptr, instance, nullptr);
}

void UpdateShadow(HWND menu) {
    HWND shadow = GetWindow(menu, GW_OWNER);
    if (!shadow) return;
    RECT body{};
    if (!GetWindowRect(menu, &body)) return;
    const UINT dpi = GetDpiForWindow(menu);
    const int padding = MulDiv(kPadding, static_cast<int>(dpi), 96);
    const int width = body.right - body.left, height = body.bottom - body.top;
    SetWindowPos(shadow, nullptr, body.left - padding, body.top - padding, width + 2 * padding, height + 2 * padding,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    PaintShadow(shadow, width, height, dpi);
}

bool PaintCard(HWND menu, HDC content) {
    RECT rect{};
    GetClientRect(menu, &rect);
    Bitmap surface(rect.right, rect.bottom);
    if (!surface || !BitBlt(surface.dc, 0, 0, rect.right, rect.bottom, content, 0, 0, SRCCOPY)) return false;
    GdiFlush();
    const double scale = GetDpiForWindow(menu) / 96.0;
    for (int y = 0; y < surface.height; ++y) {
        for (int x = 0; x < surface.width; ++x) {
            auto& pixel = surface.pixels[y * surface.width + x];
            const double distance = Distance(x + 0.5, y + 0.5, surface.width, surface.height, kRadius * scale);
            const double coverage = std::clamp(0.5 - distance, 0.0, 1.0);
            const double inside = std::clamp(0.5 - distance - scale, 0.0, 1.0);
            const double border = coverage - inside;
            const auto blue = static_cast<std::uint32_t>(std::lround((pixel & 255) * inside + 238 * border));
            const auto green = static_cast<std::uint32_t>(std::lround(((pixel >> 8) & 255) * inside + 234 * border));
            const auto red = static_cast<std::uint32_t>(std::lround(((pixel >> 16) & 255) * inside + 232 * border));
            pixel = (static_cast<std::uint32_t>(std::lround(coverage * 255)) << 24) | (red << 16) | (green << 8) | blue;
        }
    }
    return surface.Present(menu);
}
}
