/*
 * File:        signal_shaping.cpp
 * Module:      signal_shaping
 * Purpose:     Shared transition shaping helpers for CVBS timing sections.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/signal_shaping.h"

#include <algorithm>
#include <cmath>

namespace videosynth {
namespace {

double ClampUnitInterval(double value) {
  return std::max(0.0, std::min(1.0, value));
}

double SmoothStep01(double x) {
  const double clamped = ClampUnitInterval(x);
  return clamped * clamped * (3.0 - (2.0 * clamped));
}

}  // namespace

int RiseTimeToRampSamples(double rise_time_seconds, double sample_rate_hz) {
  // Specifications quote rise/fall between 10%-90%. Expand to a full 0%-100%
  // transition for discrete-time synthesis so the endpoints are also eased.
  const double ten_to_ninety_samples = rise_time_seconds * sample_rate_hz;
  const double full_transition_samples = ten_to_ninety_samples / 0.8;
  return std::max(3, static_cast<int>(std::ceil(full_transition_samples)));
}

double ShapedPulseLevel(int relative_index,
                        int pulse_width_samples,
                        int ramp_samples,
                        double baseline_level,
                        double pulse_level) {
  if (relative_index < 0 || relative_index >= pulse_width_samples || pulse_width_samples <= 0) {
    return baseline_level;
  }

  const int ramp = std::max(1, std::min(ramp_samples, std::max(1, pulse_width_samples / 2)));
  const int trailing_start = pulse_width_samples - ramp;
  double depth = 1.0;

  if (relative_index < ramp) {
    const double x = static_cast<double>(relative_index) /
                     static_cast<double>(std::max(1, ramp - 1));
    depth = SmoothStep01(x);
  } else if (relative_index >= trailing_start) {
    const int trailing_index = relative_index - trailing_start;
    const double x = static_cast<double>((ramp - 1) - trailing_index) /
                     static_cast<double>(std::max(1, ramp - 1));
    depth = SmoothStep01(x);
  }

  return baseline_level + ((pulse_level - baseline_level) * depth);
}

double ShapedGateEnvelope(int relative_index, int gate_width_samples, int ramp_samples) {
  if (relative_index < 0 || relative_index >= gate_width_samples || gate_width_samples <= 0) {
    return 0.0;
  }

  const int ramp = std::max(1, std::min(ramp_samples, std::max(1, gate_width_samples / 2)));
  const int trailing_start = gate_width_samples - ramp;

  if (relative_index < ramp) {
    const double x = static_cast<double>(relative_index) /
                     static_cast<double>(std::max(1, ramp - 1));
    return SmoothStep01(x);
  }
  if (relative_index >= trailing_start) {
    const int trailing_index = relative_index - trailing_start;
    const double x = static_cast<double>((ramp - 1) - trailing_index) /
                     static_cast<double>(std::max(1, ramp - 1));
    return SmoothStep01(x);
  }

  return 1.0;
}

}  // namespace videosynth
