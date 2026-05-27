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
#include <cstdint>
#include <vector>

#include "videosynth/signal_timing_model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

constexpr double kPi = 3.14159265358979323846;

enum class PatternKind {
  kColourBars75,
  kGrayscaleRamp,
  kPlugeBasic,
};

struct ActiveRasterGeometry {
  int first_active_line_field1 = 0;
  int first_active_line_field2 = 0;
  int active_lines_per_field = 0;
  int active_window_start_samples = 0;
  int active_window_end_samples = 0;
  int active_width_pixels = 720;
};

struct PixelYCbCr {
  std::int16_t y = 64;
  std::int16_t cb = 512;
  std::int16_t cr = 512;
};

int ClampCode(int code, int lo, int hi) {
  return std::max(lo, std::min(code, hi));
}

ActiveRasterGeometry GetActiveRasterGeometry(Standard standard, double sample_rate_hz) {
  const int active_window_start =
      std::max(0, static_cast<int>(std::lround(sample_rate_hz * 10.5e-6)));
  const int active_window_end =
      std::max(active_window_start + 1,
               static_cast<int>(std::lround(sample_rate_hz * 62.5e-6)));

  if (standard == Standard::kPal) {
    return ActiveRasterGeometry{
        .first_active_line_field1 = 23,
        .first_active_line_field2 = 336,
        .active_lines_per_field = 288,
        .active_window_start_samples = active_window_start,
        .active_window_end_samples = active_window_end,
        .active_width_pixels = 720,
    };
  }

  return ActiveRasterGeometry{
      .first_active_line_field1 = 22,
      .first_active_line_field2 = 285,
      .active_lines_per_field = 240,
      .active_window_start_samples = active_window_start,
      .active_window_end_samples = active_window_end,
      .active_width_pixels = 720,
  };
}

bool ParsePatternKind(const std::string& pattern, PatternKind* out_kind) {
  if (out_kind == nullptr) {
    return false;
  }

  if (pattern == "ebu_colour_bars" || pattern == "colour_bars_75") {
    *out_kind = PatternKind::kColourBars75;
    return true;
  }
  if (pattern == "grayscale_ramp_horizontal" || pattern == "grayscale_ramp") {
    *out_kind = PatternKind::kGrayscaleRamp;
    return true;
  }
  if (pattern == "pluge" || pattern == "pluge_basic") {
    *out_kind = PatternKind::kPlugeBasic;
    return true;
  }
  return false;
}

bool BuildFramePatternSchedule(const Project& project,
                               std::vector<PatternKind>* out_frame_patterns) {
  if (out_frame_patterns == nullptr) {
    return false;
  }

  out_frame_patterns->clear();
  for (const Section& section : project.sections) {
    if (section.type != "software_generated") {
      continue;
    }

    PatternKind kind = PatternKind::kColourBars75;
    if (!ParsePatternKind(section.pattern, &kind) || section.duration_frames <= 0) {
      return false;
    }

    for (int i = 0; i < section.duration_frames; ++i) {
      out_frame_patterns->push_back(kind);
    }
  }

  return !out_frame_patterns->empty();
}

int ActiveFrameLineIndex(const ActiveRasterGeometry& geometry, int line_1based) {
  if (line_1based >= geometry.first_active_line_field1 &&
      line_1based < (geometry.first_active_line_field1 + geometry.active_lines_per_field)) {
    return line_1based - geometry.first_active_line_field1;
  }
  if (line_1based >= geometry.first_active_line_field2 &&
      line_1based < (geometry.first_active_line_field2 + geometry.active_lines_per_field)) {
    return geometry.active_lines_per_field + (line_1based - geometry.first_active_line_field2);
  }
  return -1;
}

PixelYCbCr MakeColourBars75Pixel(int x, int width) {
  static constexpr int kBarCount = 8;
  static constexpr std::int16_t kY[kBarCount] = {720, 648, 524, 448, 336, 260, 140, 64};
  static constexpr std::int16_t kCb[kBarCount] = {512, 176, 624, 288, 736, 400, 848, 512};
  static constexpr std::int16_t kCr[kBarCount] = {512, 568, 176, 232, 792, 848, 456, 512};

  const int bar_width = std::max(1, width / kBarCount);
  const int bar = std::min(kBarCount - 1, x / bar_width);
  return PixelYCbCr{.y = kY[bar], .cb = kCb[bar], .cr = kCr[bar]};
}

PixelYCbCr MakeGrayscaleRampPixel(int x, int width) {
  const int span = std::max(1, width - 1);
  const double t = static_cast<double>(x) / static_cast<double>(span);
  const int y = static_cast<int>(std::lround(64.0 + (876.0 * t)));
  return PixelYCbCr{
      .y = static_cast<std::int16_t>(ClampCode(y, 64, 940)),
      .cb = 512,
      .cr = 512,
  };
}

PixelYCbCr MakePlugePixel(int x, int y, int width, int height) {
  (void)height;
  int y_code = 96;

  const int pluge_top = (3 * width) / 16;
  const int pluge_bottom = (7 * width) / 16;
  if (x >= pluge_top && x < pluge_bottom && y > 80) {
    const int segment = std::max(1, (pluge_bottom - pluge_top) / 4);
    const int local_x = x - pluge_top;
    const int band = std::min(3, local_x / segment);
    if (band == 0) {
      y_code = 68;
    } else if (band == 1) {
      y_code = 72;
    } else if (band == 2) {
      y_code = 80;
    } else {
      y_code = 100;
    }
  }

  return PixelYCbCr{.y = static_cast<std::int16_t>(y_code), .cb = 512, .cr = 512};
}

