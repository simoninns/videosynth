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
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "videosynth/active_sample_mapping.h"
#include "videosynth/biphase_injection_manager.h"
#include "videosynth/chroma_encoder.h"
#include "videosynth/clv_code_generator.h"
#include "videosynth/fixed_point.h"
#include "videosynth/frame_enrichment.h"
#include "videosynth/frame_line_layout.h"
#include "videosynth/osd_renderer.h"
#include "videosynth/osd_token_resolver.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/signal_shaping.h"
#include "videosynth/signal_timing_model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

// Sampled-domain synthesis detail types. They live in a named namespace rather
// than the file's anonymous namespace so that GenerationStage::
// SynthesisResources — a member of an externally-linked class — can hold them.
namespace generation_detail {

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

// No line of any supported standard carries more than two sync pulses: one at
// the line start and, on half-line lines, one at the half-line point.
constexpr int kMaxLinePulseSegments = 2;

// The sync pulses of one frame line. The schedule is a pure function of
// (standard, line), so a run resolves all of them once instead of building a
// fresh vector for every line of every frame.
struct LinePulsePlan {
  int segment_count = 0;
  std::array<LinePulseSegment, kMaxLinePulseSegments> segments{};
};

// One shaped sync pulse, rendered once per (standard, pulse kind, levels).
// A pulse truncated by the end of its line has a different S-curve to the
// nominal one, so the sample loop only copies the table when the widths agree.
struct SyncPulseWaveform {
  int width_samples = 0;
  std::vector<SampleFixed> levels;
};

// One rendered LaserDisc pilot burst, as fixed-point additions to the sync tip.
//
// 17,734,475 = 25 × 709,379, so the pilot advances a whole number of cycles per
// PAL frame and its waveform repeats on every frame; each burst is therefore
// rendered once per run from its offset within the frame.
struct PilotBurstSegment {
  int offset_in_line = 0;
  std::vector<SampleFixed> samples;
};

struct PilotBurstLine {
  int segment_count = 0;
  std::array<PilotBurstSegment, kMaxLinePulseSegments> segments;
};

// Count of SyncPulseKind enumerators, used to size the pulse waveform table.
constexpr int kSyncPulseKindCount = 3;

struct SampledSynthesisContext {
  Standard standard = Standard::kUnknown;
  double sample_rate_hz = 0.0;
  int frame_samples = 0;
  std::vector<int> line_sample_counts;
  std::vector<int> line_sample_offsets;
  int max_line_samples = 0;
  std::vector<LineTimingPrimitive> frame_lines;
  std::vector<LinePulsePlan> line_pulse_plans;
  int burst_start_samples = 0;
  int burst_end_samples = 0;
  int sync_rise_samples = 0;
  int burst_rise_samples = 0;
  ActiveRasterGeometry active;
};

// Maps each active-window sample position to its source pixel column.
//
// MapActiveSampleToSourcePixel performs an integer division per call, and the
// mapping only depends on the active window width and the source's active
// raster, so the whole line's worth of positions is resolved once per source
// geometry rather than once per sample of every active line.
//
// The mapping is monotonically non-decreasing, so samples whose pixel column
// falls outside the source raster form a suffix; in_range_sample_count records
// where that suffix starts and lets the sample loop drop its bounds check.
struct ActiveSampleColumnMap {
  int active_width = -1;
  int active_x = -1;
  int source_width = -1;
  int in_range_sample_count = 0;
  std::vector<int> pixel_x;

  bool MatchesSource(const FrameSourceImage& source) const {
    return active_width == source.active_width && active_x == source.active_x &&
           source_width == source.width;
  }

  void Rebuild(const FrameSourceImage& source, int active_window_samples) {
    active_width = source.active_width;
    active_x = source.active_x;
    source_width = source.width;
    pixel_x.assign(static_cast<std::size_t>(active_window_samples), 0);
    in_range_sample_count = active_window_samples;
    for (int x_sample = 0; x_sample < active_window_samples; ++x_sample) {
      const int mapped =
          MapActiveSampleToSourcePixel(x_sample, active_window_samples,
                                       source.active_width, source.active_x);
      pixel_x[static_cast<std::size_t>(x_sample)] = mapped;
      if (mapped >= source.width && x_sample < in_range_sample_count) {
        in_range_sample_count = x_sample;
      }
    }
  }
};

}  // namespace generation_detail

namespace {

using generation_detail::ActiveRasterGeometry;
using generation_detail::ActiveSampleColumnMap;
using generation_detail::LinePulsePlan;
using generation_detail::LinePulseSegment;
using generation_detail::SampledSynthesisContext;
using generation_detail::SyncPulseWaveform;

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kQuarterWaveRad = kPi / 2.0;

// Samples per subcarrier cycle at 4fsc. The carrier advances exactly π/2 per
// sample, so its phase is a pure function of the sample index modulo this
// value; see docs/design/performance-optimisation-plan.md "Exploitable
// structure".
constexpr std::size_t kSubcarrierLatticeSamples = 4U;

// Reduces a phase argument into [0, 2π).
//
// Every phase in the synthesiser is periodic, so this is mathematically a
// no-op; it exists so the arguments handed to sin/cos stay bounded no matter
// how long the render is, instead of growing with the absolute sample index and
// losing low-order precision.
double WrapPhaseRad(double phase_rad) {
  double wrapped = std::fmod(phase_rad, kTwoPi);
  if (wrapped < 0.0) {
    wrapped += kTwoPi;
  }
  return wrapped;
}

// Phase of the colour subcarrier at an absolute sample index, exactly.
//
// Reducing the index onto the 4-sample lattice before the multiply keeps the
// result exact for the life of the render: sin/cos see one of four bounded
// arguments rather than (π/2) × an index that reaches 1.9 × 10^11 on a
// three-hour disc.
double SubcarrierPhaseRad(std::size_t absolute_sample_index) {
  return kQuarterWaveRad *
         static_cast<double>(absolute_sample_index % kSubcarrierLatticeSamples);
}

// IEC 60856 §9.1.2: pilot burst frequency is 240 × fH = 3.75 MHz and
// amplitude is 6/7 of (white - blanking) = 6/7 × 700 mV = 600 mV p-p,
// centred on sync tip (−300 mV), giving ±300 mV swing.
constexpr double kPilotBurstFreqHz = 3.75e6;
constexpr double kPilotBurstAmplitudeMv = 300.0;

// ITU-R BT.1700 Annex 1 Part B Figure 8 with Table 1 item 10f: the 8-field
// PAL sequence pairs each burst-blanking meander position with a specific
// subcarrier-to-frame phase. Rotating the subcarrier lattice by 270° anchors
// disc frame 0 (meander parity 0, fields I/II) to the subcarrier phase that
// decoders identify as colour fields 1/2. Validated against the
// ld-decode/decode-orc field-phase detection: without this anchor the two
// fields of a frame decode as non-consecutive field IDs. The anchor applies
// to 625-line PAL only; PAL-M keeps a zero anchor.
constexpr double kPalSubcarrierAnchorRad = 3.0 * kPi / 2.0;

int BurstStartSamples(Standard standard, double sample_rate_hz);
int BurstEndSamples(Standard standard, double sample_rate_hz);
double SyncEdgeRiseTimeSeconds(Standard standard);
double BurstEnvelopeRiseTimeSeconds(Standard standard);
std::vector<generation_detail::LinePulsePlan> BuildLinePulsePlans(
    Standard standard, const std::vector<LineTimingPrimitive>& frame_lines,
    const std::vector<int>& line_sample_counts);

std::string Lowercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
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
  context.line_pulse_plans = BuildLinePulsePlans(standard, context.frame_lines,
                                                 context.line_sample_counts);
  context.burst_start_samples =
      BurstStartSamples(standard, context.sample_rate_hz);
  context.burst_end_samples = BurstEndSamples(standard, context.sample_rate_hz);
  context.sync_rise_samples = RiseTimeToRampSamples(
      SyncEdgeRiseTimeSeconds(standard), context.sample_rate_hz);
  context.burst_rise_samples = RiseTimeToRampSamples(
      BurstEnvelopeRiseTimeSeconds(standard), context.sample_rate_hz);
  context.active = GetActiveRasterGeometry(standard, context.sample_rate_hz);
  return context;
}

