#include "ConfigWS.h"
#include <WiFi.h>
#include "esp_wifi.h"

const char* ConfigWS::PREF_NAMESPACE = "wifi_config";
const char* ConfigWS::PREF_KEY_SSID   = "ssid";
const char* ConfigWS::PREF_KEY_PASS   = "pass";

static const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>ESP32 WiFi Setup</title>
  <style>
    :root {
      --primary-color: #4f46e5;
      --primary-hover: #4338ca;
      --bg-color: #f3f4f6;
      --card-bg: #ffffff;
      --text-color: #1f2937;
      --input-border: #d1d5db;
      --input-focus: #6366f1;
      --icon-color: #9ca3af;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg-color);
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      color: var(--text-color);
    }
    .container {
      background: var(--card-bg);
      padding: 2rem;
      border-radius: 16px;
      box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.1), 0 8px 10px -6px rgba(0, 0, 0, 0.1);
      width: 90%;
      max-width: 400px;
    }
    h2 {
      text-align: center;
      margin-bottom: 1.5rem;
      color: var(--primary-color);
      font-weight: 800;
      font-size: 1.5rem;
    }
    .input-group { margin-bottom: 1.25rem; }
    .input-group label {
      display: block;
      margin-bottom: 0.5rem;
      font-weight: 600;
      font-size: 0.9rem;
    }
    .input-wrapper { position: relative; }
    
    /* Input styling */
    .input-wrapper input {
      width: 100%;
      padding: 12px 40px 12px 40px; 
      border: 1px solid var(--input-border);
      border-radius: 8px;
      font-size: 1rem;
      transition: border-color 0.2s, box-shadow 0.2s;
      outline: none;
    }
    .input-wrapper input:focus {
      border-color: var(--input-focus);
      box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.2);
    }

    /* Left Icon (Lock/Wifi) styling */
    .input-icon {
      position: absolute;
      left: 12px;
      top: 50%;
      transform: translateY(-50%);
      width: 20px;
      height: 20px;
      fill: var(--icon-color);
      pointer-events: none;
    }
    input:focus + .input-icon { fill: var(--primary-color); }
    
    /* Right Icon (Eye Toggle) styling */
    .toggle-password {
      position: absolute;
      right: 12px;
      top: 50%;
      transform: translateY(-50%);
      width: 20px;
      height: 20px;
      fill: var(--icon-color);
      cursor: pointer;
      transition: fill 0.2s;
    }
    .toggle-password:hover { fill: var(--text-color); }
    .toggle-password.active { fill: var(--primary-color); }

    button {
      width: 100%;
      padding: 14px;
      background: linear-gradient(to right, var(--primary-color), var(--primary-hover));
      color: white;
      border: none;
      border-radius: 8px;
      font-weight: 700;
      font-size: 1rem;
      cursor: pointer;
      transition: transform 0.1s, opacity 0.2s;
      margin-top: 0.5rem;
    }
    button:hover { opacity: 0.95; }
    button:active { transform: scale(0.98); }
    .footer {
      margin-top: 1.5rem;
      text-align: center;
      font-size: 0.8rem;
      color: #6b7280;
    }
  </style>
  <script>
    function togglePassword() {
      var input = document.getElementById("passInput");
      var icon = document.getElementById("eyeIcon");
      if (input.type === "password") {
        input.type = "text";
        icon.classList.add("active");
      } else {
        input.type = "password";
        icon.classList.remove("active");
      }
    }
  </script>
</head>
<body>
  <div class="container">
    <h2>WiFi Configuration</h2>
    <form action="/save" method="POST">
      
      <div class="input-group">
        <label>Network Name (SSID)</label>
        <div class="input-wrapper">
          <input name="ssid" placeholder="Enter WiFi Name" required>
          <svg class="input-icon" viewBox="0 0 24 24">
            <path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8zm-1-13h2v6h-2zm0 8h2v2h-2z"/>
            <path d="M12,3C7.029,3,2,7.029,2,12s5.029,9,10,9s10-4.029,10-9S16.971,3,12,3z M12,19c-3.859,0-7-3.14,7-7s3.141-7,7-7s7,3.141,7,7S15.859,19,12,19z M11,11v6h2v-6H11z M11,7v2h2V7H11z" opacity="0"/> 
            <path d="M12 3c-4.97 0-9 4.03-9 9s4.03 9 9 9 9-4.03 9-9-4.03-9-9-9zm0 16c-3.86 0-7-3.14-7-7s3.14-7 7-7 7 3.14 7 7-3.14 7-7 7zm1-11h-2v6h2V8zm0 8h-2v-2h2v2z"/>
          </svg>
        </div>
      </div>

      <div class="input-group">
        <label>Password</label>
        <div class="input-wrapper">
          <input id="passInput" name="pass" type="password" placeholder="Enter WiFi Password">
          <svg class="input-icon" viewBox="0 0 24 24">
            <path d="M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zm-9-2c0-1.66 1.34-3 3-3s3 1.34 3 3v2H9V6zm9 14H6V10h12v10zm-6-3c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2z"/>
          </svg>
          <svg id="eyeIcon" class="toggle-password" onclick="togglePassword()" viewBox="0 0 24 24">
            <path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z"/>
            <circle cx="12" cy="12" r="3"/>
          </svg>
        </div>
      </div>

      <button type="submit">Save & Connect</button>
    </form>
    <div class="footer">
      ESP32 Device Manager
    </div>
  </div>
