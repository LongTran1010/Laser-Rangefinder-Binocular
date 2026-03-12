#include "TFT.h"
#include <WiFi.h>

// -------------------- Init --------------------

void TFTDistance::begin(){
  SPI.begin(pins_.sck, pins_.miso, pins_.mosi, pins_.cs);

  tft_.begin();
  tft_.setSPISpeed(27000000);
  tft_.setRotation(1);

  int W = tft_.width();
  int H = tft_.height();
  (void)H;

  HEADER_H_ = 65;

  BAR_X_ = MARGIN_;
  BAR_W_ = W - 2 * MARGIN_;
  BAR_H_ = 12;
  BAR_Y_ = HEADER_H_ + GAP_;

  wifiSSID_ = "";
  wifiConnected_ = false;

  drawStatic();
  drawOverlay();
  DrawWiFiStatus();
}

// -------------------- Config --------------------

void TFTDistance::setMaxRangeMeters(float m){
  if (m < 3.0f) m = 3.0f;
  maxRange_m_ = m;

  drawStatic();
  paintDistanceUI();
  drawOverlay();
  DrawWiFiStatus();
}

void TFTDistance::setStaleTimeoutMs(uint32_t ms){
  staleTimeoutMs_ = ms;
}

// -------------------- State update --------------------

void TFTDistance::setStatus(MeasStatus s){
  status_ = s;

  switch (s) {
    case MEAS_OK:         measErrorCode_ = 0;  break;
    case MEAS_TIMEOUT:    measErrorCode_ = 10; break;
    case MEAS_NO_SIGNAL:  measErrorCode_ = 11; break;
    case MEAS_BAD_CRC:    measErrorCode_ = 12; break;
    case MEAS_BAD_FRAME:  measErrorCode_ = 13; break;
    default:              measErrorCode_ = 0;  break;
  }

  drawOverlay();
}

void TFTDistance::setSystemError(uint8_t errCode){
  systemErrorCode_ = errCode;
  drawOverlay();
}

void TFTDistance::setFPS(float fps){
  fps_ = fps;
  drawOverlay();
}

void TFTDistance::setTrackState(TrackState s){
  trackState_ = s;
  drawOverlay();
}

void TFTDistance::setDisplayedDistance(float dist_m, bool valid){
  if (valid && dist_m < TC22_BLIND_M) {
    valid = false;
  }

  // Nếu invalid: giữ giá trị cũ để tránh nhấp nháy,
  // paintDistanceUI() sẽ tự quyết định có stale hay không.
  if (!valid || isnan(dist_m)){
    paintDistanceUI();
    drawOverlay();
    return;
  }

  displayedDistanceM_ = dist_m;
  lastUpdateMs_ = millis();

  paintDistanceUI();
  drawOverlay();
}

void TFTDistance::render(const TrackerOutput& out){
  setStatus(out.measStatus);
  setFPS(out.fps);
  setTrackState(out.trackState);
  setDisplayedDistance(out.filteredDistanceM, out.hasEstimate);
  DrawWiFiStatus();
}

// -------------------- WiFi status --------------------

void TFTDistance::setWiFiStatus(const char* SSID, bool connected){
  wifiSSID_ = String(SSID ? SSID : "");
  wifiConnected_ = connected;
  DrawWiFiStatus();
}

// -------------------- Static UI --------------------

void TFTDistance::drawStatic(){
  tft_.fillScreen(C_BLACK);

  // Header
  tft_.drawRect(0, 0, tft_.width(), HEADER_H_, C_CYAN);

  // Title
  const int TITLE_SIZE = 2;
  const int TITLE_Y = 25;

  tft_.setCursor(MARGIN_ + 2, TITLE_Y);
  tft_.setTextColor(C_ORANGE, C_BLACK);
  tft_.setTextSize(TITLE_SIZE);
  tft_.print("KHOANG CACH(m)");

  // Value box
  const int VALUE_SIZE = 3;
  const int VALUE_Y = TITLE_Y + TITLE_SIZE * 8 + 10;
  const int VALUE_H = VALUE_SIZE * 8;

  tft_.fillRect(MARGIN_, VALUE_Y - 2, tft_.width() - 2 * MARGIN_, VALUE_H + 6, C_BLACK);

  // Range bar
  tft_.drawRect(BAR_X_, BAR_Y_, BAR_W_, BAR_H_, C_ORANGE);

  // Labels 0 and max
  int labelY = BAR_Y_ + BAR_H_ + 12;
  tft_.setTextColor(C_WHITE, C_BLACK);
  tft_.setTextSize(2);

  tft_.setCursor(MARGIN_ + 2, labelY);
  tft_.print("0");

  String sMax = String((int)maxRange_m_);
  int xr = tft_.width() - (int)sMax.length() * 12 - (MARGIN_ + 2);
  tft_.setCursor(xr, labelY);
  tft_.print(sMax);
}

// -------------------- Value drawing --------------------

void TFTDistance::printDistanceValue(const String& s, uint16_t color){
  const int TITLE_SIZE = 2;
  const int TITLE_Y = 25;
  const int VALUE_SIZE = 3;
  const int VALUE_Y = TITLE_Y + TITLE_SIZE * 8 + 10;
  const int VALUE_H = VALUE_SIZE * 8;

  tft_.fillRect(MARGIN_, VALUE_Y - 2, tft_.width() - 2 * MARGIN_, VALUE_H + 6, C_BLACK);

  tft_.setTextColor(color, C_BLACK);
  tft_.setTextSize(VALUE_SIZE);
  tft_.setCursor(MARGIN_ + 8, VALUE_Y);
  tft_.print(s);
}

