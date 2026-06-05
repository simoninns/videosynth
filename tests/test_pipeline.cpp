/*
 * File:        test_pipeline.cpp
 * Module:      pipeline_tests
 * Purpose:     Validates pipeline control flow using deterministic mocks.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include "videosynth/pipeline.h"

namespace videosynth {
namespace {

class MockParser final : public IProjectParser {
 public:
  ParseResult result;
  std::string last_path;

  ParseResult ParseFile(const std::string& path) override {
    last_path = path;
    return result;
  }
};

class MockValidator final : public IProjectValidator {
 public:
  ValidationResult result;
  bool called = false;

  ValidationResult Validate(const Project&) override {
    called = true;
    return result;
  }
};

class MockGeneration final : public IGenerationStage {
 public:
  bool called = false;

  bool BuildFrameSchedule(const Project& project,
                          std::vector<FrameScheduleItem>* out_schedule,
                          std::vector<std::string>* errors) override {
    out_schedule->clear();
    out_schedule->push_back(FrameScheduleItem{.section = &project.sections[0],
                                              .source_frame_index = 0});
    errors->clear();
    return true;
  }

  bool GenerateFrameBatch(const Project&, const std::vector<FrameScheduleItem>&,
                          std::size_t, std::size_t,
                          std::vector<SampleFixed>* out_y_mv,
                          std::vector<SampleFixed>* out_c_mv,
                          std::vector<std::string>* errors) override {
    called = true;
    out_y_mv->assign(8, 0);
    out_c_mv->assign(8, 0);
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

  bool BeginWrite(const Project&, std::size_t,
                  std::vector<std::string>* errors) override {
    errors->clear();
    return true;
  }

  bool AppendSamples(const std::vector<SampleFixed>&,
                     const std::vector<SampleFixed>&,
                     std::vector<std::string>* errors) override {
    called = true;
    errors->clear();
    return true;
  }

  bool FinalizeWrite(std::vector<std::string>* errors) override {
    errors->clear();
    return true;
  }

  bool Write(const Project&, const std::vector<SampleFixed>&,
             const std::vector<SampleFixed>&,
             std::vector<std::string>* errors) override {
    called = true;
    errors->clear();
    return true;
  }
};

class MockLogger final : public ILogger {
 public:
  std::vector<std::string> infos;
  std::vector<std::string> errors;
  std::vector<std::string> debugs;
  std::vector<std::string> traces;

  void Info(const std::string& message) override { infos.push_back(message); }
  void Error(const std::string& message) override { errors.push_back(message); }
  void Debug(const std::string& message) override { debugs.push_back(message); }
  void Trace(const std::string& message) override { traces.push_back(message); }
};

Project MakeProject() {
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
                               .duration_frames = 1});
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

  VideoSynthPipeline pipeline(&parser, &validator, &generation, &output,
                              &logger);

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

  VideoSynthPipeline pipeline(&parser, &validator, &generation, &output,
                              &logger);

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

  VideoSynthPipeline pipeline(&parser, &validator, &generation, &output,
                              &logger);

  RunOptions options;
  options.project_path = "project.yaml";

  EXPECT_FALSE(pipeline.Run(options));
  EXPECT_FALSE(generation.called);
  EXPECT_FALSE(output.called);
  ASSERT_EQ(logger.errors.size(), 1U);
}

}  // namespace
}  // namespace videosynth
