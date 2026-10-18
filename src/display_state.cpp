#include "display_state.h"

#include <windows.h>
#include <algorithm>
#include <sstream>
#include <tuple>

namespace {
struct ConfigSnapshot {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    DISPLAYCONFIG_TOPOLOGY_ID topology{};
    LONG error = ERROR_SUCCESS;
};

using DeviceKey = std::tuple<LONG, DWORD, UINT32>;

DeviceKey Key(const LUID& adapter, UINT32 id) {
    return {adapter.HighPart, adapter.LowPart, id};
}

ConfigSnapshot ReadConfig(UINT32 flags) {
    ConfigSnapshot snapshot;
    for (int attempt = 0; attempt < 4; ++attempt) {
        UINT32 pathCount{}, modeCount{};
        snapshot.error = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);
        if (snapshot.error != ERROR_SUCCESS) return snapshot;
        // Non-null arrays are required even while a driver reports zero entries.
        snapshot.paths.resize(std::max(pathCount, UINT32{1}));
        snapshot.modes.resize(std::max(modeCount, UINT32{1}));
        auto* topology = (flags & QDC_DATABASE_CURRENT) ? &snapshot.topology : nullptr;
        snapshot.error = QueryDisplayConfig(flags, &pathCount, snapshot.paths.data(),
            &modeCount, snapshot.modes.data(), topology);
        if (snapshot.error == ERROR_INSUFFICIENT_BUFFER) continue;
        if (snapshot.error != ERROR_SUCCESS) {
            snapshot.paths.clear();
            snapshot.modes.clear();
            return snapshot;
        }
        snapshot.paths.resize(pathCount);
        snapshot.modes.resize(modeCount);
        return snapshot;
    }
    snapshot.paths.clear();
    snapshot.modes.clear();
    return snapshot;
}

std::vector<DeviceKey> TargetKeys(const ConfigSnapshot& snapshot) {
    std::vector<DeviceKey> result;
    for (const auto& path : snapshot.paths) {
        result.push_back(Key(path.targetInfo.adapterId, path.targetInfo.id));
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

int ClassifyMultiplePaths(const ConfigSnapshot& snapshot, std::size_t sourceCount,
    std::uint32_t& desktopCount) {
    std::vector<std::pair<LONG, LONG>> positions;
    bool allPositionsAvailable = true;
    bool anyVirtualPath = false;
    for (const auto& path : snapshot.paths) {
        const bool virtualPath = (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0;
        anyVirtualPath = anyVirtualPath || virtualPath;
        const UINT32 modeIndex = virtualPath ? path.sourceInfo.sourceModeInfoIdx :
            path.sourceInfo.modeInfoIdx;
        if (modeIndex >= snapshot.modes.size() ||
            snapshot.modes[modeIndex].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            allPositionsAvailable = false;
            continue;
        }
        const auto& source = snapshot.modes[modeIndex].sourceMode;
        positions.emplace_back(source.position.x, source.position.y);
    }
    if (allPositionsAvailable) {
        // Virtual clones may have distinct source IDs. Their desktop source modes
        // share the same position, as documented by the Microsoft CCD example.
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
        desktopCount = static_cast<std::uint32_t>(positions.size());
        if (positions.size() == 1) return static_cast<int>(DisplayMode::Clone);
        if (positions.size() == snapshot.paths.size()) return static_cast<int>(DisplayMode::Extend);
        return static_cast<int>(DisplayMode::Unknown); // Part clone, part extended.
    }
    // Shared hardware source IDs identify a clone even if mode data is missing.
    // Distinct virtual source IDs do not establish independent desktops.
    if (sourceCount == 1) return static_cast<int>(DisplayMode::Clone);
    if (!anyVirtualPath && sourceCount == snapshot.paths.size()) {
        return static_cast<int>(DisplayMode::Extend);
    }
    return static_cast<int>(DisplayMode::Unknown);
}

const wchar_t* ModeName(int mode) {
    switch (static_cast<DisplayMode>(mode)) {
    case DisplayMode::Internal: return L"internal";
    case DisplayMode::Clone: return L"clone";
    case DisplayMode::Extend: return L"extend";
    case DisplayMode::External: return L"external";
    default: return L"unknown";
    }
}

int DatabaseMode(DISPLAYCONFIG_TOPOLOGY_ID topology) {
    switch (topology) {
    case DISPLAYCONFIG_TOPOLOGY_INTERNAL: return static_cast<int>(DisplayMode::Internal);
    case DISPLAYCONFIG_TOPOLOGY_CLONE: return static_cast<int>(DisplayMode::Clone);
    case DISPLAYCONFIG_TOPOLOGY_EXTEND: return static_cast<int>(DisplayMode::Extend);
    case DISPLAYCONFIG_TOPOLOGY_EXTERNAL: return static_cast<int>(DisplayMode::External);
    default: return static_cast<int>(DisplayMode::Unknown);
    }
}

DisplayPathState DescribePath(const DISPLAYCONFIG_PATH_INFO& path) {
    DisplayPathState result;
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS) {
        result.sourceDevice = source.viewGdiDeviceName;
    }
    DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
    target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size = sizeof(target);
    target.header.adapterId = path.targetInfo.adapterId;
    target.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
        result.targetName = target.monitorFriendlyDeviceName;
        result.targetDevicePath = target.monitorDevicePath;
    }
    std::wostringstream identity;
    identity << L"source=" << std::hex << path.sourceInfo.adapterId.HighPart << L":"
        << path.sourceInfo.adapterId.LowPart << L":" << path.sourceInfo.id
        << L" target=" << path.targetInfo.adapterId.HighPart << L":"
        << path.targetInfo.adapterId.LowPart << L":" << path.targetInfo.id;
    result.identity = identity.str();
    return result;
}
}

