/*
 * File:        test_project_file_lifecycle.cpp
 * Module:      gui_tests
 * Purpose:     Functional tests for document-driven open/save round-trips
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QString>
#include <QTemporaryDir>
#include <filesystem>
#include <string>
#include <vector>

#include "project_document.h"
#include "project_templates.h"
#include "videosynth/project_validator.h"
#include "videosynth/results.h"
#include "videosynth/yaml_project_emitter.h"
#include "videosynth/yaml_project_parser.h"

namespace videosynth::gui {
namespace {

std::vector<std::filesystem::path> ExampleProjectFiles() {
  const std::filesystem::path examples_dir =
      std::filesystem::path(VIDEOSYNTH_SOURCE_DIR) / "docs" / "examples";
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(examples_dir)) {
    if (entry.path().extension() == ".yaml") {
      files.push_back(entry.path());
    }
  }
  return files;
}

class ProjectFileLifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.isValid()); }

  QTemporaryDir temp_dir_;
};

// Mirrors the GUI's Open -> edit-free -> Save As flow over every shipped
// example: the re-saved file must parse to the same project and pass the
// same structural validation the CLI --validate path applies.
TEST_F(ProjectFileLifecycleTest, OpenAndResaveEveryExampleStaysValid) {
  const std::vector<std::filesystem::path> files = ExampleProjectFiles();
  ASSERT_FALSE(files.empty());

  YamlProjectParser parser;
  YamlProjectEmitter emitter;
  ProjectValidator validator;

  for (const std::filesystem::path& file : files) {
    SCOPED_TRACE(file.string());

    // Open into the document (what MainWindow::LoadProjectFromFile does).
    ParseResult opened = parser.ParseFile(file.string());
    ASSERT_TRUE(opened.ok) << (opened.errors.empty() ? ""
                                                     : opened.errors.front());

    ProjectDocument document;
    document.ResetProject(opened.project,
                          QString::fromStdString(file.string()));
    EXPECT_FALSE(document.is_modified());

    // Save As to a temporary path (what MainWindow::SaveToPath does).
    const std::string saved_path =
        temp_dir_.filePath(QString::fromStdString(file.filename().string()))
            .toStdString();
    std::string error;
    ASSERT_TRUE(emitter.EmitFile(document.project(), saved_path, &error))
        << error;
    document.MarkSaved(QString::fromStdString(saved_path));
    EXPECT_FALSE(document.is_modified());

    // The saved file must load back to an equal project and validate
    // exactly as the original does.
    const ParseResult reloaded = parser.ParseFile(saved_path);
    ASSERT_TRUE(reloaded.ok)
        << (reloaded.errors.empty() ? "" : reloaded.errors.front());
    EXPECT_TRUE(reloaded.project == opened.project);

    const ValidationResult original_validation =
        validator.Validate(opened.project);
    const ValidationResult reloaded_validation =
        validator.Validate(reloaded.project);
    EXPECT_EQ(reloaded_validation.is_valid, original_validation.is_valid);
    EXPECT_EQ(reloaded_validation.errors, original_validation.errors);
  }
}

TEST_F(ProjectFileLifecycleTest, EditedDocumentSavesAndReloads) {
  ProjectDocument document;
  document.ResetProject(MakeDefaultPalProject(), QString());

  ASSERT_TRUE(
      document.SetProjectInfo("Edited", "2.0", "Edited in the lifecycle test"));
  ASSERT_TRUE(document.is_modified());

  const std::string saved_path =
      temp_dir_.filePath(QStringLiteral("edited.yaml")).toStdString();
  std::string error;
  ASSERT_TRUE(
      YamlProjectEmitter().EmitFile(document.project(), saved_path, &error))
      << error;
  document.MarkSaved(QString::fromStdString(saved_path));
  EXPECT_FALSE(document.is_modified());

  const ParseResult reloaded = YamlProjectParser().ParseFile(saved_path);
  ASSERT_TRUE(reloaded.ok);
  EXPECT_TRUE(reloaded.project == document.project());
  EXPECT_EQ(reloaded.project.name, "Edited");
}

TEST_F(ProjectFileLifecycleTest, NewProjectTemplateIsStructurallyValid) {
  const Project project = MakeDefaultPalProject();

  ProjectValidator validator;
  const ValidationResult result = validator.Validate(project);
  EXPECT_TRUE(result.is_valid)
      << (result.errors.empty() ? "" : result.errors.front());

  // And it survives a save/load cycle.
  const std::string saved_path =
      temp_dir_.filePath(QStringLiteral("new_project.yaml")).toStdString();
  std::string error;
  ASSERT_TRUE(YamlProjectEmitter().EmitFile(project, saved_path, &error))
      << error;
  const ParseResult reloaded = YamlProjectParser().ParseFile(saved_path);
  ASSERT_TRUE(reloaded.ok);
  EXPECT_TRUE(reloaded.project == project);
}

}  // namespace
}  // namespace videosynth::gui
