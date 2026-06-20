/*
 * File:        noise_injection_stage.h
 * Module:      noise_injection
 * Purpose:     Applies per-section two-component Gaussian noise to fixed-point
 *              mV Y/C frame buffers before output quantisation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth {

// Injects additive Gaussian noise into the Y and C mV-domain sample buffers
// for sections that have NoiseParameters::enabled == true.
//
// Two noise components are combined:
//   - Floor noise (sigma_f): zero-mean Gaussian, signal-independent; controls
//     Black PSNR as measured by orc-gui.
//   - Proportional noise (k): standard deviation scales with the instantaneous
//     Y-channel amplitude; makes white noisier than black, controlled by
//     noise_spread_db.
//
// A single noise draw is applied to both Y and C buffers at each sample
// position, producing correlated Y/C noise that matches real analogue sources.
//
// Seeding: by default a random base seed is captured from std::random_device at
// construction time, so each run of the same project produces different noise.
// Setting NoiseParameters::noise_seed_specified = true in a section causes that
// section to mix the user-supplied noise_seed into the per-frame seed formula
// instead, making the output fully deterministic and reproducible across runs.
// Within a single run, the same (frame, section) pair always produces identical
// output regardless of batch size.
//
// Thread-safety: NoiseInjectionStage is NOT thread-safe. InjectNoise must not
// be called concurrently from multiple threads.
class NoiseInjectionStage {
 public:
  explicit NoiseInjectionStage(ILogger* logger);

  // Adds noise to frame buffers in-place.
  //
  // Args:
  //   project:       Parsed project supplying signal levels and encoding
  //   preset. schedule:      Frame schedule mapping global frame indices to
  //   sections. frame_offset:  Index into schedule of the first frame in this
  //   batch. frame_count:   Number of frames in this batch. y_mv: Y-channel mV
  //   buffer (frame_count × samples_per_frame). c_mv:          C-channel mV
  //   buffer (frame_count × samples_per_frame).
  void InjectNoise(
      const Project& project,
      const std::vector<IGenerationStage::FrameScheduleItem>& schedule,
      std::size_t frame_offset, std::size_t frame_count,
      std::vector<SampleFixed>* y_mv, std::vector<SampleFixed>* c_mv);

 private:
  ILogger* logger_;
  uint64_t run_base_seed_;
};

}  // namespace videosynth
