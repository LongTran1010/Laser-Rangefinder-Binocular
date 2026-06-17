#include "TFT.h"
#include "MeasurementTypes.h"
#include "TC22/TC22.h"
#include "Controller.h"
#include "AlphaBetapresets.h"
#include <WiFi.h>
#include <esp_task_wdt.h>

#if __has_include("Password.h")
#include "Password.h"
#endif

// =====================
// Build flags
// =====================
#ifndef ENABLE_MQTT_LOG
#define ENABLE_MQTT_LOG 0
#endif
#ifndef USE_BUTTON
#define USE_BUTTON 0
#endif

#if ENABLE_MQTT_LOG
#include <PubSubClient.h>
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef MQTT_HOST
#define MQTT_HOST "172.20.10.2"
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
// Cau hinh thi nghiem (override qua build flags trong platformio.ini)
// =====================
#ifndef ESTIMATOR_MODE
#define ESTIMATOR_MODE EST_ALPHABETA
//   EST_BASELINE   = pipeline cu Gating + Median + EMA
//   EST_ALPHABETA  = alpha-beta tracker (giai doan 2)
//   EST_RAW_ONLY   = khong loc - dung cho fair benchmark offline
#endif
#ifndef AB_PRESET_ID
#define AB_PRESET_ID 3
//   0 = A_EMA_LIKE   1 = B_EMA_LIKE   2 = C_AB_LIGHT
//   3 = D_AB_BALANCED (v1)   4 = E_AB_AGGRESSIVE   5 = F_AB_HANDHELD
#endif
#ifndef TEST_ID
#define TEST_ID 8003
#endif

static constexpr EstimatorMode kEstimatorMode = ESTIMATOR_MODE;
static constexpr AbPreset      kAbPreset      = static_cast<AbPreset>(AB_PRESET_ID);
static constexpr uint16_t      kTestId        = TEST_ID;

// =====================
// Button helpers
// =====================
static bool noButtonEdge() { return false; }

#if USE_BUTTON
static bool btnLow() { return digitalRead(TRIGGER_PIN) == LOW; }
static bool btnFalling() {
  bool nowLow = btnLow();
  uint32_t now = millis();
  static bool btnWasLow = false;
  static uint32_t btnTs = 0;
  bool edge = (nowLow && !btnWasLow && (now - btnTs > 30));
  if (nowLow != btnWasLow) { btnWasLow = nowLow; btnTs = now; }
  return edge;
}
#endif

// =====================
// Device instances
// =====================
//màn hình
TFTPins pins{5, 21, 19, 18, 23, -1};
TFTDistance display(pins);

//cảm biến TC22 qua UART thứ 2 (Serial2)
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

#if USE_BUTTON
Controller app(lrf, display, btnFalling, mqttPublishLog);
#else
Controller app(lrf, display, noButtonEdge, mqttPublishLog);
#endif

#if ENABLE_MQTT_LOG
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

static void maintainMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) { mqttReady = true; return; }
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
    display.setSystemError(21);   // SYSTEM-E21 = MQTT connect fail
  }
}
#endif

void mqttPublishLog(const char* payload) {
#if ENABLE_MQTT_LOG
  if (!mqttReady) return;
  if (!mqttClient.connected()) {
    mqttReady = false;
    display.setSystemError(21);
    return;
  }
  bool ok = mqttClient.publish(MQTT_TOPIC_LOG, payload);
  if (!ok) {
    display.setSystemError(22);   // SYSTEM-E22 = publish fail
  }
#else
  (void)payload;
#endif
}

void setup() {
  Serial.begin(115200);
  delay(1500);
#if ENABLE_MQTT_LOG
  mqttClient.setBufferSize(1024);
#endif
#if USE_BUTTON
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
#endif

  // 1) Test ID phai set TRUOC begin() de moi publish co dung ID
  app.setTestID(kTestId);

  // 2) Estimator mode truoc begin() (begin() reset state dung mode)
  app.setEstimatorMode(kEstimatorMode);

  // 3) Ap preset alpha-beta neu can
  if (kEstimatorMode == EST_ALPHABETA) {
    AlphaBetaConfig abCfg = makeAlphaBetaPreset(kAbPreset);
    app.setAlphaBetaConfig(abCfg);
    app.setPresetId(static_cast<uint8_t>(kAbPreset));
    Serial.printf("[INIT] Estimator=ALPHABETA preset=%s alpha=%.3f beta=%.4f gate=%.1fm min_dt=%.3fs\n",
                  abPresetName(kAbPreset), abCfg.alpha, abCfg.beta, abCfg.gateThresholdM, abCfg.minDtS);
  } else if (kEstimatorMode == EST_BASELINE) {
    Serial.println("[INIT] Estimator=BASELINE (Gating+Median+EMA)");
    app.setPresetId(0xFF);
  } else if (kEstimatorMode == EST_RAW_ONLY) {
    Serial.println("[INIT] Estimator=RAW_ONLY (pass-through, no filter)");
    app.setPresetId(0xFF);
  }
  Serial.printf("[INIT] fw_version=%s test_id=%u\n", FW_VERSION_STR, static_cast<unsigned>(kTestId));

  // 4) Khoi dong loi do
  app.begin();

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

void loop() {
  esp_task_wdt_reset();
  app.tick();

#if ENABLE_MQTT_LOG
  maintainWiFi();
  maintainMQTT();
  if (mqttClient.connected()) { mqttClient.loop(); }
  if (WiFi.status() != WL_CONNECTED) { mqttReady = false; }
#endif
}
