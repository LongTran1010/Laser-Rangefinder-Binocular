#include "PresetManager.h"
#include <Preferences.h>

void PresetManager::begin(){
    Preferences p;
    p.begin("calib", true);  // readonly
    current_ = p.getUChar("preset", 3);
    p.end();

    if(current_ >= N_PRESETS){
        Serial.printf("[Preset] Invalid %d from NVS, fallback to 3\n", current_);
        current_ = 3;
    }
    Serial.printf("[Preset] Loaded id=%d name=%s\n", current_, PRESETS[current_].name);
}

void PresetManager::cycleNext(){
    current_ = (current_ + 1) % N_PRESETS;
    Serial.printf("[Preset] Cycled to id=%d name=%s\n", current_, PRESETS[current_].name);
}

// Fix #8: return bool. True = save OK, false = fail (NVS full/corrupt).
bool PresetManager::saveToNVS() {
    Preferences p;
    bool ok = p.begin("calib", false);  // read-write
    if (!ok) {
        Serial.println("[Preset] NVS begin FAILED");
        return false;
    }
    size_t written = p.putUChar("preset", current_);
    p.end();

    if(written == 0){
        Serial.println("[Preset] NVS save FAILED (0 bytes written)");
        return false;
    }
    Serial.printf("[Preset] Saved id=%d to NVS\n", current_);
    return true;
}