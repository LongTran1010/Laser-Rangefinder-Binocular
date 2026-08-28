#pragma once
#include <Arduino.h>
#include "TrackerTypes_v2.h"   // EstimatorMode enum (EST_BASELINE/ALPHABETA/RAW_ONLY)

struct PresetConfig{
    uint8_t       id;
    const char*   name;         // "AB-B"
    const char*   use_case;     // "Tripod - Balanced"
    float         alpha;
    float         beta;
    float         gate_m;
    float         min_dt_s;
    uint8_t       max_reject;
    EstimatorMode mode;         // Fix #1: EST_RAW_ONLY / EST_BASELINE / EST_ALPHABETA
};

// Fix #10: Beta cua AB-A dung Benedict-Bordner: beta = alpha^2 / (2 - alpha)
// alpha=0.45 -> beta = 0.2025 / 1.55 = 0.1306 (thay vi 0.11)
// Review#3: BASE.alpha giu EMA lambda (0.25) - la NGUON duy nhat cho baselineConfig.
// Truoc day alpha=0 -> main.cpp phai hardcode 0.25 khac. Nay dong bo lai.
static const PresetConfig PRESETS[] = {
    // {id, name,  use_case,              alpha, beta,    gate, min_dt, max_rej, mode           }
    {  0, "RAW ", "Debug / calibration",  0,     0,       999,  0.00f,  0,       EST_RAW_ONLY   },
    {  1, "BASE", "Baseline (M+EMA)",     0.25f, 0,       5,    0.10f,  5,       EST_BASELINE   },
    {  2, "AB-L", "Light smoothing",      0.15f, 0.0122f, 3,    0.10f,  3,       EST_ALPHABETA  },
    {  3, "AB-B", "Tripod - Balanced",    0.30f, 0.0529f, 4,    0.10f,  5,       EST_ALPHABETA  },
    {  4, "AB-A", "Aggressive",           0.45f, 0.1306f, 5,    0.10f,  5,       EST_ALPHABETA  },
    {  5, "HAND", "Handheld",             0.30f, 0.0529f, 8,    0.05f,  8,       EST_ALPHABETA  },
};

static constexpr uint8_t N_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);