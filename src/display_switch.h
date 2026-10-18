#pragma once

#include <windows.h>
#include "display_state.h"

enum class SwitchStart { Started, Busy, Failed };
enum class SwitchUpdate { None, Confirmed, Unconfirmed };

// Owns the asynchronous DisplaySwitch process; closing a handle does not kill it.
class DisplaySwitch final {
  public:
    ~DisplaySwitch();
    DisplaySwitch() = default;
    DisplaySwitch(const DisplaySwitch&) = delete;
    DisplaySwitch& operator=(const DisplaySwitch&) = delete;

    SwitchStart Start(int mode);
    SwitchUpdate Poll(const DisplayState& actual);

    bool Pending() const {
        return process_ != nullptr;
    }

  private:
    void Reset();
    HANDLE process_{};
    int target_ = -1;
    ULONGLONG started_{};
};
