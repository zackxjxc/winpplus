#include "display_switch.h"

#include <array>
#include <atomic>
#include <thread>

struct DisplaySwitch::Operation {
    std::atomic<bool> done{}, cancel{};
    bool timeoutReported{};
    ULONGLONG started = GetTickCount64();
    DisplayState actual;
    LONG error{};
    SwitchUpdate result = SwitchUpdate::Unconfirmed;
};

DisplaySwitch::DisplaySwitch(ApplyFunction apply, QueryFunction query, DWORD timeoutMs)
    : apply_(apply), query_(query), timeoutMs_(timeoutMs) {}

DisplaySwitch::~DisplaySwitch() {
    if (operation_)
        operation_->cancel.store(true);
}

SwitchStart DisplaySwitch::Start(int mode) {
    if (operation_)
        return SwitchStart::Busy;
    constexpr std::array<UINT32, 4> topologies{
        SDC_TOPOLOGY_INTERNAL, SDC_TOPOLOGY_CLONE, SDC_TOPOLOGY_EXTEND, SDC_TOPOLOGY_EXTERNAL};
    if (mode < 0 || mode >= static_cast<int>(topologies.size()) || !apply_ || !query_)
        return SwitchStart::Failed;
    auto operation = std::make_shared<Operation>();
    try {
        std::thread([operation, mode, flags = topologies[static_cast<size_t>(mode)],
                     apply = apply_, query = query_, timeout = timeoutMs_]() {
            try {
                if (!operation->cancel.load()) {
                    // Use Windows' saved layouts without starting a Shell flyout.
                    operation->error = apply(0, nullptr, 0, nullptr, SDC_APPLY | flags);
                    if (operation->error != ERROR_SUCCESS)
                        operation->result = SwitchUpdate::Failed;
                    else {
                        while (!operation->cancel.load() && GetTickCount64() - operation->started < timeout) {
                            operation->actual = query();
                            if (operation->actual.mode == mode) {
                                operation->result = SwitchUpdate::Confirmed;
                                break;
                            }
                            Sleep(150);
                        }
                    }
                }
            } catch (...) {
                operation->error = ERROR_GEN_FAILURE;
                operation->result = SwitchUpdate::Failed;
            }
            operation->done.store(true, std::memory_order_release);
        }).detach();
    } catch (...) {
        lastError_ = ERROR_NOT_ENOUGH_MEMORY;
        return SwitchStart::Failed;
    }
    lastError_ = ERROR_SUCCESS;
    operation_ = std::move(operation);
    return SwitchStart::Started;
}

SwitchUpdate DisplaySwitch::Poll(DisplayState& actual) {
    if (!operation_)
        return SwitchUpdate::None;
    if (operation_->done.load(std::memory_order_acquire)) {
        actual = operation_->actual;
        lastError_ = operation_->error;
        const auto result = operation_->timeoutReported && operation_->result != SwitchUpdate::Confirmed
                                ? SwitchUpdate::None : operation_->result;
        operation_.reset();
        return result;
    }
    if (!operation_->timeoutReported && GetTickCount64() - operation_->started >= timeoutMs_) {
        operation_->timeoutReported = true;
        operation_->cancel.store(true);
        lastError_ = WAIT_TIMEOUT;
        actual.summary = L"display configuration worker timed out";
        // Keep busy until the driver returns, avoiding concurrent requests.
        return SwitchUpdate::Unconfirmed;
    }
    return SwitchUpdate::None;
}
