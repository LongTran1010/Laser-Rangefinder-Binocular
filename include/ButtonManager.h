#pragma once
#include <Arduino.h>

enum ButtonEvent : uint8_t{
    BTN_NONE       = 0,
    BTN_SHORT      = 1,
    BTN_LONG       = 2,
    BTN_VERY_LONG  = 3,
};

class ButtonManager{
public:
    void begin(uint8_t pin);
    ButtonEvent update();
    bool isLocked() const   { return locked_; }
    void toggleLock()       { locked_ = !locked_; }
    void setLocked(bool l)  { locked_ = l; }

private:
    uint8_t  pin_               = 0;
    // Review#B1: 2-lop state - raw doc tuc thoi, stable la trang thai da qua debounce.
    // Chi xu ly canh khi raw giu nguyen >= DEBOUNCE_MS -> stable moi doi.
    bool     raw_state_         = true;   // doc tuc thoi tu digitalRead()
    bool     stable_state_      = true;   // trang thai da debounce, dung de dispatch event
    uint32_t raw_change_ms_     = 0;      // luc raw_state_ doi lan gan nhat
    bool     press_active_      = false;  // stable_state_ = LOW?
    uint32_t press_start_ms_    = 0;      // luc stable_state_ bat dau LOW
    bool     locked_            = false;

    static constexpr uint32_t DEBOUNCE_MS       = 30;
    // Review#B2: XOA SHORT_MAX_MS + LONG_MAX_MS (khong con dung sau cascading fix).
    static constexpr uint32_t LONG_MIN_MS       = 1000;   // >=1s -> LONG
    static constexpr uint32_t VERY_LONG_MIN_MS  = 3000;   // >=3s -> VERY_LONG
};