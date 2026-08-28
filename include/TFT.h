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

  // constexpr TFTPins(int8_t cs_=5, int8_t dc_=2, int8_t rst_=13,
  //                   int8_t sck_=18, int8_t mosi_=23, int8_t miso_=19)
  //     : cs(cs_), dc(dc_), rst(rst_), sck(sck_), mosi(mosi_), miso(miso_){}
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
  void setRangeRate(float rate_mps, bool valid);   // van toc noi suy (alpha-beta)

  void render(const TrackerOutput& out);
  void setWiFiStatus(const char* SSID, bool connected);

  // ======== UX v2 (button + preset + NVS + battery) ========
  // Set preset info (persistent trong status bar top + info bar bottom)
  void setPresetInfo(uint8_t id, const char* name, const char* use_case,
                     float alpha, float beta, float gate_m);
  // Set battery percent (hien trong status bar top)
  void setBatteryPercent(int pct);
  // Set lock icon (hien goc phai status bar)
  void setLockIcon(bool locked);
  // Show preset overlay 2-3 giay o giua main area
  void showPresetOverlay(uint32_t duration_ms = 2000);
  // Show flash message ngan 1-2 giay ("LOCKED", "UNLOCKED", "SAVED")
  void showFlash(const char* msg, uint32_t duration_ms = 1500);
  // Poll trong loop() de auto-hide overlay/flash sau timeout
  void updateOverlay();

private:
  TFTPins pins_;
  Adafruit_ILI9341 tft_;

  float displayedDistanceM_ = NAN;
  float fps_ = 0.0f;
  float rangeRateMps_ = NAN;       // van toc noi suy
  bool  rateValid_ = false;        // chi hien thi khi mode alpha-beta
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

  // UX v2 state
  uint8_t  presetId_        = 0xFF;
  String   presetName_      = "";
  String   presetUseCase_   = "";
  float    presetAlpha_     = 0.0f;
  float    presetBeta_      = 0.0f;
  float    presetGate_      = 0.0f;
  int      batteryPct_      = -1;   // -1 = chua init
  bool     locked_          = false;
  uint32_t overlayHideAtMs_ = 0;
  bool     overlayVisible_  = false;
  String   flashMsg_        = "";
  uint32_t flashHideAtMs_   = 0;
  bool     flashVisible_    = false;

  void DrawWiFiStatus();
  void drawStatic();
  void printDistanceValue(const String& s, uint16_t color);
  void paintDistanceUI();
  void drawOverlay();
  void drawRate();

  // UX v2 draw methods
  void drawPresetName();
  void drawBatteryIcon();
  void drawLockIcon();
  void drawPresetInfoBar();   // vung info bar bottom
  void drawPresetOverlay();   // popup 2s o giua main area
  void drawFlash();           // flash message
  void hideOverlay();         // xoa overlay + redraw main area
  void hideFlash();
};