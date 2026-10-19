#include "modern_ui.h"
#include "popup_surface.h"

#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <string>

namespace modern_ui {
namespace {
constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kInk = RGB(35, 39, 45);
constexpr COLORREF kMuted = RGB(109, 116, 125);
constexpr COLORREF kAccent = RGB(53, 102, 207);
constexpr COLORREF kAccentLight = RGB(240, 244, 252);
constexpr COLORREF kBorder = RGB(232, 234, 238);
constexpr COLORREF kHover = RGB(245, 246, 248);

struct Canvas {
    HWND window;
    HDC dc;
    UINT dpi;
    int width;
    int height;

    int S(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    RECT R(int x, int y, int w, int h) const {
        return {S(x), S(y), S(x + w), S(y + h)};
    }
};

class BufferedPaint {
public:
    BufferedPaint(HWND hwnd, HDC destination, bool layered = false) : window_(hwnd), target_(destination), layered_(layered) {
        GetClientRect(hwnd, &rect_);
        dc_ = CreateCompatibleDC(destination);
        bitmap_ = CreateCompatibleBitmap(destination, std::max(1L, rect_.right), std::max(1L, rect_.bottom));
        previous_ = SelectObject(dc_, bitmap_);
    }
    ~BufferedPaint() {
        if (layered_)
            popup_surface::PaintCard(window_, dc_);
        else
            BitBlt(target_, 0, 0, rect_.right, rect_.bottom, dc_, 0, 0, SRCCOPY);
        SelectObject(dc_, previous_);
        DeleteObject(bitmap_);
        DeleteDC(dc_);
    }
    Canvas Get(HWND hwnd) const {
        const UINT dpi = GetDpiForWindow(hwnd);
        return {hwnd, dc_, dpi, MulDiv(rect_.right, 96, static_cast<int>(dpi)),
            MulDiv(rect_.bottom, 96, static_cast<int>(dpi))};
    }
private:
    HWND window_{};
    HDC target_{};
    bool layered_{};
    HDC dc_{};
    HBITMAP bitmap_{};
    HGDIOBJ previous_{};
    RECT rect_{};
};

void Fill(const Canvas& c, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(c.dc, &rect, brush);
    DeleteObject(brush);
}

void Rounded(const Canvas& c, RECT rect, int radius, COLORREF fill,
    COLORREF border, int borderWidth = 1) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, c.S(borderWidth)), border);
    HGDIOBJ oldBrush = SelectObject(c.dc, brush);
    HGDIOBJ oldPen = SelectObject(c.dc, pen);
    RoundRect(c.dc, rect.left, rect.top, rect.right, rect.bottom, c.S(radius * 2), c.S(radius * 2));
    SelectObject(c.dc, oldPen);
    SelectObject(c.dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void Line(const Canvas& c, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, c.S(width)), color);
    HGDIOBJ old = SelectObject(c.dc, pen);
    MoveToEx(c.dc, c.S(x1), c.S(y1), nullptr);
    LineTo(c.dc, c.S(x2), c.S(y2));
    SelectObject(c.dc, old);
    DeleteObject(pen);
}

void Text(const Canvas& c, std::wstring_view value, RECT rect, int size,
    COLORREF color, bool bold = false, UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) {
    HFONT font = CreateFontW(-c.S(size), 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old = SelectObject(c.dc, font);
    SetBkMode(c.dc, TRANSPARENT);
    SetTextColor(c.dc, color);
    DrawTextW(c.dc, value.data(), static_cast<int>(value.size()), &rect, flags);
    SelectObject(c.dc, old);
    DeleteObject(font);
}

void Circle(const Canvas& c, RECT rect, COLORREF fill, COLORREF border, int thickness = 1) {
    struct Runtime {
        ULONG_PTR token{};
        Runtime() { Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&token, &input, nullptr); }
        ~Runtime() { if (token) Gdiplus::GdiplusShutdown(token); }
    };
    static Runtime runtime;
    Gdiplus::Graphics graphics(c.dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const auto color = [](COLORREF value) { return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value)); };
    const float stroke = static_cast<float>(std::max(1, c.S(thickness)));
    const Gdiplus::RectF ellipse(static_cast<float>(rect.left) + stroke / 2, static_cast<float>(rect.top) + stroke / 2,
                                static_cast<float>(rect.right - rect.left) - stroke,
                                static_cast<float>(rect.bottom - rect.top) - stroke);
    Gdiplus::SolidBrush brush(color(fill));
    Gdiplus::Pen pen(color(border), stroke);
    graphics.FillEllipse(&brush, ellipse);
    graphics.DrawEllipse(&pen, ellipse);
}

