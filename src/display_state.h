#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class DisplayMode : int {
    Unknown = -1,
    Internal = 0,
    Clone = 1,
    Extend = 2,
    External = 3,
};

struct DisplayPathState {
    std::wstring sourceDevice;
    std::wstring targetName;
    std::wstring targetDevicePath;
    std::wstring identity;
};

struct DisplayState {
    int mode = static_cast<int>(DisplayMode::Unknown);
    std::uint32_t activePathCount = 0;
    std::uint32_t activeSourceCount = 0;
    std::uint32_t activeDesktopCount = 0;
    long queryError = 0;
    bool persistedTopologyMatched = false;
    std::vector<DisplayPathState> paths;
    std::wstring summary;
};

// Uses active source-mode positions for clone/extend and matching CCD topology for
// internal/external. A single active monitor being primary does not identify it.
DisplayState QueryCurrentDisplayState();
