/*
 * File:        biphase_injection_manager.h
 * Module:      biphase_injection_manager
 * Purpose:     Orchestrates biphase and 40-bit FM code injection into VBI
 *              lines of the video signal for LaserDisc authoring per
 *              IEC 60856 (PAL) and IEC 60857 (NTSC).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "videosynth/biphase_encoder.h"
#include "videosynth/biphase_types.h"
#include "videosynth/code_generator.h"
#include "videosynth/fixed_point.h"
#include "videosynth/fm_code_generator.h"
#include "videosynth/fm_encoder.h"
#include "videosynth/line_placement_engine.h"
#include "videosynth/model.h"
#include "videosynth/signal_timing_model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

// Orchestrates biphase and 40-bit FM VBI injection for LaserDisc authoring.
//
// The manager maintains stateful code generators (picture_number,
// chapter_number, programme_time_code, etc.) across frames within a section.
// Section transitions are detected automatically by comparing section pointers;
// generators are re-created whenever the active section changes.
//
// Typical usage inside a frame generation loop:
//
//   BiphaseInjectionManager biphase_manager;
//   for (each frame) {
//     if (!biphase_manager.ProcessFrame(out_y_mv, local_frame_base,
//             line_offsets, line_counts, *section, standard, sample_rate,
//             frame_lines, active_window_start, errors)) {
//       return false;
//     }
//   }
//
// Call Reset() when starting a new generation pass (e.g. after
// BuildFrameSchedule) so that generators restart from their initial values.
//
// Signal level conventions:
//   PAL  24-bit biphase: baseline = 210 mV (30% white), peak = 700 mV
//   NTSC 24-bit biphase: baseline =   0 mV (0 IRE),    peak = 714.3 mV
//   NTSC 40-bit FM:      baseline =   0 mV,             peak = 714.3 mV
//   White flag:          100 IRE pulse, 135 ns rise/fall (IEC 60857 Figure 12)
//
// Horizontal start timing per IEC 60856 Figure 14 / IEC 60857 Figures 11–13:
//   24-bit biphase codes: signal starts at active_window_start_samples.
//   0.172 H offset codes (programme_status, NTSC clv_code):
//                        signal starts at round(0.172 × samples_per_line).
//   NTSC white flag (IEC 60857 Figure 12):
//                        pulse starts at round(0.160 × samples_per_line),
//                        length = round(0.790 × samples_per_line).
//   NTSC 40-bit FM codes (IEC 60857 Figure 13):
//                        signal starts at round(0.215 × samples_per_line).
//   All codes end at active_window_end_samples (exclusive).
//
// Thread-safety: NOT thread-safe. Maintains mutable generator state.
class BiphaseInjectionManager {
 public:
  BiphaseInjectionManager() = default;

  // Resets all generator state. Must be called before starting a new
  // generation pass so that stateful generators (picture_number, etc.)
  // restart from their configured initial values.
  void Reset();

  // Processes biphase VBI injection for one frame.
  //
  // Must be called for every frame in the schedule in order; the manager
  // advances stateful code generators at the end of each call. If the
  // section differs from the previous call, generators are re-created.
  //
  // Args:
  //   out_y_mv:                  Luma sample buffer for the current batch.
  //   frame_sample_base:         Byte offset of this frame's first sample
  //                              within out_y_mv.
  //   line_sample_offsets:       Per-line byte offsets relative to the
  //                              frame base (indexed by line_number - 1).
  //   line_sample_counts:        Samples per line (indexed by line_number - 1).
  //   section:                   The section being rendered for this frame.
  //   standard:                  PAL or NTSC.
  //   sample_rate_hz:            4fsc sample rate in Hz.
  //   frame_lines:               Timing primitives for every line in the frame.
  //   active_window_start_samples: Sample offset of the active-picture window
  //                              start within a line; used as the normal
  //                              biphase horizontal start position.
  //   active_window_end_samples: Sample offset (exclusive) of the
  //   active-picture
  //                              window end within a line; injection is clamped
  //                              to this boundary so signals do not bleed into
  //                              the front porch.
  //   errors:                    Output for error messages; non-null required.
  //
  // Returns true on success. Returns false and appends a message to errors
  // on any error.
  bool ProcessFrame(std::vector<SampleFixed>* out_y_mv, int frame_sample_base,
                    const std::vector<int>& line_sample_offsets,
                    const std::vector<int>& line_sample_counts,
                    const Section& section, Standard standard,
                    double sample_rate_hz,
                    const std::vector<LineTimingPrimitive>& frame_lines,
                    int active_window_start_samples,
                    int active_window_end_samples,
                    std::vector<std::string>* errors);

 private:
  // The section being processed; null = no section yet / reset.
  const Section* current_section_ = nullptr;

  // 24-bit biphase code generators, keyed by code_type string.
  std::unordered_map<std::string, std::unique_ptr<CodeGenerator>> generators_;

  // 40-bit FM generators (NTSC only).
  std::unique_ptr<FmPictureNumberGenerator> fm_picture_number_generator_;
  std::unique_ptr<FmProgrammeTimeGenerator> fm_programme_time_generator_;
  std::unique_ptr<WhiteFlagTracker> white_flag_tracker_;

  std::unique_ptr<LinePlacementEngine> placement_engine_;
  std::unique_ptr<BiphaseEncoder> biphase_encoder_;
  std::unique_ptr<FmEncoder> fm_encoder_;

  DiscType disc_type_ = DiscType::kUnknown;
  SectionType section_type_ = SectionType::kUnknown;
  bool has_laserdisc_ = false;

  // (Re)initialises all generators for a new section.
  bool InitializeSection(const Section& section, Standard standard,
                         double sample_rate_hz,
                         std::vector<std::string>* errors);

  // Writes a 24-bit biphase waveform into the Y buffer for one VBI line.
  // Overwrites samples from start_sample up to (not including) active_end with
  // the biphase signal (baseline for the post-signal tail, waveform at front).
  void InjectBiphaseCode(std::vector<SampleFixed>* out_y_mv, int line_base,
                         int active_end, const std::string& code_type,
                         Standard standard, const SignalLevels& levels,
                         int start_sample);

  // Writes a 40-bit FM waveform into the Y buffer for one VBI line (NTSC).
  // Writes from start_sample up to (not including) active_end.
  void InjectFmCode(std::vector<SampleFixed>* out_y_mv, int line_base,
                    int active_end, const std::string& code_type,
                    bool field_one, const SignalLevels& levels,
                    int start_sample);

  // Writes the white flag pulse into a VBI line (NTSC only).
  // Fills blanking from start_sample to active_end, then overlays a shaped
  // 100 IRE pulse of flag_length_samples with 135 ns rise/fall transitions
  // per IEC 60857 Figure 12.
  void InjectWhiteFlag(std::vector<SampleFixed>* out_y_mv, int line_base,
                       int active_end, const SignalLevels& levels,
                       int start_sample, int flag_length_samples);

  // Advances all stateful code generators by one frame.
  void AdvanceGenerators();

  // Returns a pointer to the generator for code_type, or nullptr if absent.
  CodeGenerator* GetGenerator(const std::string& code_type) const;
};

}  // namespace videosynth