void Outline(const Canvas& c, RECT rect, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, c.S(2)), color);
    HGDIOBJ oldPen = SelectObject(c.dc, pen);
    HGDIOBJ oldBrush = SelectObject(c.dc, GetStockObject(NULL_BRUSH));
    RoundRect(c.dc, rect.left, rect.top, rect.right, rect.bottom, c.S(5), c.S(5));
    SelectObject(c.dc, oldBrush);
    SelectObject(c.dc, oldPen);
    DeleteObject(pen);
}

void MenuDisplayGlyph(const Canvas& c, int x, int y, int kind, bool accented) {
    const COLORREF color = accented ? kAccent : kInk;
    if (kind == 0) {
        Outline(c, c.R(x + 4, y + 3, 24, 17), color);
        Line(c, x + 4, y + 20, x, y + 26, color, 2);
        Line(c, x, y + 26, x + 32, y + 26, color, 2);
        Line(c, x + 32, y + 26, x + 28, y + 20, color, 2);
    } else if (kind == 1) {
        Outline(c, c.R(x + 9, y + 8, 22, 23), color);
        Line(c, x + 3, y + 23, x + 3, y + 2, color, 2);
        Line(c, x + 3, y + 2, x + 23, y + 2, color, 2);
    } else if (kind == 2) {
        Outline(c, c.R(x + 2, y + 2, 28, 28), color);
        Line(c, x + 16, y + 3, x + 16, y + 29, color, 2);
    } else {
        Outline(c, c.R(x + 2, y + 2, 28, 21), color);
        Line(c, x + 16, y + 23, x + 16, y + 30, color, 2);
        Line(c, x + 10, y + 31, x + 22, y + 31, color, 2);
    }
}

void MenuLabel(const Canvas& c, std::wstring_view value, RECT rect, COLORREF color) {
    HFONT font = CreateFontW(-c.S(13), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old = SelectObject(c.dc, font);
    SetBkMode(c.dc, TRANSPARENT);
    SetTextColor(c.dc, color);
    constexpr UINT flags = DT_CENTER | DT_WORDBREAK | DT_NOPREFIX;
    RECT measured = rect;
    DrawTextW(c.dc, value.data(), static_cast<int>(value.size()), &measured, flags | DT_CALCRECT);
    rect.top += std::max(0L, (rect.bottom - rect.top - (measured.bottom - measured.top)) / 2);
    DrawTextW(c.dc, value.data(), static_cast<int>(value.size()), &rect, flags);
    SelectObject(c.dc, old);
    DeleteObject(font);
}

void Check(const Canvas& c, int x, int y, COLORREF color) {
    Line(c, x, y + 5, x + 4, y + 9, color, 2);
    Line(c, x + 4, y + 9, x + 12, y + 1, color, 2);
}

RECT SettingsRect(const Canvas& c, int id) {
    switch (id) {
    case 100: return c.R(24, 86, c.width - 48, 64);
    case 101: return c.R(24, 150, c.width - 48, 64);
    case 102: return c.R(24, 214, c.width - 48, 64);
    case 103: return c.R(24, 302, 165, 32);
    case 104: return c.R(c.width - 144, 302, 120, 32);
    default: return {};
    }
}

RECT MenuRow(const Canvas& c, int index) {
    constexpr int margin = 18;
    constexpr int gap = 6;
    const int width = (c.width - margin * 2 - gap * 3) / 4;
    return c.R(margin + index * (width + gap), 52, width, 112);
}

}

int Scale(HWND hwnd, int dip) { return MulDiv(dip, static_cast<int>(GetDpiForWindow(hwnd)), 96); }

