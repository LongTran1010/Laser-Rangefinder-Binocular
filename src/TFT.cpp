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
  drawRate();
  DrawWiFiStatus();
}

// -------------------- Config --------------------

void TFTDistance::setMaxRangeMeters(float m){
  if (m < 3.0f) m = 3.0f;
  maxRange_m_ = m;

  drawStatic();
  paintDistanceUI();
  drawOverlay();
  drawRate();
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

void TFTDistance::setRangeRate(float rate_mps, bool valid){
  rangeRateMps_ = rate_mps;
  // Chi hien thi khi co estimate hop le va gia tri huu han
  rateValid_ = valid && isfinite(rate_mps);
  drawRate();
}

void TFTDistance::render(const TrackerOutput& out){
  // ===== Cap nhat toan bo state, KHONG ve tung phan =====
  // (truoc day moi setter tu goi drawOverlay -> render goi drawOverlay 4-5 lan,
  //  gay cham va backlog frame. Nay gom lai, ve 1 lan duy nhat.)
  //
  // Fix #6 v2: LUON update state (khong skip). Chi skip DRAW khi overlay visible.
  // Truoc do return som lam mat cap nhat displayedDistanceM_/fps_/lastUpdateMs_,
  // dan den sau overlay hide se hien state cu (stale timeout gia).
  status_ = out.measStatus;
  switch (status_) {
    case MEAS_OK:        measErrorCode_ = 0;  break;
    case MEAS_TIMEOUT:   measErrorCode_ = 10; break;
    case MEAS_NO_SIGNAL: measErrorCode_ = 11; break;
    case MEAS_BAD_CRC:   measErrorCode_ = 12; break;
    case MEAS_BAD_FRAME: measErrorCode_ = 13; break;
    default:             measErrorCode_ = 0;  break;
  }
  fps_        = out.fps;
  trackState_ = out.trackState;

  // Khoang cach: chi cap nhat khi hop le; invalid thi giu gia tri cu
  float d = out.filteredDistanceM;
  bool  distValid = out.hasEstimate && !isnan(d) && (d >= TC22_BLIND_M);
  if (distValid) {
    displayedDistanceM_ = d;
    lastUpdateMs_ = millis();
  }

  // Van toc chi co o mode alpha-beta (baseline tra ve NAN)
  rangeRateMps_ = out.rangeRateMps;
  rateValid_    = out.hasEstimate
                  && (out.estimatorMode == EST_ALPHABETA)
                  && isfinite(out.rangeRateMps);

  // ===== Ve MOT lan duy nhat =====
  // Fix #6 v2: chi skip DRAW main area khi overlay hien.
  // State da duoc update o tren -> sau khi overlay hide, paintDistanceUI() ve
  // ngay du lieu MOI, khong bi stale.
  if (!overlayVisible_ && !flashVisible_) {
    paintDistanceUI();
    drawOverlay();
    drawRate();
  }
  // Status bar (WiFi/Preset/BAT/Lock) van duoc ve xuyen suot
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
  // Fix #5 v2: DICH XUONG duoi status bar (y=28) de khong xoa Battery/Lock icon.
  // Status bar top 28px danh cho PRESET/WIFI/BAT/LOCK - overlay stat phai duoi y>=30.
  const int OVER_SIZE = 1;
  int rightBlockW = 120;
  int xRight = tft_.width() - rightBlockW - 4;

  int yFPS   = 32;                  // truoc: 4 -> overlap status bar
  int yStat  = yFPS + 12;
  int yTrack = yStat + 12;
  int yErr   = yTrack + 12;
  int ySys   = yErr + 12;

  // Clear overlay zone (chi vung stat, khong dung status bar top)
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
    case TRACK_STABLE:    tc = C_GREEN;  tTxt = "STABLE";   break;
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

// -------------------- Range rate (velocity) --------------------

void TFTDistance::drawRate(){
  // Vi tri: ngay duoi nhan thanh muc (bar labels)
  const int RATE_SIZE = 2;
  const int rateY = BAR_Y_ + BAR_H_ + 12 + (2 * 8) + 8;  // duoi label bar
  const int rateH = RATE_SIZE * 8;

  // Xoa vung cu
  tft_.fillRect(MARGIN_, rateY - 2, tft_.width() - 2 * MARGIN_, rateH + 6, C_BLACK);

  tft_.setTextSize(RATE_SIZE);
  tft_.setCursor(MARGIN_ + 2, rateY);

  if (!rateValid_) {
    // Mode baseline hoac chua co estimate -> khong co van toc
    tft_.setTextColor(0x8410, C_BLACK);   // xam
    tft_.print("TOC DO: --");
    return;
  }

  // Quy uoc: rate < 0 -> khoang cach giam -> muc tieu lai gan
  //          rate > 0 -> khoang cach tang -> muc tieu ra xa
  const float DEADBAND = 0.15f;   // m/s, nguong coi nhu dung yen
  uint16_t color;
  const char* arrow;
  if (fabsf(rangeRateMps_) < DEADBAND) {
    color = C_WHITE;
    arrow = " ~";          // dung yen
  } else if (rangeRateMps_ < 0.0f) {
    color = C_GREEN;
    arrow = " <<";         // lai gan
  } else {
    color = C_ORANGE;
    arrow = " >>";         // ra xa
  }

  tft_.setTextColor(color, C_BLACK);
  tft_.print("V:");
  tft_.print(rangeRateMps_, 2);
  tft_.print(" m/s");
  tft_.print(arrow);
}

// -------------------- WiFi --------------------

void TFTDistance::DrawWiFiStatus(){
  // Fix #5 v2: doi vi tri WiFi de KHONG overlap voi PRESET (x=4..96).
  // Layout status bar: PRESET (4..96) | WIFI (100..188) | BAT (192..276) | LOCK (290..312).
  const int16_t x = 100;
  const int16_t y = 6;
  const int16_t w = 88;
  const int16_t h = 12;

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

// ============================================================
//  UX v2: preset name, battery, lock icon, overlay, flash
// ============================================================

// --- Layout constants cho status bar + info bar ---
// Fix #5: bo cuc lai de tranh overlap voi WiFi (x=4..154) va tinh trang bar cu.
// Chia status bar 320px thanh 4 vung: PRESET (0-100), WIFI (100-190),
// BAT (190-280), LOCK (290-315).
static constexpr int STATUS_BAR_Y      = 0;
static constexpr int STATUS_BAR_H      = 28;
static constexpr int PRESET_X          = 4;
static constexpr int PRESET_Y          = 6;
static constexpr int PRESET_W          = 92;   // PRESET: 4..96
static constexpr int BAT_X             = 192;  // BAT: 192..276 (khong overlap WIFI)
static constexpr int BAT_Y             = 6;
static constexpr int BAT_W             = 80;
static constexpr int LOCK_X            = 290;  // LOCK: 290..314
static constexpr int LOCK_Y            = 6;
static constexpr int LOCK_W            = 22;
static constexpr int LOCK_H            = 16;

static constexpr int INFOBAR_H         = 32;
static constexpr int OVERLAY_X         = 30;
static constexpr int OVERLAY_Y         = 70;
static constexpr int OVERLAY_W         = 260;
static constexpr int OVERLAY_H         = 100;

// -----------------------------------------------------------
// setPresetInfo — luu state + redraw preset name + info bar
// -----------------------------------------------------------
void TFTDistance::setPresetInfo(uint8_t id, const char* name, const char* use_case,
                                 float alpha, float beta, float gate_m) {
  presetId_       = id;
  presetName_     = (name != nullptr) ? String(name) : String("");
  presetUseCase_  = (use_case != nullptr) ? String(use_case) : String("");
  presetAlpha_    = alpha;
  presetBeta_     = beta;
  presetGate_     = gate_m;
  drawPresetName();
  drawPresetInfoBar();
}

// -----------------------------------------------------------
// setBatteryPercent — chi redraw khi % thay doi
// -----------------------------------------------------------
void TFTDistance::setBatteryPercent(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (pct == batteryPct_) return;   // khong doi thi bo qua
  batteryPct_ = pct;
  drawBatteryIcon();
}

// -----------------------------------------------------------
// setLockIcon — hien/an icon LOCK
// -----------------------------------------------------------
void TFTDistance::setLockIcon(bool locked) {
  if (locked == locked_) return;
  locked_ = locked;
  drawLockIcon();
}

// -----------------------------------------------------------
// showPresetOverlay — hien popup preset 2s giua man hinh
// -----------------------------------------------------------
void TFTDistance::showPresetOverlay(uint32_t duration_ms) {
  overlayVisible_  = true;
  overlayHideAtMs_ = millis() + duration_ms;
  drawPresetOverlay();
}

// -----------------------------------------------------------
// showFlash — hien text ngan ("LOCKED", "SAVED"...)
// -----------------------------------------------------------
void TFTDistance::showFlash(const char* msg, uint32_t duration_ms) {
  flashMsg_       = (msg != nullptr) ? String(msg) : String("");
  flashVisible_   = true;
  flashHideAtMs_  = millis() + duration_ms;
  drawFlash();
}

// -----------------------------------------------------------
// updateOverlay — poll trong loop() de auto-hide
// -----------------------------------------------------------
void TFTDistance::updateOverlay() {
  uint32_t now = millis();
  if (overlayVisible_ && now >= overlayHideAtMs_) {
    hideOverlay();
  }
  if (flashVisible_ && now >= flashHideAtMs_) {
    hideFlash();
  }
}

// ============================================================
//  Private draw methods
// ============================================================

// PRESET name goc trai status bar
void TFTDistance::drawPresetName() {
  tft_.fillRect(PRESET_X, PRESET_Y, PRESET_W, 16, C_BLACK);
  tft_.setCursor(PRESET_X, PRESET_Y);
  tft_.setTextColor(C_YELLOW, C_BLACK);
  tft_.setTextSize(1);
  tft_.print("PRESET:");
  tft_.print(presetName_);
}

// BAT % o giua status bar (icon + so)
void TFTDistance::drawBatteryIcon() {
  tft_.fillRect(BAT_X, BAT_Y, BAT_W, 16, C_BLACK);
  tft_.setCursor(BAT_X, BAT_Y);
  tft_.setTextSize(1);
  uint16_t color = C_GREEN;
  if (batteryPct_ < 20)      color = C_RED;
  else if (batteryPct_ < 40) color = C_YELLOW;
  tft_.setTextColor(color, C_BLACK);
  tft_.print("BAT:");
  tft_.print(batteryPct_);
  tft_.print("%");
}

// LOCK icon goc phai status bar (khoi vuong don gian)
void TFTDistance::drawLockIcon() {
  tft_.fillRect(LOCK_X, LOCK_Y, LOCK_W, LOCK_H, C_BLACK);
  if (locked_) {
    // Ve icon padlock don gian: hinh chu nhat + arc phia tren
    tft_.drawRect(LOCK_X + 2, LOCK_Y + 5, 14, 10, C_ORANGE);
    tft_.drawRect(LOCK_X + 3, LOCK_Y + 6, 12, 8, C_ORANGE);
    // Vong tren (shackle)
    tft_.drawRect(LOCK_X + 5, LOCK_Y, 8, 6, C_ORANGE);
    tft_.drawFastHLine(LOCK_X + 5, LOCK_Y, 8, C_ORANGE);
  }
}

// INFO BAR bottom: FPS, STATE, alpha, beta
void TFTDistance::drawPresetInfoBar() {
  int y = 240 - INFOBAR_H;
  tft_.fillRect(0, y, 320, INFOBAR_H, C_BLACK);
  tft_.setCursor(6, y + 8);
  tft_.setTextSize(1);
  tft_.setTextColor(C_WHITE, C_BLACK);
  tft_.printf("a=%.2f b=%.3f gate=%.1fm", presetAlpha_, presetBeta_, presetGate_);
  tft_.setCursor(6, y + 20);
  tft_.setTextColor(C_CYAN, C_BLACK);
  tft_.print(presetUseCase_);
}

// PRESET overlay o giua main area
void TFTDistance::drawPresetOverlay() {
  // Background yellow
  tft_.fillRoundRect(OVERLAY_X, OVERLAY_Y, OVERLAY_W, OVERLAY_H, 8, C_YELLOW);
  tft_.drawRoundRect(OVERLAY_X, OVERLAY_Y, OVERLAY_W, OVERLAY_H, 8, C_BLACK);

  tft_.setTextColor(C_BLACK, C_YELLOW);
  tft_.setTextSize(2);
  tft_.setCursor(OVERLAY_X + 20, OVERLAY_Y + 15);
  tft_.print(presetName_);
  tft_.printf(" (%d/6)", (int)presetId_ + 1);

  tft_.setTextSize(1);
  tft_.setCursor(OVERLAY_X + 20, OVERLAY_Y + 50);
  tft_.printf("a=%.2f b=%.3f", presetAlpha_, presetBeta_);
  tft_.setCursor(OVERLAY_X + 20, OVERLAY_Y + 70);
  tft_.print(presetUseCase_);
}

// FLASH message: text lon o giua
void TFTDistance::drawFlash() {
  tft_.fillRoundRect(OVERLAY_X + 40, OVERLAY_Y + 20, OVERLAY_W - 80, 60, 8, C_ORANGE);
  tft_.drawRoundRect(OVERLAY_X + 40, OVERLAY_Y + 20, OVERLAY_W - 80, 60, 8, C_BLACK);
  tft_.setTextColor(C_BLACK, C_ORANGE);
  tft_.setTextSize(2);
  int text_w = flashMsg_.length() * 12;
  int cx = (320 - text_w) / 2;
  tft_.setCursor(cx, OVERLAY_Y + 40);
  tft_.print(flashMsg_);
}

// Xoa overlay va redraw main area de tra ve view binh thuong
void TFTDistance::hideOverlay() {
  overlayVisible_ = false;
  tft_.fillRect(OVERLAY_X, OVERLAY_Y, OVERLAY_W, OVERLAY_H, C_BLACK);
  paintDistanceUI();  // redraw main area
}

void TFTDistance::hideFlash() {
  flashVisible_ = false;
  tft_.fillRect(OVERLAY_X + 40, OVERLAY_Y + 20, OVERLAY_W - 80, 60, C_BLACK);
  paintDistanceUI();
}