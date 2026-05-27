/*
 * File:        generation_stage.cpp
 * Module:      generation_stage
 * Purpose:     Generates CVBS-domain Y and C sample buffers from project sections.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/generation_stage.h"

#include <algorithm>
#include <cmath>

#include "videosynth/signal_timing_model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

constexpr double kPi = 3.14159265358979323846;

double PulseWidthSeconds(SyncPulseKind kind, Standard standard) {
  if (kind == SyncPulseKind::kHorizontal) {
    return 4.7e-6;
  }
  if (kind == SyncPulseKind::kEqualizing) {
    return 2.3e-6;
  }
  if (standard == Standard::kNtsc) {
    return 31.778e-6;
  }
  return 27.3e-6;
}

int PulseWidthSamples(SyncPulseKind kind, Standard standard, double sample_rate_hz) {
  const double pulse_seconds = PulseWidthSeconds(kind, standard);
  return std::max(1, static_cast<int>(std::lround(sample_rate_hz * pulse_seconds)));
}

int BurstStartSamples(double sample_rate_hz) {
  return std::max(0, static_cast<int>(std::lround(sample_rate_hz * 5.6e-6)));
}

int BurstEndSamples(double sample_rate_hz) {
  return std::max(0, static_cast<int>(std::lround(sample_rate_hz * 8.0e-6)));
}

}  // namespace

bool GenerationStage::Generate(const Project& project,
                               std::vector<double>* out_y_mv,
                               std::vector<double>* out_c_mv,
                               std::vector<std::string>* errors) {
  if (out_y_mv == nullptr || out_c_mv == nullptr || errors == nullptr) {
    return false;
  }

  const TimingConstants timing = GetTimingConstants(project.cvbs_presets.standard);
  const SignalLevels levels = GetSignalLevels(project.cvbs_presets.standard);
  const std::vector<LineTimingPrimitive> lines =
      BuildFrameTimingPrimitives(project.cvbs_presets.standard);
  const std::size_t sample_count =
      static_cast<std::size_t>(timing.lines_per_frame * timing.samples_per_line_4fsc);
  const double subcarrier_hz = timing.sample_rate_4fsc_hz / 4.0;
  const double burst_amplitude_mv = 150.0;
  const int burst_start = BurstStartSamples(timing.sample_rate_4fsc_hz);
  const int burst_end = BurstEndSamples(timing.sample_rate_4fsc_hz);
  const int half_line_samples = timing.samples_per_line_4fsc / 2;

  out_y_mv->assign(sample_count, levels.blanking_mv);
  out_c_mv->assign(sample_count, 0.0);

  for (const LineTimingPrimitive& line : lines) {
    const int line_index = line.line_number_1based - 1;
    const int line_base = line_index * timing.samples_per_line_4fsc;
    const int line_end = line_base + timing.samples_per_line_4fsc;

    const int pulse_width =
        PulseWidthSamples(line.sync_pulse_kind,
                          project.cvbs_presets.standard,
                          timing.sample_rate_4fsc_hz);
    const int pulse_count = line.has_two_half_line_pulses ? 2 : 1;

    for (int pulse_index = 0; pulse_index < pulse_count; ++pulse_index) {
      const int pulse_offset = line.has_two_half_line_pulses ? (pulse_index * half_line_samples) : 0;
      const int pulse_start = line_base + std::min(pulse_offset, timing.samples_per_line_4fsc - 1);
      const int pulse_end = std::min(pulse_start + pulse_width, line_end);

      for (int i = pulse_start; i < pulse_end; ++i) {
        (*out_y_mv)[i] = levels.sync_tip_mv;
      }
    }

    if (line.burst_enabled) {
      const int burst_sample_start =
          std::min(line_base + burst_start, line_end > 0 ? line_end - 1 : line_base);
      const int burst_sample_end = std::min(line_base + burst_end, line_end);
      const double phase = line.burst_phase_rad;

      for (int i = burst_sample_start; i < burst_sample_end; ++i) {
        const double t = static_cast<double>(i - line_base) / timing.sample_rate_4fsc_hz;
        (*out_c_mv)[i] = burst_amplitude_mv * std::sin((2.0 * kPi * subcarrier_hz * t) + phase);
      }
    }
  }

  return true;
}

}  // namespace videosynth
