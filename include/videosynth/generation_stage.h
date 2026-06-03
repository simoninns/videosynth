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

class GenerationStage final : public IGenerationStage {
 public:
  explicit GenerationStage(
      ILogger* logger = nullptr,
      const IVitsDefinitionProvider* vits_definition_provider = nullptr,
      const IVitsGenerator* vits_generator = nullptr);

  bool BuildFrameSchedule(const Project& project,
                          std::vector<FrameScheduleItem>* out_schedule,
                          std::vector<std::string>* errors) override;

  bool GenerateFrameBatch(const Project& project,
                          const std::vector<FrameScheduleItem>& schedule,
                          std::size_t start_frame, std::size_t frame_count,
                          std::vector<SampleFixed>* out_y_mv,
                          std::vector<SampleFixed>* out_c_mv,
                          std::vector<std::string>* errors) override;

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
