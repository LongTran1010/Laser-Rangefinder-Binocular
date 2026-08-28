#pragma once
#include <Arduino.h>

// Fix #12: BatteryMonitor cho Li-ion 2S (6.0-8.4V)
// Voltage divider R1=220k, R2=100k -> ratio 0.3125.
// V_bat = V_adc * (R1+R2)/R2 = V_adc * 3.2
//
// Usage:
//   BatteryMonitor batMon;
//   batMon.begin(34);          // GPIO 34 (ADC1_CH6, input-only)
//   batMon.update();           // goi trong loop() moi chu ky
//   int pct = batMon.percent();
//   float v = batMon.voltage();

class BatteryMonitor {
public:
  void begin(uint8_t adc_pin) {
    pin_ = adc_pin;
    pinMode(pin_, INPUT);
    // Bat dau doc ngay lan dau de co so lieu, dung force sau do
    lastUpdateMs_ = 0;
    updateInternal();
  }

  // Goi moi chu ky loop() - non-blocking, tu poll moi 5s
  void update() {
    uint32_t now = millis();
    if (now - lastUpdateMs_ < POLL_INTERVAL_MS) return;
    lastUpdateMs_ = now;
    updateInternal();
  }

  float voltage() const { return v_bat_; }
  int   percent() const { return pct_; }
  bool  isLow()   const { return pct_ < 20; }

private:
  static constexpr uint32_t POLL_INTERVAL_MS = 5000;   // poll moi 5 giay
  static constexpr float    DIVIDER_RATIO    = 3.2f;   // (R1+R2)/R2

  uint8_t  pin_          = 0;
  uint32_t lastUpdateMs_ = 0;
  float    v_bat_        = 0.0f;
  int      pct_          = 0;

  void updateInternal() {
    // Trung binh 10 mau de giam noise
    long sum = 0;
    for (int i = 0; i < 10; i++) {
      sum += analogRead(pin_);
      delayMicroseconds(100);
    }
    float v_adc = (sum / 10.0f) * 3.3f / 4095.0f;
    v_bat_ = v_adc * DIVIDER_RATIO;
    pct_   = voltageToPercent(v_bat_);
  }

  // Li-ion 2S discharge curve piecewise linear
  static int voltageToPercent(float v) {
    if (v >= 8.40f) return 100;
    if (v >= 8.00f) return 80  + (int)(20 * (v - 8.00f) / 0.40f);
    if (v >= 7.60f) return 60  + (int)(20 * (v - 7.60f) / 0.40f);
    if (v >= 7.20f) return 40  + (int)(20 * (v - 7.20f) / 0.40f);
    if (v >= 6.80f) return 20  + (int)(20 * (v - 6.80f) / 0.40f);
    if (v >= 6.00f) return (int)(20 * (v - 6.00f) / 0.80f);
    return 0;
  }
};
