#pragma once
#include <windows.h>

enum class KeyAction { None, Open, Cycle, Commit, NativeRoute };
struct KeyDecision { bool consume{}; KeyAction action{KeyAction::None}; bool maskStart{}; };

// Only the matching P press/release belongs to us. Other Win chords always pass through.
class KeyboardRouter {
public:
    void Mode(bool custom, bool native) {
        custom_ = custom; native_ = native; session_ = false; cycled_ = false;
        leftWin_ = false; rightWin_ = false; ctrl_ = alt_ = shift_ = 0;
        pDown_ = false; capturedP_ = false;
    }
    void Synchronize() {
        leftWin_ = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0;
        rightWin_ = (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        ctrl_ = ((GetAsyncKeyState(VK_LCONTROL) & 0x8000) ? 2u : 0u) |
            ((GetAsyncKeyState(VK_RCONTROL) & 0x8000) ? 4u : 0u);
        alt_ = ((GetAsyncKeyState(VK_LMENU) & 0x8000) ? 2u : 0u) |
            ((GetAsyncKeyState(VK_RMENU) & 0x8000) ? 4u : 0u);
        shift_ = ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) ? 2u : 0u) |
            ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) ? 4u : 0u);
        pDown_ = (GetAsyncKeyState('P') & 0x8000) != 0; capturedP_ = false;
    }
    bool WinHeld() const { return leftWin_ || rightWin_; }
    void MenuClosed() { session_ = false; cycled_ = false; }
    KeyDecision Feed(DWORD vk, bool down) {
        if (vk == VK_LWIN || vk == VK_RWIN) {
            bool& pressed = vk == VK_LWIN ? leftWin_ : rightWin_;
            pressed = down;
            if (!down && !leftWin_ && !rightWin_ && session_ && cycled_) {
                cycled_ = false;
                return {false, KeyAction::Commit, false};
            }
            return {};
        }
        if (Modifier(ctrl_, vk, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, down) ||
            Modifier(alt_, vk, VK_MENU, VK_LMENU, VK_RMENU, down) ||
            Modifier(shift_, vk, VK_SHIFT, VK_LSHIFT, VK_RSHIFT, down)) return {};
        if (vk != 'P') return {};
        if (!down) { const bool consume = capturedP_; capturedP_ = false; pDown_ = false; return {consume}; }
        if (pDown_) return {capturedP_};
        pDown_ = true;
        if (!(leftWin_ || rightWin_) || ctrl_ || alt_ || shift_) return {};
        if (native_) return {false, KeyAction::NativeRoute};
        if (!custom_) return {};
        capturedP_ = true;
        if (session_) { cycled_ = true; return {true, KeyAction::Cycle, true}; }
        session_ = true; cycled_ = false;
        return {true, KeyAction::Open, true};
    }
private:
    static bool Modifier(unsigned& state, DWORD vk, DWORD generic, DWORD left, DWORD right, bool down) {
        unsigned bit = vk == generic ? 1u : vk == left ? 2u : vk == right ? 4u : 0u;
        if (!bit) return false;
        if (down) state |= bit; else state &= ~bit;
        return true;
    }
    bool leftWin_{}, rightWin_{}, pDown_{}, capturedP_{};
    unsigned ctrl_{}, alt_{}, shift_{};
    bool custom_{}, native_{}, session_{}, cycled_{};
};