// Returns the picture_number start_value from a section's laserdisc injection,
// or 0 if the section has no picture_number code.
int FindSectionPictureNumberStart(const Section& section) {
  for (const Section::LineInjection& inj : section.line_injections) {
    if (Lowercase(inj.type) != "laserdisc") {
      continue;
    }
    for (const Section::LineInjectionCode& code : inj.codes) {
      if (code.code_type == "picture_number" && code.start_value_specified) {
        return code.start_value;
      }
    }
  }
  return 0;
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

        // Replay the whole resolved source duration_frames_repeat times. Each
        // pass re-emits source frames 0..N-1 (a loop), so the source frame
        // index never exceeds the source length regardless of the multiplier.
        const int repeat = section.duration_frames_repeat > 0
                               ? section.duration_frames_repeat
                               : 1;
        for (int pass = 0; pass < repeat; ++pass) {
          for (int i = 0; i < resolved_frame_count; ++i) {
            out_frame_sections->push_back(
                std::make_pair(&section, i + section.start_frame));
          }
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

// Number of entries in the luma code lookup table: the whole 10-bit code space
// the frame sources clamp their samples into (progressive_frame_source.cpp).
constexpr int kLumaCodeTableSize = 1024;

// Builds the luma code -> fixed-point millivolt table. The mapping is a pure
// function of the signal levels, so a run resolves it once instead of doing a
// divide, a multiply-add and an llround on every active sample.
std::vector<SampleFixed> BuildLumaCodeTable(const SignalLevels& levels) {
  std::vector<SampleFixed> table(static_cast<std::size_t>(kLumaCodeTableSize));
  for (int code = 0; code < kLumaCodeTableSize; ++code) {
    table[static_cast<std::size_t>(code)] =
        MillivoltsToSampleFixed(LumaMillivoltsFromCode(code, levels));
  }
  return table;
}

double PulseWidthSeconds(SyncPulseKind kind, Standard standard) {
  if (kind == SyncPulseKind::kHorizontal) {
    return 4.7e-6;
  }
  if (kind == SyncPulseKind::kEqualizing) {
    return 2.3e-6;
  }
  if (standard == Standard::kNtsc || standard == Standard::kPalM) {
    // SMPTE 170M-2004 Table 1-2 item q: broad pulses 27.1 µs for System M.
    // PAL-M uses the same System M sync structure as M/NTSC.
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

int BurstStartSamples(Standard standard, double sample_rate_hz) {
  // ITU-R BT.470-6 Table 2 item 2.14g: burst gate start after epoch O_H.
  // M/NTSC: 5.3 µs nominal; B,G,H,I/PAL: 5.6 µs; M/PAL: 5.8 µs.
  double start_us = 5.6e-6;
  if (standard == Standard::kNtsc) {
    start_us = 5.3e-6;
  } else if (standard == Standard::kPalM) {
    start_us = 5.8e-6;
  }
  return std::max(0, static_cast<int>(std::lround(sample_rate_hz * start_us)));
}

int BurstEndSamples(Standard standard, double sample_rate_hz) {
  // ITU-R BT.470-6 Table 2 item 2.14g/h: burst gate end = start + duration.
  // M/NTSC: 5.3+2.67=7.97 µs; B,G,H,I/PAL: 5.6+2.25=7.85 µs (use 8.0 µs);
  // M/PAL: 5.8+2.52=8.32 µs.
  double end_us = 8.0e-6;
  if (standard == Standard::kNtsc) {
    end_us = 7.97e-6;
  } else if (standard == Standard::kPalM) {
    end_us = 8.32e-6;
  }
  return std::max(0, static_cast<int>(std::lround(sample_rate_hz * end_us)));
}

LinePulsePlan OnePulse(SyncPulseKind kind) {
  return LinePulsePlan{
      .segment_count = 1,
      .segments = {LinePulseSegment{.offset_samples = 0, .kind = kind},
                   LinePulseSegment{}},
  };
}

LinePulsePlan TwoPulses(SyncPulseKind first_kind, SyncPulseKind second_kind,
                        int half_line_samples) {
  return LinePulsePlan{
      .segment_count = 2,
      .segments = {LinePulseSegment{.offset_samples = 0, .kind = first_kind},
                   LinePulseSegment{.offset_samples = half_line_samples,
                                    .kind = second_kind}},
  };
}

LinePulsePlan BuildLinePulseSchedule(const LineTimingPrimitive& line,
                                     Standard standard, int half_line_samples) {
  if (standard == Standard::kPal) {
    const int line_1based = line.line_number_1based;
    if (line_1based == 1 || line_1based == 2 || line_1based == 314 ||
        line_1based == 315) {
      return TwoPulses(SyncPulseKind::kVerticalSync,
                       SyncPulseKind::kVerticalSync, half_line_samples);
    }
    if (line_1based == 3) {
      return TwoPulses(SyncPulseKind::kVerticalSync, SyncPulseKind::kEqualizing,
                       half_line_samples);
    }
    if (line_1based == 313) {
      return TwoPulses(SyncPulseKind::kEqualizing, SyncPulseKind::kVerticalSync,
                       half_line_samples);
    }
    if (line_1based == 6 || line_1based == 318) {
      return OnePulse(SyncPulseKind::kEqualizing);
    }
    if (line_1based == 4 || line_1based == 5 || line_1based == 311 ||
        line_1based == 312 || line_1based == 316 || line_1based == 317 ||
        line_1based == 623 || line_1based == 624 || line_1based == 625) {
      return TwoPulses(SyncPulseKind::kEqualizing, SyncPulseKind::kEqualizing,
                       half_line_samples);
    }
  }

  if ((standard == Standard::kNtsc || standard == Standard::kPalM) &&
      line.line_number_1based == 263) {
    // SMPTE 170M-2004 / ITU-R BT.470-6 System M: line 263 is the field-2
    // transition half-line in System M (NTSC and PAL-M share this structure).
    return TwoPulses(SyncPulseKind::kHorizontal, SyncPulseKind::kEqualizing,
                     half_line_samples);
  }

  if (!line.has_two_half_line_pulses) {
    return OnePulse(line.sync_pulse_kind);
  }

  if (standard == Standard::kNtsc || standard == Standard::kPalM) {
    // Align the line-granular model with the SMPTE 170M field-2 transition
    // shape seen in 4fsc reference material, where mixed half-line pulse kinds
    // occur around frame lines 263/266/269/272. PAL-M uses the same System M
    // sync structure as M/NTSC.
    if (line.line_number_1based == 266) {
      return TwoPulses(SyncPulseKind::kEqualizing, SyncPulseKind::kVerticalSync,
                       half_line_samples);
    }
    if (line.line_number_1based == 269) {
      return TwoPulses(SyncPulseKind::kVerticalSync, SyncPulseKind::kEqualizing,
                       half_line_samples);
    }
    if (line.line_number_1based == 272) {
      return OnePulse(SyncPulseKind::kEqualizing);
    }
  }

  return TwoPulses(line.sync_pulse_kind, line.sync_pulse_kind,
                   half_line_samples);
}

// Where one scheduled sync pulse starts and ends within its line.
//
// Shared by frame synthesis and the pilot burst table builder so the two can
// never disagree about where a pulse sits.
struct PulseSegmentBounds {
  int start_in_line = 0;
  int end_in_line = 0;
};

PulseSegmentBounds ResolvePulseSegmentBounds(const LinePulseSegment& segment,
                                             int nominal_width_samples,
                                             int line_samples) {
  const int start = std::min(segment.offset_samples, line_samples - 1);
  return PulseSegmentBounds{
      .start_in_line = start,
      .end_in_line = std::min(start + nominal_width_samples, line_samples),
  };
}

// Resolves the sync pulse schedule of every frame line once per run.
std::vector<LinePulsePlan> BuildLinePulsePlans(
    Standard standard, const std::vector<LineTimingPrimitive>& frame_lines,
    const std::vector<int>& line_sample_counts) {
  std::vector<LinePulsePlan> plans;
  plans.reserve(frame_lines.size());
  for (const LineTimingPrimitive& line : frame_lines) {
    const auto line_index =
        static_cast<std::size_t>(line.line_number_1based - 1);
    const int line_samples = (line_index < line_sample_counts.size())
                                 ? line_sample_counts[line_index]
                                 : 0;
    plans.push_back(
        BuildLinePulseSchedule(line, standard, (line_samples + 1) / 2));
  }
  return plans;
}

// Renders every LaserDisc pilot burst of a frame once per run.
//
// IEC 60856 §9.1.2 fig 6: a triangle burst rides on each sync pulse, capped at
// q = 13.5 periods for line and field sync and r = 6 periods for equalizing
// pulses; the remainder of a broad field-sync pulse stays at sync tip.
//
// The pilot is period-1 in frames (17,734,475 = 25 × 709,379), so each burst
// depends only on its offset within the frame. Deriving the phase from that
// offset rather than from the absolute sample index makes every frame's pilot
// identical by construction and keeps the phase bounded, where the absolute
// index both drifted and — held in an int — overflowed past ~3,000 frames.
std::vector<generation_detail::PilotBurstLine> BuildPilotBurstLines(
    const SampledSynthesisContext& synth,
    const std::array<SyncPulseWaveform, generation_detail::kSyncPulseKindCount>&
        pulse_waveforms,
    double pilot_omega, int pilot_q_samples, int pilot_r_samples) {
  std::vector<generation_detail::PilotBurstLine> pilot_lines(
      synth.line_sample_counts.size());

  for (const LineTimingPrimitive& line : synth.frame_lines) {
    const auto line_index =
        static_cast<std::size_t>(line.line_number_1based - 1);
    if (line_index >= pilot_lines.size()) {
      continue;
    }

    const int line_samples = synth.line_sample_counts[line_index];
    const int line_offset = synth.line_sample_offsets[line_index];
    const LinePulsePlan& plan = synth.line_pulse_plans[line_index];
    generation_detail::PilotBurstLine& pilot_line = pilot_lines[line_index];

    for (int segment_index = 0; segment_index < plan.segment_count;
         ++segment_index) {
      const LinePulseSegment& segment =
          plan.segments[static_cast<std::size_t>(segment_index)];
      const SyncPulseWaveform& waveform =
          pulse_waveforms[static_cast<std::size_t>(segment.kind)];
      const PulseSegmentBounds bounds = ResolvePulseSegmentBounds(
          segment, waveform.width_samples, line_samples);

      const int max_burst_samples = (segment.kind == SyncPulseKind::kEqualizing)
                                        ? pilot_r_samples
                                        : pilot_q_samples;
      const int flat_start = bounds.start_in_line + synth.sync_rise_samples;
      const int flat_end =
          std::min(bounds.end_in_line - synth.sync_rise_samples,
                   flat_start + max_burst_samples);
      if (flat_start >= flat_end) {
        continue;
      }

      generation_detail::PilotBurstSegment& pilot_segment =
          pilot_line
              .segments[static_cast<std::size_t>(pilot_line.segment_count)];
      pilot_segment.offset_in_line = flat_start;
      pilot_segment.samples.clear();
      pilot_segment.samples.reserve(
          static_cast<std::size_t>(flat_end - flat_start));

      double phase = WrapPhaseRad(
          pilot_omega * static_cast<double>(line_offset + flat_start));
      for (int i = flat_start; i < flat_end; ++i) {
        const double phase_frac = phase / kTwoPi;
        const double triangle = (phase_frac < 0.5) ? (4.0 * phase_frac - 1.0)
                                                   : (3.0 - 4.0 * phase_frac);
        pilot_segment.samples.push_back(
            MillivoltsToSampleFixed(kPilotBurstAmplitudeMv * triangle));

        // One subtraction suffices: the pilot advances well under a full cycle
        // per sample (3.75 MHz against a 17.734475 MHz sample rate).
        phase += pilot_omega;
        if (phase >= kTwoPi) {
          phase -= kTwoPi;
        }
      }

      ++pilot_line.segment_count;
    }
  }

  return pilot_lines;
}

double SyncEdgeRiseTimeSeconds(Standard standard) {
  if (standard == Standard::kNtsc || standard == Standard::kPalM) {
    // SMPTE 170M-2004 Table 2 plus Note 1: sync pulse rise/fall 140 ns ± 20 ns
    // for System M. PAL-M uses System M sync structure.
    return 140.0e-9;
  }
  // ITU-R BT.1700 Annex 1 Part B Table 2 item f and Table 3 item s: 625 PAL
  // sync/equalizing edge rise/fall 200 ns ± 100 ns measured 10%-90%.
  return 200.0e-9;
}

double BurstEnvelopeRiseTimeSeconds(Standard standard) {
  if (standard == Standard::kNtsc || standard == Standard::kPalM) {
    // SMPTE 170M-2004 Table 2: burst envelope rise 300 ns (+200/-100) 10%-90%
    // for System M. PAL-M uses the same M-system burst envelope shape.
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

bool IsPalBurstBlankedLine(int line_1based, int colour_frame_parity) {
  // ITU-R BT.1700 Annex 1 Part B Figure 8: the four 9-line burst-blanking
  // windows are defined in frame-line numbers and span frame boundaries:
  //   I:   lines 623-006    III: lines 622-005
  //   II:  lines 310-318    IV:  lines 311-319
  // An even-parity colour frame carries fields I/II, so it takes window I's
  // head (lines 1-6), window II (310-318), and window III's start (622-625,
  // preceding the next frame's field III). An odd-parity frame carries fields
  // III/IV and takes lines 1-5, 311-319, and window I's start (623-625).
  if (colour_frame_parity == 0) {
    return line_1based <= 6 || (line_1based >= 310 && line_1based <= 318) ||
           line_1based >= 622;
  }
  return line_1based <= 5 || (line_1based >= 311 && line_1based <= 319) ||
         line_1based >= 623;
}

bool IsPalMBurstBlankedLine(int line_1based, int colour_frame_parity) {
  // ITU-R BT.1700 Annex 1 Part B Figure 9: the four 11-line burst-blanking
  // windows for 525-line M/PAL, defined in frame-line numbers and spanning
  // frame boundaries:
  //   I:   lines 523-008    III: lines 522-007
  //   II:  lines 260-270    IV:  lines 259-269
  // An even-parity colour frame carries fields I/II, so it takes window I's
  // head (lines 1-8), window II (260-270), and window III's start (522-525,
  // preceding the next frame's field III). An odd-parity frame carries fields
  // III/IV and takes lines 1-7, 259-269, and window I's start (523-525).
  if (colour_frame_parity == 0) {
    return line_1based <= 8 || (line_1based >= 260 && line_1based <= 270) ||
           line_1based >= 522;
  }
  return line_1based <= 7 || (line_1based >= 259 && line_1based <= 269) ||
         line_1based >= 523;
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
  const int colour_frame_parity = static_cast<int>(frame_index % 2U);
  return !IsPalBurstBlankedLine(line.line_number_1based, colour_frame_parity);
}

bool PalMBurstEnabledForLine(std::size_t frame_index,
                             const LineTimingPrimitive& line) {
  if (line.sync_pulse_kind != SyncPulseKind::kHorizontal) {
    return false;
  }
  const int colour_frame_parity = static_cast<int>(frame_index % 2U);
  return !IsPalMBurstBlankedLine(line.line_number_1based, colour_frame_parity);
}

bool PalInvertVAxisForLine(std::size_t frame_index,
                           const LineTimingPrimitive& line) {
  return PalBurstPhaseRadForLine(frame_index, line) < 0.0;
}

using RenderedVitsLineMap =
    std::unordered_map<int, std::shared_ptr<const VitsRenderedLine>>;

// Builds the project-wide VITS render state: every targeted frame line is
// mapped to its rendered sample buffer. VITS are a project-level setting, so
// the same set is applied to every frame regardless of section. A rendered
// line is a pure function of (vits_type, line_samples, standard), so lines
// sharing both share a single rendering and the whole map is frame-invariant.
bool BuildRenderedVitsLines(
    const std::vector<VitsInjection>& vits_set, Standard standard,
    double sample_rate_hz, const std::vector<int>& line_sample_counts,
    const IVitsDefinitionProvider& vits_definition_provider,
    const IVitsGenerator& vits_generator, RenderedVitsLineMap* out_lines,
    std::string* error) {
  if (out_lines == nullptr) {
    if (error != nullptr) {
      *error = "Internal generation error: null VITS state output.";
    }
    return false;
  }

  out_lines->clear();

  std::unordered_map<std::string, VitsSynthesisPlan> plans;
  // Keyed by "<vits_type>@<line_samples>" so a rendering is reused by every
  // targeted line of the same length.
  std::unordered_map<std::string, std::shared_ptr<const VitsRenderedLine>>
      renderings;

  for (const VitsInjection& injection : vits_set) {
    VitsSynthesisPlan* cached_plan = nullptr;
    const auto existing_plan = plans.find(injection.vits_type);
    if (existing_plan == plans.end()) {
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

      cached_plan =
          &plans.emplace(injection.vits_type, std::move(plan)).first->second;
    } else {
      cached_plan = &existing_plan->second;
    }

    for (int target_line : injection.target_lines) {
      const auto line_index = static_cast<std::size_t>(target_line - 1);
      if (target_line < 1 || line_index >= line_sample_counts.size()) {
        continue;
      }

      const int line_samples = line_sample_counts[line_index];
      const std::string rendering_key =
          injection.vits_type + "@" + std::to_string(line_samples);
      auto rendering = renderings.find(rendering_key);
      if (rendering == renderings.end()) {
        auto rendered_line = std::make_shared<VitsRenderedLine>();
        std::string render_error;
        if (!vits_generator.RenderLine(*cached_plan, sample_rate_hz,
                                       line_samples, rendered_line.get(),
                                       &render_error)) {
          if (error != nullptr) {
            *error = render_error.empty()
                         ? "Failed to render VITS line injection."
                         : render_error;
          }
          return false;
        }
        rendering =
            renderings.emplace(rendering_key, std::move(rendered_line)).first;
      }

      (*out_lines)[target_line] = rendering->second;
    }
  }

  return true;
}

}  // namespace

// Frame-invariant synthesis resources for a single worker thread.
//
// Everything here is a pure function of the project's CVBS presets and VITS
// set: the sampled timing context, the chroma encoder (which owns mutable
// per-line workspaces and therefore cannot be shared between threads), the
// rendered VITS lines, and the VBI waveform renderer. A worker builds the set
// once and reuses it for every frame it synthesises.
struct GenerationStage::SynthesisResources {
  // Configuration the resources were built from; used to detect a project
  // change and rebuild.
  CvbsPresets presets;
  std::vector<VitsInjection> vits;

  TimingConstants timing;
  SignalLevels levels;
  generation_detail::SampledSynthesisContext synth;

  int active_window_start = 0;
  int active_window_end = 0;
  int active_window_samples = 0;
  SampleFixed blanking_fixed = 0;
  double burst_amplitude_mv = 0.0;

  // Luma code -> fixed-point millivolts, indexed by 10-bit source code.
  std::vector<SampleFixed> luma_code_table;

  // Active-sample to source-column mapping for the source most recently
  // synthesised by this worker. Held here rather than per call because the pool
  // invokes GenerateFrameBatch once per frame, so a per-call table would be
  // rebuilt every frame instead of only when a section change brings in a
  // source with a different active raster.
  generation_detail::ActiveSampleColumnMap column_map;

  // Shaped sync pulses, indexed by SyncPulseKind, and the burst gate envelope
  // pre-scaled by the burst amplitude. Both are pure functions of the standard
  // and the signal levels.
  std::array<generation_detail::SyncPulseWaveform,
             generation_detail::kSyncPulseKindCount>
      pulse_waveforms;
  int burst_width_samples = 0;
  std::vector<double> burst_envelope_mv;

  bool pal_pilot_burst = false;
  double pilot_omega = 0.0;
  int pilot_q_samples = 0;
  int pilot_r_samples = 0;
  // Pre-rendered pilot bursts per frame line; empty unless the pilot is on.
  std::vector<generation_detail::PilotBurstLine> pilot_burst_lines;

  std::unique_ptr<IChromaEncoder> chroma_encoder;
  RenderedVitsLineMap vits_lines;
  std::unique_ptr<VbiWaveformRenderer> vbi_renderer;
};

// Per-worker cache of the frame-invariant synthesis resources.
//
// Thread-safety: Acquire() may be called concurrently. Each calling thread
// receives its own SynthesisResources instance, so only the lookup table is
// mutex-guarded; the returned resources are never touched by another thread
// and entries are never erased, so the returned pointer stays valid for the
// lifetime of the cache.
class GenerationStage::SynthesisResourceCache {
 public:
  // Returns the calling thread's resources, building them on first use and
  // rebuilding them if the project's synthesis configuration has changed.
  // Returns nullptr and appends to errors when the VITS set cannot be built.
  SynthesisResources* Acquire(
      const Project& project,
      const IVitsDefinitionProvider& vits_definition_provider,
      const IVitsGenerator& vits_generator, std::vector<std::string>* errors);

 private:
  static bool Build(const Project& project,
                    const IVitsDefinitionProvider& vits_definition_provider,
                    const IVitsGenerator& vits_generator,
                    SynthesisResources* out, std::vector<std::string>* errors);

  std::mutex mutex_;
  std::unordered_map<std::thread::id, std::unique_ptr<SynthesisResources>>
      per_thread_;
};

GenerationStage::SynthesisResources*
GenerationStage::SynthesisResourceCache::Acquire(
    const Project& project,
    const IVitsDefinitionProvider& vits_definition_provider,
    const IVitsGenerator& vits_generator, std::vector<std::string>* errors) {
  SynthesisResources* resources = nullptr;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::unique_ptr<SynthesisResources>& slot =
        per_thread_[std::this_thread::get_id()];
    if (slot == nullptr) {
      slot = std::make_unique<SynthesisResources>();
    }
    resources = slot.get();
  }

  if (resources->chroma_encoder != nullptr &&
      resources->presets == project.cvbs_presets &&
      resources->vits == project.line_injections.vits) {
    return resources;
  }

  if (!Build(project, vits_definition_provider, vits_generator, resources,
             errors)) {
    return nullptr;
  }
  return resources;
}

bool GenerationStage::SynthesisResourceCache::Build(
    const Project& project,
    const IVitsDefinitionProvider& vits_definition_provider,
    const IVitsGenerator& vits_generator, SynthesisResources* out,
    std::vector<std::string>* errors) {
  const Standard standard = project.cvbs_presets.video_standard_preset;

  // Leave the cache invalid until the build completes, so a failed build is
  // never mistaken for a usable set on the next call.
  out->chroma_encoder.reset();
  out->timing = GetTimingConstants(standard);
  out->levels = GetSignalLevels(project.cvbs_presets);
  out->synth = BuildSampledSynthesisContext(standard);

  out->active_window_start =
      std::max(0, std::min(out->synth.active.active_window_start_samples,
                           out->synth.max_line_samples - 1));
  out->active_window_end =
      std::max(out->active_window_start + 1,
               std::min(out->synth.active.active_window_end_samples,
                        out->synth.max_line_samples));
  out->active_window_samples =
      out->active_window_end - out->active_window_start;
  out->blanking_fixed = MillivoltsToSampleFixed(out->levels.blanking_mv);
  out->luma_code_table = BuildLumaCodeTable(out->levels);

  // SMPTE 170M-2004 Table 1: NTSC burst amplitude = 40 IRE p-p = 20 IRE peak.
  // ITU-R BT.1700 Part B Table 2 item 5: PAL burst amplitude = 300 mV p-p.
  // ITU-R BT.470-6 Table 2 item 2.15: M/PAL burst = 3/7 × (white−blanking).
  //   PAL: 3/7 × 700 mV = 300 mV p-p → 150.0 mV peak.
  //   M/PAL: 3/7 × 714.3 mV = 306.1 mV p-p → 153.05 mV peak.
  out->burst_amplitude_mv = 150.0;
  if (standard == Standard::kNtsc) {
    out->burst_amplitude_mv = 20.0 * 1000.0 / 140.0;
  } else if (standard == Standard::kPalM) {
    out->burst_amplitude_mv = (3.0 / 7.0) * 714.3 / 2.0;
  }

  // Sync pulses are pure functions of (standard, pulse kind, signal levels), so
  // each shape is rendered once here and copied into place per line instead of
  // being evaluated sample by sample (~52 k S-curve evaluations per frame).
  for (int kind_index = 0; kind_index < generation_detail::kSyncPulseKindCount;
       ++kind_index) {
    const auto kind = static_cast<SyncPulseKind>(kind_index);
    const int width =
        PulseWidthSamples(kind, standard, out->timing.sample_rate_4fsc_hz);
    generation_detail::SyncPulseWaveform& waveform =
        out->pulse_waveforms[static_cast<std::size_t>(kind_index)];
    waveform.width_samples = width;
    waveform.levels.assign(static_cast<std::size_t>(width), 0);
    for (int index = 0; index < width; ++index) {
      waveform.levels[static_cast<std::size_t>(index)] =
          MillivoltsToSampleFixed(ShapedPulseLevel(
              index, width, out->synth.sync_rise_samples,
              out->levels.blanking_mv, out->levels.sync_tip_mv));
    }
  }

  // The burst gate envelope is likewise frame-invariant; folding the burst
  // amplitude into the table keeps the multiply order — and so the rounding —
  // identical to evaluating the envelope inline.
  out->burst_width_samples = std::max(
      0, out->synth.burst_end_samples - out->synth.burst_start_samples);
  out->burst_envelope_mv.assign(
      static_cast<std::size_t>(out->burst_width_samples), 0.0);
  for (int index = 0; index < out->burst_width_samples; ++index) {
    out->burst_envelope_mv[static_cast<std::size_t>(index)] =
        out->burst_amplitude_mv *
        ShapedGateEnvelope(index, out->burst_width_samples,
                           out->synth.burst_rise_samples);
  }

  out->pal_pilot_burst = project.cvbs_presets.pal_laserdisc_pilot_burst &&
                         standard == Standard::kPal;
  out->pilot_omega =
      out->pal_pilot_burst
          ? (2.0 * kPi * kPilotBurstFreqHz / out->timing.sample_rate_4fsc_hz)
          : 0.0;
  // IEC 60856 §9.1.2 fig 6: q = 13.5 periods for line/field sync pulses,
  // r = 6 periods for equalizing pulses. Broad field-sync pulses use q here
  // (not the optional 100-period extension); the rest stays at sync tip.
  out->pilot_q_samples =
      out->pal_pilot_burst
          ? static_cast<int>(std::lround(
                13.5 * out->timing.sample_rate_4fsc_hz / kPilotBurstFreqHz))
          : 0;
  out->pilot_r_samples =
      out->pal_pilot_burst
          ? static_cast<int>(std::lround(6.0 * out->timing.sample_rate_4fsc_hz /
                                         kPilotBurstFreqHz))
          : 0;
  out->pilot_burst_lines =
      out->pal_pilot_burst
          ? BuildPilotBurstLines(out->synth, out->pulse_waveforms,
                                 out->pilot_omega, out->pilot_q_samples,
                                 out->pilot_r_samples)
          : std::vector<generation_detail::PilotBurstLine>{};

  std::string vits_error;
  if (!BuildRenderedVitsLines(project.line_injections.vits, standard,
                              out->timing.sample_rate_4fsc_hz,
                              out->synth.line_sample_counts,
                              vits_definition_provider, vits_generator,
                              &out->vits_lines, &vits_error)) {
    errors->push_back(vits_error.empty()
                          ? "Unable to prepare section VITS state."
                          : vits_error);
    return false;
  }

  out->vbi_renderer = std::make_unique<VbiWaveformRenderer>(
      standard, out->timing.sample_rate_4fsc_hz);

  std::unique_ptr<IChromaEncoder> chroma_encoder =
      CreateChromaEncoder(standard, out->timing.sample_rate_4fsc_hz);
  if (chroma_encoder == nullptr) {
    errors->push_back("Unsupported video standard for chroma encoding.");
    return false;
  }

  out->presets = project.cvbs_presets;
  out->vits = project.line_injections.vits;
  out->chroma_encoder = std::move(chroma_encoder);
  return true;
}

GenerationStage::GenerationStage(
    ILogger* logger, const IVitsDefinitionProvider* vits_definition_provider,
    const IVitsGenerator* vits_generator)
    : logger_(logger),
      resource_cache_(std::make_unique<SynthesisResourceCache>()),
      vits_definition_provider_(vits_definition_provider != nullptr
                                    ? vits_definition_provider
                                    : &default_vits_definition_provider_),
      vits_generator_(vits_generator != nullptr ? vits_generator
                                                : &default_vits_generator_) {}

GenerationStage::~GenerationStage() = default;

bool GenerationStage::BuildFrameSchedule(
    const Project& project, std::vector<FrameScheduleItem>* out_schedule,
    std::vector<std::string>* errors) {
  if (out_schedule == nullptr || errors == nullptr) {
    return false;
  }

  out_schedule->clear();
  progressive_source_.ClearCache();
  biphase_manager_.Reset();
  biphase_manager_.SetProjectDiscType(
      DiscTypeFromString(project.line_injections.disc_type));

  // Compute disc_frame_offset = (first_PN - 1) from the first CAV
  // picture_number code. This offset is added to global_frame_index when
  // computing the colour-subcarrier phase so that sources not starting at PN 1
  // still produce disc-accurate colour sequences ((PN-1) % colour_period).
  std::size_t initial_frame_offset = 0;
  for (const Section& sec : project.sections) {
    bool found = false;
    for (const Section::LineInjection& inj : sec.line_injections) {
      if (Lowercase(inj.type) != "laserdisc") {
        continue;
      }
      for (const Section::LineInjectionCode& code : inj.codes) {
        if (code.code_type == "picture_number" && code.start_value_specified &&
            code.start_value > 1) {
          initial_frame_offset = static_cast<std::size_t>(code.start_value - 1);
          found = true;
          break;
        }
      }
      if (found) break;
    }
    if (found) break;
  }
  biphase_manager_.SetInitialFrameCount(static_cast<int>(initial_frame_offset));

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

  // Build the output schedule, computing the per-frame disc picture number so
  // that colour-subcarrier phase is derived from disc position (PN - 1) rather
  // than file position even when sections have non-contiguous start_values
  // (backward-skip replay or post-gap forward-skip sections).
  out_schedule->reserve(frame_sections.size());
  const Section* prev_schedule_section = nullptr;
  int section_pn_start = 0;
  int section_frame_offset = 0;
  for (const std::pair<const Section*, int>& frame_section : frame_sections) {
    const Section* sec = frame_section.first;
    if (sec != prev_schedule_section) {
      section_pn_start = FindSectionPictureNumberStart(*sec);
      section_frame_offset = 0;
      prev_schedule_section = sec;
    }
    const int disc_pn =
        (section_pn_start > 0) ? (section_pn_start + section_frame_offset) : 0;
    out_schedule->push_back(FrameScheduleItem{
        .section = sec,
        .source_frame_index = frame_section.second,
        .disc_picture_number = disc_pn,
    });
    ++section_frame_offset;
  }

  // Sequential enrichment pass: resolve the per-frame VBI payload, colour
  // context, and OSD token strings once, in schedule order, by advancing the
  // biphase generators over the whole schedule. With the payload attached,
  // GenerateFrameBatch needs no mutable cross-frame state and frames can be
  // synthesised out of order.
  const Standard standard = project.cvbs_presets.video_standard_preset;
  const SampledSynthesisContext synth = BuildSampledSynthesisContext(standard);
  const int active_window_start =
      std::max(0, std::min(synth.active.active_window_start_samples,
                           synth.max_line_samples - 1));
  // The {timecode} OSD token is a continuous CLV programme timecode; it is only
  // meaningful on CLV discs, where it runs from the start of the output.
  const bool disc_is_clv =
      DiscTypeFromString(project.line_injections.disc_type) == DiscType::kCLV;
  for (std::size_t frame_index = 0; frame_index < out_schedule->size();
       ++frame_index) {
    FrameScheduleItem& item = (*out_schedule)[frame_index];
    auto enrichment = std::make_shared<FrameEnrichment>();

    // disc_frame_index represents (PN - 1) for the frame, anchoring
    // colour-subcarrier and pilot-burst phase to the disc picture number.
    enrichment->disc_frame_index =
        (item.disc_picture_number > 0)
            ? static_cast<std::size_t>(item.disc_picture_number - 1)
            : (frame_index + initial_frame_offset);

    if (!biphase_manager_.ResolveFrame(
            *item.section, standard, synth.sample_rate_hz, active_window_start,
            enrichment.get(), errors)) {
      return false;
    }

    // The {frame_number} OSD token is the 1-based position of this frame in the
    // whole generated output; it is independent of biphase codes and cannot be
    // re-anchored by a section, so it is derived from the schedule position
    // here rather than in the biphase manager.
    enrichment->context.frame_number = static_cast<int>(frame_index) + 1;

    // The {timecode} OSD token is a continuous CLV programme timecode driven by
    // the 0-based output frame position, so it advances on every frame of a CLV
    // disc regardless of which VBI codes (if any) a section injects. It renders
    // a placeholder on non-CLV discs where has_clv_timecode stays false.
    if (disc_is_clv) {
      const ClvTimecode tc = ClvTimecodeForFrame(frame_index, standard);
      enrichment->context.has_clv_timecode = true;
      enrichment->context.clv_hours = tc.hours;
      enrichment->context.clv_minutes = tc.minutes;
      enrichment->context.clv_seconds = tc.seconds;
      enrichment->context.clv_frames = tc.frames;
    }

    enrichment->osd_texts.reserve(item.section->osd.overlays.size());
    for (const OsdOverlay& overlay : item.section->osd.overlays) {
      enrichment->osd_texts.push_back(osd_token_resolver_.Resolve(
          overlay.text, enrichment->context, item.section->name));
    }

    item.enrichment = std::move(enrichment);
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

  // Frame-invariant setup (sampled timing context, chroma encoder, rendered
  // VITS lines, VBI encoders) is built once per worker thread and reused for
  // every frame that worker synthesises.
  // Non-const: the resources are this worker's alone, and the source-column
  // map is updated in place as sections change.
  SynthesisResources* resources = resource_cache_->Acquire(
      project, *vits_definition_provider_, *vits_generator_, errors);
  if (resources == nullptr) {
    return false;
  }

  const TimingConstants& timing = resources->timing;
  const SignalLevels& levels = resources->levels;
  const generation_detail::SampledSynthesisContext& synth = resources->synth;
  const int frame_samples = synth.frame_samples;
  const Standard video_standard = project.cvbs_presets.video_standard_preset;
  const double burst_amplitude_mv = resources->burst_amplitude_mv;
  const IChromaEncoder* chroma_encoder = resources->chroma_encoder.get();

  const std::size_t sample_count =
      frame_count * static_cast<std::size_t>(frame_samples);

  const int active_window_start = resources->active_window_start;
  const int active_window_end = resources->active_window_end;
  const int active_window_samples = resources->active_window_samples;

  const bool pal_pilot_burst = resources->pal_pilot_burst;

  const SampleFixed blanking_fixed = resources->blanking_fixed;
  const SampleFixed* const luma_code_table = resources->luma_code_table.data();

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

  // Active luma comes from the precomputed code table. Sources clamp their
  // samples into the 10-bit code space, so the guard only covers hand-built
  // images and costs a predictable compare rather than a divide and an llround.
  auto LumaFixedFromCode = [&](int y_code) -> SampleFixed {
    if (y_code >= 0 && y_code < kLumaCodeTableSize) {
      return luma_code_table[y_code];
    }
    return MillivoltsToSampleFixed(LumaMillivoltsFromCode(y_code, levels));
  };

  std::vector<YCbCr444Pixel> line_source_samples(
      static_cast<std::size_t>(active_window_samples), YCbCr444Pixel{});
  std::vector<int> active_sample_indices(
      static_cast<std::size_t>(active_window_samples), 0);
  std::vector<SampleFixed> encoded_line_chroma(
      static_cast<std::size_t>(active_window_samples),
      MillivoltsToSampleFixed(0.0));

  // Active-sample to source-column mapping, held by this worker's resources so
  // it survives across calls and is rebuilt only when a section change brings
  // in a source with a different active raster.
  ActiveSampleColumnMap& column_map = resources->column_map;

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

    // The enrichment payload carries the resolved VBI code words and OSD
    // token strings. Hand-built schedules without a payload are accepted only
    // for sections that need neither.
    const FrameEnrichment* enrichment = frame_item.enrichment.get();
    if (enrichment == nullptr) {
      bool has_laserdisc_injection = false;
      for (const Section::LineInjection& injection : section->line_injections) {
        if (Lowercase(injection.type) == "laserdisc") {
          has_laserdisc_injection = true;
          break;
        }
      }
      if (has_laserdisc_injection || !section->osd.overlays.empty()) {
        errors->push_back(
            "Frame schedule item is missing its enrichment payload; build "
            "the schedule with BuildFrameSchedule for sections using "
            "laserdisc line injections or OSD overlays.");
        return false;
      }
    }

    if (logger_ != nullptr && schedule.size() <= 120U) {
      logger_->Trace("Generating frame " +
                     std::to_string(global_frame_index + 1U) + " of " +
                     std::to_string(schedule.size()) + " from section '" +
                     section->name + "' (" + section->type + ").");
    }

    // Shared, immutable decoded source image: delivery is a reference count,
    // not a per-frame copy of the ~2.4 MiB raster.
    std::shared_ptr<const FrameSourceImage> source_frame_image;
    std::string frame_error;
    const bool generated = progressive_source_.GenerateFrame(
        *section, source_frame_index,
        project.cvbs_presets.video_standard_preset, &source_frame_image,
        &frame_error);

    if (!generated || source_frame_image == nullptr) {
      errors->push_back(frame_error.empty()
                            ? "Unable to generate frame-based source data."
                            : frame_error);
      return false;
    }
    const FrameSourceImage& source_frame = *source_frame_image;
    if (!column_map.MatchesSource(source_frame)) {
      column_map.Rebuild(source_frame, active_window_samples);
    }
    // Progressive imports use field-2-dominant row pairing; the section type
    // decides it once per frame rather than once per active sample.
    const bool progressive_section = section->type == "progressive";

    const int local_frame_base = static_cast<int>(
        local_frame_index * static_cast<std::size_t>(frame_samples));
    // disc_frame_index represents (PN - 1) for the current frame, anchoring
    // colour-subcarrier phase and pilot-burst phase to the disc picture
    // number. Resolved during the BuildFrameSchedule enrichment pass; for
    // hand-built schedules without a payload, fall back to the schedule
    // position (or the picture number when populated).
    const std::size_t disc_frame_index =
        (enrichment != nullptr) ? enrichment->disc_frame_index
                                : ((frame_item.disc_picture_number > 0)
                                       ? static_cast<std::size_t>(
                                             frame_item.disc_picture_number - 1)
                                       : global_frame_index);
    // Held as size_t: on a three-hour disc the absolute sample index reaches
    // ~1.9 × 10^11, far outside int.
    const std::size_t absolute_frame_base =
        disc_frame_index * static_cast<std::size_t>(frame_samples);

    for (const LineTimingPrimitive& line : synth.frame_lines) {
      const int line_index = line.line_number_1based - 1;
      const int local_line_base =
          local_frame_base +
          synth.line_sample_offsets[static_cast<std::size_t>(line_index)];
      const std::size_t absolute_line_base =
          absolute_frame_base +
          static_cast<std::size_t>(
              synth.line_sample_offsets[static_cast<std::size_t>(line_index)]);
      const int line_samples =
          synth.line_sample_counts[static_cast<std::size_t>(line_index)];
      const int local_line_end = local_line_base + line_samples;

      const generation_detail::LinePulsePlan& pulse_plan =
          synth.line_pulse_plans[static_cast<std::size_t>(line_index)];

      for (int segment_index = 0; segment_index < pulse_plan.segment_count;
           ++segment_index) {
        const LinePulseSegment& segment =
            pulse_plan.segments[static_cast<std::size_t>(segment_index)];
        const generation_detail::SyncPulseWaveform& pulse_waveform =
            resources->pulse_waveforms[static_cast<std::size_t>(segment.kind)];
        const int pulse_offset = segment.offset_samples;
        const int pulse_start =
            local_line_base + std::min(pulse_offset, line_samples - 1);
        const int pulse_end = std::min(
            pulse_start + pulse_waveform.width_samples, local_line_end);
        const int pulse_width_samples = pulse_end - pulse_start;

        if (pulse_width_samples == pulse_waveform.width_samples) {
          std::copy(pulse_waveform.levels.begin(), pulse_waveform.levels.end(),
                    out_y_mv->begin() + pulse_start);
        } else {
          // A pulse clipped by the end of its line has a different S-curve to
          // the nominal shape, so it is still shaped sample by sample.
          for (int i = pulse_start; i < pulse_end; ++i) {
            const int relative_index = i - pulse_start;
            SetYMillivolts(
                static_cast<std::size_t>(i),
                ShapedPulseLevel(relative_index, pulse_width_samples,
                                 synth.sync_rise_samples, levels.blanking_mv,
                                 levels.sync_tip_mv));
          }
        }
      }

      if (pal_pilot_burst) {
        // The pilot burst repeats on every frame, so each line's bursts are
        // added straight from the table rendered once per run.
        const generation_detail::PilotBurstLine& pilot_line =
            resources->pilot_burst_lines[static_cast<std::size_t>(line_index)];
        for (int segment_index = 0; segment_index < pilot_line.segment_count;
             ++segment_index) {
          const generation_detail::PilotBurstSegment& pilot_segment =
              pilot_line.segments[static_cast<std::size_t>(segment_index)];
          SampleFixed* const destination =
              out_y_mv->data() +
              static_cast<std::size_t>(local_line_base +
                                       pilot_segment.offset_in_line);
          for (std::size_t i = 0; i < pilot_segment.samples.size(); ++i) {
            destination[i] += pilot_segment.samples[i];
          }
        }
      }

      const bool is_pal = video_standard == Standard::kPal;
      const bool is_pal_m = video_standard == Standard::kPalM;
      bool burst_enabled = line.burst_enabled;
      double burst_phase_rad = line.burst_phase_rad;
      if (is_pal) {
        burst_enabled = PalBurstEnabledForLine(disc_frame_index, line);
        burst_phase_rad = PalBurstPhaseRadForLine(disc_frame_index, line);
      } else if (is_pal_m) {
        // ITU-R BT.470-6 Table 2 item 2.17: PAL-M burst blanking uses
        // System M line numbers (IsPalMBurstBlankedLine). Burst phase
        // follows the same ±135° alternating pattern as 625-line PAL.
        burst_enabled = PalMBurstEnabledForLine(disc_frame_index, line);
        burst_phase_rad = PalBurstPhaseRadForLine(disc_frame_index, line);
      }

      if (burst_enabled) {
        const int burst_sample_start =
            std::min(local_line_base + synth.burst_start_samples,
                     local_line_end > 0 ? local_line_end - 1 : local_line_base);
        const int burst_sample_end =
            std::min(local_line_base + synth.burst_end_samples, local_line_end);
        const int burst_width_samples = burst_sample_end - burst_sample_start;
        const std::size_t burst_sample_start_absolute =
            absolute_line_base +
            static_cast<std::size_t>(burst_sample_start - local_line_base);

        // ITU-R BT.1700 Annex 1 Part B Table 1 item 10f: PAL/PAL-M burst at
        // ±135° from EU axis, alternating per line per PalBurstPhaseRadForLine.
        // Item 10h: V-switching direction is carried in the EV' component of
        // the burst; this requires burst_phase_rad to remain ±135° so decoders
        // can extract the sign. NTSC burst_phase_rad is kept at π/4 (45°) per
        // the constant-reference convention in signal_timing_model.h.
        const double subcarrier_anchor_rad =
            is_pal ? kPalSubcarrierAnchorRad : 0.0;
        const double burst_start_phase_rad =
            WrapPhaseRad(SubcarrierPhaseRad(burst_sample_start_absolute) +
                         subcarrier_anchor_rad + burst_phase_rad);
        double burst_sin = std::sin(burst_start_phase_rad);
        double burst_cos = std::cos(burst_start_phase_rad);

        // The gate envelope is frame-invariant, so the pre-scaled table is read
        // directly whenever the burst has its nominal width; a burst clipped by
        // the end of its line keeps the sample-by-sample shaping.
        const bool burst_is_nominal_width =
            burst_width_samples == resources->burst_width_samples;

        for (int i = burst_sample_start; i < burst_sample_end; ++i) {
          const int relative_index = i - burst_sample_start;
          const double envelope_mv =
              burst_is_nominal_width
                  ? resources->burst_envelope_mv[static_cast<std::size_t>(
                        relative_index)]
                  : (burst_amplitude_mv *
                     ShapedGateEnvelope(relative_index, burst_width_samples,
                                        synth.burst_rise_samples));
          SetCMillivolts(static_cast<std::size_t>(i), envelope_mv * burst_sin);

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
          const bool invert_pal_v_axis =
              (is_pal || is_pal_m) &&
              PalInvertVAxisForLine(disc_frame_index, line);
          const std::size_t active_window_line_start_absolute =
              absolute_line_base +
              static_cast<std::size_t>(active_window_start);
          // SMPTE 170M-2004 Section 10: defines active chroma with burst+180
          // deg reference for NTSC. PAL and PAL-M use 0 phase offset; PAL
          // additionally carries the subcarrier anchor so picture chroma stays
          // phase-coherent with the anchored burst.
          const double phase_offset =
              (video_standard == Standard::kNtsc)
                  ? (line.burst_phase_rad + kPi)
                  : (is_pal ? kPalSubcarrierAnchorRad : 0.0);
          const double phase_start = WrapPhaseRad(
              SubcarrierPhaseRad(active_window_line_start_absolute) +
              phase_offset);

          // Map field lines onto progressive source rows by interleaving
          // fields. Progressive imports use field-2-dominant row pairing, so
          // field 1 consumes odd rows and field 2 consumes even rows. The row
          // is the same for every sample of the line.
          const int field_line = active_y % synth.active.active_lines_per_field;
          const int source_row =
              source_frame.active_y +
              ((line.field_index_1based == 1)
                   ? (2 * field_line + (progressive_section ? 1 : 0))
                   : (2 * field_line + (progressive_section ? 0 : 1)));

          // Samples that used to be skipped one at a time form a suffix of the
          // active window: the sample index leaves the line, or the mapped
          // source column leaves the raster, and neither condition can revert
          // as x_sample grows. Taking the smallest such bound as the loop limit
          // removes both per-sample checks while leaving the skipped tail with
          // exactly the placeholder values it had before.
          const int line_sample_limit = line_samples - active_window_start;
          const int painted_sample_count =
              (source_row < source_frame.height)
                  ? std::max(0,
                             std::min({active_window_samples, line_sample_limit,
                                       column_map.in_range_sample_count}))
                  : 0;

          const int active_line_sample_base =
              local_line_base + active_window_start;
          const YCbCr444Pixel* const source_row_pixels =
              (painted_sample_count > 0) ? &source_frame.PixelAt(0, source_row)
                                         : nullptr;

          for (int x_sample = 0; x_sample < painted_sample_count; ++x_sample) {
            const int sample_index = active_line_sample_base + x_sample;
            const std::size_t sample_slot = static_cast<std::size_t>(x_sample);
            const YCbCr444Pixel& pixel =
                source_row_pixels[column_map.pixel_x[sample_slot]];

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
            SampleFixed& luma_sample =
                (*out_y_mv)[static_cast<std::size_t>(sample_index)];
            if (luma_sample >= blanking_fixed) {
              luma_sample = LumaFixedFromCode(pixel.y);
            }
          }

          // Restore the placeholders for the tail the loop above did not paint,
          // so the chroma encoder sees neutral pixels there and the accumulate
          // step folds their output onto the line's first sample, exactly as
          // the per-sample skip used to.
          std::fill(line_source_samples.begin() + painted_sample_count,
                    line_source_samples.end(), YCbCr444Pixel{});
          std::fill(active_sample_indices.begin() + painted_sample_count,
                    active_sample_indices.end(), local_line_base);

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

      const auto vits_line =
          resources->vits_lines.find(line.line_number_1based);
      if (vits_line != resources->vits_lines.end()) {
        const VitsRenderedLine& rendered_line = *vits_line->second;
        for (int sample_offset = 0; sample_offset < line_samples;
             ++sample_offset) {
          const std::size_t frame_sample_index =
              static_cast<std::size_t>(local_line_base) +
              static_cast<std::size_t>(sample_offset);
          (*out_y_mv)[frame_sample_index] +=
              rendered_line
                  .y_samples_mv[static_cast<std::size_t>(sample_offset)];
          (*out_c_mv)[frame_sample_index] +=
              rendered_line
                  .c_samples_mv[static_cast<std::size_t>(sample_offset)];
        }
      }
    }

    if (enrichment != nullptr) {
      resources->vbi_renderer->Render(
          *enrichment, out_y_mv, local_frame_base, synth.line_sample_offsets,
          synth.line_sample_counts, active_window_end);
    }

    if (!section->osd.overlays.empty()) {
      // Non-null and correctly sized: enforced by the enrichment guard above
      // and the BuildFrameSchedule enrichment pass.
      const std::vector<std::string>& resolved_texts = enrichment->osd_texts;
      const SignalLevels osd_levels =
          GetSignalLevels(project.cvbs_presets.video_standard_preset);
      const int field1_start = synth.active.first_active_line_field1 - 1;
      const int field1_end = field1_start + synth.active.active_lines_per_field;
      osd_renderer_.Render(section->osd, resolved_texts, out_y_mv, out_c_mv,
                           local_frame_base, synth.line_sample_offsets,
                           field1_start, field1_end, active_window_start,
                           active_window_end, osd_levels);
      const int field2_start = synth.active.first_active_line_field2 - 1;
      const int field2_end = field2_start + synth.active.active_lines_per_field;
      osd_renderer_.Render(section->osd, resolved_texts, out_y_mv, out_c_mv,
                           local_frame_base, synth.line_sample_offsets,
                           field2_start, field2_end, active_window_start,
                           active_window_end, osd_levels);
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
