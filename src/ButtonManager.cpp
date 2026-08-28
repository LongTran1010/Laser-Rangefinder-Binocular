#include "ButtonManager.h"

void ButtonManager::begin(uint8_t pin){
    pin_            = pin;
    pinMode(pin_, INPUT_PULLUP);
    bool s          = digitalRead(pin_);
    raw_state_      = s;
    stable_state_   = s;
    raw_change_ms_  = millis();
    press_active_   = false;
    locked_         = false;
}

ButtonEvent ButtonManager::update(){
    bool     s   = digitalRead(pin_);
    uint32_t now = millis();

    // Review#B1: 2-lop debounce (raw + stable).
    // Buoc 1 - track raw edge de reset debounce timer.
    if (s != raw_state_){
        raw_state_     = s;
        raw_change_ms_ = now;
        return BTN_NONE;   // canh chua duoc xac nhan, cho on dinh
    }

    // Buoc 2 - raw da giu nguyen >= DEBOUNCE_MS chua? Chua thi cho tiep.
    if ((now - raw_change_ms_) < DEBOUNCE_MS){
        return BTN_NONE;
    }
    if (s == stable_state_){
        return BTN_NONE;   // stable khong doi -> khong co event
    }

    // Buoc 3 - stable_state_ vua doi HIGH<->LOW: dispatch event.
    // Truoc day (buggy): xu ly canh tren prev_state_ != state luon -> canh gia
    // 40ms tao BTN_SHORT roi canh that sau do tao BTN_LONG (2 event / 1 lan nhan).
    stable_state_ = s;

    // FALLING (stable): nhan xuong
    if (s == LOW){
        press_start_ms_ = now;
        press_active_   = true;
        return BTN_NONE;
    }

    // RISING (stable): nha ra
    if (s == HIGH && press_active_){
        uint32_t hold = now - press_start_ms_;
        press_active_ = false;

        // Cascading (Review#6): khong con gap 500-999 ms.
        // Debounce da xu ly o buoc 2 -> khong can guard < DEBOUNCE_MS o day nua.
        if (hold < LONG_MIN_MS)          return BTN_SHORT;      // 30..999 ms
        if (hold < VERY_LONG_MIN_MS)     return BTN_LONG;       // 1000..2999 ms
        return BTN_VERY_LONG;                                    // >= 3000 ms
    }
    return BTN_NONE;
}
