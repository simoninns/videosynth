/*
 * File:        generation_stage.cpp
 * Module:      generation_stage
 * Purpose:     Generates CVBS-domain Y and C sample buffers from frame-based source data.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/generation_stage.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "videosynth/chroma_encoder.h"
#include "videosynth/frame_source.h"
#include "videosynth/signal_timing_model.h"
#include "videosynth/signal_shaping.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct ActiveRasterGeometry {
  int first_active_line_field1 = 0;
  int first_active_line_field2 = 0;
  int active_lines_per_field = 0;
  int active_window_start_samples = 0;
  int active_window_end_samples = 0;
  int active_width_pixels = 720;
};

struct LinePulseSegment {
  int offset_samples = 0;
  SyncPulseKind kind = SyncPulseKind::kHorizontal;
};

std::vector<int> BuildLineSampleCounts(Standard standard, int lines_per_frame, int nominal_samples) {
  std::vector<int> counts(static_cast<std::size_t>(lines_per_frame), nominal_samples);
  if (standard == Standard::kPal) {
    // EBU Tech. 3280-E Section 1.2: 625-line PAL at 4fsc has 1135.0064
    // samples/line average, i.e. 709,379 samples/frame. The normative placement
    // of the four extra samples per frame is two on line 313 and two on line 625.
    constexpr int kLongLines[] = {313, 625};
    for (int line_1based : kLongLines) {
      counts[static_cast<std::size_t>(line_1based - 1)] += 2;
    }
  }
  return counts;
}

std::vector<int> BuildLineSampleOffsets(const std::vector<int>& line_samples) {
  std::vector<int> offsets(line_samples.size(), 0);
  int running = 0;
  for (std::size_t i = 0; i < line_samples.size(); ++i) {
    offsets[i] = running;
    running += line_samples[i];
  }
  return offsets;
}

int MaxLineSamples(const std::vector<int>& line_samples) {
  int max_samples = 0;
  for (int samples : line_samples) {
    max_samples = std::max(max_samples, samples);
  }
  return max_samples;
}

int ClampCode(int code, int lo, int hi) {
  return std::max(lo, std::min(code, hi));
}

ActiveRasterGeometry GetActiveRasterGeometry(Standard standard, double sample_rate_hz) {
  if (standard == Standard::kPal) {
  // EBU Tech. 3280-E Section 1.2 defines PAL 4fsc line numbering with
  // digital active samples at indices 0..947 and blanking at 948..1134.
  // Relative to the sample following sync leading edge, active starts at
  // +177 samples and spans 948 samples.
    return ActiveRasterGeometry{
        .first_active_line_field1 = 23,
        .first_active_line_field2 = 336,
        .active_lines_per_field = 288,
    .active_window_start_samples = 177,
    .active_window_end_samples = 1125,
        .active_width_pixels = 720,
    };
  }

  const int active_window_start =
    std::max(0, static_cast<int>(std::lround(sample_rate_hz * 10.5e-6)));
  const int active_window_end =
    std::max(active_window_start + 1,
         static_cast<int>(std::lround(sample_rate_hz * 62.5e-6)));

  return ActiveRasterGeometry{
      .first_active_line_field1 = 22,
      .first_active_line_field2 = 284,
      .active_lines_per_field = 240,
      .active_window_start_samples = active_window_start,
      .active_window_end_samples = active_window_end,
      .active_width_pixels = 720,
  };
}

bool BuildFramePatternSchedule(const Project& project,
                               const TestPatternFrameSource& frame_source,
                               std::vector<std::string>* out_frame_patterns) {
  if (out_frame_patterns == nullptr) {
    return false;
  }

  out_frame_patterns->clear();
  for (const Section& section : project.sections) {
    if (section.type != "software_generated") {
      continue;
    }

    if (!frame_source.SupportsPattern(section.pattern) || section.duration_frames <= 0) {
      return false;
    }

    for (int i = 0; i < section.duration_frames; ++i) {
      out_frame_patterns->push_back(section.pattern);
    }
  }

  return !out_frame_patterns->empty();
}

int ActiveFrameLineIndex(const ActiveRasterGeometry& geometry,
                         Standard standard,
                         int line_1based) {
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

int InvertCenteredChromaCode(int code) {
  const int clamped = ClampCode(code, 64, 960);
  return 1024 - clamped;
}

double LumaMillivoltsFromCode(int y_code, const SignalLevels& levels) {
  const int clamped = ClampCode(y_code, 64, 940);
  const double y_norm = static_cast<double>(clamped - 64) / 876.0;
  return levels.black_mv + (y_norm * (levels.white_mv - levels.black_mv));
}

double PulseWidthSeconds(SyncPulseKind kind, Standard standard) {
  if (kind == SyncPulseKind::kHorizontal) {
    return 4.7e-6;
  }
  if (kind == SyncPulseKind::kEqualizing) {
    return 2.3e-6;
  }
  if (standard == Standard::kNtsc) {
    // SMPTE 170M broad pulses are shorter than a half-line, leaving the
    // equalizing interval inside each half-line during the 3H sync block.
    return 27.1e-6;
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

std::vector<LinePulseSegment> BuildLinePulseSchedule(const LineTimingPrimitive& line,
                                                     Standard standard,
                                                     int half_line_samples) {
  std::vector<LinePulseSegment> schedule;

  if (standard == Standard::kPal) {
    const int line_1based = line.line_number_1based;
    if (line_1based == 1 || line_1based == 2 || line_1based == 314 || line_1based == 315) {
      return {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kVerticalSync},
          LinePulseSegment{.offset_samples = half_line_samples, .kind = SyncPulseKind::kVerticalSync},
      };
    }
    if (line_1based == 3) {
      return {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kVerticalSync},
          LinePulseSegment{.offset_samples = half_line_samples, .kind = SyncPulseKind::kEqualizing},
      };
    }
    if (line_1based == 313) {
      return {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kEqualizing},
          LinePulseSegment{.offset_samples = half_line_samples, .kind = SyncPulseKind::kVerticalSync},
      };
    }
    if (line_1based == 6 || line_1based == 318) {
      return {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kEqualizing},
      };
    }
    if (line_1based == 4 || line_1based == 5 || line_1based == 311 || line_1based == 312 ||
        line_1based == 316 || line_1based == 317 || line_1based == 623 || line_1based == 624 ||
        line_1based == 625) {
      return {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kEqualizing},
          LinePulseSegment{.offset_samples = half_line_samples, .kind = SyncPulseKind::kEqualizing},
      };
    }
  }

  if (standard == Standard::kNtsc && line.line_number_1based == 263) {
    return {
        LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kHorizontal},
        LinePulseSegment{.offset_samples = half_line_samples, .kind = SyncPulseKind::kEqualizing},
    };
  }

  if (!line.has_two_half_line_pulses) {
    schedule.push_back(LinePulseSegment{.offset_samples = 0, .kind = line.sync_pulse_kind});
    return schedule;
  }

  schedule.push_back(LinePulseSegment{.offset_samples = 0, .kind = line.sync_pulse_kind});
  schedule.push_back(
      LinePulseSegment{.offset_samples = half_line_samples, .kind = line.sync_pulse_kind});

  if (standard == Standard::kNtsc) {
    // Align the line-granular model with the SMPTE 170M field-2 transition shape
    // seen in 4fsc reference material, where mixed half-line pulse kinds occur
    // around frame lines 263/266/269/272.
    if (line.line_number_1based == 266) {
      schedule = {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kEqualizing},
          LinePulseSegment{.offset_samples = half_line_samples, .kind = SyncPulseKind::kVerticalSync},
      };
    } else if (line.line_number_1based == 269) {
      schedule = {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kVerticalSync},
          LinePulseSegment{.offset_samples = half_line_samples, .kind = SyncPulseKind::kEqualizing},
      };
    } else if (line.line_number_1based == 272) {
      schedule = {
          LinePulseSegment{.offset_samples = 0, .kind = SyncPulseKind::kEqualizing},
      };
    }
  }

  return schedule;
}

double SyncEdgeRiseTimeSeconds(Standard standard) {
  if (standard == Standard::kNtsc) {
    // SMPTE 170M-2004 Table 2 plus Note 1: sync pulse rise/fall 140 ns ± 20 ns
    // measured 10%-90%.
    return 140.0e-9;
  }
  // ITU-R BT.1700 Table 2 item f and Table 3 item s: 625 PAL sync/equalizing
  // edge rise/fall 200 ns ± 100 ns measured 10%-90%.
  return 200.0e-9;
}

double BurstEnvelopeRiseTimeSeconds(Standard standard) {
  if (standard == Standard::kNtsc) {
    // SMPTE 170M-2004 Table 2: burst envelope rise 300 ns (+200/-100) 10%-90%.
    return 300.0e-9;
  }
  // ITU-R BT.1700 Table 2 items g/h define PAL burst placement and duration, and
  // item e defines line-blanking edge rise of 300 ns ± 100 ns. This model uses
  // the same time constant to apply a finite PAL burst gate envelope.
  return 300.0e-9;
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
  const std::vector<int> line_sample_counts =
      BuildLineSampleCounts(project.cvbs_presets.video_standard_preset,
                            timing.lines_per_frame,
                            timing.samples_per_line_4fsc);
  const std::vector<int> line_sample_offsets = BuildLineSampleOffsets(line_sample_counts);
  const int frame_samples = SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset);
  const int max_line_samples = MaxLineSamples(line_sample_counts);
  const std::vector<LineTimingPrimitive> lines =
      BuildFrameTimingPrimitives(project.cvbs_presets.video_standard_preset);
  const double subcarrier_hz = timing.sample_rate_4fsc_hz / 4.0;
  const double burst_amplitude_mv = 150.0;
  const int burst_start = BurstStartSamples(timing.sample_rate_4fsc_hz);
  const int burst_end = BurstEndSamples(timing.sample_rate_4fsc_hz);
  const int sync_rise_samples =
      RiseTimeToRampSamples(SyncEdgeRiseTimeSeconds(project.cvbs_presets.video_standard_preset),
                timing.sample_rate_4fsc_hz);
  const int burst_rise_samples =
      RiseTimeToRampSamples(BurstEnvelopeRiseTimeSeconds(project.cvbs_presets.video_standard_preset),
                timing.sample_rate_4fsc_hz);
  const ActiveRasterGeometry active =
      GetActiveRasterGeometry(project.cvbs_presets.video_standard_preset, timing.sample_rate_4fsc_hz);
  const TestPatternFrameSource frame_source;
  std::unique_ptr<IChromaEncoder> chroma_encoder =
      CreateChromaEncoder(project.cvbs_presets.video_standard_preset, timing.sample_rate_4fsc_hz);
  std::vector<std::string> frame_patterns;

  if (chroma_encoder == nullptr) {
    errors->push_back("Unsupported video standard for chroma encoding.");
    return false;
  }

  if (!BuildFramePatternSchedule(project, frame_source, &frame_patterns)) {
    errors->push_back(
      "Unsupported or missing software-generated pattern. Supported patterns are "
        "'ebu_colour_bars', 'grayscale_ramp_horizontal', and 'pluge'.");
    return false;
  }

  const std::size_t frame_count = frame_patterns.size();
    const std::size_t sample_count = frame_count * static_cast<std::size_t>(frame_samples);

  const int active_window_start =
      std::max(0, std::min(active.active_window_start_samples, max_line_samples - 1));
  const int active_window_end =
      std::max(active_window_start + 1,
         std::min(active.active_window_end_samples, max_line_samples));
  const int active_window_samples = active_window_end - active_window_start;

  out_y_mv->assign(sample_count, levels.blanking_mv);
  out_c_mv->assign(sample_count, 0.0);

  for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    FrameSourceImage source_frame;
    std::string frame_error;
    if (!frame_source.GenerateFrame(frame_patterns[frame_index],
                                    project.cvbs_presets.video_standard_preset,
                                    &source_frame,
                                    &frame_error)) {
      errors->push_back(frame_error.empty() ? "Unable to generate frame-based source data."
                                            : frame_error);
      return false;
    }

    // NTSC: 525 lines/frame × π rad/line = 525π ≡ π (mod 2π) accumulated per frame,
    // producing the 2-frame SC-H phase pattern defined in SMPTE 170M.
    const double frame_phase_offset =
        (project.cvbs_presets.video_standard_preset == Standard::kNtsc)
            ? static_cast<double>(frame_index % 2) * kPi
            : 0.0;

    for (const LineTimingPrimitive& line : lines) {
      const int line_index = line.line_number_1based - 1;
      const int frame_base = static_cast<int>(frame_index * static_cast<std::size_t>(frame_samples));
      const int line_base = frame_base + line_sample_offsets[static_cast<std::size_t>(line_index)];
      const int line_samples = line_sample_counts[static_cast<std::size_t>(line_index)];
      const int line_end = line_base + line_samples;
      const int half_line_samples = (line_samples + 1) / 2;

      const std::vector<LinePulseSegment> pulse_schedule =
          BuildLinePulseSchedule(line,
                                 project.cvbs_presets.video_standard_preset,
                                 half_line_samples);

      for (const LinePulseSegment& segment : pulse_schedule) {
        const int pulse_width =
            PulseWidthSamples(segment.kind,
                              project.cvbs_presets.video_standard_preset,
                              timing.sample_rate_4fsc_hz);
        const int pulse_offset = segment.offset_samples;
        const int pulse_start = line_base + std::min(pulse_offset, line_samples - 1);
        const int pulse_end = std::min(pulse_start + pulse_width, line_end);
        const int pulse_width_samples = pulse_end - pulse_start;

        for (int i = pulse_start; i < pulse_end; ++i) {
          const int relative_index = i - pulse_start;
          (*out_y_mv)[i] = ShapedPulseLevel(relative_index,
                                           pulse_width_samples,
                                           sync_rise_samples,
                                           levels.blanking_mv,
                                           levels.sync_tip_mv);
        }
      }

      if (line.burst_enabled) {
        const int burst_sample_start =
            std::min(line_base + burst_start, line_end > 0 ? line_end - 1 : line_base);
        const int burst_sample_end = std::min(line_base + burst_end, line_end);
        const int burst_width_samples = burst_sample_end - burst_sample_start;
        const double phase = line.burst_phase_rad + frame_phase_offset;

        for (int i = burst_sample_start; i < burst_sample_end; ++i) {
          const int relative_index = i - burst_sample_start;
          const double envelope = ShapedGateEnvelope(relative_index,
                               burst_width_samples,
                               burst_rise_samples);
          const double t = static_cast<double>(i - line_base) / timing.sample_rate_4fsc_hz;
          (*out_c_mv)[i] =
              burst_amplitude_mv * envelope * std::sin((2.0 * kPi * subcarrier_hz * t) + phase);
        }
      }

      if (line.sync_pulse_kind != SyncPulseKind::kHorizontal ||
          line.content_kind != LineContentKind::kActivePicture) {
        continue;
      }

      const int active_y = ActiveFrameLineIndex(active,
                    project.cvbs_presets.video_standard_preset,
                    line.line_number_1based);
      if (active_y < 0) {
        continue;
      }

      std::vector<YCbCr444Pixel> line_source_samples(
          static_cast<std::size_t>(active_window_samples), YCbCr444Pixel{});
      std::vector<double> carrier_phases_rad(static_cast<std::size_t>(active_window_samples), 0.0);
      std::vector<int> active_sample_indices(static_cast<std::size_t>(active_window_samples), line_base);

      for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
        const int sample_index = line_base + active_window_start + x_sample;
        if (sample_index < line_base || sample_index >= line_end) {
          continue;
        }

        int pixel_x = (x_sample * active.active_width_pixels) / active_window_samples;
        pixel_x = std::min(active.active_width_pixels - 1, std::max(0, pixel_x));

        // Map from interlaced field-line index to the progressive source row.
        // Field 1 occupies even rows (0, 2, 4 …) and field 2 occupies odd rows
        // (1, 3, 5 …) so that when a viewer deinterlaces by interleaving the two
        // fields, each display row reads from the correct progressive source line.
        const int field_line = active_y % active.active_lines_per_field;
        const int source_row = (line.field_index_1based == 1)
                                   ? (2 * field_line)
                                   : (2 * field_line + 1);

        if (pixel_x >= source_frame.width || source_row >= source_frame.height) {
          continue;
        }

        const YCbCr444Pixel& pixel = source_frame.PixelAt(pixel_x, source_row);
        const std::size_t sample_slot = static_cast<std::size_t>(x_sample);
        active_sample_indices[sample_slot] = sample_index;
        line_source_samples[sample_slot] = pixel;

        if (project.cvbs_presets.video_standard_preset == Standard::kPal &&
            (line.line_number_1based % 2) == 0) {
          // PAL phase alternation is implemented by inverting the V axis on
          // successive lines while keeping U unchanged.
          line_source_samples[sample_slot].cr =
              static_cast<std::int16_t>(InvertCenteredChromaCode(pixel.cr));
        }

        // Preserve any sync-domain sample already placed for this line; only
        // paint active luma where the waveform is at/above blanking level.
        if ((*out_y_mv)[sample_index] >= levels.blanking_mv) {
          (*out_y_mv)[sample_index] = LumaMillivoltsFromCode(pixel.y, levels);
        }

        const double t = static_cast<double>(sample_index - line_base) / timing.sample_rate_4fsc_hz;
        double carrier_phase = 0.0;
        if (project.cvbs_presets.video_standard_preset == Standard::kNtsc) {
          carrier_phase = (2.0 * kPi * subcarrier_hz * t) + line.burst_phase_rad + frame_phase_offset;
          // SMPTE 170M-2004 Section 10 defines wt using burst+180 deg as the
          // active chroma phase reference.
          carrier_phase += kPi;
        } else {
          // For PAL, use the subcarrier's +U-axis reference and perform the
          // mandated line-alternation by flipping V on even lines.
          carrier_phase = (2.0 * kPi * subcarrier_hz * t) + (kPi / 4.0);
        }
        carrier_phases_rad[sample_slot] = carrier_phase;
      }

      std::vector<double> encoded_line_chroma;
      chroma_encoder->EncodeLine(line_source_samples, carrier_phases_rad, &encoded_line_chroma);
      for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
        (*out_c_mv)[static_cast<std::size_t>(active_sample_indices[static_cast<std::size_t>(x_sample)])] +=
            encoded_line_chroma[static_cast<std::size_t>(x_sample)];
      }
    }
  }

  return true;
}

}  // namespace videosynth
