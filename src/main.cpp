#include "TFT.h"
#include "MeasurementTypes.h"
#include "TC22.h"
#include "Controller.h"

#include <WiFi.h>
#include <esp_task_wdt.h>

#if __has_include("Password.h")
#include "Password.h"
#endif

// =====================
// Chế độ build
// 1 = benchmark / log qua MQTT
// 0 = demo / release, không dùng WiFi/MQTT
// =====================
#ifndef ENABLE_MQTT_LOG
#define ENABLE_MQTT_LOG 0
#endif

#ifndef USE_BUTTON
#define USE_BUTTON 0
#endif

#if ENABLE_MQTT_LOG
//#define MQTT_MAX_PACKET_SIZE 768
#include <PubSubClient.h>
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef MQTT_HOST
#define MQTT_HOST "192.168.88.56"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_TOPIC_LOG
#define MQTT_TOPIC_LOG "thesis/tc22/log"
#endif

#ifndef MQTT_TOPIC_STATUS
#define MQTT_TOPIC_STATUS "thesis/tc22/status"
#endif

#define TRIGGER_PIN 25

// =====================
// Button helpers
// =====================
static bool noButtonEdge() {
  return false;
}

#if USE_BUTTON
static bool btnLow() {
  return digitalRead(TRIGGER_PIN) == LOW;
}

static bool btnFalling() {
  bool nowLow = btnLow();
  uint32_t now = millis();

  static bool btnWasLow = false;
  static uint32_t btnTs = 0;

  bool edge = (nowLow && !btnWasLow && (now - btnTs > 30));
  if (nowLow != btnWasLow) {
    btnWasLow = nowLow;
    btnTs = now;
  }
  return edge;
}
#endif

// =====================
// Device instances
// =====================
TFTPins pins{5, 21, 19, 18, 23, -1};
TFTDistance display(pins);

HardwareSerial& LRF = Serial2;
TC22Driver lrf(LRF, 16, 17);

#if ENABLE_MQTT_LOG
WiFiClient espClient;
PubSubClient mqttClient(espClient);

static uint32_t lastWiFiAttemptMs = 0;
static uint32_t lastMQTTAttemptMs = 0;

static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
static constexpr uint32_t MQTT_RETRY_INTERVAL_MS = 10000;

static bool mqttReady = false;
#endif

void mqttPublishLog(const char* payload);

// =====================
// App
// =====================
#if USE_BUTTON
Controller app(lrf, display, btnFalling, mqttPublishLog);
#else
Controller app(lrf, display, noButtonEdge, mqttPublishLog);
#endif

#if ENABLE_MQTT_LOG
// =====================
// WiFi maintenance (non-blocking)
// =====================
static void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    display.setWiFiStatus(WIFI_SSID, true);
    return;
  }

  display.setWiFiStatus(WIFI_SSID, false);

  uint32_t now = millis();
  if (now - lastWiFiAttemptMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWiFiAttemptMs = now;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// =====================
// MQTT maintenance (retry thưa, không block)
// SYSTEM-E21 = MQTT connect fail
// =====================
static void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) {
    mqttReady = true;
    return;
  }

  uint32_t now = millis();
  if (now - lastMQTTAttemptMs < MQTT_RETRY_INTERVAL_MS) return;
  lastMQTTAttemptMs = now;

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  String clientId = String("ESP32_TC22_") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = mqttClient.connect(clientId.c_str());

  if (ok) {
    mqttReady = true;
    mqttClient.publish(MQTT_TOPIC_STATUS, "TC22 logger online");
    display.setSystemError(0);
  } else {
    mqttReady = false;
    display.setSystemError(21);   // connect fail
  }
}
#endif

// =====================
// Callback publish log
// SYSTEM-E22 = publish fail
// =====================
void mqttPublishLog(const char* payload) {
#if ENABLE_MQTT_LOG
  if (!mqttReady) return;

  if (!mqttClient.connected()) {
    mqttReady = false;
    display.setSystemError(21);   // mất kết nối MQTT
    return;
  }

  bool ok = mqttClient.publish(MQTT_TOPIC_LOG, payload);
  if (!ok) {
    display.setSystemError(22);   // publish fail
  }
#else
  (void)payload;
#endif
}

// =====================
// setup
// =====================
void setup() {
  Serial.begin(115200);
  delay(1500);
#if ENABLE_MQTT_LOG
  mqttClient.setBufferSize(1024);
#endif
#if USE_BUTTON
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
#endif
  // Khởi động lõi đo trước
  app.begin();

  // Đổi mã này trước mỗi bài test nếu cần
  app.setTestID(0);

#if ENABLE_MQTT_LOG
  WiFi.mode(WIFI_STA);
  lastWiFiAttemptMs = 0;
  lastMQTTAttemptMs = 0;
  mqttReady = false;
#else
  WiFi.mode(WIFI_OFF);
  btStop();
#endif

  esp_task_wdt_init(5, true);
  esp_task_wdt_add(NULL);
}

// =====================
// loop
// app.tick() luôn là ưu tiên số 1
// =====================
void loop() {
  esp_task_wdt_reset();

  // 1) Lõi thiết bị luôn chạy
  app.tick();

#if ENABLE_MQTT_LOG
  // 2) Nhánh phụ: duy trì WiFi + MQTT kiểu nhẹ
  maintainWiFi();
  maintainMQTT();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // Nếu WiFi rớt, hạ cờ MQTT
  if (WiFi.status() != WL_CONNECTED) {
    mqttReady = false;
  }
#endif
}