/*
 * File:        pipeline.cpp
 * Module:      pipeline
 * Purpose:     Orchestrates parsing, validation, generation, and output stages.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/pipeline.h"

namespace videosynth {

VideoSynthPipeline::VideoSynthPipeline(IProjectParser* parser,
                                       IProjectValidator* validator,
                                       IGenerationStage* generation,
                                       IOutputStage* output,
                                       ILogger* logger)
    : parser_(parser),
      validator_(validator),
      generation_(generation),
      output_(output),
      logger_(logger) {}

bool VideoSynthPipeline::Run(const RunOptions& options) {
  logger_->Info("Starting pipeline: parse -> validate -> generate -> output");

  const ParseResult parse_result = parser_->ParseFile(options.project_path);
  if (!parse_result.ok) {
    for (const std::string& error : parse_result.errors) {
      logger_->Error(error);
    }
    return false;
  }

  const ValidationResult validation_result = validator_->Validate(parse_result.project);
  if (!validation_result.is_valid) {
    for (const std::string& error : validation_result.errors) {
      logger_->Error(error);
    }
    return false;
  }

  if (options.validate_only) {
    logger_->Info("Validation successful.");
    return true;
  }

  std::vector<SampleFixed> y_mv;
  std::vector<SampleFixed> c_mv;
  std::vector<std::string> generation_errors;
  if (!generation_->Generate(parse_result.project, &y_mv, &c_mv, &generation_errors)) {
    for (const std::string& error : generation_errors) {
      logger_->Error(error);
    }
    return false;
  }

  std::vector<std::string> output_errors;
  if (!output_->Write(parse_result.project,
                      y_mv,
                      c_mv,
                      &output_errors)) {
    for (const std::string& error : output_errors) {
      logger_->Error(error);
    }
    return false;
  }

  logger_->Info("Generation completed successfully.");
  return true;
}

}  // namespace videosynth