void TFTDistance::paintDistanceUI() {
  bool stale = (millis() - lastUpdateMs_) > staleTimeoutMs_;
  float d = displayedDistanceM_;

  if (isnan(d) || stale) {
    printDistanceValue(String("---.- m"), C_RED);
    tft_.fillRect(BAR_X_ + 1, BAR_Y_ + 1, BAR_W_ - 2, BAR_H_ - 2, C_BLACK);
    tft_.drawRect(BAR_X_, BAR_Y_, BAR_W_, BAR_H_, C_ORANGE);
    return;
  }

  String s = String(d, 1) + " m";
  printDistanceValue(s, C_YELLOW);

  float r = d / maxRange_m_;
  if (r < 0.0f) r = 0.0f;
  if (r > 1.0f) r = 1.0f;

  int fillW = (int)(r * (BAR_W_ - 2));

  tft_.fillRect(BAR_X_ + 1, BAR_Y_ + 1, BAR_W_ - 2, BAR_H_ - 2, C_BLACK);
  tft_.fillRect(BAR_X_ + 1, BAR_Y_ + 1, fillW, BAR_H_ - 2, C_GREEN);
  tft_.drawRect(BAR_X_, BAR_Y_, BAR_W_, BAR_H_, (d >= maxRange_m_) ? C_RED : C_ORANGE);
}

// -------------------- Overlay --------------------

void TFTDistance::drawOverlay(){
  const int OVER_SIZE = 1;
  int rightBlockW = 120;
  int xRight = tft_.width() - rightBlockW - 4;

  int yFPS   = 4;
  int yStat  = yFPS + 12;
  int yTrack = yStat + 12;
  int yErr   = yTrack + 12;
  int ySys   = yErr + 12;

  // Clear overlay zone
  tft_.fillRect(xRight, yFPS, rightBlockW, 60, C_BLACK);

  // FPS
  tft_.setTextSize(OVER_SIZE);
  tft_.setTextColor(C_CYAN, C_BLACK);
  tft_.setCursor(xRight, yFPS);
  tft_.print("FPS: ");
  tft_.print(fps_, 1);

  // Measurement status
  uint16_t sc = C_WHITE;
  const char* sTxt = "OK";
  switch (status_) {
    case MEAS_OK:         sc = C_WHITE;  sTxt = "OK";      break;
    case MEAS_TIMEOUT:    sc = C_YELLOW; sTxt = "TIMEOUT"; break;
    case MEAS_BAD_CRC:    sc = C_RED;    sTxt = "BAD_CRC"; break;
    case MEAS_BAD_FRAME:  sc = C_RED;    sTxt = "BAD_FRM"; break;
    case MEAS_NO_SIGNAL:  sc = 0x8410;   sTxt = "NO_SIG";  break;
    default:              sc = C_WHITE;  sTxt = "UNK";     break;
  }

  tft_.setTextColor(sc, C_BLACK);
  tft_.setCursor(xRight, yStat);
  tft_.print("STAT: ");
  tft_.print(sTxt);

  // Track state
  uint16_t tc = C_YELLOW;
  const char* tTxt = "SEARCH";
  switch (trackState_) {
    case TRACK_SEARCHING: tc = C_YELLOW; tTxt = "SEARCH"; break;
    case TRACK_CANDIDATE: tc = C_CYAN;   tTxt = "CAND";   break;
    case TRACK_LOCKED:    tc = C_GREEN;  tTxt = "LOCK";   break;
    case TRACK_LOST:      tc = C_RED;    tTxt = "LOST";   break;
    default:              tc = C_YELLOW; tTxt = "SEARCH"; break;
  }

  tft_.setTextColor(tc, C_BLACK);
  tft_.setCursor(xRight, yTrack);
  tft_.print("TRK: ");
  tft_.print(tTxt);

  // Measurement error code
  if (measErrorCode_) {
    tft_.setTextColor(C_RED, C_BLACK);
    tft_.setCursor(xRight, yErr);
    tft_.print("[TC22-E");
    tft_.print(measErrorCode_);
    tft_.print("]");
  }

  // System error code
  if (systemErrorCode_) {
    tft_.setTextColor(C_RED, C_BLACK);
    tft_.setCursor(xRight, ySys);
    tft_.print("SYSTEM-E");
    tft_.print(systemErrorCode_);
  }
}

// -------------------- WiFi --------------------

void TFTDistance::DrawWiFiStatus(){
  const int16_t x = 4;
  const int16_t y = 4;
  const int16_t w = 150;
  const int16_t h = 10;

  tft_.fillRect(x, y, w, h, ILI9341_BLACK);

  if (wifiSSID_.length() == 0) {
    return;
  }

  tft_.setCursor(x, y);
  tft_.setTextSize(1);

  if (wifiConnected_) {
    tft_.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft_.print("WiFi: ");
    tft_.print(wifiSSID_);
  } else {
    tft_.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft_.print("WiFi: ");
    tft_.print(wifiSSID_);
    tft_.print(" ERR");
  }
}