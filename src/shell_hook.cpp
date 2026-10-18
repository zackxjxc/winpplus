#include <windows.h>
#include <shellscalingapi.h>
#include <MinHook.h>
#include <string>
#include <algorithm>
#include "native_route_state.h"

extern "C" __declspec(dllexport) volatile DWORD WinPPlusHookStatus = 0;

namespace {
using SetWindowPosFn = BOOL (WINAPI*)(HWND, HWND, int, int, int, int, UINT);
SetWindowPosFn originalSetWindowPos = nullptr;
thread_local bool routing = false;
HANDLE routeMapping{};
const volatile winpplus::NativeRouteState* routeState{};

void Log(const wchar_t* text) {
    wchar_t temp[MAX_PATH]{}; GetTempPathW(ARRAYSIZE(temp), temp);
    std::wstring path = std::wstring(temp) + L"WinPPlus.Hook.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st{}; GetLocalTime(&st); wchar_t line[1024]{};
    swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, text);
    const int characters = static_cast<int>(wcslen(line));
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, characters, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) { CloseHandle(file); return; }
    std::string utf8(static_cast<size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, line, characters, utf8.data(), bytes, nullptr, nullptr)) { CloseHandle(file); return; }
    DWORD written{}; WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr); CloseHandle(file);
}
bool IsProject(HWND hwnd) { wchar_t cls[128]{}; return hwnd && GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) && _wcsicmp(cls, L"ControlCenterWindow") == 0; }
UINT MonitorDpi(HMONITOR monitor) { UINT x=96,y=96; GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y); return x ? x : 96; }

bool ReadRoute(RECT& bounds) {
    if (!routeState) {
        routeMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, winpplus::kRouteStateMapping);
        if (!routeMapping) return false;
        routeState = static_cast<const volatile winpplus::NativeRouteState*>(
            MapViewOfFile(routeMapping, FILE_MAP_READ, 0, 0, sizeof(winpplus::NativeRouteState)));
        if (!routeState) { CloseHandle(routeMapping); routeMapping = nullptr; return false; }
    }
    if (!routeState->enabled) return false;
    MemoryBarrier();
    const ULONGLONG validUntil = routeState->validUntil;
    const ULONGLONG now = GetTickCount64();
    if (!validUntil || now >= validUntil || validUntil - now > winpplus::kRouteIntentLifetimeMs) return false;
    bounds.left = routeState->targetMonitorBounds.left;
    bounds.top = routeState->targetMonitorBounds.top;
    bounds.right = routeState->targetMonitorBounds.right;
    bounds.bottom = routeState->targetMonitorBounds.bottom;
    MemoryBarrier();
    return routeState->enabled && routeState->validUntil == validUntil && bounds.right > bounds.left && bounds.bottom > bounds.top;
}

