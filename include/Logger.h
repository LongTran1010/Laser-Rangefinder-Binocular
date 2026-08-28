#pragma once
#include <Arduino.h>
#include "TrackerTypes_v2.h"
#include "TC22/TC22Protocol.h"

// =====================================================================
// Logger
// ---------------------------------------------------------------------
// Tach JSON serialization cua 1 snapshot ra khoi Controller de:
//  - test duoc doc lap (goi serializeSnapshot trong unit test)
//  - thay backend de (MQTT / SD-card / UART debug) qua callback
//  - dam bao schema CSV on dinh, khong lech giua cac file
//
// Schema phai khop CSV_COLUMNS trong tc22_mqtt_log.py.
// =====================================================================

using PublishLogFn = void (*)(const char* payload);

// Firmware version - tang khi co thay doi quan trong (filter, schema, etc.)
// Fix #13: uu tien CURRENT_FW_VERSION tu platformio.ini neu co, thong nhat.
// v1.0.2-UX-v2: them ButtonManager/PresetManager/BatteryMonitor + fix 13 bugs.
#ifndef FW_VERSION_STR
  #ifdef CURRENT_FW_VERSION
    #define FW_VERSION_STR CURRENT_FW_VERSION
  #else
    #define FW_VERSION_STR "1.0.2-UX-v2"
  #endif
#endif
// Schema CSV version - tang khi them/xoa cot
// v2 -> v3: them predict_hold_count (dung cho DT5 verify B4 velocity decay)
// v3 -> v4: them estimator_decision (Proposal 1 - tach validity va decision)
#define LOG_SCHEMA_VERSION 4

struct LogContext {
    uint32_t bootId       = 0;
    uint32_t msgSeq       = 0;
    uint16_t testId       = 0;
    uint8_t  mode         = 0;
    uint8_t  systemError  = 0;
    uint8_t  restartCount = 0;
    Tc22RejectReason rejectReason = TC22_REJECT_NONE;

    // ---- Config snapshot (cap nhat khi setAlphaBetaConfig hoac begin) ----
    // Log moi message de CSV self-describing - tranh mislabel nhu lan 8003.
    float   cfgAlpha       = 0.0f;
    float   cfgBeta        = 0.0f;
    float   cfgGateM       = 0.0f;
    float   cfgMinDtS      = 0.0f;
    uint8_t cfgMaxReject   = 0;
    uint8_t cfgPresetId    = 0xFF;   // 0xFF = unset / not applicable
};

class Logger {
public:
    explicit Logger(PublishLogFn publishFn = nullptr) : publish_(publishFn) {}

    void setPublishFn(PublishLogFn fn) { publish_ = fn; }

    static size_t serializeSnapshot(char* buf,
                                    size_t bufSize,
                                    const TrackerOutput& out,
                                    const LogContext& ctx);

    bool publishSnapshot(const TrackerOutput& out, const LogContext& ctx);

private:
    PublishLogFn publish_;
};
