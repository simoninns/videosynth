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
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

#include "videosynth/audio_track_generator.h"
#include "videosynth/dropout_injection_stage.h"
#include "videosynth/fixed_point.h"
#include "videosynth/frame_synthesis_pool.h"
#include "videosynth/noise_injection_stage.h"
#include "videosynth/path_resolution.h"
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

std::vector<std::size_t> ComputeDiscOutputFrameOrder(
    const std::vector<DiscSkip>& disc_skips, std::size_t total_disc_frames) {
  const std::vector<DiscFrameAction> plan =
      ComputeDiscSkipPlan(disc_skips, total_disc_frames);

  std::vector<std::size_t> output_order;
  output_order.reserve(total_disc_frames);
  for (std::size_t disc_frame = 0U; disc_frame < total_disc_frames;
       ++disc_frame) {
    const DiscFrameAction& action = plan[disc_frame];
    if (action.write_to_output) {
      output_order.push_back(disc_frame);
    }
    for (const std::size_t src : action.replay_disc_frames) {
      output_order.push_back(src);
    }
  }
  return output_order;
}

VideoSynthPipeline::VideoSynthPipeline(IProjectParser* parser,
                                       IProjectValidator* validator,
                                       IGenerationStage* generation,
                                       NoiseInjectionStage* noise_injection,
                                       DropoutInjectionStage* dropout_injection,
                                       IOutputStage* output, ILogger* logger,
                                       AudioTrackGenerator* audio_generator)
    : parser_(parser),
      validator_(validator),
      generation_(generation),
      noise_injection_(noise_injection),
      dropout_injection_(dropout_injection),
      output_(output),
      logger_(logger),
      audio_generator_(audio_generator) {}

bool VideoSynthPipeline::Run(const RunOptions& options,
                             IPipelineObserver* observer,
                             CancellationToken* cancellation) {
  logger_->Info("Starting pipeline: parse -> validate -> generate -> output");

  const ParseResult parse_result = parser_->ParseFile(options.project_path);
  if (!parse_result.ok) {
    for (const std::string& error : parse_result.errors) {
      logger_->Error(error);
    }
    if (observer != nullptr) {
      observer->OnRunFinished(PipelineRunStatus::kFailed);
    }
    return false;
  }

  // Resolve {name}/path logical-root tokens against the run's asset roots.
  // Plain relative paths keep their historical working-directory-relative
  // behaviour (anchor_unset = false), so existing projects are unaffected.
  const std::string project_dir =
      std::filesystem::path(options.project_path).parent_path().string();
  const Project resolved =
      ResolveProjectPaths(parse_result.project, options.asset_roots,
                          project_dir, /*anchor_unset=*/false);
  return RunProject(resolved, options, observer, cancellation);
}