</body>
</html>
)rawliteral";
ConfigWS::ConfigWS(const char* AP_ssid, const char* AP_pass)
  : _AP_ssid(AP_ssid), _AP_pass(AP_pass), server(80), prefs(), cfg(), SuccessConnect(false) {
}

bool ConfigWS::load(Config& outConfig) {
  prefs.begin(PREF_NAMESPACE, true);
  outConfig.ssid = prefs.getString(PREF_KEY_SSID, "");
  outConfig.pass = prefs.getString(PREF_KEY_PASS, "");
  prefs.end();

  Serial.println("Loaded WiFi: SSID='" + outConfig.ssid + "'");
  return outConfig.ssid.length() > 0;
}

bool ConfigWS::connectSTA(uint32_t timeoutMs) {
  if(cfg.ssid.isEmpty()) {
    return false;
  }
  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

  Serial.println("Connecting to WiFi SSID='" + cfg.ssid + "' ...");

  uint32_t startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected! IP address: " + WiFi.localIP().toString());
    return true;
  } else {
    Serial.println("Failed to connect");
    return false;
  }
  Serial.println();
}

void ConfigWS::clearSavedWiFi() {
    esp_wifi_restore();   // Xóa tất cả WiFi đã lưu
    delay(100);
    WiFi.disconnect(true, true); // clear luôn cả RAM + Flash
    prefs.begin(PREF_NAMESPACE, false);
    prefs.remove(PREF_KEY_SSID);
    prefs.remove(PREF_KEY_PASS);
    prefs.end();
    cfg.ssid = "";
    cfg.pass = "";
    SuccessConnect = false;
}

void ConfigWS::startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(_AP_ssid, _AP_pass);
  Serial.println("Started AP SSID='" + String(_AP_ssid) + "'");
  Serial.println("AP IP address: " + WiFi.softAPIP().toString());
}

void ConfigWS::handleRoot() {
  server.send_P(200, "text/html", CONFIG_HTML);
}

void ConfigWS::handleSave(){
  if(server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if(ssid.length() == 0) {
    server.send(400, "text/plain", "Bad Request: SSID is required");
    return;
  }

  cfg.ssid = ssid;
  cfg.pass = pass;
  // Lưu vào NVS
  prefs.begin(PREF_NAMESPACE, false);
  prefs.putString(PREF_KEY_SSID, cfg.ssid);
  prefs.putString(PREF_KEY_PASS, cfg.pass);
  prefs.end();


  
  Serial.println("Saved WiFi: SSID='" + cfg.ssid + "'");

  server.send(200, "text/html", "Configuration saved. Trying to connect...");
  SuccessConnect = true;
}

void ConfigWS::setupRoutes(){
  server.on("/", HTTP_GET, [this]() {this->handleRoot(); });
  server.on("/save", HTTP_POST, [this]() {this->handleSave(); });
}

void ConfigWS::loopPortal() {
  SuccessConnect = false;
  startAP();
  setupRoutes();
  server.begin();
  
  Serial.println("Waiting for configuration...");

  while(!SuccessConnect) {
    server.handleClient();
    delay(10);
  }

  server.stop();
  WiFi.softAPdisconnect(true);
  Serial.println("Configuration portal stopped.");
}

//API chính, đảm bảo có wifi, nếu cần thì vô chế độ config
bool ConfigWS::ensureWiFi(Config& outConfig, uint32_t connectTimeoutMs) {
  //1) Thử load config & connect
  if(load(cfg)) {
    outConfig = cfg;
    if(connectSTA(connectTimeoutMs)) {
      return true;
    }
    Serial.println("Failed to connect with saved config, fall back to AP mode.");
  }

  //2) Mở AP + webserver cho user cấu hình
  loopPortal();

  //3) Thử connect lại với config mới
  if(SuccessConnect && connectSTA(connectTimeoutMs)) {
    outConfig = cfg;
    return true;
  }

  return false;
}