PixelYCbCr MakePatternPixel(PatternKind kind, int x, int y, int width, int height) {
  if (kind == PatternKind::kColourBars75) {
    return MakeColourBars75Pixel(x, width);
  }
  if (kind == PatternKind::kGrayscaleRamp) {
    return MakeGrayscaleRampPixel(x, width);
  }
  return MakePlugePixel(x, y, width, height);
}

double LumaMillivoltsFromCode(int y_code, const SignalLevels& levels) {
  const int clamped = ClampCode(y_code, 64, 940);
  const double y_norm = static_cast<double>(clamped - 64) / 876.0;
  return levels.black_mv + (y_norm * (levels.white_mv - levels.black_mv));
}

double ChromaMillivoltsFromCodes(int cb_code, int cr_code, double carrier_phase_rad) {
  const int cb = ClampCode(cb_code, 64, 960);
  const int cr = ClampCode(cr_code, 64, 960);
  const double cb_norm = static_cast<double>(cb - 512) / 448.0;
  const double cr_norm = static_cast<double>(cr - 512) / 448.0;
  const double chroma_scale_mv = 350.0;
  return chroma_scale_mv * ((cr_norm * std::sin(carrier_phase_rad)) +
                            (cb_norm * std::cos(carrier_phase_rad)));
}

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

    const TimingConstants timing = GetTimingConstants(project.cvbs_presets.video_standard_preset);
    const SignalLevels levels = GetSignalLevels(project.cvbs_presets.video_standard_preset);
  const std::vector<LineTimingPrimitive> lines =
      BuildFrameTimingPrimitives(project.cvbs_presets.video_standard_preset);
  const double subcarrier_hz = timing.sample_rate_4fsc_hz / 4.0;
  const double burst_amplitude_mv = 150.0;
  const int burst_start = BurstStartSamples(timing.sample_rate_4fsc_hz);
  const int burst_end = BurstEndSamples(timing.sample_rate_4fsc_hz);
  const int half_line_samples = timing.samples_per_line_4fsc / 2;
  const ActiveRasterGeometry active =
      GetActiveRasterGeometry(project.cvbs_presets.video_standard_preset, timing.sample_rate_4fsc_hz);
  std::vector<PatternKind> frame_patterns;

  if (!BuildFramePatternSchedule(project, &frame_patterns)) {
    errors->push_back(
      "Unsupported or missing software-generated pattern. Supported patterns are "
        "'ebu_colour_bars', 'grayscale_ramp_horizontal', and 'pluge'.");
    return false;
  }

  const std::size_t frame_count = frame_patterns.size();
  const std::size_t sample_count =
      frame_count * static_cast<std::size_t>(timing.lines_per_frame * timing.samples_per_line_4fsc);

  const int active_window_start =
      std::max(0, std::min(active.active_window_start_samples, timing.samples_per_line_4fsc - 1));
  const int active_window_end =
      std::max(active_window_start + 1,
           std::min(active.active_window_end_samples, timing.samples_per_line_4fsc));
  const int active_window_samples = active_window_end - active_window_start;
  const int active_height_lines = active.active_lines_per_field * 2;

  out_y_mv->assign(sample_count, levels.blanking_mv);
  out_c_mv->assign(sample_count, 0.0);

  for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    const PatternKind frame_pattern = frame_patterns[frame_index];

    for (const LineTimingPrimitive& line : lines) {
      const int line_index = line.line_number_1based - 1;
      const std::size_t frame_line_offset =
          (frame_index * static_cast<std::size_t>(timing.lines_per_frame)) +
          static_cast<std::size_t>(line_index);
      const int line_base =
          static_cast<int>(frame_line_offset * static_cast<std::size_t>(timing.samples_per_line_4fsc));
      const int line_end = line_base + timing.samples_per_line_4fsc;

      const int pulse_width =
          PulseWidthSamples(line.sync_pulse_kind,
                            project.cvbs_presets.video_standard_preset,
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

      if (line.sync_pulse_kind != SyncPulseKind::kHorizontal ||
          line.content_kind != LineContentKind::kActivePicture) {
        continue;
      }

      const int active_y = ActiveFrameLineIndex(active, line.line_number_1based);
      if (active_y < 0) {
        continue;
      }

      for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
        const int sample_index = line_base + active_window_start + x_sample;
        if (sample_index < line_base || sample_index >= line_end) {
          continue;
        }

        int pixel_x = (x_sample * active.active_width_pixels) / active_window_samples;
        pixel_x = std::min(active.active_width_pixels - 1, std::max(0, pixel_x));

        const PixelYCbCr pixel =
            MakePatternPixel(frame_pattern,
                             pixel_x,
                             active_y,
                             active.active_width_pixels,
                             active_height_lines);

        (*out_y_mv)[sample_index] = LumaMillivoltsFromCode(pixel.y, levels);

        const double t = static_cast<double>(sample_index - line_base) / timing.sample_rate_4fsc_hz;
        const double carrier_phase = (2.0 * kPi * subcarrier_hz * t) + line.burst_phase_rad;
        (*out_c_mv)[sample_index] +=
            ChromaMillivoltsFromCodes(pixel.cb, pixel.cr, carrier_phase);
      }
    }
  }

  return true;
}

}  // namespace videosynth
