#pragma once

#include <windows.h>
#include <memory>
#include <string>

class NativeHookController final {
public:
    explicit NativeHookController(std::wstring dllPath);
    ~NativeHookController();
    NativeHookController(const NativeHookController&) = delete;
    NativeHookController& operator=(const NativeHookController&) = delete;

    void Enable(bool enabled);
    // A null monitor releases pending and visible Win+P routes without disabling the mode.
    void RouteTo(HMONITOR monitor);
    // Call on a UI timer. Injection completion is polled, never waited on the UI thread.
    void Tick();
    bool Ready() const;
    DWORD LastError() const;
    bool IsProjectionWindow(HWND window) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