DisplayState QueryCurrentDisplayState() {
    DisplayState state;
    const auto active = ReadConfig(QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE);
    state.queryError = active.error;
    if (active.error != ERROR_SUCCESS || active.paths.empty()) {
        state.summary = L"active display query failed error=" + std::to_wstring(active.error);
        return state;
    }
    state.activePathCount = static_cast<std::uint32_t>(active.paths.size());
    if (active.paths.size() == 1) state.activeDesktopCount = 1;
    std::vector<DeviceKey> sourceKeys;
    for (const auto& path : active.paths) {
        sourceKeys.push_back(Key(path.sourceInfo.adapterId, path.sourceInfo.id));
        state.paths.push_back(DescribePath(path));
    }
    std::sort(sourceKeys.begin(), sourceKeys.end());
    sourceKeys.erase(std::unique(sourceKeys.begin(), sourceKeys.end()), sourceKeys.end());
    state.activeSourceCount = static_cast<std::uint32_t>(sourceKeys.size());

    const auto database = ReadConfig(QDC_DATABASE_CURRENT | QDC_VIRTUAL_MODE_AWARE);
    state.persistedTopologyMatched = database.error == ERROR_SUCCESS &&
        TargetKeys(active) == TargetKeys(database);
    const int persistedMode = DatabaseMode(database.topology);
    if (active.paths.size() > 1) {
        state.mode = ClassifyMultiplePaths(active, sourceKeys.size(), state.activeDesktopCount);
    } else if (state.persistedTopologyMatched &&
        (persistedMode == static_cast<int>(DisplayMode::Internal) ||
         persistedMode == static_cast<int>(DisplayMode::External))) {
        state.mode = persistedMode;
    }

    std::wostringstream summary;
    summary << L"mode=" << ModeName(state.mode) << L" activePaths=" << state.activePathCount
        << L" activeSources=" << state.activeSourceCount << L" activeDesktops="
        << state.activeDesktopCount << L" databaseMode="
        << ModeName(persistedMode) << L" databaseMatched=" << state.persistedTopologyMatched
        << L" databaseError=" << database.error;
    for (const auto& path : state.paths) {
        summary << L" [" << path.identity << L" gdi=" << path.sourceDevice
            << L" name=" << path.targetName << L"]";
    }
    state.summary = summary.str();
    return state;
}
