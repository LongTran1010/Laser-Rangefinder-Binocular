#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "MeasurementTypes.h"
#include "TrackerTypes_v2.h"

// RGB565 colors
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFD20
#define C_GREEN   0x07E0
#define C_RED     0xF800
#define C_CYAN    0x07FF

struct TFTPins{
  int8_t cs;
  int8_t dc;
  int8_t rst;
  int8_t sck;
  int8_t mosi;
  int8_t miso;

  constexpr TFTPins(int8_t cs_=5, int8_t dc_=21, int8_t rst_=19,
                    int8_t sck_=18, int8_t mosi_=23, int8_t miso_=-1)
      : cs(cs_), dc(dc_), rst(rst_), sck(sck_), mosi(mosi_), miso(miso_){}
};

class TFTDistance{
public:
  explicit TFTDistance(const TFTPins& pins = {})
      : pins_(pins), tft_(pins.cs, pins.dc, pins.rst){}

  void begin();
  void setMaxRangeMeters(float m);
  void setStaleTimeoutMs(uint32_t ms);

  void setStatus(MeasStatus s);
  void setSystemError(uint8_t errCode);
  void setFPS(float fps);
  void setTrackState(TrackState s);
  void setDisplayedDistance(float dist_m, bool valid);

  void render(const TrackerOutput& out);
  void setWiFiStatus(const char* SSID, bool connected);

private:
  TFTPins pins_;
  Adafruit_ILI9341 tft_;

  float displayedDistanceM_ = NAN;
  float fps_ = 0.0f;
  MeasStatus status_ = MEAS_TIMEOUT;
  TrackState trackState_ = TRACK_SEARCHING;

  uint8_t systemErrorCode_ = 0;
  uint8_t measErrorCode_ = 0;

  int BAR_X_, BAR_Y_, BAR_W_, BAR_H_;
  int HEADER_H_ = 42, MARGIN_ = 10, GAP_ = 18;

  float maxRange_m_ = 800.0f;
  uint32_t lastUpdateMs_ = 0;
  uint32_t staleTimeoutMs_ = 1000;

  String wifiSSID_ = "";
  bool wifiConnected_ = false;

  void DrawWiFiStatus();
  void drawStatic();
  void printDistanceValue(const String& s, uint16_t color);
  void paintDistanceUI();
  void drawOverlay();
};