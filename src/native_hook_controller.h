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
    // A null monitor cancels a pending Win+P route without disabling the mode.
    void RouteTo(HMONITOR monitor);
    // Call on a UI timer. Injection completion is polled, never waited on the UI thread.
    void Tick();
    bool Ready() const;
    DWORD LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
