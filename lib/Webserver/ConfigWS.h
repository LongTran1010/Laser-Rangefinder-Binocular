#ifndef CONFIGWS_H
#define CONFIGWS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// Cấu hình mạng
struct Config {
  String ssid;
  String pass;
};

class ConfigWS {
public:
  // apSsid/apPass: tên & pass của WiFi AP khi vào chế độ config
  ConfigWS(const char* AP_ssid = "setup_webserser",
               const char* AP_pass = "12345678");

  //Đọc config từ NVS, trả về true nếu đã có SSID
  bool load(Config& outConfig);

  // Đảm bảo có WiFi:
  // 1) Nếu đã có config & connect được -> true
  // 2) Nếu chưa có / connect fail -> mở AP + webserver cho user cấu hình,
  bool ensureWiFi(Config& outConfig,
                  uint32_t connectTimeoutMs = 15000);

private:
  const char* _AP_ssid;
  const char* _AP_pass;

  WebServer   server;
  Preferences prefs;
  Config cfg;
  bool SuccessConnect ;

  void startAP();
  void setupRoutes();
  void handleRoot();
  void handleSave();
  void loopPortal();

  bool connectSTA(uint32_t timeoutMs);

  static const char* PREF_NAMESPACE;
  static const char* PREF_KEY_SSID;
  static const char* PREF_KEY_PASS;
};

#endif // CONFIGWS_H
