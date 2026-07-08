/*
 * File:        test_validation_issues_model.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for issue building and the issues list model
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "validation_issues_model.h"
#include "videosynth/model.h"
#include "videosynth/results.h"

namespace videosynth::gui {
namespace {

Project MakeTwoSectionProject() {
  Project project;
  Section first;
  first.name = "Bars";
  project.sections.push_back(first);
  Section second;
  second.name = "Ramp";
  project.sections.push_back(second);
  return project;
}

TEST(ValidationIssuesModelTest, BuildMapsErrorsAndWarningsWithSeverity) {
  ValidationResult result;
  result.errors.push_back("Project configuration error: broken.");
  result.warnings.push_back("Something looks odd.");

  const std::vector<ValidationIssue> issues =
      BuildValidationIssues(MakeTwoSectionProject(), result);

  ASSERT_EQ(issues.size(), 2U);
  EXPECT_EQ(issues[0].severity, ValidationIssue::Severity::kError);
  EXPECT_EQ(issues[0].message,
            QStringLiteral("Project configuration error: broken."));
  EXPECT_EQ(issues[1].severity, ValidationIssue::Severity::kWarning);
}

TEST(ValidationIssuesModelTest, BuildAttributesSectionByQuotedName) {
  ValidationResult result;
  result.errors.push_back("Noise validation error in section 'Ramp': bad.");
  result.errors.push_back("Project configuration error: no section named.");

  const std::vector<ValidationIssue> issues =
      BuildValidationIssues(MakeTwoSectionProject(), result);

  ASSERT_EQ(issues.size(), 2U);
  EXPECT_EQ(issues[0].section_index, 1);
  EXPECT_EQ(issues[1].section_index, -1);
}

TEST(ValidationIssuesModelTest, ModelExposesRolesAndCounts) {
  ValidationResult result;
  result.errors.push_back("Error in section 'Bars': broken.");
  result.warnings.push_back("Warning one.");
  result.warnings.push_back("Warning two.");

  ValidationIssuesModel model;
  model.SetIssues(BuildValidationIssues(MakeTwoSectionProject(), result));

  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(model.error_count(), 1);
  EXPECT_EQ(model.warning_count(), 2);

  const QModelIndex first = model.index(0);
  EXPECT_EQ(first.data(Qt::DisplayRole).toString(),
            QStringLiteral("Error in section 'Bars': broken."));
  EXPECT_EQ(first.data(ValidationIssuesModel::kSeverityRole).toInt(),
            static_cast<int>(ValidationIssue::Severity::kError));
  EXPECT_EQ(first.data(ValidationIssuesModel::kSectionIndexRole).toInt(), 0);
}

TEST(ValidationIssuesModelTest, SetIssuesReplacesRows) {
  ValidationIssuesModel model;

  ValidationResult result;
  result.errors.push_back("Broken.");
  model.SetIssues(BuildValidationIssues(Project{}, result));
  ASSERT_EQ(model.rowCount(), 1);

  model.SetIssues({});
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(model.error_count(), 0);
  EXPECT_EQ(model.warning_count(), 0);
}

}  // namespace
}  // namespace videosynth::gui
