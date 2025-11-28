#include "TFT.h"
#include <WiFi.h>
#include "Password.h"
#include <PubSubClient.h>
#include "MeasurementTypes.h"  // MeasStatus, Measurement
//#include "TF02-Pro.h"        // TF02ProDriver
#include "TC22.h"          // TC22Driver
#include <esp_task_wdt.h>
#include "ConfigWS.h"

#define TRIGGER_PIN 25

WiFiClient espClient;
PubSubClient client(espClient);

ConfigWS wfconf("ESP32_setup_AP", "12345678");
Config wifiConfig;

const char* mqtt_server = "192.168.88.52";
//const char* mqtt_server = "172.20.10.3";
const int mqtt_port = 1883;
const char* mqtt_topic_sub = "thesis/tc22/log";
const char* mqtt_topic_status = "thesis/tc22/status";

//button
static bool btnWasLow = false;
static uint32_t btnTs = 0;
auto btnLow = [&](){ return digitalRead(TRIGGER_PIN) == LOW; };
auto btnFalling = [&](){
  bool nowLow = btnLow();
  uint32_t now = millis();
  bool edge = (nowLow && !btnWasLow && (now - btnTs > 30)); // debounce ~30ms
  if (nowLow != btnWasLow) { btnWasLow = nowLow; btnTs = now; }
  return edge;
};
//ONE-SHOT
bool singleShot = false, targetlocked = false;
uint32_t lockedTime = 0;
float lockedValue = 0;
// TFT
TFTPins pins{5,21,19,18,23,-1};  // CS=5, DC=21, RST=19, SCK=18, MOSI=23, MISO=-1
TFTDistance display(pins);

static uint32_t tc22LastFrameMs = 0; // thời gian nhận mẫu TC22 cuối cùng
static uint32_t tc22LastOkMs      = 0;   // lần cuối nhận MEAS_OK
static uint32_t tc22LastRestartMs = 0;   // lần cuối thử restart
static uint8_t  tc22RestartCount  = 0;   // số lần restart đã thử từ lúc bật máy

// Ngưỡng thời gian 
constexpr uint32_t TC22_FRAME_TIMEOUT_MS   = 1000;   // 1s không có frame -> coi là TIMEOUT
constexpr uint32_t TC22_OK_TIMEOUT_MS      = 3000;  // 3s không có MEAS_OK -> coi là mất mục tiêu / sensor có vấn đề
constexpr uint32_t TC22_RESTART_INTERVAL_MS = 5000; // 5s không frame -> thử restart
constexpr uint8_t  TC22_MAX_RESTARTS        = 3;    // thử tối đa 3 lần rồi bỏ

#if defined(USE_TC22)
// TC22trên UART2 (ESP32): RX2=16, TX2=17
HardwareSerial& LRF = Serial2; 
TC22Driver lrf(LRF, /*RX*/16, /*TX*/17);
#endif

// Tính FPS dựa trên các mẫu OK liên tiếp
static float computeFPS() {
  static uint32_t lastOk = 0;
  static float fps = 0.0f;
  uint32_t now = millis();
  if (lastOk != 0) {
    float dt = (now - lastOk) / 1000.0f;
    if (dt > 0.001f && dt < 2.0f) fps = 1.0f / dt;
  }
  lastOk = now;
  return fps;
}

void connectWiFi() {
  display.setWiFiStatus(WIFI_SSID, false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  //Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    //Serial.print(".");
  }
  //Serial.println("\nWiFi connected!");
  display.setWiFiStatus(WIFI_SSID, true);
  display.setSystemError(0); //Xóa lỗi WiFi nếu có
}

