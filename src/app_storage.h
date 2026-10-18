#pragma once

#include <filesystem>
#include <string>

struct AppSettings {
    bool chinese = true;
    bool custom = true;
    bool native = false;
};

// Owns user data paths, configuration and embedded DLL extraction.
// Constructing a store does not change the registry or write configuration.
class AppStorage final {
  public:
    AppStorage();
    explicit AppStorage(std::filesystem::path root);

    const std::filesystem::path& Root() const {
        return root_;
    }

    AppSettings Load() const;
    bool Save(const AppSettings& settings) const;
    std::wstring ExtractHook() const;
    void Log(const std::wstring& message) const;

  private:
    std::filesystem::path root_;
};
