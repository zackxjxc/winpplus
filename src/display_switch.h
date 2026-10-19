#pragma once

#include <windows.h>
#include <memory>
#include "display_state.h"

enum class SwitchStart { Started, Busy, Failed };
enum class SwitchUpdate { None, Confirmed, Unconfirmed, Failed };

// CCD calls can block in the graphics driver. The worker owns its state and never
// accesses a window or the owner; polling and destruction do not wait for it.
class DisplaySwitch final {
  public:
    using ApplyFunction = decltype(&SetDisplayConfig);
    using QueryFunction = decltype(&QueryCurrentDisplayState);
    ~DisplaySwitch();
    explicit DisplaySwitch(ApplyFunction apply = &SetDisplayConfig,
                           QueryFunction query = &QueryCurrentDisplayState, DWORD timeoutMs = 12000);
    DisplaySwitch(const DisplaySwitch&) = delete;
    DisplaySwitch& operator=(const DisplaySwitch&) = delete;

    SwitchStart Start(int mode);
    SwitchUpdate Poll(DisplayState& actual);

    LONG LastError() const { return lastError_; }

    bool Pending() const {
        return operation_ != nullptr;
    }

  private:
    struct Operation;
    std::shared_ptr<Operation> operation_;
    ApplyFunction apply_;
    QueryFunction query_;
    DWORD timeoutMs_;
    LONG lastError_{};
};
