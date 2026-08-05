/*
 * File:        test_noise_injection.cpp
 * Module:      noise_injection_tests
 * Purpose:     Validates YAML parsing, project validation, and statistical
 *              correctness of NoiseInjectionStage.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "videosynth/fixed_point.h"
#include "videosynth/interfaces.h"
#include "videosynth/model.h"
#include "videosynth/noise_injection_stage.h"
#include "videosynth/project_validator.h"
#include "videosynth/timing_constants.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// Shared YAML helpers
// ---------------------------------------------------------------------------

const std::string kMinimalYamlPrefix =
    "project:\n"
    "  name: NoiseTest\n"
    "  version: \"1.0\"\n"
    "cvbs_presets:\n"
    "  video_standard_preset: PAL\n"
    "  sample_encoding_preset: CVBS_U10_4FSC\n"
    "  signal_state_preset: STANDARD_TBC_LOCKED\n"
    "output:\n"
    "  video_path: out.cvbs\n";

// ---------------------------------------------------------------------------
// Parser tests
// ---------------------------------------------------------------------------

TEST(NoiseInjectionTest, ParserNoNoiseSectionHasDisabledNoise) {
  const std::string yaml = kMinimalYamlPrefix +
                           "sections:\n"
                           "  - name: S\n"
                           "    type: progressive\n"
                           "    source: f.exr\n"
                           "    duration_frames: 8\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  EXPECT_FALSE(result.project.sections[0].noise.enabled);
}

TEST(NoiseInjectionTest, ParserNoiseDbOnlyEnablesNoise) {
  const std::string yaml = kMinimalYamlPrefix +
                           "sections:\n"
                           "  - name: S\n"
                           "    type: progressive\n"
                           "    source: f.exr\n"
                           "    duration_frames: 8\n"
                           "    noise:\n"
                           "      noise_db: 48.0\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  const NoiseParameters& noise = result.project.sections[0].noise;
  EXPECT_TRUE(noise.enabled);
  EXPECT_DOUBLE_EQ(noise.noise_db, 48.0);
  EXPECT_DOUBLE_EQ(noise.noise_spread_db, 0.0);
}

TEST(NoiseInjectionTest, ParserNoiseDbAndSpreadParsedCorrectly) {
  const std::string yaml = kMinimalYamlPrefix +
                           "sections:\n"
                           "  - name: S\n"
                           "    type: progressive\n"
                           "    source: f.exr\n"
                           "    duration_frames: 8\n"
                           "    noise:\n"
                           "      noise_db: 52.0\n"
                           "      noise_spread_db: 4.0\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  const NoiseParameters& noise = result.project.sections[0].noise;
  EXPECT_TRUE(noise.enabled);
  EXPECT_DOUBLE_EQ(noise.noise_db, 52.0);
  EXPECT_DOUBLE_EQ(noise.noise_spread_db, 4.0);
}

TEST(NoiseInjectionTest, ParserRejectsSpreadWithoutNoiseDb) {
  const std::string yaml = kMinimalYamlPrefix +
                           "sections:\n"
                           "  - name: S\n"
                           "    type: progressive\n"
                           "    source: f.exr\n"
                           "    duration_frames: 8\n"
                           "    noise:\n"
                           "      noise_spread_db: 4.0\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("noise_spread_db"), std::string::npos);
}

TEST(NoiseInjectionTest, ParserNoiseSeedParsedCorrectly) {
  const std::string yaml = kMinimalYamlPrefix +
                           "sections:\n"
                           "  - name: S\n"
                           "    type: progressive\n"
                           "    source: f.exr\n"
                           "    duration_frames: 8\n"
                           "    noise:\n"
                           "      noise_db: 48.0\n"
                           "      noise_seed: 12345\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(result.project.sections.size(), 1U);
  const NoiseParameters& noise = result.project.sections[0].noise;
  EXPECT_TRUE(noise.noise_seed_specified);
  EXPECT_EQ(noise.noise_seed, 12345);
}

TEST(NoiseInjectionTest, ParserRejectsUnknownNoiseKey) {
  const std::string yaml = kMinimalYamlPrefix +
                           "sections:\n"
                           "  - name: S\n"
                           "    type: progressive\n"
                           "    source: f.exr\n"
                           "    duration_frames: 8\n"
                           "    noise:\n"
                           "      noise_db: 48.0\n"
                           "      unknown_key: 1.0\n";

  YamlProjectParser parser;
  const ParseResult result = parser.ParseString(yaml);

  EXPECT_FALSE(result.ok);
}

// ---------------------------------------------------------------------------
// Validator tests (noise-specific rules)
// ---------------------------------------------------------------------------

// Probe is not needed for noise validation tests — pass nullptr to skip the
// progressive source profile checks, matching the pattern used in
// test_project_validator.cpp for configuration-only assertions.
Project MakeValidNoiseProject(double noise_db, double noise_spread_db) {
  Project project;
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  project.output.video_path = "/tmp/noise_test.cvbs";
  project.output.metadata_path = "/tmp/noise_test.meta";

  Section section;
  section.name = "Noisy";
  section.type = "progressive";
  section.source = "f.exr";
  section.duration_frames = 8;
  section.noise.enabled = true;
  section.noise.noise_db = noise_db;
  section.noise.noise_spread_db = noise_spread_db;

  project.sections.push_back(section);
  return project;
}

TEST(NoiseInjectionTest, ValidatorAcceptsValidNoiseDb) {
  ProjectValidator validator;
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(48.0, 0.0));
  EXPECT_TRUE(result.is_valid);
}

TEST(NoiseInjectionTest, ValidatorAcceptsValidNoiseDbAndSpread) {
  ProjectValidator validator;
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(48.0, 4.0));
  EXPECT_TRUE(result.is_valid);
}

TEST(NoiseInjectionTest, ValidatorRejectsNoiseDbTooLow) {
  ProjectValidator validator;
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(19.9, 0.0));
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("noise_db"), std::string::npos);
}

TEST(NoiseInjectionTest, ValidatorRejectsNoiseDbTooHigh) {
  ProjectValidator validator;
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(61.1, 0.0));
  EXPECT_FALSE(result.is_valid);
}

TEST(NoiseInjectionTest, ValidatorRejectsNegativeNoiseSpread) {
  ProjectValidator validator;
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(48.0, -1.0));
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("noise_spread_db"), std::string::npos);
}

TEST(NoiseInjectionTest, ValidatorRejectsSpreadDrivingWhiteSnrBelowFloor) {
  ProjectValidator validator;
  // noise_db=30.0, spread=11.0 → white_snr=19.0 < 20.0
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(30.0, 11.0));
  EXPECT_FALSE(result.is_valid);
  ASSERT_FALSE(result.errors.empty());
  EXPECT_NE(result.errors[0].find("White SNR floor"), std::string::npos);
}

TEST(NoiseInjectionTest, ValidatorWarnsWhenNoVitsForWhiteSnrCheck) {
  ProjectValidator validator;
  // Valid noise_spread_db > 0 but no VITS on line 19 → warning
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(48.0, 4.0));
  EXPECT_TRUE(result.is_valid);
  ASSERT_FALSE(result.warnings.empty());
  EXPECT_NE(result.warnings[0].find("White SNR"), std::string::npos);
}

TEST(NoiseInjectionTest, ValidatorNoWarnWhenVitsOnWhiteSnrLine) {
  ProjectValidator validator;
  Project project = MakeValidNoiseProject(48.0, 4.0);
  // Add a project-wide VITS injection on PAL line 19 to suppress the warning.
  project.line_injections.vits.push_back(VitsInjection{"uk-national", {19}});

  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.warnings.empty());
}

TEST(NoiseInjectionTest, ValidatorNoWarnWhenSpreadIsZero) {
  ProjectValidator validator;
  // spread=0 → white SNR check not applicable, no warning expected
  const ValidationResult result =
      validator.Validate(MakeValidNoiseProject(48.0, 0.0));
  EXPECT_TRUE(result.is_valid);
  EXPECT_TRUE(result.warnings.empty());
}

// ---------------------------------------------------------------------------
// NoiseInjectionStage unit tests
// ---------------------------------------------------------------------------

class NullLogger final : public ILogger {
 public:
  void Info(const std::string&) override {}
  void Warning(const std::string&) override {}
  void Error(const std::string&) override {}
  void Debug(const std::string&) override {}
  void Trace(const std::string&) override {}
};

// Constructs a minimal project and a one-frame schedule for the given section.
struct TestContext {
  Project project;
  std::vector<IGenerationStage::FrameScheduleItem> schedule;
};

TestContext MakeTestContext(Standard standard, bool noise_enabled,
                            double noise_db, double noise_spread_db) {
  TestContext ctx;
  ctx.project.cvbs_presets.video_standard_preset = standard;
  ctx.project.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  ctx.project.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  ctx.project.output.video_path = "/tmp/noise_stage_test.cvbs";
  ctx.project.output.metadata_path = "/tmp/noise_stage_test.meta";

  Section section;
  section.name = "Test";
  section.type = "progressive";
  section.source = "f.exr";
  section.duration_frames = 1;
  section.noise.enabled = noise_enabled;
  section.noise.noise_db = noise_db;
  section.noise.noise_spread_db = noise_spread_db;
  ctx.project.sections.push_back(section);

  IGenerationStage::FrameScheduleItem item;
  item.section = &ctx.project.sections[0];
  item.source_frame_index = 0;
  ctx.schedule.push_back(item);

  return ctx;
}

TEST(NoiseInjectionTest, DisabledNoiseDoesNotChangeBuffers) {
  NullLogger logger;
  NoiseInjectionStage stage(&logger);

  auto ctx = MakeTestContext(Standard::kPal, false, 48.0, 0.0);
  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  std::vector<SampleFixed> y(n, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(n, MillivoltsToSampleFixed(0.0));
  const std::vector<SampleFixed> y_orig = y;
  const std::vector<SampleFixed> c_orig = c;

  stage.InjectNoise(ctx.project, ctx.schedule, 0, 1, &y, &c);

  EXPECT_EQ(y, y_orig);
  EXPECT_EQ(c, c_orig);
}

// Computes the sample standard deviation of a vector of SampleFixed values
// converted to millivolts.
double ComputeStdDevMv(const std::vector<SampleFixed>& samples,
                       double reference_mv) {
  if (samples.empty()) {
    return 0.0;
  }
  double sum_sq = 0.0;
  for (SampleFixed s : samples) {
    const double delta = SampleFixedToMillivolts(s) - reference_mv;
    sum_sq += delta * delta;
  }
  return std::sqrt(sum_sq / static_cast<double>(samples.size()));
}

TEST(NoiseInjectionTest, FloorNoiseStdDevMatchesTargetAtBlanking) {
  NullLogger logger;
  NoiseInjectionStage stage(&logger);

  // PAL, noise_db=40, spread=0 → sigma_f_ire=100/100=1 IRE at 40 dB,
  // sigma_f_mV=1*7.0=7.0 mV
  constexpr double kNoiseDd = 40.0;
  auto ctx = MakeTestContext(Standard::kPal, true, kNoiseDd, 0.0);

  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  // Blanking level: 0 mV → proportional component is zero.
  std::vector<SampleFixed> y(n, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(n, MillivoltsToSampleFixed(0.0));

  stage.InjectNoise(ctx.project, ctx.schedule, 0, 1, &y, &c);

  // sigma_f_ire = 100 / 10^(40/20) = 100 / 100 = 1 IRE
  // sigma_f_mV  = 1 * (700/100) = 7.0 mV
  const double expected_sigma_mv = 7.0;
  const double measured_y = ComputeStdDevMv(y, 0.0);
  const double measured_c = ComputeStdDevMv(c, 0.0);

  // Accept ±5 % at ~709 k samples.
  EXPECT_NEAR(measured_y, expected_sigma_mv, expected_sigma_mv * 0.05);
  EXPECT_NEAR(measured_c, expected_sigma_mv, expected_sigma_mv * 0.05);
}

TEST(NoiseInjectionTest, YAndCNoiseAreEqualPerSampleAtBlanking) {
  NullLogger logger;
  NoiseInjectionStage stage(&logger);

  auto ctx = MakeTestContext(Standard::kPal, true, 40.0, 0.0);

  // Use a small count — just verify sample-level equality (correlated noise).
  constexpr std::size_t kSamples = 1024;
  std::vector<SampleFixed> y(kSamples, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c(kSamples, MillivoltsToSampleFixed(0.0));

  // Override schedule so only one "frame" with exactly kSamples is processed.
  // We exploit that InjectNoise uses samples_per_frame from the project preset,
  // so we directly verify the correlated property via a full PAL frame instead.
  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  std::vector<SampleFixed> y_full(n, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> c_full(n, MillivoltsToSampleFixed(0.0));

  stage.InjectNoise(ctx.project, ctx.schedule, 0, 1, &y_full, &c_full);

  // Every sample must have the same noise added to Y and C.
  for (std::size_t i = 0; i < n; ++i) {
    ASSERT_EQ(y_full[i], c_full[i]) << "Mismatch at sample " << i;
  }
}

TEST(NoiseInjectionTest, ProportionalNoiseIncreasesNoiseAtWhiteLevel) {
  NullLogger logger;
  NoiseInjectionStage stage(&logger);

  // PAL, noise_db=48, spread=4 → white_snr_target=44
  // sigma_f_mV = 100/10^(48/20) * 7.0 = 100/251.2 * 7.0 ≈ 2.786 mV
  // sigma_w_ire = 100/10^(44/20) = 100/158.5 ≈ 0.631 IRE
  // sigma_w_mV = 0.631 * 7.0 ≈ 4.416 mV
  auto ctx = MakeTestContext(Standard::kPal, true, 48.0, 4.0);

  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  // Set all Y samples to white level (700 mV) to drive proportional component.
  std::vector<SampleFixed> y(n, MillivoltsToSampleFixed(700.0));
  std::vector<SampleFixed> c(n, MillivoltsToSampleFixed(0.0));

  stage.InjectNoise(ctx.project, ctx.schedule, 0, 1, &y, &c);

  // Expected sigma_total at white: sqrt(sigma_f^2 + (k*700)^2) = sigma_w_mV
  const double sigma_w_mv =
      (100.0 / std::pow(10.0, 44.0 / 20.0)) * (700.0 / 100.0);
  // After noise injection the Y distribution has mean ≈ 700 mV.
  const double measured_y = ComputeStdDevMv(y, 700.0);
  EXPECT_NEAR(measured_y, sigma_w_mv, sigma_w_mv * 0.05);
}

TEST(NoiseInjectionTest, OutputIsDeterministicForSameFrameAndSection) {
  // Within a single run, the same (frame, section) always produces the same
  // noise regardless of how many times InjectNoise is called.
  NullLogger logger;
  NoiseInjectionStage stage(&logger);

  auto ctx = MakeTestContext(Standard::kPal, true, 48.0, 4.0);

  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  std::vector<SampleFixed> y1(n, MillivoltsToSampleFixed(350.0));
  std::vector<SampleFixed> c1(n, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> y2 = y1;
  std::vector<SampleFixed> c2 = c1;

  stage.InjectNoise(ctx.project, ctx.schedule, 0, 1, &y1, &c1);
  stage.InjectNoise(ctx.project, ctx.schedule, 0, 1, &y2, &c2);

  EXPECT_EQ(y1, y2);
  EXPECT_EQ(c1, c2);
}

TEST(NoiseInjectionTest, FixedSeedProducesSameOutputAcrossInstances) {
  // Two separately constructed stages with the same noise_seed produce
  // identical output — the user-supplied seed overrides the random base seed.
  NullLogger logger;

  auto ctx1 = MakeTestContext(Standard::kPal, true, 48.0, 4.0);
  ctx1.project.sections[0].noise.noise_seed = 99999;
  ctx1.project.sections[0].noise.noise_seed_specified = true;
  ctx1.schedule[0].section = &ctx1.project.sections[0];

  auto ctx2 = MakeTestContext(Standard::kPal, true, 48.0, 4.0);
  ctx2.project.sections[0].noise.noise_seed = 99999;
  ctx2.project.sections[0].noise.noise_seed_specified = true;
  ctx2.schedule[0].section = &ctx2.project.sections[0];

  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  std::vector<SampleFixed> y1(n, MillivoltsToSampleFixed(350.0));
  std::vector<SampleFixed> c1(n, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> y2 = y1;
  std::vector<SampleFixed> c2 = c1;

  NoiseInjectionStage stage1(&logger);
  NoiseInjectionStage stage2(&logger);

  stage1.InjectNoise(ctx1.project, ctx1.schedule, 0, 1, &y1, &c1);
  stage2.InjectNoise(ctx2.project, ctx2.schedule, 0, 1, &y2, &c2);

  EXPECT_EQ(y1, y2);
  EXPECT_EQ(c1, c2);
}

TEST(NoiseInjectionTest, RandomSeedProducesDifferentOutputAcrossInstances) {
  // Two separately constructed stages without a fixed noise_seed should
  // produce different output (each captures a distinct random_device seed).
  // The probability of a false failure is 1/2^64 per sample — negligible.
  NullLogger logger;

  auto ctx1 = MakeTestContext(Standard::kPal, true, 48.0, 4.0);
  auto ctx2 = MakeTestContext(Standard::kPal, true, 48.0, 4.0);

  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  std::vector<SampleFixed> y1(n, MillivoltsToSampleFixed(350.0));
  std::vector<SampleFixed> c1(n, MillivoltsToSampleFixed(0.0));
  std::vector<SampleFixed> y2 = y1;
  std::vector<SampleFixed> c2 = c1;

  NoiseInjectionStage stage1(&logger);
  NoiseInjectionStage stage2(&logger);

  stage1.InjectNoise(ctx1.project, ctx1.schedule, 0, 1, &y1, &c1);
  stage2.InjectNoise(ctx2.project, ctx2.schedule, 0, 1, &y2, &c2);

  EXPECT_NE(y1, y2);
}

TEST(NoiseInjectionTest, ClampingKeepsOutputWithinLegalRange) {
  NullLogger logger;
  NoiseInjectionStage stage(&logger);

  // Use maximum noise (20 dB → sigma ≈ 70 mV) to stress the clamping path.
  auto ctx = MakeTestContext(Standard::kPal, true, 20.0, 0.0);

  const std::size_t n =
      static_cast<std::size_t>(SamplesPerFrame4fsc(Standard::kPal));
  // Set Y to white (700 mV) and C to white to stress upper clamp.
  std::vector<SampleFixed> y(n, MillivoltsToSampleFixed(700.0));
  std::vector<SampleFixed> c(n, MillivoltsToSampleFixed(700.0));

  stage.InjectNoise(ctx.project, ctx.schedule, 0, 1, &y, &c);

  // PAL legal range: approx -300.0 to +908.5 mV.
  const SampleFixed min_legal = MillivoltsToSampleFixed(-300.1);
  const SampleFixed max_legal = MillivoltsToSampleFixed(908.5);

  for (std::size_t i = 0; i < n; ++i) {
    ASSERT_GE(y[i], min_legal) << "Y sample " << i << " below legal floor";
    ASSERT_LE(y[i], max_legal) << "Y sample " << i << " above legal ceiling";
    ASSERT_GE(c[i], min_legal) << "C sample " << i << " below legal floor";
    ASSERT_LE(c[i], max_legal) << "C sample " << i << " above legal ceiling";
  }
}

}  // namespace
}  // namespace videosynth
