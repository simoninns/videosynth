/*
 * File:        generation_stage.cpp
 * Module:      generation_stage
 * Purpose:     Generates CVBS-domain Y and C sample buffers from frame-based
 * source data.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/generation_stage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "videosynth/active_sample_mapping.h"
#include "videosynth/biphase_injection_manager.h"
#include "videosynth/chroma_encoder.h"
#include "videosynth/fixed_point.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/signal_shaping.h"
#include "videosynth/signal_timing_model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kQuarterWaveRad = kPi / 2.0;

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

struct SampledSynthesisContext {
  Standard standard = Standard::kUnknown;
  double sample_rate_hz = 0.0;
  int frame_samples = 0;
  std::vector<int> line_sample_counts;
  std::vector<int> line_sample_offsets;
  int max_line_samples = 0;
  std::vector<LineTimingPrimitive> frame_lines;
  int burst_start_samples = 0;
  int burst_end_samples = 0;
  int sync_rise_samples = 0;
  int burst_rise_samples = 0;
  ActiveRasterGeometry active;
};

int BurstStartSamples(double sample_rate_hz);
int BurstEndSamples(double sample_rate_hz);
double SyncEdgeRiseTimeSeconds(Standard standard);
double BurstEnvelopeRiseTimeSeconds(Standard standard);

std::string Lowercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::vector<int> BuildLineSampleCounts(Standard standard, int lines_per_frame,
                                       int nominal_samples) {
  std::vector<int> counts(static_cast<std::size_t>(lines_per_frame),
                          nominal_samples);
  if (standard == Standard::kPal) {
    // EBU Tech. 3280-E Section 1.2: 625-line PAL at 4fsc has 1135.0064
    // samples/line average, i.e., 709,379 samples/frame. The normative
    // placement of the four extra samples per frame is two on line 313 and two
    // on line 625.
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

ActiveRasterGeometry GetActiveRasterGeometry(Standard standard,
                                             double sample_rate_hz) {
  if (standard == Standard::kPal) {
    // ITU-R BT.1700 Annex 1 Part B Table 3: PAL line timing, mapped through
    // BT.601's 13.5 MHz sampling model. Keep the established PAL line start
    // anchor at +177 4fsc samples, but limit active-picture synthesis to
    // the 52.0 us visible-aperture duration.
    const int active_window_start = 177;
    const int active_window_end =
        active_window_start +
        std::max(1, static_cast<int>(std::lround(sample_rate_hz * 52.0e-6)));
    return ActiveRasterGeometry{
        .first_active_line_field1 = 23,
        .first_active_line_field2 = 335,
        .active_lines_per_field = 288,
        .active_window_start_samples = active_window_start,
        .active_window_end_samples = active_window_end,
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

SampledSynthesisContext BuildSampledSynthesisContext(Standard standard) {
  const TimingConstants timing = GetTimingConstants(standard);
  SampledSynthesisContext context;
  context.standard = standard;
  context.sample_rate_hz = timing.sample_rate_4fsc_hz;
  context.frame_samples = SamplesPerFrame4fsc(standard);
  context.line_sample_counts = BuildLineSampleCounts(
      standard, timing.lines_per_frame, timing.samples_per_line_4fsc);
  context.line_sample_offsets =
      BuildLineSampleOffsets(context.line_sample_counts);
  context.max_line_samples = MaxLineSamples(context.line_sample_counts);
  context.frame_lines = BuildFrameTimingPrimitives(standard);
  context.burst_start_samples = BurstStartSamples(context.sample_rate_hz);
  context.burst_end_samples = BurstEndSamples(context.sample_rate_hz);
  context.sync_rise_samples = RiseTimeToRampSamples(
      SyncEdgeRiseTimeSeconds(standard), context.sample_rate_hz);
  context.burst_rise_samples = RiseTimeToRampSamples(
      BurstEnvelopeRiseTimeSeconds(standard), context.sample_rate_hz);
  context.active = GetActiveRasterGeometry(standard, context.sample_rate_hz);
  return context;
}

bool BuildFramePatternSchedule(
    const Project& project, const ProgressiveFrameSource& progressive_source,
    Standard standard,
    std::vector<std::pair<const Section*, int>>* out_frame_sections,
    std::string* error) {
  if (out_frame_sections == nullptr) {
    return false;
  }

  out_frame_sections->clear();
  for (const Section& section : project.sections) {
    if (section.type == "progressive") {
      if (!progressive_source.SupportsSection(section)) {
        if (error != nullptr) {
          *error = "Unsupported progressive source family in section schedule.";
        }
        return false;
      }

      if (section.duration_frames_all) {
        int resolved_frame_count = 0;
        std::string count_error;
        if (!progressive_source.ResolveFrameCount(
                section, standard, &resolved_frame_count, &count_error)) {
          if (error != nullptr) {
            *error = count_error.empty()
                         ? "Failed to resolve progressive section frame count "
                           "for duration_frames='all'."
                         : count_error;
          }
          return false;
        }

        if (resolved_frame_count <= 0) {
          if (error != nullptr) {
            *error =
                "Progressive section duration_frames='all' resolved to zero "
                "frames.";
          }
          return false;
        }

        for (int i = 0; i < resolved_frame_count; ++i) {
          out_frame_sections->push_back(
              std::make_pair(&section, i + section.start_frame));
        }
        continue;
      }

      if (section.duration_frames <= 0) {
        if (error != nullptr) {
          *error =
              "Progressive sections must define duration_frames > 0 or 'all'.";
        }
        return false;
      }

      for (int i = 0; i < section.duration_frames; ++i) {
        out_frame_sections->push_back(
            std::make_pair(&section, i + section.start_frame));
      }
      continue;
    }

    if (error != nullptr) {
      *error = "Unsupported section type in generation schedule.";
    }
    return false;
  }

  return !out_frame_sections->empty();
}

int ActiveFrameLineIndex(const ActiveRasterGeometry& geometry,
                         [[maybe_unused]] Standard standard, int line_1based) {
  if (line_1based >= geometry.first_active_line_field1 &&
      line_1based < (geometry.first_active_line_field1 +
                     geometry.active_lines_per_field)) {
    return line_1based - geometry.first_active_line_field1;
  }
  if (line_1based >= geometry.first_active_line_field2 &&
      line_1based < (geometry.first_active_line_field2 +
                     geometry.active_lines_per_field)) {
    return geometry.active_lines_per_field +
           (line_1based - geometry.first_active_line_field2);
  }

  return -1;
}

int InvertCenteredChromaCode(int code) { return 1024 - code; }

double LumaMillivoltsFromCode(int y_code, const SignalLevels& levels) {
  const double y_norm = static_cast<double>(y_code - 64) / 876.0;
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
    // SMPTE 170M-2004: broad pulses are shorter than a half-line, leaving the
    // equalizing interval inside each half-line during the 3H sync block.
    return 27.1e-6;
  }
  return 27.3e-6;
}

int PulseWidthSamples(SyncPulseKind kind, Standard standard,
                      double sample_rate_hz) {
  const double pulse_seconds = PulseWidthSeconds(kind, standard);
  return std::max(
      1, static_cast<int>(std::lround(sample_rate_hz * pulse_seconds)));
}

int BurstStartSamples(double sample_rate_hz) {
  return std::max(0, static_cast<int>(std::lround(sample_rate_hz * 5.6e-6)));
}

int BurstEndSamples(double sample_rate_hz) {
  return std::max(0, static_cast<int>(std::lround(sample_rate_hz * 8.0e-6)));
}

std::vector<LinePulseSegment> BuildLinePulseSchedule(
    const LineTimingPrimitive& line, Standard standard, int half_line_samples) {
  std::vector<LinePulseSegment> schedule;

  if (standard == Standard::kPal) {
    const int line_1based = line.line_number_1based;
    if (line_1based == 1 || line_1based == 2 || line_1based == 314 ||
        line_1based == 315) {
      return {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kVerticalSync},
          LinePulseSegment{.offset_samples = half_line_samples,
                           .kind = SyncPulseKind::kVerticalSync},
      };
    }
    if (line_1based == 3) {
      return {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kVerticalSync},
          LinePulseSegment{.offset_samples = half_line_samples,
                           .kind = SyncPulseKind::kEqualizing},
      };
    }
    if (line_1based == 313) {
      return {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kEqualizing},
          LinePulseSegment{.offset_samples = half_line_samples,
                           .kind = SyncPulseKind::kVerticalSync},
      };
    }
    if (line_1based == 6 || line_1based == 318) {
      return {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kEqualizing},
      };
    }
    if (line_1based == 4 || line_1based == 5 || line_1based == 311 ||
        line_1based == 312 || line_1based == 316 || line_1based == 317 ||
        line_1based == 623 || line_1based == 624 || line_1based == 625) {
      return {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kEqualizing},
          LinePulseSegment{.offset_samples = half_line_samples,
                           .kind = SyncPulseKind::kEqualizing},
      };
    }
  }

  if (standard == Standard::kNtsc && line.line_number_1based == 263) {
    return {
        LinePulseSegment{.offset_samples = 0,
                         .kind = SyncPulseKind::kHorizontal},
        LinePulseSegment{.offset_samples = half_line_samples,
                         .kind = SyncPulseKind::kEqualizing},
    };
  }

  if (!line.has_two_half_line_pulses) {
    schedule.push_back(
        LinePulseSegment{.offset_samples = 0, .kind = line.sync_pulse_kind});
    return schedule;
  }

  schedule.push_back(
      LinePulseSegment{.offset_samples = 0, .kind = line.sync_pulse_kind});
  schedule.push_back(LinePulseSegment{.offset_samples = half_line_samples,
                                      .kind = line.sync_pulse_kind});

  if (standard == Standard::kNtsc) {
    // Align the line-granular model with the SMPTE 170M field-2 transition
    // shape seen in 4fsc reference material, where mixed half-line pulse kinds
    // occur around frame lines 263/266/269/272.
    if (line.line_number_1based == 266) {
      schedule = {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kEqualizing},
          LinePulseSegment{.offset_samples = half_line_samples,
                           .kind = SyncPulseKind::kVerticalSync},
      };
    } else if (line.line_number_1based == 269) {
      schedule = {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kVerticalSync},
          LinePulseSegment{.offset_samples = half_line_samples,
                           .kind = SyncPulseKind::kEqualizing},
      };
    } else if (line.line_number_1based == 272) {
      schedule = {
          LinePulseSegment{.offset_samples = 0,
                           .kind = SyncPulseKind::kEqualizing},
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
  // ITU-R BT.1700 Annex 1 Part B Table 2 item f and Table 3 item s: 625 PAL
  // sync/equalizing edge rise/fall 200 ns ± 100 ns measured 10%-90%.
  return 200.0e-9;
}

double BurstEnvelopeRiseTimeSeconds(Standard standard) {
  if (standard == Standard::kNtsc) {
    // SMPTE 170M-2004 Table 2: burst envelope rise 300 ns (+200/-100) 10%-90%.
    return 300.0e-9;
  }
  // ITU-R BT.1700 Annex 1 Part B Table 2 items g/h: define PAL burst placement
  // and duration, and item e defines line-blanking edge rise of 300 ns ±
  // 100 ns. This model uses the same time constant to apply a finite PAL burst
  // gate envelope.
  return 300.0e-9;
}

int PalBurstSequenceIndex(std::size_t frame_index, int field_index_1based) {
  // ITU-R BT.1700 Annex 1 Part B Table 1 item 10f: defines sequence I/II/III/IV
  // repeating over colour fields. A frame contains two consecutive fields.
  return static_cast<int>(
      ((2U * frame_index) + static_cast<std::size_t>(field_index_1based - 1)) %
      4U);
}

bool PalBurstPositiveOnOddLine(int burst_sequence_index) {
  // Sequence I/II: odd lines use +135 deg, even lines use -135 deg.
  // Sequence III/IV: odd lines use -135 deg, even lines use +135 deg.
  return burst_sequence_index == 0 || burst_sequence_index == 1;
}

bool IsPalBurstBlankedLine(int line_1based, int burst_sequence_index) {
  // ITU-R BT.1700 Annex 1 Part B Figure 8: burst blanking windows for 625 PAL.
  if (burst_sequence_index == 0) {
    return line_1based >= 623 || line_1based <= 6;
  }
  if (burst_sequence_index == 1) {
    return line_1based >= 310 && line_1based <= 318;
  }
  if (burst_sequence_index == 2) {
    return line_1based >= 622 || line_1based <= 5;
  }
  return line_1based >= 311 && line_1based <= 319;
}

double PalBurstPhaseRadForLine(std::size_t frame_index,
                               const LineTimingPrimitive& line) {
  const int burst_sequence_index =
      PalBurstSequenceIndex(frame_index, line.field_index_1based);
  const bool line_is_odd = (line.line_number_1based % 2) == 1;
  const bool positive_on_odd = PalBurstPositiveOnOddLine(burst_sequence_index);
  const bool positive_phase = line_is_odd ? positive_on_odd : !positive_on_odd;
  return positive_phase ? (3.0 * kPi / 4.0) : (-3.0 * kPi / 4.0);
}

bool PalBurstEnabledForLine(std::size_t frame_index,
                            const LineTimingPrimitive& line) {
  if (line.sync_pulse_kind != SyncPulseKind::kHorizontal) {
    return false;
  }
  const int burst_sequence_index =
      PalBurstSequenceIndex(frame_index, line.field_index_1based);
  return !IsPalBurstBlankedLine(line.line_number_1based, burst_sequence_index);
}

bool PalInvertVAxisForLine(std::size_t frame_index,
                           const LineTimingPrimitive& line) {
  return PalBurstPhaseRadForLine(frame_index, line) < 0.0;
}

using SectionVitsPlanMap = std::unordered_map<std::string, VitsSynthesisPlan>;
using SectionVitsLineMap =
    std::unordered_map<int, const Section::LineInjection*>;

bool BuildSectionVitsState(
    const Section& section, Standard standard,
    const IVitsDefinitionProvider& vits_definition_provider,
    const IVitsGenerator& vits_generator, SectionVitsLineMap* out_line_map,
    SectionVitsPlanMap* out_plan_map, std::string* error) {
  if (out_line_map == nullptr || out_plan_map == nullptr) {
    if (error != nullptr) {
      *error = "Internal generation error: null VITS state output.";
    }
    return false;
  }

  out_line_map->clear();
  out_plan_map->clear();

  for (const Section::LineInjection& injection : section.line_injections) {
    if (Lowercase(injection.type) != "vits") {
      continue;
    }

    VitsSynthesisPlan* cached_plan = nullptr;
    const auto existing_plan = out_plan_map->find(injection.vits_type);
    if (existing_plan == out_plan_map->end()) {
      VitsDefinition definition;
      std::string definition_error;
      if (!vits_definition_provider.TryGetDefinition(
              standard, injection.vits_type, &definition, &definition_error)) {
        if (error != nullptr) {
          *error = definition_error.empty()
                       ? "Failed to resolve VITS definition for line injection."
                       : definition_error;
        }
        return false;
      }

      VitsSynthesisPlan plan;
      std::string plan_error;
      if (!vits_generator.BuildSynthesisPlan(definition, &plan, &plan_error)) {
        if (error != nullptr) {
          *error = plan_error.empty() ? "Failed to build VITS synthesis plan."
                                      : plan_error;
        }
        return false;
      }

      if (plan.primitives.empty() && plan.render_order.empty()) {
        if (error != nullptr) {
          *error = "VITS type '" + injection.vits_type +
                   "' has no renderable primitives in the current runtime.";
        }
        return false;
      }

      cached_plan = &out_plan_map->emplace(injection.vits_type, std::move(plan))
                         .first->second;
    } else {
      cached_plan = &existing_plan->second;
    }

    (void)cached_plan;
    for (int target_line : injection.target_lines) {
      (*out_line_map)[target_line] = &injection;
    }
  }

  return true;
}

}  // namespace

GenerationStage::GenerationStage(
    ILogger* logger, const IVitsDefinitionProvider* vits_definition_provider,
    const IVitsGenerator* vits_generator)
    : logger_(logger),
      vits_definition_provider_(vits_definition_provider != nullptr
                                    ? vits_definition_provider
                                    : &default_vits_definition_provider_),
      vits_generator_(vits_generator != nullptr ? vits_generator
                                                : &default_vits_generator_) {}

bool GenerationStage::BuildFrameSchedule(
    const Project& project, std::vector<FrameScheduleItem>* out_schedule,
    std::vector<std::string>* errors) {
  if (out_schedule == nullptr || errors == nullptr) {
    return false;
  }

  out_schedule->clear();
  progressive_source_.ClearCache();
  biphase_manager_.Reset();
  std::vector<std::pair<const Section*, int>> frame_sections;
  std::string schedule_error;
  if (!BuildFramePatternSchedule(project, progressive_source_,
                                 project.cvbs_presets.video_standard_preset,
                                 &frame_sections, &schedule_error)) {
    errors->push_back(schedule_error.empty()
                          ? "Unable to build section frame schedule."
                          : schedule_error);
    return false;
  }

  out_schedule->reserve(frame_sections.size());
  for (const std::pair<const Section*, int>& frame_section : frame_sections) {
    out_schedule->push_back(FrameScheduleItem{
        .section = frame_section.first,
        .source_frame_index = frame_section.second,
    });
  }

  if (logger_ != nullptr) {
    logger_->Debug("Built frame schedule for " +
                   std::to_string(out_schedule->size()) + " frame(s).");
  }

  return true;
}

bool GenerationStage::GenerateFrameBatch(
    const Project& project, const std::vector<FrameScheduleItem>& schedule,
    std::size_t start_frame, std::size_t frame_count,
    std::vector<SampleFixed>* out_y_mv, std::vector<SampleFixed>* out_c_mv,
    std::vector<std::string>* errors) {
  if (out_y_mv == nullptr || out_c_mv == nullptr || errors == nullptr) {
    return false;
  }

  if (start_frame > schedule.size() ||
      (start_frame + frame_count) > schedule.size()) {
    errors->push_back("Requested frame batch is out of schedule bounds.");
    return false;
  }

  if (frame_count == 0U) {
    out_y_mv->clear();
    out_c_mv->clear();
    return true;
  }

  const TimingConstants timing =
      GetTimingConstants(project.cvbs_presets.video_standard_preset);
  const SignalLevels levels = GetSignalLevels(project.cvbs_presets);
  const SampledSynthesisContext synth =
      BuildSampledSynthesisContext(project.cvbs_presets.video_standard_preset);
  const int frame_samples = synth.frame_samples;
  const double burst_amplitude_mv = 150.0;
  std::unique_ptr<IChromaEncoder> chroma_encoder = CreateChromaEncoder(
      project.cvbs_presets.video_standard_preset, timing.sample_rate_4fsc_hz);

  if (chroma_encoder == nullptr) {
    errors->push_back("Unsupported video standard for chroma encoding.");
    return false;
  }

  const std::size_t sample_count =
      frame_count * static_cast<std::size_t>(frame_samples);

  const int active_window_start =
      std::max(0, std::min(synth.active.active_window_start_samples,
                           synth.max_line_samples - 1));
  const int active_window_end = std::max(
      active_window_start + 1,
      std::min(synth.active.active_window_end_samples, synth.max_line_samples));
  const int active_window_samples = active_window_end - active_window_start;

  const SampleFixed blanking_fixed =
      MillivoltsToSampleFixed(levels.blanking_mv);

  out_y_mv->assign(sample_count, blanking_fixed);
  out_c_mv->assign(sample_count, 0);

  auto SetYMillivolts = [&](std::size_t index, double value_mv) {
    (*out_y_mv)[index] = MillivoltsToSampleFixed(value_mv);
  };

  auto SetCMillivolts = [&](std::size_t index, double value_mv) {
    (*out_c_mv)[index] = MillivoltsToSampleFixed(value_mv);
  };

  auto AddCFixed = [&](std::size_t index, SampleFixed value_fixed) {
    (*out_c_mv)[index] += value_fixed;
  };

  auto IsYAtOrAboveBlanking = [&](std::size_t index) {
    return (*out_y_mv)[index] >= blanking_fixed;
  };

  std::vector<YCbCr444Pixel> line_source_samples(
      static_cast<std::size_t>(active_window_samples), YCbCr444Pixel{});
  std::vector<int> active_sample_indices(
      static_cast<std::size_t>(active_window_samples), 0);
  std::vector<SampleFixed> encoded_line_chroma(
      static_cast<std::size_t>(active_window_samples),
      MillivoltsToSampleFixed(0.0));

  for (std::size_t local_frame_index = 0; local_frame_index < frame_count;
       ++local_frame_index) {
    const std::size_t global_frame_index = start_frame + local_frame_index;
    const FrameScheduleItem& frame_item = schedule[global_frame_index];
    const Section* section = frame_item.section;
    const int source_frame_index = frame_item.source_frame_index;
    if (section == nullptr) {
      errors->push_back(
          "Internal generation error: null section in frame schedule.");
      return false;
    }

    if (logger_ != nullptr && schedule.size() <= 120U) {
      logger_->Trace("Generating frame " +
                     std::to_string(global_frame_index + 1U) + " of " +
                     std::to_string(schedule.size()) + " from section '" +
                     section->name + "' (" + section->type + ").");
    }

    FrameSourceImage source_frame;
    std::string frame_error;
    const bool generated = progressive_source_.GenerateFrame(
        *section, source_frame_index,
        project.cvbs_presets.video_standard_preset, &source_frame,
        &frame_error);

    if (!generated) {
      errors->push_back(frame_error.empty()
                            ? "Unable to generate frame-based source data."
                            : frame_error);
      return false;
    }

    const int local_frame_base = static_cast<int>(
        local_frame_index * static_cast<std::size_t>(frame_samples));
    const int absolute_frame_base = static_cast<int>(
        global_frame_index * static_cast<std::size_t>(frame_samples));
    SectionVitsLineMap section_vits_lines;
    SectionVitsPlanMap section_vits_plans;
    std::string vits_state_error;
    if (!BuildSectionVitsState(
            *section, project.cvbs_presets.video_standard_preset,
            *vits_definition_provider_, *vits_generator_, &section_vits_lines,
            &section_vits_plans, &vits_state_error)) {
      errors->push_back(vits_state_error.empty()
                            ? "Unable to prepare section VITS state."
                            : vits_state_error);
      return false;
    }

    for (const LineTimingPrimitive& line : synth.frame_lines) {
      const int line_index = line.line_number_1based - 1;
      const int local_line_base =
          local_frame_base +
          synth.line_sample_offsets[static_cast<std::size_t>(line_index)];
      const int absolute_line_base =
          absolute_frame_base +
          synth.line_sample_offsets[static_cast<std::size_t>(line_index)];
      const int line_samples =
          synth.line_sample_counts[static_cast<std::size_t>(line_index)];
      const int local_line_end = local_line_base + line_samples;
      const int half_line_samples = (line_samples + 1) / 2;

      const std::vector<LinePulseSegment> pulse_schedule =
          BuildLinePulseSchedule(line,
                                 project.cvbs_presets.video_standard_preset,
                                 half_line_samples);

      for (const LinePulseSegment& segment : pulse_schedule) {
        const int pulse_width = PulseWidthSamples(
            segment.kind, project.cvbs_presets.video_standard_preset,
            timing.sample_rate_4fsc_hz);
        const int pulse_offset = segment.offset_samples;
        const int pulse_start =
            local_line_base + std::min(pulse_offset, line_samples - 1);
        const int pulse_end =
            std::min(pulse_start + pulse_width, local_line_end);
        const int pulse_width_samples = pulse_end - pulse_start;

        for (int i = pulse_start; i < pulse_end; ++i) {
          const int relative_index = i - pulse_start;
          SetYMillivolts(
              static_cast<std::size_t>(i),
              ShapedPulseLevel(relative_index, pulse_width_samples,
                               synth.sync_rise_samples, levels.blanking_mv,
                               levels.sync_tip_mv));
        }
      }

      const bool is_pal =
          project.cvbs_presets.video_standard_preset == Standard::kPal;
      const bool burst_enabled =
          is_pal ? PalBurstEnabledForLine(global_frame_index, line)
                 : line.burst_enabled;
      const double burst_phase_rad =
          is_pal ? PalBurstPhaseRadForLine(global_frame_index, line)
                 : line.burst_phase_rad;

      if (burst_enabled) {
        const int burst_sample_start =
            std::min(local_line_base + synth.burst_start_samples,
                     local_line_end > 0 ? local_line_end - 1 : local_line_base);
        const int burst_sample_end =
            std::min(local_line_base + synth.burst_end_samples, local_line_end);
        const int burst_width_samples = burst_sample_end - burst_sample_start;
        const int burst_sample_start_absolute =
            absolute_line_base + (burst_sample_start - local_line_base);

        double burst_sin =
            std::sin((kQuarterWaveRad *
                      static_cast<double>(burst_sample_start_absolute)) +
                     burst_phase_rad);
        double burst_cos =
            std::cos((kQuarterWaveRad *
                      static_cast<double>(burst_sample_start_absolute)) +
                     burst_phase_rad);

        for (int i = burst_sample_start; i < burst_sample_end; ++i) {
          const int relative_index = i - burst_sample_start;
          const double envelope = ShapedGateEnvelope(
              relative_index, burst_width_samples, synth.burst_rise_samples);
          SetCMillivolts(static_cast<std::size_t>(i),
                         burst_amplitude_mv * envelope * burst_sin);

          const double next_sin = burst_cos;
          const double next_cos = -burst_sin;
          burst_sin = next_sin;
          burst_cos = next_cos;
        }
      }

      if (line.sync_pulse_kind == SyncPulseKind::kHorizontal &&
          line.content_kind == LineContentKind::kActivePicture) {
        const int active_y = ActiveFrameLineIndex(
            synth.active, project.cvbs_presets.video_standard_preset,
            line.line_number_1based);
        if (active_y >= 0) {
          std::fill(line_source_samples.begin(), line_source_samples.end(),
                    YCbCr444Pixel{});
          std::fill(active_sample_indices.begin(), active_sample_indices.end(),
                    local_line_base);

          const bool invert_pal_v_axis =
              is_pal && PalInvertVAxisForLine(global_frame_index, line);
          const int active_window_line_start_absolute =
              absolute_line_base + active_window_start;
          // SMPTE 170M-2004 Section 10: defines active chroma with burst+180
          // deg reference for NTSC.
          const double phase_offset =
              (project.cvbs_presets.video_standard_preset == Standard::kNtsc)
                  ? (line.burst_phase_rad + kPi)
                  : 0.0;
          const double phase_start =
              (kQuarterWaveRad *
               static_cast<double>(active_window_line_start_absolute)) +
              phase_offset;

          for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
            const int sample_index =
                local_line_base + active_window_start + x_sample;
            if (sample_index < local_line_base ||
                sample_index >= local_line_end) {
              continue;
            }

            const int pixel_x = MapActiveSampleToSourcePixel(
                x_sample, active_window_samples, source_frame.active_width,
                source_frame.active_x);

            // Map field lines onto progressive source rows by interleaving
            // fields. Progressive imports use field-2-dominant row pairing, so
            // field 1 consumes odd rows and field 2 consumes even rows.
            const int field_line =
                active_y % synth.active.active_lines_per_field;
            const bool progressive_section = section->type == "progressive";
            const int source_row =
                source_frame.active_y +
                ((line.field_index_1based == 1)
                     ? (2 * field_line + (progressive_section ? 1 : 0))
                     : (2 * field_line + (progressive_section ? 0 : 1)));

            if (pixel_x >= source_frame.width ||
                source_row >= source_frame.height) {
              continue;
            }

            const YCbCr444Pixel& pixel =
                source_frame.PixelAt(pixel_x, source_row);
            const std::size_t sample_slot = static_cast<std::size_t>(x_sample);
            active_sample_indices[sample_slot] = sample_index;
            line_source_samples[sample_slot] = pixel;

            if (invert_pal_v_axis) {
              // ITU-R BT.1700 Annex 1 Part B Table 1 item 10f: PAL V-axis
              // switching follows the burst-sequence-dependent odd/even
              // polarity map.
              line_source_samples[sample_slot].cr =
                  static_cast<std::int16_t>(InvertCenteredChromaCode(pixel.cr));
            }

            // Preserve any sync-domain sample already placed for this line;
            // only paint active luma where the waveform is at/above blanking
            // level.
            if (IsYAtOrAboveBlanking(static_cast<std::size_t>(sample_index))) {
              SetYMillivolts(static_cast<std::size_t>(sample_index),
                             LumaMillivoltsFromCode(pixel.y, levels));
            }
          }

          chroma_encoder->EncodeLineFromPhaseStart(
              line_source_samples, phase_start, &encoded_line_chroma);
          for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
            AddCFixed(
                static_cast<std::size_t>(
                    active_sample_indices[static_cast<std::size_t>(x_sample)]),
                encoded_line_chroma[static_cast<std::size_t>(x_sample)]);
          }
        }
      }

      const auto vits_line = section_vits_lines.find(line.line_number_1based);
      if (vits_line != section_vits_lines.end()) {
        const auto vits_plan =
            section_vits_plans.find(vits_line->second->vits_type);
        if (vits_plan == section_vits_plans.end()) {
          errors->push_back(
              "Internal generation error: missing VITS plan for targeted line "
              "injection.");
          return false;
        }

        VitsRenderedLine rendered_line;
        std::string render_error;
        if (!vits_generator_->RenderLine(
                vits_plan->second, timing.sample_rate_4fsc_hz, line_samples,
                &rendered_line, &render_error)) {
          errors->push_back(render_error.empty()
                                ? "Failed to render VITS line injection."
                                : render_error);
          return false;
        }

        for (int sample_offset = 0; sample_offset < line_samples;
             ++sample_offset) {
          const std::size_t frame_sample_index =
              static_cast<std::size_t>(local_line_base + sample_offset);
          (*out_y_mv)[frame_sample_index] +=
              rendered_line
                  .y_samples_mv[static_cast<std::size_t>(sample_offset)];
          (*out_c_mv)[frame_sample_index] +=
              rendered_line
                  .c_samples_mv[static_cast<std::size_t>(sample_offset)];
        }
      }
    }

    if (!biphase_manager_.ProcessFrame(
            out_y_mv, local_frame_base, synth.line_sample_offsets,
            synth.line_sample_counts, *section,
            project.cvbs_presets.video_standard_preset,
            timing.sample_rate_4fsc_hz, synth.frame_lines, active_window_start,
            errors)) {
      return false;
    }
  }

  return true;
}

bool GenerationStage::Generate(const Project& project,
                               std::vector<SampleFixed>* out_y_mv,
                               std::vector<SampleFixed>* out_c_mv,
                               std::vector<std::string>* errors) {
  if (out_y_mv == nullptr || out_c_mv == nullptr || errors == nullptr) {
    return false;
  }

  if (logger_ != nullptr) {
    logger_->Debug("Generating full frame buffer for " +
                   std::to_string(project.sections.size()) + " section(s).");
  }

  std::vector<FrameScheduleItem> schedule;
  if (!BuildFrameSchedule(project, &schedule, errors)) {
    return false;
  }

  if (!GenerateFrameBatch(project, schedule, 0, schedule.size(), out_y_mv,
                          out_c_mv, errors)) {
    return false;
  }

  if (logger_ != nullptr) {
    logger_->Info("Generated " + std::to_string(schedule.size()) +
                  " frame(s) of signal data.");
  }

  return true;
}

}  // namespace videosynth
