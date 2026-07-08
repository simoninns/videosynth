/*
 * File:        dropout_injection_stage.h
 * Module:      dropout_injection
 * Purpose:     Applies per-section random and scratch dropout events to
 *              fixed-point mV Y/C buffers before output quantisation, and
 *              writes a conformant dropout sidecar SQLite file.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/interfaces.h"
#include "videosynth/model.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

// One pre-computed scratch event bound to a section.
struct ScratchEvent {
  int anchor_line;
  int anchor_offset;
  int duration_frames;
  int peak_width;
  int direction;  // -1 (down) or +1 (up)
  double max_push_fraction;
};

// Applies random and scratch dropout events to the Y/C mV-domain sample
// buffers for sections that have dropout injection enabled, and writes dropout
// annotation rows to a conformant SQLite sidecar file
// (<basename>.dropouts.meta, schema version 5).
//
// Placement: called after NoiseInjectionStage and before IOutputStage::
// AppendSamples in VideoSynthPipeline::Run.
//
// Seeding: mirrors the scheme used by NoiseInjectionStage. When a user-
// supplied seed is present it is used; otherwise run_base_seed_ (captured
// from std::random_device at construction) provides per-run randomness that
// is fixed within a single pipeline invocation.
//
// Sidecar lifecycle: Begin() opens (or creates) the sidecar; InjectDropouts()
// appends rows inside a single open transaction; Finalize() commits and closes.
// If no section has dropout injection enabled, Begin() becomes a no-op and
// Finalize() does nothing.
//
// Thread-safety: DropoutInjectionStage is NOT thread-safe. InjectDropouts must
// not be called concurrently from multiple threads.
class DropoutInjectionStage {
 public:
  explicit DropoutInjectionStage(ILogger* logger);
  ~DropoutInjectionStage();

  // Disallow copy and move — owns a raw SQLite handle.
  DropoutInjectionStage(const DropoutInjectionStage&) = delete;
  DropoutInjectionStage& operator=(const DropoutInjectionStage&) = delete;
  DropoutInjectionStage(DropoutInjectionStage&&) = delete;
  DropoutInjectionStage& operator=(DropoutInjectionStage&&) = delete;

  // Opens the sidecar SQLite file when at least one section has dropout
  // injection enabled. Must be called before the first InjectDropouts call.
  // Returns false and populates errors on failure.
  bool Begin(const Project& project, std::vector<std::string>* errors);

  // Applies random and scratch dropout events to y_mv/c_mv in-place for each
  // frame in the batch, and appends dropout_run rows to the sidecar.
  // Args match the contract used by NoiseInjectionStage::InjectNoise.
  void InjectDropouts(
      const Project& project,
      const std::vector<IGenerationStage::FrameScheduleItem>& schedule,
      std::size_t frame_offset, std::size_t frame_count,
      std::vector<SampleFixed>* y_mv, std::vector<SampleFixed>* c_mv);

  // Commits the sidecar transaction and closes the database handle.
  // No-op if Begin was not called or no rows were written.
  // Returns false and populates errors on SQLite failure.
  bool Finalize(std::vector<std::string>* errors);

  // Abandons an in-progress sidecar session: discards the open transaction,
  // closes the database handle, and removes the partially-written sidecar
  // file. No-op if Begin was not called or the sidecar was not created.
  void Abort();

 private:
  // Returns the index of section within project.sections, or 0.
  static std::size_t FindSectionIndex(const Project& project,
                                      const Section* section);

  // Derives the sidecar file path from the project metadata path.
  static std::string DeriveSidecarPath(const std::string& metadata_path);

  // Returns true if any section in project has at least one dropout type
  // with scale > 0.
  static bool AnyDropoutEnabled(const Project& project);

  // Ensures scratch_events_ is populated for the given section.
  void EnsureScratchEvents(const Section& section, std::size_t section_index,
                           int lines_per_frame, int samples_per_line);

  // Writes a single dropout_run row to the sidecar.
  void WriteSidecarRow(int64_t frame_id, int64_t sample_start,
                       int64_t sample_count, int severity);

  // Applies random dropout events for one frame after overlap resolution.
  void ProcessRandomDropouts(
      const Section& section, std::size_t section_index,
      std::size_t global_frame, int samples_per_frame, int lines_per_frame,
      int samples_per_line, int64_t frame_id,
      const std::vector<std::pair<int, int>>& scratch_intervals,
      const SignalLevels& levels, SampleFixed clamp_min, SampleFixed clamp_max,
      std::vector<SampleFixed>* y_mv, std::vector<SampleFixed>* c_mv,
      std::size_t frame_buffer_offset);

  // Applies scratch dropout events for one frame.
  void ProcessScratchDropouts(
      int samples_per_line, int64_t frame_id, int frame_index_in_section,
      std::vector<std::pair<int, int>>* out_scratch, const SignalLevels& levels,
      SampleFixed clamp_min, SampleFixed clamp_max,
      std::vector<SampleFixed>* y_mv, std::vector<SampleFixed>* c_mv,
      std::size_t frame_buffer_offset, int samples_per_frame);

  // Splits a run at any active-picture boundary, writing sidecar rows and
  // applying the signal push for each resulting sub-run.
  void ApplyAndRecordRun(int sample_start, int sample_count, int direction,
                         double push_fraction, int64_t frame_id,
                         int active_picture_start, int active_picture_end,
                         const SignalLevels& levels, SampleFixed clamp_min,
                         SampleFixed clamp_max, std::vector<SampleFixed>* y_mv,
                         std::vector<SampleFixed>* c_mv,
                         std::size_t frame_buffer_offset);

  ILogger* logger_;
  uint64_t run_base_seed_;

  sqlite3* db_ = nullptr;
  sqlite3_stmt* insert_stmt_ = nullptr;
  // Sidecar file path for the currently open session; used by Abort to
  // remove the partially-written file.
  std::string sidecar_path_;

  // Cached scratch events for the current section.
  const Section* cached_scratch_section_ = nullptr;
  std::vector<ScratchEvent> scratch_events_;
};

}  // namespace videosynth
