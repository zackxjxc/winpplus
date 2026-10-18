#include "app_storage.h"
#include "app_identity.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <utility>

AppStorage::AppStorage() {
    wchar_t folder[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, folder);
    root_ = std::filesystem::path(folder) / winpplus::kDataFolder;
}

AppStorage::AppStorage(std::filesystem::path root) : root_(std::move(root)) {}

AppSettings AppStorage::Load() const {
    AppSettings settings;
    const auto path = (root_ / L"config.ini").wstring();
    settings.chinese = GetPrivateProfileIntW(L"General", L"Language", 0, path.c_str()) == 0;
    settings.custom = GetPrivateProfileIntW(L"General", L"CustomMode", 1, path.c_str()) != 0;
    settings.native =
        !settings.custom && GetPrivateProfileIntW(L"General", L"OptimizeMode", 0, path.c_str()) != 0;
    return settings;
}

bool AppStorage::Save(const AppSettings& settings) const {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error)
        return false;
    const auto path = (root_ / L"config.ini").wstring();
    const bool languageSaved =
        WritePrivateProfileStringW(L"General", L"Language", settings.chinese ? L"0" : L"1", path.c_str()) !=
        FALSE;
    const bool customSaved = WritePrivateProfileStringW(L"General", L"CustomMode",
                                                        settings.custom ? L"1" : L"0", path.c_str()) != FALSE;
    const bool nativeSaved = WritePrivateProfileStringW(L"General", L"OptimizeMode",
                                                        settings.native ? L"1" : L"0", path.c_str()) != FALSE;
    return languageSaved && customSaved && nativeSaved;
}

std::wstring AppStorage::ExtractHook() const {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(201), RT_RCDATA);
    if (!resource)
        return {};
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto* bytes = static_cast<const BYTE*>(LockResource(loaded));
    const DWORD size = SizeofResource(nullptr, resource);
    if (!bytes || !size)
        return {};
    unsigned long long hash = 1469598103934665603ULL;
    for (DWORD i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    const auto path = root_ / (L"Hook-" + std::to_wstring(hash) + L".dll");
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error)
        return {};
    if (!std::filesystem::exists(path, error)) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes), size);
        file.close();
        if (!file)
            return {};
    }
    return path.wstring();
}

void AppStorage::Log(const std::wstring& message) const {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    std::wofstream file(root_ / L"app.log", std::ios::app);
    file << GetTickCount64() << L" " << message << L"\n";
}
