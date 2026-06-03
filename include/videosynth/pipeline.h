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

class VideoSynthPipeline {
 public:
  VideoSynthPipeline(IProjectParser* parser, IProjectValidator* validator,
                     IGenerationStage* generation, IOutputStage* output,
                     ILogger* logger);

  bool Run(const RunOptions& options);

 private:
  IProjectParser* parser_;
  IProjectValidator* validator_;
  IGenerationStage* generation_;
  IOutputStage* output_;
  ILogger* logger_;
};

}  // namespace videosynth
