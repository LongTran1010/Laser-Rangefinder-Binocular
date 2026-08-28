#include "TFT.h"
#include "MeasurementTypes.h"
#include "TC22/TC22.h"
#include "Controller.h"
#include "AlphaBetapresets.h"
#include <WiFi.h>
#include <esp_task_wdt.h>
#include "ButtonManager.h"
#include "PresetManager.h"
#include "BatteryMonitor.h"   // Fix #12
#if __has_include("Password.h")
#include "Password.h"
#endif

// =====================
// Build flags
// =====================
#ifndef ENABLE_MQTT_LOG
#define ENABLE_MQTT_LOG 0
#endif

// NOTE (v2): USE_BUTTON legacy da bo. Thay bang ButtonManager (GPIO 25)
// dung de cycle preset thay vi single-shot trigger nhu v1.

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
#define MQTT_HOST "172.20.10.3"
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

#define BUTTON_PIN 25

#ifndef BUZZER_PIN
#define BUZZER_PIN 33
#endif

// Fix #2: GPIO 2 (LED_BUILTIN tren esp32doit-devkit-v1) xung dot voi TFT DC pin.
// Doi sang GPIO 32 (free, khong xung dot TFT/UART/strapping).
#ifndef LED_PIN
#define LED_PIN 32
#endif

// Fix #12: Battery ADC pin (GPIO 34 = ADC1_CH6, input-only, khong xung dot WiFi)
#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN 34
#endif

// Proposal 2: Runtime preset cycling (button-driven).
// Default = 1 (enabled). Benchmark envs set = 0 de dam bao reproducibility.
#ifndef ENABLE_RUNTIME_PRESETS
#define ENABLE_RUNTIME_PRESETS 1
#endif

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
#define TEST_ID 9301
#endif

static constexpr EstimatorMode kEstimatorMode = ESTIMATOR_MODE;
// Proposal 2: AB_PRESET_ID map thang PresetTable id (0=RAW,1=BASE,2=AB-L,3=AB-B,4=AB-A,5=HAND).
static constexpr uint8_t       kAbPresetId    = AB_PRESET_ID;
static constexpr uint16_t      kTestId        = TEST_ID;

// =====================
// Button callback cho Controller (v2)
// =====================
// Legacy single-shot trigger da bo o v2 (button GPIO 25 dung cycle preset).
// Neu can single-shot mode, trigger qua MQTT command (future).
// noButtonEdge = luon tra false -> Controller khong vao single-shot mode.
static bool noButtonEdge() { return false; }

// =====================
// Device instances
// =====================
//nút nhấn + preset manager (UX v2)
ButtonManager   btn;
PresetManager   presetMgr;
BatteryMonitor  batMon;         // Fix #12

// Fix #7: Non-blocking LED blinker de tranh delay() chan UART frames.
// Truoc do ledBlinkN(3, 80) block 480ms lam range-rate estimation sai.
class LedBlinker {
public:
  void begin(uint8_t pin) { pin_ = pin; digitalWrite(pin_, LOW); }
  void trigger(int times, uint32_t period_ms = 80) {
    remaining_toggles_ = times * 2;   // ON + OFF = 2 toggles
    period_ms_ = period_ms;
    next_toggle_ms_ = millis();
  }
  // Non-blocking - goi trong loop() moi chu ky
  void update() {
    if (remaining_toggles_ == 0) return;
    uint32_t now = millis();
    if (now >= next_toggle_ms_) {
      // remaining_toggles even (2, 4, 6) -> ON, odd -> OFF
      bool level = (remaining_toggles_ % 2 == 0);
      digitalWrite(pin_, level ? HIGH : LOW);
      remaining_toggles_--;
      next_toggle_ms_ = now + period_ms_;
      if (remaining_toggles_ == 0) digitalWrite(pin_, LOW);
    }
  }
private:
  uint8_t  pin_               = 0;
  uint8_t  remaining_toggles_ = 0;
  uint32_t period_ms_         = 80;
  uint32_t next_toggle_ms_    = 0;
};
static LedBlinker ledBlinker;

// tone() da non-blocking san (dung PWM/RMT), giu nguyen.
static inline void buzzerBeepShort() { tone(BUZZER_PIN, 2000, 100); }
static inline void buzzerBeepLong()  { tone(BUZZER_PIN, 2000, 500); }

//màn hình
TFTPins pins{5, 2, 13, 18, 23, 19};   // CS, DC, RST, SCK, MOSI, MISO
// TFTPins pins{5, 21, 19, 18, 23, -1}; // cũ
TFTDistance display(pins);

//cảm biến TC22 qua UART thứ 2 (Serial2)
HardwareSerial& LRF = Serial2;
TC22Driver lrf(LRF, 17, 16); //đảo chân vs mạch cũ

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