void reconnectMQTT() {
  client.setServer(mqtt_server, mqtt_port);
  while (!client.connected()) {
    if(WiFi.status() != WL_CONNECTED){
      //Serial.println("Wi-Fi lost, reconnecting...");
      display.setWiFiStatus(WIFI_SSID, false);
      display.setSystemError(20); //Lỗi WiFi
      connectWiFi();
    }
    //Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32_Client")) {
      //Serial.println("Connected!");
      client.subscribe(mqtt_topic_status);
      //Serial.println("[MQTT] Subscribed to topic: " + String(mqtt_topic_status)); 
      client.publish(mqtt_topic_status, "TC22 logger online");
      display.setSystemError(0); //wifi + MQTT OK
    }else{
      //Serial.print("Failed, rc=");
      //Serial.print(client.state());
      //Serial.println(" retrying in 5s...");
      display.setSystemError(21); //Lỗi MQTT
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
}

void publishMeasurementMQTT(const Measurement& m, float ema_m, float fps) {
  static bool init = false;
  static uint32_t t0_ms = 0;
  static uint32_t last_ms = 0;

  if (!init) {
    t0_ms = m.t_ms;
    last_ms = m.t_ms;
    init = true;
  }

  float t_rel_s = (m.t_ms - t0_ms) / 1000.0f;
  float dt_s    = (m.t_ms - last_ms) / 1000.0f;
  last_ms       = m.t_ms;

  if (!client.connected()) return;  // không làm gì nếu mất MQTT

  char payload[200];
  // JSON khớp với các cột bạn đang phân tích: distance_m, distance_filt_m, t_rel_s, dt_s, fps
  snprintf(payload, sizeof(payload),
           "{\"distance_m\":%.3f,\"distance_filt_m\":%.3f,"
           "\"t_rel_s\":%.3f,\"dt_s\":%.3f,\"fps\":%.2f}",
           m.dist_m, ema_m, t_rel_s, dt_s, fps);

  client.publish(mqtt_topic_sub, payload);
}

void setup() {
  Serial.begin(115200);
  //WiFi.disconnect(true, true); // Xóa WiFi cũ, xóa config trong NVS
  delay(2000);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  // TFT UI
  display.begin();
  display.setMaxRangeMeters(800.0f); //Max range 800m
  display.setSmoothing(0.25f); // Set EMA ở 0.25
  display.setStaleTimeoutMs(1000);
  if(!wfconf.ensureWiFi(wifiConfig, 15000)){
    Serial.println("Failed to connect to WiFi and get config.");
    //Xử lý khi không kết nối được WiFi
    //Ví dụ: Khởi động lại thiết bị hoặc vào chế độ AP để cấu hình lại
    while(true) vTaskDelay(pdMS_TO_TICKS(1000));
  }else{
    display.setWiFiStatus(wifiConfig.ssid.c_str(), true);
  } 
  //connectWiFi();
  client.setServer(mqtt_server, mqtt_port);
  reconnectMQTT();

// #if defined(USE_TF02_PRO)
//   // TF02-Pro
//   lrf.begin(115200);          // cấu hình UART + reset parser
//   lrf.setTimeoutMs(120);      // timeout khung dở
//   lrf.setStrengthMin(30);     // ngưỡng tín hiệu yếu
// #endif
// }

  #if defined(USE_TC22)
  // TC22
    lrf.begin(115200);          // cấu hình UART + reset parser
    delay(500);     // timeout khung dở
    lrf.setLittleEndianPayload(true); 
    lrf.startContinuous();     // ngưỡng tín hiệu yếu
    uint32_t now = millis();
    tc22LastFrameMs = now;
    tc22LastOkMs = now;
    tc22LastRestartMs = now;
    tc22RestartCount = 0;
  #endif
  esp_task_wdt_init(5, true); // timeout 5s, reset chip nếu treo
  esp_task_wdt_add(NULL);   // thêm task chính vào giám sát
}

void loop(){
  esp_task_wdt_reset(); // đặt lại đồng hồ giám sát
  static uint32_t lastMQTTCheck = 0;
  if (!client.connected() && millis() - lastMQTTCheck > 5000) {
    lastMQTTCheck = millis();
    reconnectMQTT();
  }
  client.loop();
#if defined(USE_TC22)
  if (!targetlocked && !singleShot && btnFalling()) {
    singleShot = true;
    lrf.startSingle();  // Gửi lệnh đo một lần
  }
  static uint32_t lastFPSUpdateMs = millis();
  uint32_t now = millis();

  Measurement m;
  bool haveFrame = lrf.poll(m);
  if(haveFrame){
    tc22LastFrameMs = now;
    if(m.status == MEAS_OK) {
      tc22LastOkMs = millis();
      tc22RestartCount = 0; // Khi có mẫu OK thì TC22 ổn định, reset bộ đếm khởi động lại
      if(singleShot && !targetlocked) {
        targetlocked = true;
        lockedTime = millis() + 4000; // Giữ 4 giây
        lockedValue = m.dist_m;
        singleShot = false;

        display.updateDistanceMeters(lockedValue, true); //Trả về giá trị khóa ngay lập tức
        display.setStatus(MEAS_OK);
        display.setFPS(0.0f);
      }else if(!targetlocked) {
        //Đo liên tục
        float fps = computeFPS();

        //Serial.printf("Start parse frame,%lu\n", (unsigned long)m.t_ms); //Điểm bắt đầu tính độ trễ end--to-end (in trước khi lọc)
        display.update(m, fps);           // cập nhật số đo + overlay FPS/Status

        float ema = display.getFilter();
        Serial.printf("%lu,%.3f,%.3f,%u,%.1f\n",
          (unsigned long)m.t_ms, 
          m.dist_m, 
          ema, 
          -1, //ko trả strength
          fps);

        publishMeasurementMQTT(m, ema, fps); //update 14/11/2025
        //Serial.printf("render TFT,%lu\n", (unsigned long)millis()); //Điểm kết thúc tính độ trễ end--to-end (in ngay sau khi vẽ xong TFT)
      } 
    }else if(!targetlocked){
      // Không đưa mẫu lỗi vào EMA; chỉ cập nhật overlay
      display.setStatus(m.status);
      display.setFPS(0.0f);
    }
  }else{
    if(!targetlocked && (now - tc22LastFrameMs > TC22_FRAME_TIMEOUT_MS)){
      display.setStatus(MEAS_TIMEOUT);
      display.setFPS(0.0f);
      // Vùng số sẽ tự về "---.- m" khi stale > 1000 ms (đã cấu hình)
      display.updateDistanceMeters(NAN, false);
    }
  }
  if(targetlocked) {
    //Vẽ lại giá trị đã khóa để tránh bị stale
    display.updateDistanceMeters(lockedValue, true);
    display.setStatus(MEAS_OK);
    display.setFPS(0.0f);
    if(now > lockedTime || btnFalling()) {
      targetlocked = false; // Hết thời gian khóa hoặc nhấn nút lần nữa
      lrf.startContinuous(); // Quay lại chế độ liên tục
      // Vùng số sẽ tự về "---.- m" khi stale > 1000 ms (đã cấu hình)
    }
  }
  //-------- WATCHDOG / RESTART TC22 NẾU CẦN ---------
  if(!targetlocked){
    // Quá lâu k có mẫu OK -> Khởi động lại TC22 (có giới hạn)
    bool tooLongNo_OK = (now - tc22LastOkMs > TC22_OK_TIMEOUT_MS);
    bool canRestart = (now - tc22LastRestartMs > TC22_RESTART_INTERVAL_MS);
    bool havingRestartsLeft = (tc22RestartCount < TC22_MAX_RESTARTS);

    if(tooLongNo_OK && canRestart && havingRestartsLeft){
      //Serial.println("No MEAS_OK for a while, restarting TC22...");
      tc22LastRestartMs = now;
      tc22RestartCount++;
      lrf.stop();
      delay(500);
      lrf.startContinuous();
    }
    //Sau khi restart, nếu TC22 có gửi lại đc mẫu OK thì lasOK sẽ đc cập nhật, 
    // restartCount sẽ về 0, ko bị restart liên tục
    if(tc22RestartCount >= TC22_MAX_RESTARTS && tooLongNo_OK){
      // Quá nhiều lần restart mà vẫn không OK -> Báo lỗi hệ thống
      display.setStatus(MEAS_TIMEOUT);
      display.setSystemError(1); //Mã lỗi hệ thống 1: TC22 không phản hồi
      display.setFPS(0.0f);
      display.updateDistanceMeters(NAN, false); //Bắt buộc reset hệ thống từ ng dùng, sensor coi như mất kết nối
    }
  }

#endif
#if defined(USE_TF02_PRO)
  // ----------- FAKE DEMO (không cần TF02) -----------
  static uint32_t last = 0;
  static float mval = 0.0f;
  static float step = 0.6f;

  if (millis() - last > 120) {        // ~8.3 Hz
    last = millis();
    mval += step;
    if (mval >= 30.0f) { mval = 30.0f; step = -step; }
    if (mval <= 0.0f)  { mval = 0.0f;  step = -step; }

    Measurement meas{};
    meas.t_ms     = millis();
    meas.dist_m   = mval;
    meas.strength = 100;
    meas.temp_C   = NAN;
    meas.status   = MEAS_OK;

    display.update(meas, 8.3f);
  }
#endif
}
