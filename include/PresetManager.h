#pragma once
#include "PresetTable.h"

class PresetManager{
public:
    void begin();
    void cycleNext();
    // Fix #8: return bool de UI biet save success/fail thay vi luon report SAVED
    bool saveToNVS();
    uint8_t currentId() const { return current_; }
    const char* currentName() const { return PRESETS[current_].name; }
    const char* currentUseCase() const { return PRESETS[current_].use_case; }
    const PresetConfig& current() const { return PRESETS[current_]; }

private:
    uint8_t current_ = 3;  // Default AB-B
};