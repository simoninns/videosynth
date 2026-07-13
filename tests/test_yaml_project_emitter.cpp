/*
 * File:        test_yaml_project_emitter.cpp
 * Module:      yaml_project_emitter_tests
 * Purpose:     Verifies YAML emission round-trips and canonical form.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "videosynth/model.h"
#include "videosynth/project_validator.h"
#include "videosynth/results.h"
#include "videosynth/yaml_project_emitter.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

// Parses `yaml`, emits the parsed project, re-parses the emission, and
// asserts the two parsed projects are equal and the emission is stable
// (emitting the re-parsed project reproduces the same text).
void ExpectRoundTrip(const std::string& yaml) {
  YamlProjectParser parser;
  YamlProjectEmitter emitter;

  const ParseResult first = parser.ParseString(yaml);
  ASSERT_TRUE(first.ok) << (first.errors.empty() ? "" : first.errors.front());

  const std::string emitted = emitter.EmitString(first.project);
  const ParseResult second = parser.ParseString(emitted);
  ASSERT_TRUE(second.ok) << "Re-parse failed for emitted YAML:\n"
                         << emitted << "\nError: "
                         << (second.errors.empty() ? ""
                                                   : second.errors.front());

  EXPECT_TRUE(first.project == second.project)
      << "Round-trip project mismatch. Emitted YAML:\n"
      << emitted;

  // Canonical stability: emit(parse(emit(p))) == emit(p).
  EXPECT_EQ(emitter.EmitString(second.project), emitted);
}

constexpr const char* kMinimalPalYaml = R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Bars
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
)";

TEST(YamlProjectEmitterTest, MinimalProjectRoundTripsToEqualProject) {
  ExpectRoundTrip(kMinimalPalYaml);
}

TEST(YamlProjectEmitterTest, MinimalProjectOmitsUnsetOptionalBlocks) {
  YamlProjectParser parser;
  const ParseResult parsed = parser.ParseString(kMinimalPalYaml);
  ASSERT_TRUE(parsed.ok);

  const std::string emitted = YamlProjectEmitter().EmitString(parsed.project);
  EXPECT_EQ(emitted.find("noise"), std::string::npos);
  EXPECT_EQ(emitted.find("dropouts"), std::string::npos);
  EXPECT_EQ(emitted.find("osd"), std::string::npos);
  EXPECT_EQ(emitted.find("audio"), std::string::npos);
  EXPECT_EQ(emitted.find("line_injections"), std::string::npos);
  EXPECT_EQ(emitted.find("disc_skips"), std::string::npos);
  EXPECT_EQ(emitted.find("signal_type"), std::string::npos);
  EXPECT_EQ(emitted.find("start_frame"), std::string::npos);
  EXPECT_EQ(emitted.find("ntsc_black_setup_ire"), std::string::npos);
}

TEST(YamlProjectEmitterTest, AssetRootTokenSourceRoundTrips) {
  const std::string yaml = R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Bars
    type: progressive
    source: "{bundled}/exr/720x576/75_BARS.exr"
    duration_frames: 10
)";
  ExpectRoundTrip(yaml);

  YamlProjectParser parser;
  const ParseResult parsed = parser.ParseString(yaml);
  ASSERT_TRUE(parsed.ok);
  EXPECT_EQ(parsed.project.sections[0].source,
            "{bundled}/exr/720x576/75_BARS.exr");
}

TEST(YamlProjectEmitterTest, MinimalProjectKeepsCanonicalTopLevelOrder) {
  YamlProjectParser parser;
  const ParseResult parsed = parser.ParseString(kMinimalPalYaml);
  ASSERT_TRUE(parsed.ok);

  const std::string emitted = YamlProjectEmitter().EmitString(parsed.project);
  const std::size_t presets_pos = emitted.find("cvbs_presets:");
  const std::size_t output_pos = emitted.find("output:");
  const std::size_t sections_pos = emitted.find("sections:");
  ASSERT_NE(presets_pos, std::string::npos);
  ASSERT_NE(output_pos, std::string::npos);
  ASSERT_NE(sections_pos, std::string::npos);
  EXPECT_LT(presets_pos, output_pos);
  EXPECT_LT(output_pos, sections_pos);
}

TEST(YamlProjectEmitterTest, ProjectMetadataRoundTripsNameVersionDescription) {
  ExpectRoundTrip(R"(
project:
  name: "Example PAL CVBS Output"
  version: "1.0"
  description: "A test output with colour bars and line injections"
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Bars
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
)");
}

TEST(YamlProjectEmitterTest, CvbsPresetsRoundTripsNtscSetupAndBurstFlags) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: NTSC
  sample_encoding_preset: CVBS_U16_4FSC
  signal_state_preset: STANDARD_TBC_LOCKED
  ntsc_black_setup_ire: 0.0
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Bars
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
)");
}

TEST(YamlProjectEmitterTest, PalPilotBurstRoundTrips) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
  sample_encoding_preset: CVBS_S16_FSC
  pal_laserdisc_pilot_burst: true
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Bars
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
)");
}

TEST(YamlProjectEmitterTest, YcOutputRoundTripsSignalType) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.y
  metadata_path: out/metadata.meta
  signal_type: yc
sections:
  - name: Bars
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
)");
}

TEST(YamlProjectEmitterTest, DurationFramesAllRoundTrips) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: WholeSource
    type: progressive
    source: assets/clip.mkv
    duration_frames: all
    start_frame: 5
)");
}

TEST(YamlProjectEmitterTest, VitsInjectionRoundTripsTargetLines) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Vits
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
    line_injections:
      - type: vits
        target_lines: [19, 332]
        vits_type: "pal_ccir330"
)");
}

TEST(YamlProjectEmitterTest, LaserdiscInjectionRoundTripsAllCodeFields) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Chapter0
    type: progressive
    source: assets/chapter0.mkv
    duration_frames: 500
    section_type: programme_area
    line_injections:
      - type: laserdisc
        disc_type: CAV
        codes:
          - code_type: picture_number
            start_value: 1
          - code_type: chapter_number
            chapter: 5
          - code_type: picture_stop
          - code_type: programme_status
            programme_status: "0x8DC000"
          - code_type: users_code
            users_code: "0x80D000"
)");
}

TEST(YamlProjectEmitterTest, SectionTypesRoundTripLeadInAndLeadOut) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: LeadIn
    type: progressive
    source: assets/bars.exr
    duration_frames: 938
    section_type: lead_in
  - name: Programme
    type: progressive
    source: assets/bars.exr
    duration_frames: 500
    section_type: programme_area
  - name: LeadOut
    type: progressive
    source: assets/bars.exr
    duration_frames: 1250
    section_type: lead_out
)");
}

TEST(YamlProjectEmitterTest, NoiseBlockRoundTripsLevelsAndSeed) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Noisy
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
    noise:
      noise_db: 48.5
      noise_spread_db: 4.25
      noise_seed: 1234567890123
  - name: SeedOnly
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
    noise:
      noise_seed: -7
)");
}

TEST(YamlProjectEmitterTest, DropoutBlocksRoundTripScalesAndSeeds) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Damaged
    type: progressive
    source: assets/bars.exr
    duration_frames: 50
    dropouts:
      random:
        scale: 5
        seed: 42
      scratch:
        scale: 3
  - name: ScratchOnly
    type: progressive
    source: assets/bars.exr
    duration_frames: 50
    dropouts:
      scratch:
        scale: 20
        seed: -99
)");
}

TEST(YamlProjectEmitterTest, OsdOverlaysRoundTripTokensAndGeometry) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Osd
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
    osd:
      overlays:
        - text: "{picture_number} {biphase_hex}"
          x: 32
          y: 48
          scale: 2
          fg_luma: 0.75
          bg_luma: 0.125
        - text: "plain"
)");
}

TEST(YamlProjectEmitterTest, AudioBlocksRoundTripFixedToneAndRamp) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: FixedTone
    type: progressive
    source: assets/bars.exr
    duration_frames: 8
    audio:
      waveform: sine
      frequency: 997.5
      amplitude: 0.25
  - name: DefaultsTone
    type: progressive
    source: assets/bars.exr
    duration_frames: 8
    audio:
      frequency: 1000.0
  - name: Sweep
    type: progressive
    source: assets/bars.exr
    duration_frames: 8
    audio:
      waveform: sawtooth
      ramp:
        start: 200.0
        end: 4000.0
        mode: bounce
        period: 0.5
)");
}

TEST(YamlProjectEmitterTest, DiscSkipsRoundTripBothDirections) {
  ExpectRoundTrip(R"(
cvbs_presets:
  video_standard_preset: PAL
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Bars
    type: progressive
    source: assets/bars.exr
    duration_frames: 100
disc_skips:
  - at_frame: 5
    direction: forward
    count: 2
  - at_frame: 20
    direction: backward
    count: 4
)");
}

TEST(YamlProjectEmitterTest, EmittedProjectValidatesCleanly) {
  YamlProjectParser parser;
  const ParseResult parsed = parser.ParseString(R"(
project:
  name: ValidProject
cvbs_presets:
  video_standard_preset: PAL
  sample_encoding_preset: CVBS_U10_4FSC
  signal_state_preset: STANDARD_TBC_LOCKED
output:
  video_path: out/video.composite
  metadata_path: out/metadata.meta
sections:
  - name: Bars
    type: progressive
    source: assets/bars.exr
    duration_frames: 10
    noise:
      noise_db: 48.0
)");
  ASSERT_TRUE(parsed.ok);

  const std::string emitted = YamlProjectEmitter().EmitString(parsed.project);
  const ParseResult reparsed = parser.ParseString(emitted);
  ASSERT_TRUE(reparsed.ok);

  ProjectValidator validator;
  const ValidationResult validation = validator.Validate(reparsed.project);
  EXPECT_TRUE(validation.is_valid)
      << (validation.errors.empty() ? "" : validation.errors.front());
}

// Functional suite: round-trips every project YAML shipped in docs/examples/
// and tests/projects/ through parse -> emit -> parse and cross-checks
// validation outcomes. Touches the filesystem, so classified functional.
class YamlProjectEmitterFixturesTest : public ::testing::Test {
 protected:
  static std::vector<std::filesystem::path> CollectProjectFiles() {
    const std::filesystem::path source_dir(VIDEOSYNTH_SOURCE_DIR);
    std::vector<std::filesystem::path> files;

    for (const auto& entry : std::filesystem::directory_iterator(
             source_dir / "docs" / "examples")) {
      if (entry.path().extension() == ".yaml") {
        files.push_back(entry.path());
      }
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             source_dir / "tests" / "projects")) {
      if (entry.path().extension() == ".yaml") {
        files.push_back(entry.path());
      }
    }
    return files;
  }
};

TEST_F(YamlProjectEmitterFixturesTest, AllFixturesRoundTripToEqualProjects) {
  const std::vector<std::filesystem::path> files = CollectProjectFiles();
  ASSERT_FALSE(files.empty());

  YamlProjectParser parser;
  YamlProjectEmitter emitter;
  ProjectValidator validator;

  for (const std::filesystem::path& file : files) {
    SCOPED_TRACE(file.string());

    const ParseResult original = parser.ParseFile(file.string());
    ASSERT_TRUE(original.ok)
        << (original.errors.empty() ? "" : original.errors.front());

    const std::string emitted = emitter.EmitString(original.project);
    const ParseResult reparsed = parser.ParseString(emitted);
    ASSERT_TRUE(reparsed.ok)
        << "Re-parse failed. Emitted YAML:\n"
        << emitted << "\nError: "
        << (reparsed.errors.empty() ? "" : reparsed.errors.front());

    EXPECT_TRUE(original.project == reparsed.project)
        << "Round-trip mismatch. Emitted YAML:\n"
        << emitted;

    // Canonical stability of the emitted form.
    EXPECT_EQ(emitter.EmitString(reparsed.project), emitted);

    // The emitted file must be at least as valid as the source file
    // (structural validation without source probing).
    const ValidationResult original_validation =
        validator.Validate(original.project);
    const ValidationResult reparsed_validation =
        validator.Validate(reparsed.project);
    EXPECT_EQ(reparsed_validation.is_valid, original_validation.is_valid);
    EXPECT_EQ(reparsed_validation.errors, original_validation.errors);
  }
}

}  // namespace
}  // namespace videosynth
