#pragma once
#include "Password.h"
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h> 
#include <MeasurementTypes.h>
#include "DistanceFilter.h"
// RGB565 colors
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFD20
#define C_GREEN   0x07E0
#define C_RED     0xF800
#define C_CYAN    0x07FF

//Pin map cho ILI9341 (mặc định VSPI)
struct TFTPins {
  int8_t cs   ;  // Chip Select
  int8_t dc   ;  // Data/Command ---- A0
  int8_t rst  ;  // Reset
  int8_t sck  ;  // SPI SCK  (VSPI)
  int8_t mosi ;  // SPI MOSI (VSPI) ---- SDA
  int8_t miso ;  // Không dùng với ILI9341
  constexpr TFTPins(int8_t cs_=5, int8_t dc_=21, int8_t rst_=19,
                    int8_t sck_=18, int8_t mosi_=23, int8_t miso_=-1)
  : cs(cs_), dc(dc_), rst(rst_), sck(sck_), mosi(mosi_), miso(miso_) {}  
};

//Widget hiển thị “Khoảng cách(m)” + thanh mức
class TFTDistance {
public:
  explicit TFTDistance(const TFTPins& pins = {}) //Constructor
    : pins_(pins), //
      tft_(pins.cs, pins.dc, pins.rst) {} //Hardware SPI, tham chiếu hằng

  void begin();                                            // khởi tạo SPI + vẽ khung
  // void updateDistanceCm(uint16_t dist_cm, bool valid){
  //   updateDistanceMeters(dist_cm / 100.0f, valid);
  // };     // cập nhật số đo (cm) + trạng thái hợp lệ

  //update 24/10
  void updateDistanceMeters(float dist_m, bool valid);     // cập nhật số đo (m) + trạng thái hợp lệ
  void updateDistanceDm(uint16_t dist_dm, bool valid) {      // TC22
    updateDistanceMeters(dist_dm / 10.0f, valid);
  }
  // Tùy chỉnh
  void setMaxRangeMeters(float m);       // full-scale cho thanh mức (mặc định 30.0)
  void setSmoothing(float alpha);        // hệ số EMA 0..1 (mặc định 0.25)
  void setStaleTimeoutMs(uint32_t ms);   // timeout ms (mặc định 1000)

  void setStatus(MeasStatus s);                 // vẽ/ghi nhớ trạng thái
  void setSystemError (uint8_t errCode);        // vẽ/ghi nhớ mã lỗi hệ thống
  void setFPS(float fps);                       // vẽ/ghi nhớ FPS
  void update(const Measurement& m, float fps); // tiện ích gộp
  //update 17/10/2025
  float getFilter() const { return filter_.value(); }    // lấy hệ số EMA hiện tại
  //update 14/11/2025
  void setWiFiStatus(const char* SSID, bool connected); // hiển thị trạng thái WiFi
private:

  TFTPins pins_;
  Adafruit_ILI9341 tft_;

  //trạng thái hiển thị, các mặc định
  //float    distEMA_m_      = NAN;     // giá trị đã làm mượt (m)
  float    alpha_          = 0.25f;   // hệ số EMA
  //mới
  float     fps_           = 0.0f;
  MeasStatus status_       = MEAS_TIMEOUT;
  uint8_t   systemErrorCode   = 0;      // mã lỗi hệ thống (nếu có)
  uint8_t   measErrorCode = 0;      // mã lỗi đo (nếu có)
  //layout động (tính theo width/height sau khi begin)
  int BAR_X_, BAR_Y_, BAR_W_, BAR_H_;
  int HEADER_H_ = 42, MARGIN_ = 10, GAP_ = 18;
  //update 26/11/2025
  //uint8_t rejectCount = 0; //đếm lần chặn
  //const uint8_t MAX_REJECT = 5; //ngưỡng chặn trc khi nhận giá trị mới
  //const float GATE_THRESHOLD = 10.0f; //ngưỡng chênh cho phép giữa 2 lần đo.
  //update 17/10/2025
  //Thêm median lọc nhiễu xung quanh EMA
  //float buff_[5]; uint8_t length_ = 0, idx_ = 0;
  // float median5() const {
  //   //sort (insertion sort) 
  //   float a[5];
  //   for (int i = 0; i < 5; i++) a[i] = buff_[i];
  //   for (int i = 1; i < 5; i++) {
  //     float k = a[i];
  //     int j = i - 1;
  //     while (j >= 0 && a[j] > k) {
  //       a[j + 1] = a[j];
  //       j--;
  //     }
  //     a[j + 1] = k;
  //   }
  //   return a[2];
  // }
  //update 8/12/2025
  DistanceFilter filter_; // bộ lọc khoảng cách
  float    maxRange_m_     = 800.0f;   // thang hiển thị
  uint32_t lastUpdateMs_   = 0, staleTimeoutMs_ = 1000;
  //update 14/11/2025
  String wifiSSID_ = "";
  bool wifiConnected_ = false;
  void DrawWiFiStatus(); // vẽ trạng thái WiFi
  // helpers
  void drawStatic();                                       // vẽ khung/nhãn 1 lần
  void printDistanceValue(const String& s, uint16_t color);// in số lớn
  void paintDistanceUI();                                  // vẽ số + thanh mức theo trạng thái hiện tại
  void drawOverlay(); //FPS + trạng thái
};
