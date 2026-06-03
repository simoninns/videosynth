/*
 * File:        pipeline.h
 * Module:      pipeline
 * Purpose:     Orchestrates parsing, validation, generation, and output stages.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

// Thread-safety: VideoSynthPipeline is NOT thread-safe. The Run method must
// not be called concurrently from multiple threads. All component pointers
// (parser_, validator_, generation_, output_, logger_) are accessed without
// synchronization.
class VideoSynthPipeline {
 public:
  VideoSynthPipeline(IProjectParser* parser, IProjectValidator* validator,
                     IGenerationStage* generation, IOutputStage* output,
                     ILogger* logger);

  // Orchestrates the full pipeline: parse -> validate -> generate -> output.
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
  IOutputStage* output_;
  ILogger* logger_;
};

}  // namespace videosynth