bool VideoSynthPipeline::RunProject(const Project& project,
                                    const RunOptions& options,
                                    IPipelineObserver* observer,
                                    CancellationToken* cancellation) {
  // Reports the terminal status exactly once and converts it to the boolean
  // pipeline result.
  auto FinishRun = [&](PipelineRunStatus status) {
    if (observer != nullptr) {
      observer->OnRunFinished(status);
    }
    return status == PipelineRunStatus::kSucceeded;
  };

  auto NotifyStage = [&](const char* stage_name) {
    if (observer != nullptr) {
      observer->OnStageStarted(stage_name);
    }
  };

  auto IsCancelled = [&]() {
    return cancellation != nullptr && cancellation->IsCancellationRequested();
  };

  NotifyStage("validate");
  const ValidationResult validation_result = validator_->Validate(project);
  for (const std::string& warning : validation_result.warnings) {
    logger_->Warning(warning);
    if (observer != nullptr) {
      observer->OnWarning(warning);
    }
  }
  if (!validation_result.is_valid) {
    for (const std::string& error : validation_result.errors) {
      logger_->Error(error);
    }
    return FinishRun(PipelineRunStatus::kFailed);
  }

  if (options.validate_only) {
    logger_->Info("Validation successful.");
    return FinishRun(PipelineRunStatus::kSucceeded);
  }

  if (IsCancelled()) {
    logger_->Info("Pipeline run cancelled before generation started.");
    return FinishRun(PipelineRunStatus::kCancelled);
  }

  NotifyStage("generate");
  std::vector<IGenerationStage::FrameScheduleItem> schedule;
  std::vector<std::string> generation_errors;
  if (!generation_->BuildFrameSchedule(project, &schedule,
                                       &generation_errors)) {
    for (const std::string& error : generation_errors) {
      logger_->Error(error);
    }
    return FinishRun(PipelineRunStatus::kFailed);
  }

  const std::size_t total_disc_frames = schedule.size();

  // Determine whether disc skips are configured.  When present, an output plan
  // is built and the pipeline processes one disc frame at a time; otherwise
  // the normal batch path is used.
  const bool has_skips = !project.disc_skips.empty();

  std::vector<DiscFrameAction> skip_plan;
  std::size_t total_output_frames = total_disc_frames;

  if (has_skips) {
    skip_plan = ComputeDiscSkipPlan(project.disc_skips, total_disc_frames);
    total_output_frames = 0U;
    for (const DiscFrameAction& a : skip_plan) {
      if (a.write_to_output) {
        ++total_output_frames;
      }
      total_output_frames += a.replay_disc_frames.size();
    }
  }

  std::vector<std::string> output_errors;
  if (!output_->BeginWrite(project, total_output_frames, &output_errors)) {
    for (const std::string& error : output_errors) {
      logger_->Error(error);
    }
    return FinishRun(PipelineRunStatus::kFailed);
  }

  if (dropout_injection_ != nullptr) {
    std::vector<std::string> dropout_errors;
    if (!dropout_injection_->Begin(project, &dropout_errors)) {
      for (const std::string& error : dropout_errors) {
        logger_->Error(error);
      }
      std::vector<std::string> cleanup_errors;
      output_->FinalizeWrite(&cleanup_errors);
      return FinishRun(PipelineRunStatus::kFailed);
    }
  }

  auto CloseOutputSessionOnFailure = [&]() {
    std::vector<std::string> cleanup_errors;
    output_->FinalizeWrite(&cleanup_errors);
  };

  // -------------------------------------------------------------------
  // Optional frame-locked audio tracks. Active when an audio generator is
  // supplied and at least one section declares an audio channel pair. Audio is
  // a pure function of output position: the generator is driven with the
  // output-order section sequence so every stored frame carries the correct
  // per-frame sample count and each channel pair stays sample-accurately
  // frame-locked to the output stream (including under disc-skip replay).
  // -------------------------------------------------------------------
  const bool audio_enabled =
      audio_generator_ != nullptr && ProjectEnablesAudio(project);
  if (audio_enabled) {
    // The section shown by each stored output frame, in output order. Mirrors
    // the disc-skip withhold/replay emission below exactly.
    std::vector<const Section*> output_frame_sections;
    output_frame_sections.reserve(total_output_frames);
    if (has_skips) {
      for (std::size_t disc_frame = 0U; disc_frame < total_disc_frames;
           ++disc_frame) {
        const DiscFrameAction& a = skip_plan[disc_frame];
        if (a.write_to_output) {
          output_frame_sections.push_back(schedule[disc_frame].section);
        }
        for (const std::size_t src : a.replay_disc_frames) {
          output_frame_sections.push_back(schedule[src].section);
        }
      }
    } else {
      for (const IGenerationStage::FrameScheduleItem& item : schedule) {
        output_frame_sections.push_back(item.section);
      }
    }

    std::vector<std::string> audio_errors;
    if (!audio_generator_->Begin(project, output_frame_sections,
                                 &audio_errors)) {
      for (const std::string& error : audio_errors) {
        logger_->Error(error);
      }
      CloseOutputSessionOnFailure();
      return FinishRun(PipelineRunStatus::kFailed);
    }
  }

  // Monotonic output-frame counter driving audio emission. Incremented for
  // every stored output frame in output order (fresh writes and skip replays).
  std::size_t audio_output_index = 0U;

  // Aborts all in-progress output artefacts after a cancellation request:
  // video/chroma/metadata via the output stage, the WAV tracks, and the
  // dropout sidecar. Each abort is a no-op when its session is not open.
  auto CancelRun = [&]() {
    output_->AbortWrite();
    if (audio_enabled) {
      audio_generator_->Abort();
    }
    if (dropout_injection_ != nullptr) {
      dropout_injection_->Abort();
    }
    logger_->Info(
        "Pipeline run cancelled; in-progress output files were removed.");
    return FinishRun(PipelineRunStatus::kCancelled);
  };

  auto NotifyProgress = [&](std::size_t frames_completed) {
    if (observer != nullptr) {
      observer->OnFrameProgress(frames_completed, total_disc_frames);
    }
  };

  // Emits the next output frame's audio across all channel pairs, in output
  // order. Returns false and logs on failure.
  auto EmitAudioFrame = [&]() -> bool {
    std::vector<std::string> audio_errors;
    if (!audio_generator_->EmitFrame(audio_output_index, &audio_errors)) {
      for (const std::string& error : audio_errors) {
        logger_->Error(error);
      }
      return false;
    }
    ++audio_output_index;
    return true;
  };

  // Resolve the frame synthesis worker count. Disc-skip projects always use
  // the sequential per-frame loop: skips replay cached frames and the plan
  // logic is inherently order-dependent.
  const unsigned synthesis_threads =
      FrameSynthesisPool::ResolveThreadCount(options.threads);
  const bool parallel_synthesis = !has_skips && synthesis_threads > 1U;
  if (has_skips && synthesis_threads > 1U) {
    logger_->Info(
        "Disc skips present; using the sequential generation path regardless "
        "of the requested thread count.");
  }

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
      if (IsCancelled()) {
        return CancelRun();
      }

      std::vector<SampleFixed> y_mv;
      std::vector<SampleFixed> c_mv;

      generation_errors.clear();
      if (!generation_->GenerateFrameBatch(project, schedule, disc_frame, 1U,
                                           &y_mv, &c_mv, &generation_errors)) {
        CloseOutputSessionOnFailure();
        for (const std::string& error : generation_errors) {
          logger_->Error(error);
        }
        return FinishRun(PipelineRunStatus::kFailed);
      }

      if (noise_injection_ != nullptr) {
        noise_injection_->InjectNoise(project, schedule, disc_frame, 1U, &y_mv,
                                      &c_mv);
      }

      if (dropout_injection_ != nullptr) {
        dropout_injection_->InjectDropouts(project, schedule, disc_frame, 1U,
                                           &y_mv, &c_mv);
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
          return FinishRun(PipelineRunStatus::kFailed);
        }
        if (audio_enabled && !EmitAudioFrame()) {
          CloseOutputSessionOnFailure();
          return FinishRun(PipelineRunStatus::kFailed);
        }
      }

      for (const std::size_t src : action.replay_disc_frames) {
        const auto it = frame_cache.find(src);
        if (it == frame_cache.end()) {
          CloseOutputSessionOnFailure();
          logger_->Error("Disc skip internal error: cache miss for frame " +
                         std::to_string(src) + ".");
          return FinishRun(PipelineRunStatus::kFailed);
        }
        output_errors.clear();
        if (!output_->AppendSamples(it->second.first, it->second.second,
                                    &output_errors)) {
          CloseOutputSessionOnFailure();
          for (const std::string& error : output_errors) {
            logger_->Error(error);
          }
          return FinishRun(PipelineRunStatus::kFailed);
        }
        if (audio_enabled && !EmitAudioFrame()) {
          CloseOutputSessionOnFailure();
          return FinishRun(PipelineRunStatus::kFailed);
        }
      }

      NotifyProgress(disc_frame + 1U);
      if (disc_frame + 1U >= next_progress_mark ||
          disc_frame + 1U == total_disc_frames) {
        logger_->Info("Pipeline progress: " + std::to_string(disc_frame + 1U) +
                      "/" + std::to_string(total_disc_frames) +
                      " disc frame(s) processed.");
        next_progress_mark += progress_interval;
      }
    }
  } else if (parallel_synthesis) {
    // -------------------------------------------------------------------
    // Worker-pool frame synthesis (no disc skips, threads > 1).
    //
    // Frames are synthesised out of order on the pool using the enriched
    // schedule and per-frame noise/dropout seeds, then reassembled in frame
    // order on this thread for output, sidecar rows, and audio — so the
    // emitted bytes are identical to the sequential path.
    // -------------------------------------------------------------------
    const std::size_t progress_interval =
        ComputeProgressInterval(total_disc_frames);

    logger_->Info("Generating and writing " +
                  std::to_string(total_disc_frames) + " frame(s) using " +
                  std::to_string(synthesis_threads) + " synthesis threads.");

    auto SynthesizeFrameJob = [&](std::size_t disc_frame,
                                  SynthesizedFrame* out_frame) {
      std::vector<std::string> job_errors;
      if (!generation_->GenerateFrameBatch(project, schedule, disc_frame, 1U,
                                           &out_frame->y_mv, &out_frame->c_mv,
                                           &job_errors)) {
        out_frame->ok = false;
        out_frame->errors = job_errors;
        return;
      }
      if (noise_injection_ != nullptr) {
        noise_injection_->InjectNoise(project, schedule, disc_frame, 1U,
                                      &out_frame->y_mv, &out_frame->c_mv);
      }
      if (dropout_injection_ != nullptr) {
        dropout_injection_->ComputeFrameDropouts(
            project, schedule, disc_frame, &out_frame->y_mv, &out_frame->c_mv,
            0U, &out_frame->dropout_rows);
      }
    };

    std::size_t next_progress_mark = progress_interval;
    bool consumer_failed = false;
    auto ConsumeFrame = [&](std::size_t disc_frame,
                            SynthesizedFrame&& frame) -> bool {
      output_errors.clear();
      if (!output_->AppendSamples(frame.y_mv, frame.c_mv, &output_errors)) {
        for (const std::string& error : output_errors) {
          logger_->Error(error);
        }
        consumer_failed = true;
        return false;
      }

      if (dropout_injection_ != nullptr) {
        dropout_injection_->CommitSidecarRows(frame.dropout_rows);
      }

      if (audio_enabled && !EmitAudioFrame()) {
        consumer_failed = true;
        return false;
      }

      NotifyProgress(disc_frame + 1U);
      if (disc_frame + 1U >= next_progress_mark ||
          disc_frame + 1U == total_disc_frames) {
        logger_->Info("Pipeline progress: " + std::to_string(disc_frame + 1U) +
                      "/" + std::to_string(total_disc_frames) +
                      " frame(s) written.");
        next_progress_mark += progress_interval;
      }
      return true;
    };

    FrameSynthesisPool pool(synthesis_threads);
    std::vector<std::string> pool_errors;
    if (!pool.RunOrdered(total_disc_frames, SynthesizeFrameJob, ConsumeFrame,
                         cancellation, &pool_errors)) {
      if (IsCancelled()) {
        return CancelRun();
      }
      for (const std::string& error : pool_errors) {
        logger_->Error(error);
      }
      if (!consumer_failed && pool_errors.empty()) {
        logger_->Error("Frame synthesis pool stopped without completing.");
      }
      CloseOutputSessionOnFailure();
      return FinishRun(PipelineRunStatus::kFailed);
    }
  } else {
    // -------------------------------------------------------------------
    // Standard batched loop (no disc skips).
    // -------------------------------------------------------------------
    const std::size_t batch_frame_count = ComputeBatchFrameCount(project);
    const std::size_t progress_interval =
        ComputeProgressInterval(total_disc_frames);

    logger_->Info(
        "Generating and writing " + std::to_string(total_disc_frames) +
        " frame(s) in batches of " + std::to_string(batch_frame_count) + ".");

    std::size_t processed_frames = 0U;
    std::size_t next_progress_mark = progress_interval;

    while (processed_frames < total_disc_frames) {
      if (IsCancelled()) {
        return CancelRun();
      }

      const std::size_t frames_this_batch =
          std::min(batch_frame_count, total_disc_frames - processed_frames);
      std::vector<SampleFixed> y_mv;
      std::vector<SampleFixed> c_mv;

      generation_errors.clear();
      if (!generation_->GenerateFrameBatch(project, schedule, processed_frames,
                                           frames_this_batch, &y_mv, &c_mv,
                                           &generation_errors)) {
        CloseOutputSessionOnFailure();
        for (const std::string& error : generation_errors) {
          logger_->Error(error);
        }
        return FinishRun(PipelineRunStatus::kFailed);
      }

      if (noise_injection_ != nullptr) {
        noise_injection_->InjectNoise(project, schedule, processed_frames,
                                      frames_this_batch, &y_mv, &c_mv);
      }

      if (dropout_injection_ != nullptr) {
        dropout_injection_->InjectDropouts(project, schedule, processed_frames,
                                           frames_this_batch, &y_mv, &c_mv);
      }

      output_errors.clear();
      if (!output_->AppendSamples(y_mv, c_mv, &output_errors)) {
        CloseOutputSessionOnFailure();
        for (const std::string& error : output_errors) {
          logger_->Error(error);
        }
        return FinishRun(PipelineRunStatus::kFailed);
      }

      if (audio_enabled) {
        for (std::size_t i = 0U; i < frames_this_batch; ++i) {
          if (!EmitAudioFrame()) {
            CloseOutputSessionOnFailure();
            return FinishRun(PipelineRunStatus::kFailed);
          }
        }
      }

      processed_frames += frames_this_batch;
      NotifyProgress(processed_frames);
      if (processed_frames >= next_progress_mark ||
          processed_frames == total_disc_frames) {
        logger_->Info("Pipeline progress: " + std::to_string(processed_frames) +
                      "/" + std::to_string(total_disc_frames) +
                      " frame(s) written.");
        next_progress_mark += progress_interval;
      }
    }
  }

  NotifyStage("finalize");
  output_errors.clear();
  if (!output_->FinalizeWrite(&output_errors)) {
    for (const std::string& error : output_errors) {
      logger_->Error(error);
    }
    return FinishRun(PipelineRunStatus::kFailed);
  }

  if (audio_enabled) {
    std::vector<std::string> audio_errors;
    if (!audio_generator_->Finalize(&audio_errors)) {
      for (const std::string& error : audio_errors) {
        logger_->Error(error);
      }
      return FinishRun(PipelineRunStatus::kFailed);
    }
  }

  if (dropout_injection_ != nullptr) {
    std::vector<std::string> dropout_errors;
    if (!dropout_injection_->Finalize(&dropout_errors)) {
      for (const std::string& error : dropout_errors) {
        logger_->Error(error);
      }
      return FinishRun(PipelineRunStatus::kFailed);
    }
  }

  logger_->Info("Generation completed successfully.");
  return FinishRun(PipelineRunStatus::kSucceeded);
}

}  // namespace videosynth