RECT GetSettingsControlRect(HWND hwnd, int controlId) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT dpi = GetDpiForWindow(hwnd);
    const Canvas c{hwnd, nullptr, dpi, MulDiv(client.right, 96, static_cast<int>(dpi)),
        MulDiv(client.bottom, 96, static_cast<int>(dpi))};
    return SettingsRect(c, controlId);
}

int SettingsHitTest(HWND hwnd, POINT point) {
    for (int id : {100, 101, 102, 103, 104}) {
        RECT rect = GetSettingsControlRect(hwnd, id);
        if (PtInRect(&rect, point)) return id;
    }
    return 0;
}

int MenuHitTest(HWND hwnd, POINT point) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT dpi = GetDpiForWindow(hwnd);
    const Canvas c{hwnd, nullptr, dpi, MulDiv(client.right, 96, static_cast<int>(dpi)),
        MulDiv(client.bottom, 96, static_cast<int>(dpi))};
    for (int i = 0; i < 4; ++i) {
        RECT rect = MenuRow(c, i);
        if (PtInRect(&rect, point)) return i;
    }
    return -1;
}

bool MenuCloseHitTest(HWND hwnd, POINT point) {
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT rect{client.right - Scale(hwnd, 44), Scale(hwnd, 8),
        client.right - Scale(hwnd, 10), Scale(hwnd, 42)};
    return PtInRect(&rect, point) != FALSE;
}

void DrawSettings(HWND hwnd, HDC dc, bool chinese, bool optimize, bool custom,
    bool startup, int hovered, std::wstring_view status, std::wstring_view startupHint) {
    BufferedPaint paint(hwnd, dc);
    const Canvas c = paint.Get(hwnd);
    Fill(c, c.R(0, 0, c.width, c.height), kWhite);
    Text(c, chinese ? L"投屏设置" : L"Projection settings", c.R(24, 20, c.width - 48, 30), 23, kInk, true);
    Text(c, chinese ? L"按下 Win + P 时" : L"When you press Win + P", c.R(24, 62, c.width - 48, 18), 12, kMuted);
    const std::array<std::wstring_view, 3> titles = chinese
        ? std::array<std::wstring_view, 3>{L"使用系统默认菜单", L"让原生菜单跟随活动屏幕", L"使用自定义居中菜单"}
        : std::array<std::wstring_view, 3>{L"Use the default Windows menu", L"Follow the active display", L"Use the centered custom menu"};
    const std::array<std::wstring_view, 3> descriptions = chinese
        ? std::array<std::wstring_view, 3>{L"保留 Windows 原有的打开方式", L"在活动窗口所在的屏幕显示系统菜单", L"在每个活动屏幕显示，支持键盘与鼠标"}
        : std::array<std::wstring_view, 3>{L"Keep the original Windows behavior", L"Show the native menu on the active window’s display", L"On every active display, with keyboard and mouse controls"};
    for (int i = 0; i < 3; ++i) {
        const int id = 100 + i, top = 86 + 64 * i;
        const bool enabled = i == 0 ? !optimize && !custom : i == 1 ? optimize : custom;
        if (hovered == id) Rounded(c, SettingsRect(c, id), 5, kHover, kHover);
        Circle(c, c.R(32, top + 24, 16, 16), kWhite, enabled ? kAccent : RGB(175, 181, 190), enabled ? 4 : 1);
        Text(c, titles[static_cast<size_t>(i)], c.R(60, top + 9, c.width - 108, 22), 14, kInk);
        Text(c, descriptions[static_cast<size_t>(i)], c.R(60, top + 33, c.width - 84, 18), 12, kMuted);
        if (enabled) Check(c, c.width - 45, top + 15, kAccent);
    }
    Line(c, 24, 290, c.width - 24, 290, kBorder);
    Rounded(c, c.R(25, 310, 16, 16), 3, startup ? kAccent : kWhite, startup ? kAccent : RGB(175, 181, 190));
    if (startup) Check(c, 27, 312, kWhite);
    Text(c, chinese ? L"开机启动" : L"Start with Windows", c.R(49, 302, 140, 32), 12, kInk);
    Rounded(c, SettingsRect(c, 104), 4, hovered == 104 ? kHover : kWhite, kBorder);
    Text(c, chinese ? L"简体中文" : L"English", c.R(c.width - 132, 302, 87, 32), 12, kInk);
    Line(c, c.width - 43, 316, c.width - 39, 320, kMuted);
    Line(c, c.width - 39, 320, c.width - 35, 316, kMuted);
    constexpr std::array<std::wstring_view, 6> routineStatus{L"自定义菜单已启用", L"Custom menu enabled",
        L"系统菜单优化已启用", L"System menu optimization enabled", L"使用 Windows 默认菜单", L"Using the default Windows menu"};
    if (!startupHint.empty())
        Text(c, startupHint, c.R(24, 346, c.width - 48, 28), 11, kMuted, false, DT_LEFT | DT_WORDBREAK);
    else if (std::find(routineStatus.begin(), routineStatus.end(), status) == routineStatus.end())
        Text(c, status, c.R(24, 346, c.width - 48, 28), 11, kMuted, false, DT_LEFT | DT_WORDBREAK);
}

