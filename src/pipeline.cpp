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
#include <unordered_map>
#include <utility>
#include <vector>

#include "videosynth/dropout_injection_stage.h"
#include "videosynth/fixed_point.h"
#include "videosynth/noise_injection_stage.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

// Per-disc-frame action used when disc_skips are present.
struct DiscFrameAction {
  // If false the frame is generated (to advance burst-phase state) but not
  // written to the output stream.  Used for forward skips.
  bool write_to_output = true;
  // If true the fully-processed sample buffers for this disc frame are kept
  // in a cache so they can be re-emitted as backward-skip copies.
  bool cache_samples = false;
  // Disc frame indices (0-based) whose cached samples should be appended to
  // the output stream immediately after this disc frame is processed.
  std::vector<std::size_t> replay_disc_frames;
};

// Builds one DiscFrameAction per disc frame from the skip declarations.
// disc_skips uses 1-based at_frame; this function converts to 0-based indices.
std::vector<DiscFrameAction> ComputeDiscSkipPlan(
    const std::vector<DiscSkip>& disc_skips, std::size_t total_disc_frames) {
  std::vector<DiscFrameAction> plan(total_disc_frames);

  for (const DiscSkip& skip : disc_skips) {
    // Convert to 0-based.
    const std::size_t at =
        static_cast<std::size_t>(std::max(0, skip.at_frame - 1));
    const std::size_t count = static_cast<std::size_t>(skip.count);

    if (skip.direction == DiscSkipDirection::kForward) {
      // Mark frames [at, at+count) as withheld from output.
      for (std::size_t i = 0; i < count && (at + i) < total_disc_frames; ++i) {
        plan[at + i].write_to_output = false;
      }
    } else {
      // Backward skip: after disc frame 'at', replay the C frames ending at
      // 'at' (i.e. [at-count+1 .. at]) as identical copies.
      if (count > at + 1) {
        continue;  // Validated earlier; skip malformed entry defensively.
      }
      const std::size_t first_src = at - count + 1;
      for (std::size_t i = 0; i < count; ++i) {
        const std::size_t src = first_src + i;
        if (src < total_disc_frames) {
          plan[src].cache_samples = true;
          plan[at].replay_disc_frames.push_back(src);
        }
      }
    }
  }

  return plan;
}

std::size_t ComputeBatchFrameCount(const Project& project) {
  constexpr std::size_t kTargetBatchBytes = 64ULL * 1024ULL * 1024ULL;
  const std::size_t frame_span = static_cast<std::size_t>(
      SamplesPerFrame4fsc(project.cvbs_presets.video_standard_preset));
  if (frame_span == 0U) {
    return 1U;
  }

  const std::size_t bytes_per_frame = frame_span * sizeof(SampleFixed) * 2U;
  if (bytes_per_frame == 0U) {
    return 1U;
  }

  const std::size_t computed =
      std::max<std::size_t>(1U, kTargetBatchBytes / bytes_per_frame);
  return std::min<std::size_t>(computed, 64U);
}

std::size_t ComputeProgressInterval(std::size_t total_frames) {
  if (total_frames <= 120U) {
    return 1U;
  }
  constexpr std::size_t kTargetProgressMessages = 40U;
  return std::max<std::size_t>(
      1U,
      (total_frames + kTargetProgressMessages - 1U) / kTargetProgressMessages);
}

}  // namespace