// Controller khoi tao voi noButtonEdge (single-shot legacy tat).
// Button GPIO 25 duoc handle rieng qua ButtonManager (cycle preset).
Controller app(lrf, display, noButtonEdge, mqttPublishLog);

// ============================================================
//  UX v2: Preset apply + Button event dispatch
//  (dat SAU khai bao 'app' de tranh forward-reference)
// ============================================================

// Proposal 2: SINGLE source of truth = PresetTable.
// Ca compile-time (benchmark AB_PRESET_ID) va runtime (button/NVS) deu goi
// applyPreset(id) -> khong duplicate logic va tham so.
static void applyPresetById(uint8_t preset_id) {
  if (preset_id >= N_PRESETS) preset_id = 3;  // fallback AB-B
  const PresetConfig& cfg = PRESETS[preset_id];

  // Proposal 3: Config va cache metadata do cac setter rieng dam nhiem.
  // setEstimatorMode() chi select estimator; setBaselineConfig / setAlphaBetaConfig
  // rieng cho tung mode.
  //
  // Fix #1: apply dung mode tu preset table
  app.setEstimatorMode(cfg.mode);

  if (cfg.mode == EST_ALPHABETA) {
    // AlphaBeta: dung PresetTable lam single source of truth (Proposal 2).
    // KHONG goi makeAlphaBetaPreset() de tranh conflict gate value.
    AlphaBetaConfig abCfg;
    abCfg.alpha           = cfg.alpha;
    abCfg.beta            = cfg.beta;
    abCfg.gateThresholdM  = cfg.gate_m;
    abCfg.minDtS          = cfg.min_dt_s;
    abCfg.maxReject       = cfg.max_reject;
    // Cac field khac dung default cua AlphaBetaConfig struct
    app.setAlphaBetaConfig(abCfg);
  } else if (cfg.mode == EST_BASELINE) {
    // Proposal 3 + Review#3: BASELINE preset dung setBaselineConfig() proper API.
    // Truoc day: emaLambda hardcode 0.25f o day, PresetTable.BASE.alpha=0 -> conflict.
    // Nay: PresetTable la SINGLE SOURCE, cfg.alpha carry emaLambda cho BASELINE.
    BaselineConfig bCfg;
    bCfg.emaLambda      = cfg.alpha;      // EMA lambda tu PresetTable (0.25)
    bCfg.gateThresholdM = cfg.gate_m;     // 5m theo PresetTable
    bCfg.maxReject      = cfg.max_reject; // 5 theo PresetTable
    app.setBaselineConfig(bCfg);
  }
  // EST_RAW_ONLY: khong can config, setEstimatorMode da reset cache.

  app.setPresetId(cfg.id);

  // Update TFT thong tin preset (status bar + info bar)
  display.setPresetInfo(cfg.id, cfg.name, cfg.use_case,
                        cfg.alpha, cfg.beta, cfg.gate_m);

  Serial.printf("[Preset] Applied id=%d name=%s mode=%d\n",
                cfg.id, cfg.name, (int)cfg.mode);
}

// Wrapper backward-compat: apply preset dang chon boi presetMgr.
static void applyCurrentPreset() {
  applyPresetById(presetMgr.currentId());
}

// Dispatch button event -> action (goi trong loop())
// Fix #7: dung ledBlinker (non-blocking) thay vi ledBlinkN() blocking.
// Fix #8: check bool return cua saveToNVS() de hien SAVE ERR khi fail.
// Proposal 2: neu ENABLE_RUNTIME_PRESETS=0 (benchmark env), khoa cycle preset
// hoan toan de dam bao reproducibility (khong bi user tinh cu bam nut).
static void handleButtonEvent(ButtonEvent ev) {
  switch (ev) {
    case BTN_SHORT:
      if (btn.isLocked()) {
        display.showFlash("LOCKED", 1000);
        buzzerBeepShort();
      }
#if !ENABLE_RUNTIME_PRESETS
      else {
        // Benchmark env: khoa cycle de reproducibility.
        display.showFlash("BENCH LOCK", 1000);
        buzzerBeepShort();
      }
#else
      else {
        presetMgr.cycleNext();
        applyCurrentPreset();
        display.showPresetOverlay(2000);
        ledBlinker.trigger(1);
      }
#endif
      break;

    case BTN_LONG:
      btn.toggleLock();
      display.setLockIcon(btn.isLocked());
      display.showFlash(btn.isLocked() ? "LOCKED" : "UNLOCKED", 1500);
      ledBlinker.trigger(2);
      break;

    case BTN_VERY_LONG: {
#if !ENABLE_RUNTIME_PRESETS
      // Benchmark: khoa save NVS de reproducibility.
      display.showFlash("BENCH LOCK", 1000);
      buzzerBeepShort();
      break;
#endif
      bool ok = presetMgr.saveToNVS();
      if (ok) {
        display.showFlash("SAVED", 2000);
        ledBlinker.trigger(3);
        buzzerBeepLong();
      } else {
        display.showFlash("SAVE ERR", 2000);
        ledBlinker.trigger(6, 40);   // nhap nhay nhanh de canh bao
        buzzerBeepShort();
      }
      break;
    }

    case BTN_NONE:
    default:
      break;
  }
}

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

  // GPIO cho UX v2 (LED + BUZZER + BUTTON)
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  ledBlinker.begin(LED_PIN);   // Fix #7: init non-blocking blinker
  pinMode(BUZZER_PIN, OUTPUT);
  btn.begin(BUTTON_PIN);
  // batMon.begin(BATTERY_ADC_PIN);   // Tam bo qua - chua wire voltage divider

  // Fix #9: cho phep benchmark env skip NVS override.
  // Trong platformio.ini cua env benchmark (tc22_baseline/raw/ab_*),
  // them build_flag -DIGNORE_NVS_ON_BOOT=1 de dam bao reproducibility.
