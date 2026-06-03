/*
 * File:        generation_stage.h
 * Module:      generation_stage
 * Purpose:     Generates CVBS-domain Y and C sample buffers from frame-based
 * source data.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"
#include "videosynth/progressive_frame_source.h"
#include "videosynth/vits_definition_provider.h"
#include "videosynth/vits_generator.h"

namespace videosynth {

// Thread-safety: GenerationStage is NOT thread-safe. Inherits the NOT
// thread-safe guarantee from IGenerationStage. Concurrent calls to any public
// method will result in undefined behavior.
class GenerationStage final : public IGenerationStage {
 public:
  explicit GenerationStage(
      ILogger* logger = nullptr,
      const IVitsDefinitionProvider* vits_definition_provider = nullptr,
      const IVitsGenerator* vits_generator = nullptr);

  // Builds a schedule mapping project sections to output frames.
  //
  // Args:
  //   project: The validated project configuration.
  //   out_schedule: Output vector populated with FrameScheduleItem entries.
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool BuildFrameSchedule(const Project& project,
                          std::vector<FrameScheduleItem>* out_schedule,
                          std::vector<std::string>* errors) override;

  // Generates a batch of frames from the project's progressive frame sources.
  //
  // Args:
  //   project: The validated project configuration.
  //   schedule: The frame schedule built by BuildFrameSchedule.
  //   start_frame: Index of the first frame to generate in this batch.
  //   frame_count: Number of frames to generate.
  //   out_y_mv: Output vector for luma samples (Y channel).
  //   out_c_mv: Output vector for chroma samples (C channel).
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool GenerateFrameBatch(const Project& project,
                          const std::vector<FrameScheduleItem>& schedule,
                          std::size_t start_frame, std::size_t frame_count,
                          std::vector<SampleFixed>* out_y_mv,
                          std::vector<SampleFixed>* out_c_mv,
                          std::vector<std::string>* errors) override;

  // Convenience wrapper that builds the schedule and generates all frames.
  //
  // Args:
  //   project: The validated project configuration.
  //   out_y_mv: Output vector for luma samples (Y channel).
  //   out_c_mv: Output vector for chroma samples (C channel).
  //   errors: Output vector for any error messages.
  //
  // Returns:
  //   true on success, false on any error.
  bool Generate(const Project& project, std::vector<SampleFixed>* out_y_mv,
                std::vector<SampleFixed>* out_c_mv,
                std::vector<std::string>* errors) override;

 private:
  ILogger* logger_;
  ProgressiveFrameSource progressive_source_;
  VitsDefinitionProvider default_vits_definition_provider_;
  VitsGenerator default_vits_generator_;
  const IVitsDefinitionProvider* vits_definition_provider_;
  const IVitsGenerator* vits_generator_;
};

}  // namespace videosynth
