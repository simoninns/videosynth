/*
 * File:        pipeline.h
 * Module:      pipeline
 * Purpose:     Orchestrates parsing, validation, generation, and output stages.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/dropout_injection_stage.h"
#include "videosynth/interfaces.h"
#include "videosynth/noise_injection_stage.h"

namespace videosynth {

// Thread-safety: VideoSynthPipeline is NOT thread-safe. The Run method must
// not be called concurrently from multiple threads. All component pointers
// are accessed without synchronization.
class VideoSynthPipeline {
 public:
  VideoSynthPipeline(IProjectParser* parser, IProjectValidator* validator,
                     IGenerationStage* generation,
                     NoiseInjectionStage* noise_injection,
                     DropoutInjectionStage* dropout_injection,
                     IOutputStage* output, ILogger* logger);

  // Orchestrates the full pipeline:
  //   parse -> validate -> generate -> noise -> dropout -> output.
  //
  // Args:
  //   options: Contains project path and runtime configuration.
  //
  // Returns:
  //   true if the entire pipeline completed successfully, false on any error.
  //   Errors are logged via the logger_ and also returned through the
  //   IProjectParser, IProjectValidator, IGenerationStage, and IOutputStage
  //   interfaces' error output parameters.
  bool Run(const RunOptions& options);

 private:
  IProjectParser* parser_;
  IProjectValidator* validator_;
  IGenerationStage* generation_;
  NoiseInjectionStage* noise_injection_;
  DropoutInjectionStage* dropout_injection_;
  IOutputStage* output_;
  ILogger* logger_;
};

}  // namespace videosynth
