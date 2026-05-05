#pragma once
#include "AlphaBetaTracker.h"

enum class AbPreset : uint8_t {
  PRESET_A = 0,   // candidate v1
  PRESET_B,
  PRESET_C,
  PRESET_D
};

inline const char* abPresetName(AbPreset p) {
  switch (p) {
    case AbPreset::PRESET_A: return "A";
    case AbPreset::PRESET_B: return "B";
    case AbPreset::PRESET_C: return "C";
    case AbPreset::PRESET_D: return "D";
    default: return "UNK";
  }
}

inline AlphaBetaConfig makeAlphaBetaPreset(AbPreset p) {
  AlphaBetaConfig cfg{};

  // các tham số chung có thể giữ giống nhau trước
  cfg.reinitOnSwitch = true;
  cfg.defaultDtS = 0.40f;
  cfg.minDtS = 0.001f;
  cfg.maxDtS = 2.0f;
  cfg.initRateMps = 0.0f;

  switch (p) {
    case AbPreset::PRESET_A:
      cfg.alpha = 0.10f;
      cfg.beta = 0.00f;
      cfg.gateThresholdM = 10.0f;
      cfg.maxReject = 7;
      break;

    case AbPreset::PRESET_B:
      cfg.alpha = 0.10f;
      cfg.beta = 0.00f;
      cfg.gateThresholdM = 12.0f;
      cfg.maxReject = 7;
      break;

    case AbPreset::PRESET_C:
      cfg.alpha = 0.15f;
      cfg.beta = 0.00f;
      cfg.gateThresholdM = 10.0f;
      cfg.maxReject = 7;
      break;

    case AbPreset::PRESET_D:
      cfg.alpha = 0.10f;
      cfg.beta = 0.01f;
      cfg.gateThresholdM = 10.0f;
      cfg.maxReject = 7;
      break;
  }

  return cfg;
}