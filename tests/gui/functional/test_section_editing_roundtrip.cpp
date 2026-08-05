/*
 * File:        test_section_editing_roundtrip.cpp
 * Module:      gui_tests
 * Purpose:     Functional round-trip: example projects opened through the
 *              document layer, edited trivially, and re-saved stay CLI-valid
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QString>
#include <filesystem>
#include <string>
#include <vector>

#include "fixture_paths.h"
#include "project_document.h"
#include "videosynth/model.h"
#include "videosynth/project_validator.h"
#include "videosynth/yaml_project_emitter.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth::gui {
namespace {

std::vector<std::string> ExampleProjectFiles() {
  std::vector<std::string> files;
  for (const std::filesystem::path& path : test_support::AllProjectFixtures()) {
    files.push_back(path.string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

// Every project fixture opened into the document layer, trivially edited the
// way the section editors do (rename through SetSection, project info through
// SetProjectInfo), then emitted and re-parsed, must still pass the same
// validation the CLI --validate path runs.
TEST(SectionEditingRoundTripTest, EditedExamplesStayCliValid) {
  const std::vector<std::string> files = ExampleProjectFiles();
  ASSERT_FALSE(files.empty());

  for (const std::string& file : files) {
    SCOPED_TRACE(file);

    YamlProjectParser parser;
    ParseResult parsed = parser.ParseFile(file);
    ASSERT_TRUE(parsed.ok) << (parsed.errors.empty() ? ""
                                                     : parsed.errors.front());

    ProjectDocument document;
    document.ResetProject(std::move(parsed.project),
                          QString::fromStdString(file));

    // Trivial edits through the same commands the editors use.
    document.SetProjectInfo(document.project().name + " (edited)",
                            document.project().version,
                            document.project().description);
    ASSERT_GT(document.section_count(), 0);
    Section section = document.project().sections[0];
    section.name += " (edited)";
    document.SetSection(0, section);
    EXPECT_TRUE(document.is_modified());

    // Save → reload → validate (the CLI --validate path without a probe).
    const YamlProjectEmitter emitter;
    const std::string yaml = emitter.EmitString(document.project());
    const ParseResult reparsed = parser.ParseString(yaml);
    ASSERT_TRUE(reparsed.ok)
        << (reparsed.errors.empty() ? "" : reparsed.errors.front());
    EXPECT_EQ(reparsed.project, document.project());

    ProjectValidator validator;
    const ValidationResult result = validator.Validate(reparsed.project);
    EXPECT_TRUE(result.is_valid)
        << (result.errors.empty() ? "" : result.errors.front());
  }
}

}  // namespace
}  // namespace videosynth::gui
