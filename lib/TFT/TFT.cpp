#include "TFT.h"
#include <WiFi.h>

//------ Khởi tạo và cấu hình TFT -------

//Khởi tạo SPI vs panel TFT
void TFTDistance::begin() {
  SPI.begin(pins_.sck, pins_.miso, pins_.mosi, pins_.cs); //remap
  tft_.begin();         
  tft_.setSPISpeed(27000000);
  tft_.setRotation(1);
  // Tính layout theo kích thước thực
  int W = tft_.width();   // 320
  int H = tft_.height();  // 240

  HEADER_H_ = 65;

  BAR_X_ = MARGIN_;
  BAR_W_ = W - 2*MARGIN_;
  BAR_H_ = 12;
  BAR_Y_ = HEADER_H_ + GAP_;
  wifiSSID_ = "";
  wifiConnected_ = false;
  drawStatic(); //vẽ khung label + thanh mức
  drawOverlay(); //vẽ overlay trạng thái
}

//full-scale cho thanh mức (mặc định max 800.0)
void TFTDistance::setMaxRangeMeters(float m) {
  if (m < 3.0f) m = 3.0f; //chặn dưới 3m
  maxRange_m_ = m;
}

//Tracking 
void TFTDistance::setSmoothing(float alpha) {
  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha > 1.0f) alpha = 1.0f;
  alpha_ = alpha;
  filter_.setAlpha(alpha);
}

//Giới hạn thời gian dữ liệu cũ
void TFTDistance::setStaleTimeoutMs(uint32_t ms) { staleTimeoutMs_ = ms; }

//Cập nhật số đo (cm) -> (m) + trạng thái hợp lệ
void TFTDistance::updateDistanceMeters(float m, bool valid) {
  //lọc cơ bản, loại các giá trị <3m hoặc invalid.
  if (valid && m < TC22_BLIND_M) valid = false;
  //mất tín hiệu lâu thì reset hiển thị, còn 1-2 mẫu thì kệ đỡ nhấp nháy màn.
  if(!valid){
    paintDistanceUI();
    drawOverlay();
    return;
  }
  //cập nhật bộ lọc
  float filt = filter_.update(m, valid);
  if(!filter_.hasValue()){
    //Chưa có giá trị hợp lệ thì hiển thị ---.--m
    tft_.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft_.setCursor(MARGIN_ + 8, 25 + 2*8 + 10 );
    tft_.print("---.- m");
    drawOverlay();
    return;
  }

  //Đã có giá trị sau lọc
  float d_m = filt;
  lastUpdateMs_ = millis();

  paintDistanceUI(); //Vẽ số + thanh mức
  drawOverlay(); // Update cho header về FPS và status
}

void TFTDistance::setStatus(MeasStatus s) {
  status_ = s;
  switch (s) {
    case MEAS_OK:        measErrorCode = 0; break;
    case MEAS_TIMEOUT:   measErrorCode = 10; break;
    case MEAS_NO_SIGNAL:   measErrorCode = 11; break;
    case MEAS_BAD_CRC: measErrorCode = 12; break;
    default:           measErrorCode = 0; break;
  }
  drawOverlay();
}

void TFTDistance::setSystemError (uint8_t errCode) {
  systemErrorCode = errCode; //0 nếu k lỗi
  drawOverlay();
}

void TFTDistance::setFPS(float fps) {
  fps_ = fps;
  drawOverlay();
}

void TFTDistance::update(const Measurement& m, float fps) {
  setStatus(m.status);
  setFPS(fps);
  // hợp lệ khi MEAS_OK; còn lại coi như invalid để UI hiện ---.- m
  bool valid = (m.status == MEAS_OK);
  updateDistanceMeters(m.dist_m, valid);
  DrawWiFiStatus();
}

// ---------------- UI ----------------
// update 14/11/2025
void TFTDistance::setWiFiStatus(const char* SSID, bool connected) {
  wifiSSID_ = String(SSID ? SSID : "");
  wifiConnected_ = connected;
    // vẽ lại status mỗi lần thay đổi
  DrawWiFiStatus();
}
void TFTDistance::drawStatic() {
  tft_.fillScreen(C_BLACK); //clear

  //Header khung
  tft_.drawRect(0,0,tft_.width(), HEADER_H_, C_CYAN);
  //Dòng 1
  const int TITLE_SIZE = 2;         // 16 px
  const int TITLE_Y    = 25;  //old:6
  tft_.setCursor(MARGIN_ + 2, TITLE_Y);
  tft_.setTextColor(C_ORANGE, C_BLACK);
  tft_.setTextSize(TITLE_SIZE);
  tft_.print("KHOANG CACH(m)");
  //Dòng 2
  const int VALUE_SIZE = 3;         // 24 px
  const int VALUE_Y    = TITLE_Y + TITLE_SIZE*8 + 10; // 6 + 16 + 4 = 26
  const int VALUE_H    = VALUE_SIZE*8;               // 24 px

  tft_.fillRect(MARGIN_, VALUE_Y - 2, tft_.width() - 2*MARGIN_, VALUE_H + 6, C_BLACK);
  //Thanh mức
  tft_.drawRect(BAR_X_, BAR_Y_, BAR_W_, BAR_H_, C_ORANGE);

  //Nhãn 0 & max ngay dưới bar
  int labelY = BAR_Y_ + BAR_H_ + 12;
  tft_.setTextColor(C_WHITE, C_BLACK);
  tft_.setTextSize(2);
  tft_.setCursor(MARGIN_ + 2, labelY); tft_.print("0");
  String sMax = String((int)maxRange_m_);
  int xr = tft_.width() - (int)sMax.length()*12 - (MARGIN_+2);
  tft_.setCursor(xr, labelY); tft_.print(sMax);

  //clear vùng số trong header
  //tft_.fillRect(MARGIN_, 8 + (HEADER_H_ - 30)/2, tft_.width()-2*MARGIN_, 36, C_BLACK);
}

