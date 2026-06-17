#pragma once
#include "AlphaBetaTracker.h"

// =====================================================================
// Alpha-Beta presets
// ---------------------------------------------------------------------
// Quy uoc beta theo Benedict-Bordner critically damped:
//     beta = alpha^2 / (2 - alpha)
// Bo tracker thuc su chi kich hoat khi beta > 0 (co velocity update).
// Cac preset *_EMA_LIKE giu beta = 0 phuc vu A/B voi baseline EMA-only.
// =====================================================================

enum class AbPreset : uint8_t {
  // ---- preset giữ Beta = 0 (chi position, khong velocity) ----
  PRESET_A_EMA_LIKE = 0,    // alpha thap, beta=0  -> tuong duong baseline EMA manh
  PRESET_B_EMA_LIKE,        // alpha thap, gate rong

  // ---- preset alpha-beta thuc su (beta > 0 theo Benedict-Bordner) ----
  PRESET_C_AB_LIGHT,        // alpha=0.20, beta theo BB  -> muot, bam cham
  PRESET_D_AB_BALANCED,     // alpha=0.30, beta theo BB  -> v1 mac dinh cho thi nghiem
  PRESET_E_AB_AGGRESSIVE,   // alpha=0.45, beta theo BB  -> bam nhanh, it muot
  PRESET_F_AB_HANDHELD      // alpha=0.30, beta=0.05 (giam jitter velocity khi rung tay)
};

inline const char* abPresetName(AbPreset p) {
  switch (p) {
    case AbPreset::PRESET_A_EMA_LIKE:      return "A_EMA_LIKE";
    case AbPreset::PRESET_B_EMA_LIKE:      return "B_EMA_LIKE";
    case AbPreset::PRESET_C_AB_LIGHT:      return "C_AB_LIGHT";
    case AbPreset::PRESET_D_AB_BALANCED:   return "D_AB_BALANCED";
    case AbPreset::PRESET_E_AB_AGGRESSIVE: return "E_AB_AGGRESSIVE";
    case AbPreset::PRESET_F_AB_HANDHELD:   return "F_AB_HANDHELD";
    default:                               return "UNK";
  }
}

// Benedict-Bordner critically damped: beta = alpha^2 / (2 - alpha)
constexpr float bbBeta(float alpha){
  return (alpha * alpha) / (2.0f - alpha);
}

inline AlphaBetaConfig makeAlphaBetaPreset(AbPreset p) {
  AlphaBetaConfig cfg{};

  // ---- tham so chung ----
  cfg.reinitOnSwitch          = true;
  cfg.defaultDtS              = 0.40f;       // tuong duong fps ~2.5 Hz quan sat thuc te tren TC22
  cfg.minDtS                  = 0.10f;       // Sua tu 0.001 -> 0.10 de tranh velocity spike
                                              // khi UART drain co frame timestamp qua gan nhau
  cfg.maxDtS                  = 2.0f;        // gap > 2s coi la missing, khong trust velocity
  cfg.initRateMps             = 0.0f;
  cfg.invalidVelocityDecay    = 0.85f;       // decay v khi mau invalid (giam troi state)
  cfg.maxPredictHoldSamples   = 8;           // sau N lan predict-only -> v=0

  switch (p) {
    case AbPreset::PRESET_A_EMA_LIKE:
      cfg.alpha = 0.10f;  cfg.beta = 0.00f;
      cfg.gateThresholdM = 10.0f;  cfg.maxReject = 7;
      break;
    case AbPreset::PRESET_B_EMA_LIKE:
      cfg.alpha = 0.10f;  cfg.beta = 0.00f;
      cfg.gateThresholdM = 12.0f;  cfg.maxReject = 7;
      break;
    case AbPreset::PRESET_C_AB_LIGHT:
      cfg.alpha = 0.20f;  cfg.beta = bbBeta(0.20f);   // ~0.0222
      cfg.gateThresholdM = 10.0f;  cfg.maxReject = 5;
      break;
    case AbPreset::PRESET_D_AB_BALANCED:
      cfg.alpha = 0.30f;  cfg.beta = bbBeta(0.30f);   // ~0.0529
      cfg.gateThresholdM = 10.0f;  cfg.maxReject = 5;
      break;
    case AbPreset::PRESET_E_AB_AGGRESSIVE:
      cfg.alpha = 0.45f;  cfg.beta = bbBeta(0.45f);   // ~0.1306
      cfg.gateThresholdM = 12.0f;  cfg.maxReject = 4;
      break;
    case AbPreset::PRESET_F_AB_HANDHELD:
      cfg.alpha = 0.30f;  cfg.beta = 0.05f;
      cfg.gateThresholdM = 12.0f;  cfg.maxReject = 6;
      break;
  }
  return cfg;
}