void DrawMenu(HWND hwnd, HDC dc, bool chinese, int selected, int current, int hovered, bool preview) {
    BufferedPaint paint(hwnd, dc, true);
    const Canvas c = paint.Get(hwnd);
    Fill(c, c.R(0, 0, c.width, c.height), kWhite);
    Text(c, chinese ? L"投屏" : L"Project", c.R(24, 14, 90, 28), 17, kInk, true);
    if (preview) Text(c, chinese ? L"预览 · 不切换显示模式" : L"Preview · no display changes",
        c.R(chinese ? 82 : 102, 15, c.width - 250, 26), 11, kMuted);
    Text(c, L"Win + P", c.R(c.width - 125, 15, 64, 26), 11, kMuted, false, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    if (hovered == 4) Rounded(c, c.R(c.width - 44, 8, 34, 34), 5, kHover, kHover);
    Line(c, c.width - 31, 21, c.width - 23, 29, kMuted);
    Line(c, c.width - 23, 21, c.width - 31, 29, kMuted);
    constexpr std::array<std::wstring_view, 4> zh{L"仅电脑屏幕", L"复制", L"扩展", L"仅第二屏幕"};
    constexpr std::array<std::wstring_view, 4> en{L"PC screen only", L"Duplicate", L"Extend", L"Second screen only"};
    for (int i = 0; i < 4; ++i) {
        const bool chosen = i == selected;
        const bool active = i == current;
        const RECT card = MenuRow(c, i);
        const int left = MulDiv(card.left, 96, static_cast<int>(c.dpi));
        const int width = MulDiv(card.right - card.left, 96, static_cast<int>(c.dpi));
        const COLORREF fill = chosen ? kAccentLight : (i == hovered ? kHover : kWhite);
        if (chosen || i == hovered) Rounded(c, card, 5, fill, fill);
        const int top = MulDiv(card.top, 96, static_cast<int>(c.dpi));
        const int height = MulDiv(card.bottom - card.top, 96, static_cast<int>(c.dpi));
        const int contentTop = top + (height - 72) / 2;
        MenuDisplayGlyph(c, left + (width - 32) / 2, contentTop, i, chosen);
        MenuLabel(c, (chinese ? zh : en)[static_cast<size_t>(i)],
            c.R(left + 6, contentTop + 44, width - 12, 28), chosen ? kAccent : kInk);
        if (active)
            Fill(c, c.R(left + (width - 28) / 2, top + height - 2, 28, 2), kAccent);
    }
    Line(c, 24, 177, c.width - 24, 177, kBorder);
    std::wstring state;
    const auto& labels = chinese ? zh : en;
    state += current >= 0 && current < 4 ? labels[static_cast<size_t>(current)] : (chinese ? L"自定义布局" : L"Custom layout");
    if (selected >= 0 && selected < 4 && selected != current) {
        state += L" → ";
        state += labels[static_cast<size_t>(selected)];
    }
    Text(c, state, c.R(24, 185, 302, 18), 11, kMuted);
    Text(c, chinese ? L"← → 选择   Enter 确认   Esc 关闭" : L"← → select   Enter confirm   Esc close",
        c.R(330, 185, c.width - 354, 18), 11, kMuted, false, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
}

HICON CreateAppIcon(int size, bool light) {
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(light ? 102 : 101),
                                         IMAGE_ICON, size, size, 0));
}
}
