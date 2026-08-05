/*
 * File:        test_yaml_project_fixtures.cpp
 * Module:      yaml_project_emitter_tests
 * Purpose:     Round-trips every committed and generated project fixture
 *              through parse -> emit -> parse and cross-checks validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "fixture_paths.h"
#include "videosynth/project_validator.h"
#include "videosynth/results.h"
#include "videosynth/yaml_project_emitter.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth {
namespace {

// Reads the project fixtures from disk, so classified functional.
TEST(YamlProjectEmitterFixturesTest, AllFixturesRoundTripToEqualProjects) {
  const std::vector<std::filesystem::path> files =
      test_support::AllProjectFixtures();
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
