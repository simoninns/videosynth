/*
 * File:        test_pipeline.cpp
 * Module:      pipeline_tests
 * Purpose:     Validates pipeline control flow using deterministic mocks.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "videosynth/pipeline.h"

namespace videosynth {
namespace {

class MockParser final : public IProjectParser {
 public:
  ParseResult result;
  std::string last_path;
  std::vector<std::string>* call_log = nullptr;

  ParseResult ParseFile(const std::string& path) override {
    last_path = path;
    if (call_log != nullptr) {
      call_log->push_back("parse");
    }
    return result;
  }
};

class MockValidator final : public IProjectValidator {
 public:
  ValidationResult result;
  bool called = false;
  std::vector<std::string>* call_log = nullptr;

  ValidationResult Validate(const Project&) override {
    called = true;
    if (call_log != nullptr) {
      call_log->push_back("validate");
    }
    return result;
  }
};

class MockGeneration final : public IGenerationStage {
 public:
  bool called = false;
  // When true, embeds start_frame as a unique marker in every sample so
  // tests can verify which disc frame produced each output frame.
  bool embed_frame_id = false;
  std::vector<std::string>* call_log = nullptr;

  bool BuildFrameSchedule(const Project& project,
                          std::vector<FrameScheduleItem>* out_schedule,
                          std::vector<std::string>* errors) override {
    if (call_log != nullptr) {
      call_log->push_back("build_schedule");
    }
    out_schedule->clear();
    for (const Section& s : project.sections) {
      for (int i = 0; i < s.duration_frames; ++i) {
        out_schedule->push_back(
            FrameScheduleItem{.section = &s, .source_frame_index = i});
      }
    }
    errors->clear();
    return true;
  }

  bool GenerateFrameBatch(const Project&, const std::vector<FrameScheduleItem>&,
                          std::size_t start_frame, std::size_t frame_count,
                          std::vector<SampleFixed>* out_y_mv,
                          std::vector<SampleFixed>* out_c_mv,
                          std::vector<std::string>* errors) override {
    called = true;
    if (call_log != nullptr) {
      call_log->push_back("generate_batch");
    }
    out_y_mv->clear();
    out_c_mv->clear();
    for (std::size_t i = 0; i < frame_count; ++i) {
      // 8 samples per frame; value = disc-frame index when embed_frame_id.
      const SampleFixed val =
          embed_frame_id ? static_cast<SampleFixed>(start_frame + i) : 0;
      for (int s = 0; s < 8; ++s) {
        out_y_mv->push_back(val);
        out_c_mv->push_back(0);
      }
    }
    errors->clear();
    return true;
  }

  bool Generate(const Project&, std::vector<SampleFixed>* out_y_mv,
                std::vector<SampleFixed>* out_c_mv,
                std::vector<std::string>* errors) override {
    called = true;
    out_y_mv->assign(8, 0);
    out_c_mv->assign(8, 0);
    errors->clear();
    return true;
  }
};

class MockOutput final : public IOutputStage {
 public:
  bool called = false;
  std::size_t begin_write_frame_count = 0;
  // Stores the first sample of each appended y_mv chunk.
  std::vector<SampleFixed> appended_first_samples;
  std::vector<std::string>* call_log = nullptr;
  // When non-null, requests cancellation on this token after the first
  // AppendSamples call, simulating a cancel arriving mid-run.
  CancellationToken* cancel_after_first_append = nullptr;

  bool BeginWrite(const Project&, std::size_t expected_frame_count,
                  std::vector<std::string>* errors) override {
    begin_write_frame_count = expected_frame_count;
    if (call_log != nullptr) {
      call_log->push_back("begin_write");
    }
    errors->clear();
    return true;
  }

  bool AppendSamples(const std::vector<SampleFixed>& y_mv,
                     const std::vector<SampleFixed>&,
                     std::vector<std::string>* errors) override {
    called = true;
    if (call_log != nullptr) {
      call_log->push_back("append_samples");
    }
    if (!y_mv.empty()) {
      appended_first_samples.push_back(y_mv[0]);
    }
    if (cancel_after_first_append != nullptr) {
      cancel_after_first_append->RequestCancellation();
    }
    errors->clear();
    return true;
  }

  bool FinalizeWrite(std::vector<std::string>* errors) override {
    finalize_called = true;
    if (call_log != nullptr) {
      call_log->push_back("finalize_write");
    }
    errors->clear();
    return true;
  }

  void AbortWrite() override { abort_called = true; }

  bool Write(const Project&, const std::vector<SampleFixed>&,
             const std::vector<SampleFixed>&,
             std::vector<std::string>* errors) override {
    called = true;
    errors->clear();
    return true;
  }

  bool finalize_called = false;
  bool abort_called = false;
};

class MockLogger final : public ILogger {
 public:
  std::vector<std::string> infos;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
  std::vector<std::string> debugs;
  std::vector<std::string> traces;

  void Info(const std::string& message) override { infos.push_back(message); }
  void Warning(const std::string& message) override {
    warnings.push_back(message);
  }
  void Error(const std::string& message) override { errors.push_back(message); }
  void Debug(const std::string& message) override { debugs.push_back(message); }
  void Trace(const std::string& message) override { traces.push_back(message); }
};

Project MakeProject(int duration_frames = 1) {
  Project p;
  p.cvbs_presets.video_standard_preset = Standard::kPal;
  p.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  p.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  p.output.video_path = "/tmp/videosynth_pipeline_test.composite";
  p.output.metadata_path = "/tmp/videosynth_pipeline_test.meta";
  p.sections.push_back(Section{.name = "Valid",
                               .type = "progressive",
                               .line_injections = {},
                               .source = "/tmp/videosynth_pipeline_test.exr",
                               .duration_frames = duration_frames});
  return p;
}

TEST(PipelineTest, ValidateOnlyStopsBeforeGeneration) {
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = MakeProject();

  MockValidator validator;
  validator.result.is_valid = true;

  MockGeneration generation;
  MockOutput output;
  MockLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);

  RunOptions options;
  options.project_path = "project.yaml";
  options.validate_only = true;

  EXPECT_TRUE(pipeline.Run(options));
  EXPECT_TRUE(validator.called);
  EXPECT_FALSE(generation.called);
  EXPECT_FALSE(output.called);
}

TEST(PipelineTest, FullRunCallsGenerationAndOutput) {
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = MakeProject();

  MockValidator validator;
  validator.result.is_valid = true;

  MockGeneration generation;
  MockOutput output;
  MockLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);

  RunOptions options;
  options.project_path = "project.yaml";

  EXPECT_TRUE(pipeline.Run(options));
  EXPECT_TRUE(generation.called);
  EXPECT_TRUE(output.called);
}

TEST(PipelineTest, ValidationFailureStopsPipeline) {
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = MakeProject();

  MockValidator validator;
  validator.result.is_valid = false;
  validator.result.errors = {"bad config"};

  MockGeneration generation;
  MockOutput output;
  MockLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);

  RunOptions options;
  options.project_path = "project.yaml";

  EXPECT_FALSE(pipeline.Run(options));
  EXPECT_FALSE(generation.called);
  EXPECT_FALSE(output.called);
  ASSERT_EQ(logger.errors.size(), 1U);
}

// ---------------------------------------------------------------------------
// Disc skip pipeline tests
// ---------------------------------------------------------------------------

TEST(PipelineTest, ForwardSkipReducesOutputFrameCount) {
  // 6 disc frames; at_frame=3 (1-based), count=2 → 0-based indices [2,3] are
  // withheld → 4 output frames: disc frames 0,1,4,5.
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = MakeProject(6);
  DiscSkip fwd;
  fwd.at_frame = 3;
  fwd.direction = DiscSkipDirection::kForward;
  fwd.count = 2;
  parser.result.project.disc_skips.push_back(fwd);

  MockValidator validator;
  validator.result.is_valid = true;

  MockGeneration generation;
  generation.embed_frame_id = true;

  MockOutput output;
  MockLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);
  RunOptions options;
  options.project_path = "project.yaml";

  EXPECT_TRUE(pipeline.Run(options));
  EXPECT_EQ(output.begin_write_frame_count, 4U);
  ASSERT_EQ(output.appended_first_samples.size(), 4U);
  EXPECT_EQ(output.appended_first_samples[0], 0);
  EXPECT_EQ(output.appended_first_samples[1], 1);
  // Disc frames at 0-based indices 2 and 3 are withheld; next is index 4.
  EXPECT_EQ(output.appended_first_samples[2], 4);
  EXPECT_EQ(output.appended_first_samples[3], 5);
}

TEST(PipelineTest, BackwardSkipIncreasesOutputFrameCount) {
  // 5 disc frames; at_frame=3 (1-based), count=2 → at=2 (0-based).
  // first_src = 2-2+1 = 1; replay_disc_frames at index 2 = [1,2].
  // Output: 0,1,2, copies[1,2], 3,4 → 7 frames.
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = MakeProject(5);
  DiscSkip bwd;
  bwd.at_frame = 3;
  bwd.direction = DiscSkipDirection::kBackward;
  bwd.count = 2;
  parser.result.project.disc_skips.push_back(bwd);

  MockValidator validator;
  validator.result.is_valid = true;

  MockGeneration generation;
  generation.embed_frame_id = true;

  MockOutput output;
  MockLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);
  RunOptions options;
  options.project_path = "project.yaml";

  EXPECT_TRUE(pipeline.Run(options));
  EXPECT_EQ(output.begin_write_frame_count, 7U);
  ASSERT_EQ(output.appended_first_samples.size(), 7U);
  EXPECT_EQ(output.appended_first_samples[0], 0);
  EXPECT_EQ(output.appended_first_samples[1], 1);
  EXPECT_EQ(output.appended_first_samples[2], 2);
  // Copies of disc frames 1 and 2 (0-based) follow disc frame 2 in output.
  EXPECT_EQ(output.appended_first_samples[3], output.appended_first_samples[1]);
  EXPECT_EQ(output.appended_first_samples[4], output.appended_first_samples[2]);
  EXPECT_EQ(output.appended_first_samples[5], 3);
  EXPECT_EQ(output.appended_first_samples[6], 4);
}

TEST(PipelineTest, ForwardSkipAtStartReducesOutputAndMaintainsOrder) {
  // Skip first 2 disc frames → output starts at disc frame 2.
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = MakeProject(5);
  DiscSkip fwd;
  fwd.at_frame = 1;
  fwd.direction = DiscSkipDirection::kForward;
  fwd.count = 2;
  parser.result.project.disc_skips.push_back(fwd);

  MockValidator validator;
  validator.result.is_valid = true;

  MockGeneration generation;
  generation.embed_frame_id = true;

  MockOutput output;
  MockLogger logger;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);
  RunOptions options;
  options.project_path = "project.yaml";

  EXPECT_TRUE(pipeline.Run(options));
  EXPECT_EQ(output.begin_write_frame_count, 3U);
  ASSERT_EQ(output.appended_first_samples.size(), 3U);
  // First output frame is disc frame 2 (0-based).
  EXPECT_EQ(output.appended_first_samples[0], 2);
  EXPECT_EQ(output.appended_first_samples[1], 3);
  EXPECT_EQ(output.appended_first_samples[2], 4);
}

// ---------------------------------------------------------------------------
// In-memory execution path (RunProject), observer, and cancellation tests
// ---------------------------------------------------------------------------

class RecordingObserver final : public IPipelineObserver {
 public:
  std::vector<std::string> stages;
  std::vector<std::pair<std::size_t, std::size_t>> progress;
  std::vector<std::string> warnings;
  std::vector<PipelineRunStatus> statuses;

  void OnStageStarted(const std::string& stage_name) override {
    stages.push_back(stage_name);
  }
  void OnFrameProgress(std::size_t frames_completed,
                       std::size_t frames_total) override {
    progress.emplace_back(frames_completed, frames_total);
  }
  void OnWarning(const std::string& message) override {
    warnings.push_back(message);
  }
  void OnRunFinished(PipelineRunStatus status) override {
    statuses.push_back(status);
  }
};

TEST(PipelineTest, RunProjectMatchesFileBasedStageCallSequence) {
  const Project project = MakeProject(3);

  // File-based path.
  std::vector<std::string> file_calls;
  MockParser parser;
  parser.result.ok = true;
  parser.result.project = project;
  parser.call_log = &file_calls;
  MockValidator validator;
  validator.result.is_valid = true;
  validator.call_log = &file_calls;
  MockGeneration generation;
  generation.call_log = &file_calls;
  MockOutput output;
  output.call_log = &file_calls;
  MockLogger logger;

  VideoSynthPipeline file_pipeline(&parser, &validator, &generation, nullptr,
                                   nullptr, &output, &logger);
  RunOptions options;
  options.project_path = "project.yaml";
  EXPECT_TRUE(file_pipeline.Run(options));

  // In-memory path with an independent set of mocks.
  std::vector<std::string> memory_calls;
  MockParser unused_parser;
  MockValidator memory_validator;
  memory_validator.result.is_valid = true;
  memory_validator.call_log = &memory_calls;
  MockGeneration memory_generation;
  memory_generation.call_log = &memory_calls;
  MockOutput memory_output;
  memory_output.call_log = &memory_calls;

  VideoSynthPipeline memory_pipeline(&unused_parser, &memory_validator,
                                     &memory_generation, nullptr, nullptr,
                                     &memory_output, &logger);
  EXPECT_TRUE(memory_pipeline.RunProject(project, options));

  // The file-based path is exactly parse followed by the in-memory sequence.
  ASSERT_FALSE(file_calls.empty());
  EXPECT_EQ(file_calls.front(), "parse");
  EXPECT_EQ(std::vector<std::string>(file_calls.begin() + 1, file_calls.end()),
            memory_calls);
  EXPECT_TRUE(unused_parser.last_path.empty());
}

TEST(PipelineTest, ObserverReceivesMonotonicProgressAndSuccessStatus) {
  // 30 frames force multiple batches with the PAL batch size, so progress is
  // reported more than once.
  const Project project = MakeProject(30);

  MockParser parser;
  MockValidator validator;
  validator.result.is_valid = true;
  validator.result.warnings = {"minor issue"};
  MockGeneration generation;
  MockOutput output;
  MockLogger logger;
  RecordingObserver observer;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);
  RunOptions options;
  EXPECT_TRUE(pipeline.RunProject(project, options, &observer));

  ASSERT_GE(observer.progress.size(), 2U);
  std::size_t previous_completed = 0U;
  for (const auto& [completed, total] : observer.progress) {
    EXPECT_EQ(total, 30U);
    EXPECT_GE(completed, previous_completed);
    previous_completed = completed;
  }
  EXPECT_EQ(observer.progress.back().first, 30U);

  EXPECT_EQ(observer.stages,
            (std::vector<std::string>{"validate", "generate", "finalize"}));
  EXPECT_EQ(observer.warnings, (std::vector<std::string>{"minor issue"}));
  ASSERT_EQ(observer.statuses.size(), 1U);
  EXPECT_EQ(observer.statuses[0], PipelineRunStatus::kSucceeded);
}

TEST(PipelineTest, CancellationBetweenBatchesReportsCancelledAndAborts) {
  const Project project = MakeProject(30);

  MockParser parser;
  MockValidator validator;
  validator.result.is_valid = true;
  MockGeneration generation;
  MockOutput output;
  MockLogger logger;
  RecordingObserver observer;
  CancellationToken cancellation;
  output.cancel_after_first_append = &cancellation;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);
  RunOptions options;
  EXPECT_FALSE(pipeline.RunProject(project, options, &observer, &cancellation));

  // The run stopped after the first batch: the abort path was requested on
  // the output stage and the session was never finalized.
  EXPECT_EQ(output.appended_first_samples.size(), 1U);
  EXPECT_TRUE(output.abort_called);
  EXPECT_FALSE(output.finalize_called);
  ASSERT_EQ(observer.statuses.size(), 1U);
  EXPECT_EQ(observer.statuses[0], PipelineRunStatus::kCancelled);
}

TEST(PipelineTest, CancellationBeforeGenerationSkipsAllStages) {
  const Project project = MakeProject(4);

  MockParser parser;
  MockValidator validator;
  validator.result.is_valid = true;
  MockGeneration generation;
  MockOutput output;
  MockLogger logger;
  RecordingObserver observer;
  CancellationToken cancellation;
  cancellation.RequestCancellation();

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);
  RunOptions options;
  EXPECT_FALSE(pipeline.RunProject(project, options, &observer, &cancellation));

  EXPECT_TRUE(validator.called);
  EXPECT_FALSE(generation.called);
  EXPECT_FALSE(output.called);
  ASSERT_EQ(observer.statuses.size(), 1U);
  EXPECT_EQ(observer.statuses[0], PipelineRunStatus::kCancelled);
}

TEST(PipelineTest, ObserverReportsFailedStatusOnValidationError) {
  const Project project = MakeProject(2);

  MockParser parser;
  MockValidator validator;
  validator.result.is_valid = false;
  validator.result.errors = {"bad config"};
  MockGeneration generation;
  MockOutput output;
  MockLogger logger;
  RecordingObserver observer;

  VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                              nullptr, &output, &logger);
  RunOptions options;
  EXPECT_FALSE(pipeline.RunProject(project, options, &observer));

  ASSERT_EQ(observer.statuses.size(), 1U);
  EXPECT_EQ(observer.statuses[0], PipelineRunStatus::kFailed);
}

TEST(PipelineTest, SequentialRunsFromWorkerThreadProduceIdenticalResults) {
  // Two sequential runs of the same project on a non-main thread must
  // produce the same stage outputs as each other and as a main-thread run.
  const Project project = MakeProject(6);
  RunOptions options;

  auto RunOnce = [&project, &options](std::vector<SampleFixed>* out_samples) {
    MockParser parser;
    MockValidator validator;
    validator.result.is_valid = true;
    MockGeneration generation;
    generation.embed_frame_id = true;
    MockOutput output;
    MockLogger logger;
    VideoSynthPipeline pipeline(&parser, &validator, &generation, nullptr,
                                nullptr, &output, &logger);
    const bool ok = pipeline.RunProject(project, options);
    *out_samples = output.appended_first_samples;
    return ok;
  };

  std::vector<SampleFixed> worker_first;
  std::vector<SampleFixed> worker_second;
  bool worker_first_ok = false;
  bool worker_second_ok = false;
  std::thread worker([&]() {
    worker_first_ok = RunOnce(&worker_first);
    worker_second_ok = RunOnce(&worker_second);
  });
  worker.join();

  std::vector<SampleFixed> main_thread_samples;
  ASSERT_TRUE(RunOnce(&main_thread_samples));

  EXPECT_TRUE(worker_first_ok);
  EXPECT_TRUE(worker_second_ok);
  EXPECT_EQ(worker_first, worker_second);
  EXPECT_EQ(worker_first, main_thread_samples);
}

}  // namespace
}  // namespace videosynth