#ifndef IGNORE_NVS_ON_BOOT
  presetMgr.begin();      // Load preset id from NVS (default = 3)
#else
  Serial.println("[Preset] IGNORE_NVS_ON_BOOT - dung compile-time preset");
#endif

#if ENABLE_MQTT_LOG
  mqttClient.setBufferSize(1024);
#endif

  // 1) Test ID phai set TRUOC begin() de moi publish co dung ID
  app.setTestID(kTestId);

  // 2) Xac dinh preset TRUOC begin(), nhung khoan APPLY lai SAU begin()
  //    (Review#4: truoc day applyPresetById() goi display.setPresetInfo() TRUOC
  //     app.begin() -> Controller.begin() re-init display va xoa preset info).
  uint8_t compile_preset_id;
  if (kEstimatorMode == EST_ALPHABETA) {
    compile_preset_id = (kAbPresetId < N_PRESETS) ? kAbPresetId : 3;
  } else if (kEstimatorMode == EST_BASELINE) {
    compile_preset_id = 1;   // BASE
  } else {
    compile_preset_id = 0;   // RAW
  }

  // 3) Chi set estimator mode truoc begin() (khong dung display).
  app.setEstimatorMode(kEstimatorMode);
  Serial.printf("[INIT] fw_version=%s test_id=%u\n", FW_VERSION_STR, static_cast<unsigned>(kTestId));

  // 4) Khoi dong loi do (Controller init display + tracker)
  app.begin();

  // 5) Xac dinh preset cuoi cung: NVS (neu enabled) override compile-time.
  //    Chi apply DUNG MOT LAN de tranh double-config.
#ifndef IGNORE_NVS_ON_BOOT
  const uint8_t final_preset_id = presetMgr.currentId();
#else
  const uint8_t final_preset_id = compile_preset_id;
#endif
  applyPresetById(final_preset_id);
  // Review#M1: log MODE cua preset thuc su duoc apply, khong phai kEstimatorMode
  // (kEstimatorMode chi la default compile-time; NVS co the override sang
  // preset BASE/RAW voi mode khac).
  const uint8_t final_mode = (final_preset_id < N_PRESETS)
                              ? (uint8_t)PRESETS[final_preset_id].mode
                              : (uint8_t)kEstimatorMode;
  Serial.printf("[INIT] Applied preset_id=%d (compile=%d) mode=%d\n",
                final_preset_id, compile_preset_id, (int)final_mode);

  display.setLockIcon(false);
  display.showPresetOverlay(3000);   // welcome overlay 3s

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

  // 1) Button dispatch (UX v2)
  //    ButtonManager tra ve event tren edge, TFT tu hien overlay/flash
  ButtonEvent ev = btn.update();
  if (ev != BTN_NONE) {
    handleButtonEvent(ev);
  }

  // 2) Main tick: doc TC22, filter, TFT, MQTT publish
  app.tick();

  // 3) TFT auto-hide overlay/flash sau timeout
  display.updateOverlay();

  // 4) Fix #7: Non-blocking LED blinker update (thay delay() blocking)
  ledBlinker.update();

  // 5) Fix #12: Battery monitor update (auto-poll moi 5s)
  //    Tam thoi bo qua hien thi pin - chua wire voltage divider vat ly.
  //    Bat lai bang cach uncomment 2 dong duoi khi da han R1=220k/R2=100k
  //    vao BAT+ - GPIO 34 - GND.
  // batMon.update();
  // display.setBatteryPercent(batMon.percent());

#if ENABLE_MQTT_LOG
  maintainWiFi();
  maintainMQTT();
  if (mqttClient.connected()) { mqttClient.loop(); }
  if (WiFi.status() != WL_CONNECTED) { mqttReady = false; }
#endif
}