BOOL WINAPI HookedSetWindowPos(HWND hwnd, HWND insertAfter, int x, int y, int cx, int cy, UINT flags) {
    const bool project = IsProject(hwnd);
    if (!originalSetWindowPos) { SetLastError(ERROR_INVALID_FUNCTION); return FALSE; }
    if (routing || !project || (flags & SWP_NOMOVE)) return originalSetWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
    RECT targetBounds{};
    if (!ReadRoute(targetBounds)) return originalSetWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
    HMONITOR target = MonitorFromRect(&targetBounds, MONITOR_DEFAULTTONULL);
    if (flags & SWP_NOSIZE) {
        RECT existing{};
        if (!GetWindowRect(hwnd, &existing)) return originalSetWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
        cx = existing.right - existing.left;
        cy = existing.bottom - existing.top;
    }
    if (cx <= 0 || cy <= 0) return originalSetWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
    RECT requested{x, y, x + cx, y + cy}; HMONITOR current = MonitorFromRect(&requested, MONITOR_DEFAULTTONEAREST);
    if (!target || !current || target == current) return originalSetWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
    MONITORINFO currentInfo{sizeof(currentInfo)}, targetInfo{sizeof(targetInfo)};
    if (!GetMonitorInfoW(current, &currentInfo) || !GetMonitorInfoW(target, &targetInfo)) return originalSetWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
    if (!EqualRect(&targetInfo.rcMonitor, &targetBounds)) return originalSetWindowPos(hwnd, insertAfter, x, y, cx, cy, flags);
    const UINT sourceDpi = MonitorDpi(current), targetDpi = MonitorDpi(target);
    const int scaledWidth = MulDiv(cx, static_cast<int>(targetDpi), static_cast<int>(sourceDpi));
    const int scaledHeight = MulDiv(cy, static_cast<int>(targetDpi), static_cast<int>(sourceDpi));
    const int width = std::min(scaledWidth, static_cast<int>(targetInfo.rcWork.right - targetInfo.rcWork.left));
    const int height = std::min(scaledHeight, static_cast<int>(targetInfo.rcWork.bottom - targetInfo.rcWork.top));
    const int rightOffset = currentInfo.rcWork.right - requested.right;
    const int bottomOffset = currentInfo.rcWork.bottom - requested.bottom;
    const int calculatedX = targetInfo.rcWork.right - MulDiv(rightOffset, static_cast<int>(targetDpi), static_cast<int>(sourceDpi)) - width;
    const int calculatedY = targetInfo.rcWork.bottom - MulDiv(bottomOffset, static_cast<int>(targetDpi), static_cast<int>(sourceDpi)) - height;
    const int targetLeft = static_cast<int>(targetInfo.rcWork.left), targetTop = static_cast<int>(targetInfo.rcWork.top);
    const int targetRight = static_cast<int>(targetInfo.rcWork.right), targetBottom = static_cast<int>(targetInfo.rcWork.bottom);
    const int newX = std::max(targetLeft, std::min(calculatedX, targetRight - width));
    const int newY = std::max(targetTop, std::min(calculatedY, targetBottom - height));
    wchar_t line[256]{}; swprintf_s(line, L"SETWINDOWPOS hwnd=%p from=[%d,%d,%d,%d] to=[%d,%d,%d,%d] dpi=%u->%u", hwnd, x, y, cx, cy, newX, newY, width, height, sourceDpi, targetDpi); Log(line);
    routing = true;
    const BOOL result = originalSetWindowPos(hwnd, insertAfter, newX, newY, width, height, flags & ~(SWP_NOMOVE | SWP_NOSIZE));
    const DWORD error = result ? ERROR_SUCCESS : GetLastError();
    routing = false;
    if (!result) { wchar_t errorLine[128]{}; swprintf_s(errorLine, L"ROUTE_FAILED hwnd=%p error=%u", hwnd, error); Log(errorLine); SetLastError(error); }
    return result;
}

DWORD WINAPI InstallThread(void*) {
    Sleep(50);
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&WinPPlusHookStatus), static_cast<LONG>(0x80000000u | static_cast<DWORD>(initialized)));
        wchar_t line[128]{}; swprintf_s(line, L"HOOK_INSTALL_FAILED initialize status=%d", static_cast<int>(initialized)); Log(line); return 0;
    }
    const MH_STATUS created = MH_CreateHookApi(L"user32.dll", "SetWindowPos", &HookedSetWindowPos, reinterpret_cast<LPVOID*>(&originalSetWindowPos));
    if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&WinPPlusHookStatus), static_cast<LONG>(0x80000000u | static_cast<DWORD>(created)));
        wchar_t line[128]{}; swprintf_s(line, L"HOOK_INSTALL_FAILED create status=%d", static_cast<int>(created)); Log(line); return 0;
    }
    const MH_STATUS enabledStatus = MH_EnableHook(MH_ALL_HOOKS);
    const bool enabled = enabledStatus == MH_OK || enabledStatus == MH_ERROR_ENABLED;
    if (!enabled) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&WinPPlusHookStatus), static_cast<LONG>(0x80000000u | static_cast<DWORD>(enabledStatus)));
        wchar_t line[128]{}; swprintf_s(line, L"HOOK_INSTALL_FAILED enable status=%d", static_cast<int>(enabledStatus)); Log(line);
    } else {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&WinPPlusHookStatus), 1);
        Log(L"HOOK_INSTALLED routeState=v1 scope=Win+P");
    }
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, InstallThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
        else InterlockedExchange(reinterpret_cast<volatile LONG*>(&WinPPlusHookStatus), static_cast<LONG>(0x80000000u | GetLastError()));
    }
    return TRUE;
}
