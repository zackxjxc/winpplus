#pragma once

#include <windows.h>

namespace winpplus {
inline constexpr wchar_t kRouteStateMapping[] = L"Local\\WinPPlus.RouteState.v1";
inline constexpr ULONGLONG kRouteIntentLifetimeMs = 1500;

// Shared ABI between the controller and the injected DLL. Do not put process-local
// handles or pointers here. The short expiry confines routing to a Win+P intent.
struct alignas(8) NativeRouteState {
    DWORD enabled{};
    RECT targetMonitorBounds{};
    ULONGLONG validUntil{};
};

static_assert(sizeof(NativeRouteState) == 32);
} // namespace winpplus