//In số lớn
void TFTDistance::printDistanceValue(const String& s, uint16_t color) {
  const int TITLE_SIZE = 2;
  const int TITLE_Y    = 25;
  const int VALUE_SIZE = 3;
  const int VALUE_Y    = TITLE_Y + TITLE_SIZE*8 + 10; //ngay dưới tiêu đề
  const int VALUE_H    = VALUE_SIZE*8;
  tft_.fillRect(MARGIN_, VALUE_Y - 2, tft_.width() - 2*MARGIN_, VALUE_H+6, C_BLACK);
//tft_.fillRect(MARGIN_, 8 + (HEADER_H_ - 30)/2, tft_.width()-2*MARGIN_, 36, C_BLACK);     // xóa số cũ
  tft_.setTextColor(color, C_BLACK);
  tft_.setTextSize(VALUE_SIZE);
  tft_.setCursor(MARGIN_ + 8, VALUE_Y );
  tft_.print(s);
}

// Vẽ số + thanh mức theo trạng thái hiện tại (hiển thị stale khi mất dữ liệu)
void TFTDistance::paintDistanceUI() {
  bool stale = (millis() - lastUpdateMs_) > staleTimeoutMs_;
  float d = filter_.value();
  if (isnan(d) || stale) {
    printDistanceValue(String("---.- m"), C_RED);
    tft_.fillRect(BAR_X_+1, BAR_Y_+1, BAR_W_-2, BAR_H_-2, C_BLACK);
    tft_.drawRect(BAR_X_, BAR_Y_, BAR_W_, BAR_H_, C_ORANGE);
    return;
  }

  String s = String(d, 1) + " m";
  printDistanceValue(s, C_YELLOW);

  float r = d / maxRange_m_;
  if (r < 0) r = 0; if (r > 1) r = 1;
  int fillW = (int)(r * (BAR_W_ - 2));

  tft_.fillRect(BAR_X_+1, BAR_Y_+1, BAR_W_-2, BAR_H_-2, C_BLACK);
  tft_.fillRect(BAR_X_+1, BAR_Y_+1, fillW,      BAR_H_-2, C_GREEN);
  tft_.drawRect(BAR_X_, BAR_Y_, BAR_W_, BAR_H_, (d >= maxRange_m_) ? C_RED : C_ORANGE);
}

// Overlay: hiện FPS + Status trong header (góc phải)
void TFTDistance::drawOverlay() {
  // Khu vực nhỏ bên phải header
  const int OVER_SIZE = 1;    // chữ nhỏ
  //const int TITLE_Y   = 6;
  int rightBlockW = 110;
  int xRight = tft_.width() - rightBlockW - 4/*MARGIN_*/;
  int yFPS = 4;
  int yStat  = yFPS + 12;


  // Xoá 2 dòng overlay
  tft_.fillRect(xRight, yFPS,        rightBlockW, 10, C_BLACK);
  tft_.fillRect(xRight, yStat,       rightBlockW, 10, C_BLACK);

  // FPS (dòng trên)
  tft_.setTextSize(OVER_SIZE);
  tft_.setTextColor(C_CYAN, C_BLACK);
  tft_.setCursor(xRight, yFPS);
  tft_.print("FPS: ");
  tft_.print(fps_, 1);

  // Map trạng thái -> text & màu
  uint16_t sc = C_WHITE;
  const char* sTxt = "OK";
  switch (status_) {
    case MEAS_OK:        sc = C_WHITE;  sTxt = "OK";        break;
    case MEAS_TIMEOUT:   sc = C_YELLOW; sTxt = "TIMEOUT";   break;
    case MEAS_BAD_CRC:   sc = C_RED;    sTxt = "BAD_CRC";   break;
    case MEAS_BAD_FRAME: sc = C_RED;    sTxt = "BAD_FRM";   break;
    case MEAS_NO_SIGNAL: sc = 0x8410;   sTxt = "NO_SIG";    break; // xám
  }

  // Status (dòng dưới)
  tft_.setTextColor(sc, C_BLACK);
  tft_.setCursor(xRight, yStat);
  tft_.print("STAT: ");
  tft_.print(sTxt);

  int yErr = yStat + 12;
  if(measErrorCode){
    tft_.setCursor(xRight, yErr);
    tft_.setTextColor(C_RED, C_BLACK);
    tft_.print("[TC22-E");
    tft_.print(measErrorCode);
    tft_.print("]");
  }

  int ySysErr = yErr + 12;
  if(systemErrorCode){
    tft_.setCursor(xRight, ySysErr);
    tft_.setTextColor(C_RED, C_BLACK);
    tft_.print("SYSTEM-E");
    tft_.print(systemErrorCode);
  }
}
void TFTDistance::DrawWiFiStatus() {
  // Khoảng vùng nhỏ ở góc trên để hiển thị WiFi
  const int16_t x = 4;
  const int16_t y = 4;
  const int16_t w = 150;   // rộng bao nhiêu tuỳ độ phân giải
  const int16_t h = 10;    // cao ~1 dòng chữ size 1

  // Xoá vùng cũ
  tft_.fillRect(x, y, w, h, ILI9341_BLACK);

  tft_.setCursor(x, y);
  tft_.setTextSize(1);

  if (wifiSSID_.length() == 0) {
        // chưa có SSID thì không vẽ gì thêm
        return;
    }

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