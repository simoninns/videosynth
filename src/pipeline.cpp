/*
 * File:        pipeline.cpp
 * Module:      pipeline
 * Purpose:     Orchestrates parsing, validation, generation, and output stages.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/pipeline.h"

#include <algorithm>

#include "videosynth/fixed_point.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

std::size_t ComputeBatchFrameCount(const Project& project) {
  constexpr std::size_t kTargetBatchBytes = 64U * 1024U * 1024U;
  const std::size_t frame_span =
      static_cast<std::size_t>(SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
  if (frame_span == 0U) {
    return 1U;
  }

  const std::size_t bytes_per_frame = frame_span * sizeof(SampleFixed) * 2U;
  if (bytes_per_frame == 0U) {
    return 1U;
  }

  const std::size_t computed = std::max<std::size_t>(1U, kTargetBatchBytes / bytes_per_frame);
  return std::min<std::size_t>(computed, 64U);
}

std::size_t ComputeProgressInterval(std::size_t total_frames) {
  if (total_frames <= 120U) {
    return 1U;
  }
  constexpr std::size_t kTargetProgressMessages = 40U;
  return std::max<std::size_t>(1U,
                               (total_frames + kTargetProgressMessages - 1U) /
                                   kTargetProgressMessages);
}

}  // namespace

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

  std::vector<IGenerationStage::FrameScheduleItem> schedule;
  std::vector<std::string> generation_errors;
  if (!generation_->BuildFrameSchedule(parse_result.project, &schedule, &generation_errors)) {
    for (const std::string& error : generation_errors) {
      logger_->Error(error);
    }
    return false;
  }

  const std::size_t total_frames = schedule.size();
  const std::size_t batch_frame_count = ComputeBatchFrameCount(parse_result.project);
  const std::size_t progress_interval = ComputeProgressInterval(total_frames);

  std::vector<std::string> output_errors;
  if (!output_->BeginWrite(parse_result.project, total_frames, &output_errors)) {
    for (const std::string& error : output_errors) {
      logger_->Error(error);
    }
    return false;
  }

  logger_->Info("Generating and writing " + std::to_string(total_frames) +
                " frame(s) in batches of " + std::to_string(batch_frame_count) + ".");

  auto CloseOutputSessionOnFailure = [&]() {
    std::vector<std::string> cleanup_errors;
    output_->FinalizeWrite(&cleanup_errors);
  };

  std::size_t processed_frames = 0U;
  std::size_t next_progress_mark = progress_interval;

  while (processed_frames < total_frames) {
    const std::size_t frames_this_batch =
        std::min(batch_frame_count, total_frames - processed_frames);
    std::vector<SampleFixed> y_mv;
    std::vector<SampleFixed> c_mv;

    generation_errors.clear();
    if (!generation_->GenerateFrameBatch(parse_result.project,
                                         schedule,
                                         processed_frames,
                                         frames_this_batch,
                                         &y_mv,
                                         &c_mv,
                                         &generation_errors)) {
      CloseOutputSessionOnFailure();
      for (const std::string& error : generation_errors) {
        logger_->Error(error);
      }
      return false;
    }

    output_errors.clear();
    if (!output_->AppendSamples(y_mv, c_mv, &output_errors)) {
      CloseOutputSessionOnFailure();
      for (const std::string& error : output_errors) {
        logger_->Error(error);
      }
      return false;
    }

    processed_frames += frames_this_batch;
    if (processed_frames >= next_progress_mark || processed_frames == total_frames) {
      logger_->Info("Pipeline progress: " + std::to_string(processed_frames) + "/" +
                    std::to_string(total_frames) + " frame(s) written.");
      next_progress_mark += progress_interval;
    }
  }

  output_errors.clear();
  if (!output_->FinalizeWrite(&output_errors)) {
    for (const std::string& error : output_errors) {
      logger_->Error(error);
    }
    return false;
  }

  logger_->Info("Generation completed successfully.");
  return true;
}

}  // namespace videosynth
