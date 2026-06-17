#include "Logger.h"
#include <math.h>
#include <stdio.h>

namespace {
inline void formatFloat(char* buf, size_t bufSize, float v, int precision = 3){
    if (isfinite(v)) {
        snprintf(buf, bufSize, "%.*f", precision, v);
    } else {
        snprintf(buf, bufSize, "null");
    }
}
}  // namespace

size_t Logger::serializeSnapshot(char* buf,
                                 size_t bufSize,
                                 const TrackerOutput& out,
                                 const LogContext& ctx){
    if (!buf || bufSize < 384) return 0;

    char rawBuf[24], estBuf[24], fpsBuf[16];
    char rateBuf[24], predBuf[24], residBuf[24];
    char alphaBuf[16], betaBuf[16], gateBuf[16], minDtBuf[16];

    formatFloat(rawBuf,   sizeof(rawBuf),   out.rawDistanceM,        3);
    formatFloat(estBuf,   sizeof(estBuf),   out.filteredDistanceM,   3);
    formatFloat(fpsBuf,   sizeof(fpsBuf),   isfinite(out.fps) ? out.fps : 0.0f, 2);
    formatFloat(rateBuf,  sizeof(rateBuf),  out.rangeRateMps,        3);
    formatFloat(predBuf,  sizeof(predBuf),  out.predictedDistanceM,  3);
    formatFloat(residBuf, sizeof(residBuf), out.residualM,           3);
    formatFloat(alphaBuf, sizeof(alphaBuf), ctx.cfgAlpha,            3);
    formatFloat(betaBuf,  sizeof(betaBuf),  ctx.cfgBeta,             4);
    formatFloat(gateBuf,  sizeof(gateBuf),  ctx.cfgGateM,            2);
    formatFloat(minDtBuf, sizeof(minDtBuf), ctx.cfgMinDtS,           3);

    int n = snprintf(
        buf, bufSize,
        "{"
        "\"boot_id\":%lu,"
        "\"msg_seq\":%lu,"
        "\"dev_ts_ms\":%lu,"
        "\"mode\":%u,"
        "\"raw_m\":%s,"
        "\"est_m\":%s,"
        "\"fps\":%s,"
        "\"meas_status\":%u,"
        "\"track_state\":%u,"
        "\"has_estimate\":%u,"
        "\"consecutive_valids\":%u,"
        "\"consecutive_invalids\":%u,"
        "\"last_good_ts_ms\":%lu,"
        "\"restart_count\":%u,"
        "\"system_error\":%u,"
        "\"rate_mps\":%s,"
        "\"predicted_m\":%s,"
        "\"residual_m\":%s,"
        "\"rejected_by_gate\":%u,"
        "\"reject_reason\":%u,"
        "\"estimator_mode\":%u,"
        "\"test_id\":%u,"
        "\"cfg_alpha\":%s,"
        "\"cfg_beta\":%s,"
        "\"cfg_gate_m\":%s,"
        "\"cfg_min_dt_s\":%s,"
        "\"cfg_max_reject\":%u,"
        "\"cfg_preset_id\":%u,"
        "\"fw_version\":\"" FW_VERSION_STR "\","
        "\"schema_version\":%u"
        "}",
        static_cast<unsigned long>(ctx.bootId),
        static_cast<unsigned long>(ctx.msgSeq),
        static_cast<unsigned long>(out.sampleTimeMs),
        static_cast<unsigned>(ctx.mode),
        rawBuf, estBuf, fpsBuf,
        static_cast<unsigned>(out.measStatus),
        static_cast<unsigned>(out.trackState),
        static_cast<unsigned>(out.hasEstimate),
        static_cast<unsigned>(out.consecutiveValids),
        static_cast<unsigned>(out.consecutiveInvalids),
        static_cast<unsigned long>(out.lastGoodTimeMs),
        static_cast<unsigned>(ctx.restartCount),
        static_cast<unsigned>(ctx.systemError),
        rateBuf, predBuf, residBuf,
        static_cast<unsigned>(out.rejectedByGate),
        static_cast<unsigned>(ctx.rejectReason),
        static_cast<unsigned>(out.estimatorMode),
        static_cast<unsigned>(ctx.testId),
        alphaBuf, betaBuf, gateBuf, minDtBuf,
        static_cast<unsigned>(ctx.cfgMaxReject),
        static_cast<unsigned>(ctx.cfgPresetId),
        static_cast<unsigned>(LOG_SCHEMA_VERSION));

    return (n > 0 && static_cast<size_t>(n) < bufSize) ? static_cast<size_t>(n) : 0;
}

bool Logger::publishSnapshot(const TrackerOutput& out, const LogContext& ctx){
    if (!publish_) return false;
    char payload[768];
    size_t n = serializeSnapshot(payload, sizeof(payload), out, ctx);
    if (n == 0) return false;
    publish_(payload);
    return true;
}