VideoSynthPipeline::VideoSynthPipeline(IProjectParser* parser,
                                       IProjectValidator* validator,
                                       IGenerationStage* generation,
                                       NoiseInjectionStage* noise_injection,
                                       DropoutInjectionStage* dropout_injection,
                                       IOutputStage* output, ILogger* logger)
    : parser_(parser),
      validator_(validator),
      generation_(generation),
      noise_injection_(noise_injection),
      dropout_injection_(dropout_injection),
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

  const ValidationResult validation_result =
      validator_->Validate(parse_result.project);
  for (const std::string& warning : validation_result.warnings) {
    logger_->Warning(warning);
  }
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
  if (!generation_->BuildFrameSchedule(parse_result.project, &schedule,
                                       &generation_errors)) {
    for (const std::string& error : generation_errors) {
      logger_->Error(error);
    }
    return false;
  }

  const std::size_t total_disc_frames = schedule.size();

  // Determine whether disc skips are configured.  When present, an output plan
  // is built and the pipeline processes one disc frame at a time; otherwise
  // the normal batch path is used.
  const bool has_skips = !parse_result.project.disc_skips.empty();

  std::vector<DiscFrameAction> skip_plan;
  std::size_t total_output_frames = total_disc_frames;

  if (has_skips) {
    skip_plan =
        ComputeDiscSkipPlan(parse_result.project.disc_skips, total_disc_frames);
    total_output_frames = 0U;
    for (const DiscFrameAction& a : skip_plan) {
      if (a.write_to_output) {
        ++total_output_frames;
      }
      total_output_frames += a.replay_disc_frames.size();
    }
  }

  std::vector<std::string> output_errors;
  if (!output_->BeginWrite(parse_result.project, total_output_frames,
                           &output_errors)) {
    for (const std::string& error : output_errors) {
      logger_->Error(error);
    }
    return false;
  }

  if (dropout_injection_ != nullptr) {
    std::vector<std::string> dropout_errors;
    if (!dropout_injection_->Begin(parse_result.project, &dropout_errors)) {
      for (const std::string& error : dropout_errors) {
        logger_->Error(error);
      }
      std::vector<std::string> cleanup_errors;
      output_->FinalizeWrite(&cleanup_errors);
      return false;
    }
  }

  auto CloseOutputSessionOnFailure = [&]() {
    std::vector<std::string> cleanup_errors;
    output_->FinalizeWrite(&cleanup_errors);
  };

  if (has_skips) {
    // -------------------------------------------------------------------
    // Skip-aware per-frame loop.
    //
    // Every disc frame is generated once (advancing burst-phase state).
    // Forward-skipped frames are withheld from output; backward-skip copies
    // are bit-identical re-emissions of cached fully-processed frames.
    // -------------------------------------------------------------------
    logger_->Info("Generating " + std::to_string(total_disc_frames) +
                  " disc frame(s) with skip plan → " +
                  std::to_string(total_output_frames) + " output frame(s).");

    // Sample cache keyed by 0-based disc frame index.
    std::unordered_map<std::size_t, std::pair<std::vector<SampleFixed>,
                                              std::vector<SampleFixed>>>
        frame_cache;
    frame_cache.reserve(total_disc_frames);

    const std::size_t progress_interval =
        ComputeProgressInterval(total_disc_frames);
    std::size_t next_progress_mark = progress_interval;

    for (std::size_t disc_frame = 0U; disc_frame < total_disc_frames;
         ++disc_frame) {
      std::vector<SampleFixed> y_mv;
      std::vector<SampleFixed> c_mv;

      generation_errors.clear();
      if (!generation_->GenerateFrameBatch(parse_result.project, schedule,
                                           disc_frame, 1U, &y_mv, &c_mv,
                                           &generation_errors)) {
        CloseOutputSessionOnFailure();
        for (const std::string& error : generation_errors) {
          logger_->Error(error);
        }
        return false;
      }

      if (noise_injection_ != nullptr) {
        noise_injection_->InjectNoise(parse_result.project, schedule,
                                      disc_frame, 1U, &y_mv, &c_mv);
      }

      if (dropout_injection_ != nullptr) {
        dropout_injection_->InjectDropouts(parse_result.project, schedule,
                                           disc_frame, 1U, &y_mv, &c_mv);
      }

      const DiscFrameAction& action = skip_plan[disc_frame];

      if (action.cache_samples) {
        frame_cache[disc_frame] = {y_mv, c_mv};
      }

      if (action.write_to_output) {
        output_errors.clear();
        if (!output_->AppendSamples(y_mv, c_mv, &output_errors)) {
          CloseOutputSessionOnFailure();
          for (const std::string& error : output_errors) {
            logger_->Error(error);
          }
          return false;
        }
      }

      for (const std::size_t src : action.replay_disc_frames) {
        const auto it = frame_cache.find(src);
        if (it == frame_cache.end()) {
          CloseOutputSessionOnFailure();
          logger_->Error("Disc skip internal error: cache miss for frame " +
                         std::to_string(src) + ".");
          return false;
        }
        output_errors.clear();
        if (!output_->AppendSamples(it->second.first, it->second.second,
                                    &output_errors)) {
          CloseOutputSessionOnFailure();
          for (const std::string& error : output_errors) {
            logger_->Error(error);
          }
          return false;
        }
      }

      if (disc_frame + 1U >= next_progress_mark ||
          disc_frame + 1U == total_disc_frames) {
        logger_->Info("Pipeline progress: " + std::to_string(disc_frame + 1U) +
                      "/" + std::to_string(total_disc_frames) +
                      " disc frame(s) processed.");
        next_progress_mark += progress_interval;
      }
    }
  } else {
    // -------------------------------------------------------------------
    // Standard batched loop (no disc skips).
    // -------------------------------------------------------------------
    const std::size_t batch_frame_count =
        ComputeBatchFrameCount(parse_result.project);
    const std::size_t progress_interval =
        ComputeProgressInterval(total_disc_frames);

    logger_->Info(
        "Generating and writing " + std::to_string(total_disc_frames) +
        " frame(s) in batches of " + std::to_string(batch_frame_count) + ".");

    std::size_t processed_frames = 0U;
    std::size_t next_progress_mark = progress_interval;

    while (processed_frames < total_disc_frames) {
      const std::size_t frames_this_batch =
          std::min(batch_frame_count, total_disc_frames - processed_frames);
      std::vector<SampleFixed> y_mv;
      std::vector<SampleFixed> c_mv;

      generation_errors.clear();
      if (!generation_->GenerateFrameBatch(parse_result.project, schedule,
                                           processed_frames, frames_this_batch,
                                           &y_mv, &c_mv, &generation_errors)) {
        CloseOutputSessionOnFailure();
        for (const std::string& error : generation_errors) {
          logger_->Error(error);
        }
        return false;
      }

      if (noise_injection_ != nullptr) {
        noise_injection_->InjectNoise(parse_result.project, schedule,
                                      processed_frames, frames_this_batch,
                                      &y_mv, &c_mv);
      }

      if (dropout_injection_ != nullptr) {
        dropout_injection_->InjectDropouts(parse_result.project, schedule,
                                           processed_frames, frames_this_batch,
                                           &y_mv, &c_mv);
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
      if (processed_frames >= next_progress_mark ||
          processed_frames == total_disc_frames) {
        logger_->Info("Pipeline progress: " + std::to_string(processed_frames) +
                      "/" + std::to_string(total_disc_frames) +
                      " frame(s) written.");
        next_progress_mark += progress_interval;
      }
    }
  }

  output_errors.clear();
  if (!output_->FinalizeWrite(&output_errors)) {
    for (const std::string& error : output_errors) {
      logger_->Error(error);
    }
    return false;
  }

  if (dropout_injection_ != nullptr) {
    std::vector<std::string> dropout_errors;
    if (!dropout_injection_->Finalize(&dropout_errors)) {
      for (const std::string& error : dropout_errors) {
        logger_->Error(error);
      }
      return false;
    }
  }

  logger_->Info("Generation completed successfully.");
  return true;
}

}  // namespace videosynth
