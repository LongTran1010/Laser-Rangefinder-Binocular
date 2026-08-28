#pragma once
#include "AlphaBetaTracker.h"

// =====================================================================
// Alpha-Beta helpers (Proposal 2: PresetTable la single source of truth)
// ---------------------------------------------------------------------
// Sau refactor UX v2:
//   * enum AbPreset -> XOA (dead code, thay bang PresetTable id 0..5)
//   * makeAlphaBetaPreset() -> XOA (thay bang applyPresetById() trong main.cpp)
//   * abPresetName() -> XOA (thay bang PRESETS[id].name)
//
// File nay chi giu HELPER bbBeta() cho phep tinh beta chuan tu alpha
// theo Benedict-Bordner (1962) khi khoi tao PresetTable.
//
// Reference: Benedict, T.R. & Bordner, G.W. (1962), IRE Trans. Auto. Control AC-7:27-32.
// Quy uoc: beta = alpha^2 / (2 - alpha) (gain relationship, KHONG phai critically damped).
// =====================================================================

constexpr float bbBeta(float alpha){
  return (alpha * alpha) / (2.0f - alpha);
}
