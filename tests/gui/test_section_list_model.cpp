/*
 * File:        test_section_list_model.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the section list model, templates, and the
 *              reorder/duplicate document operations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QItemSelectionModel>
#include <string>

#include "project_document.h"
#include "project_templates.h"
#include "section_list_model.h"
#include "videosynth/model.h"
#include "videosynth/project_validator.h"
#include "videosynth/yaml_project_emitter.h"

namespace videosynth::gui {
namespace {

Section MakeSection(const std::string& name, int duration_frames) {
  Section section;
  section.name = name;
  section.type = "progressive";
  section.source = "assets/" + name + ".exr";
  section.duration_frames = duration_frames;
  return section;
}

Project MakeProject() {
  Project project;
  project.name = "Test";
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.output.video_path = "out/video.composite";
  project.output.metadata_path = "out/video.meta";
  project.sections.push_back(MakeSection("First", 100));
  project.sections.push_back(MakeSection("Second", 50));
  project.sections.push_back(MakeSection("Third", 25));
  return project;
}

TEST(SectionListModelTest, RowsRecalculateStartFrames) {
  const std::vector<SectionListRow> rows = BuildSectionListRows(MakeProject());
  ASSERT_EQ(rows.size(), 3U);
  EXPECT_EQ(rows[0].start_frame, 0);
  EXPECT_EQ(rows[1].start_frame, 100);
  EXPECT_EQ(rows[2].start_frame, 150);
  EXPECT_EQ(rows[1].DurationText(), QStringLiteral("50 frames"));
}

TEST(SectionListModelTest, AllDurationRowShowsAllFrames) {
  Project project = MakeProject();
  project.sections[1].duration_frames_all = true;
  project.sections[1].duration_frames = 0;
  const std::vector<SectionListRow> rows = BuildSectionListRows(project);
  EXPECT_EQ(rows[1].DurationText(), QStringLiteral("all frames"));
}

TEST(SectionListModelTest, ModelTracksDocumentMutations) {
  ProjectDocument document;
  document.ResetProject(MakeProject(), QString());
  SectionListModel model(&document);

  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(
      model.data(model.index(0, SectionListModel::kNameColumn), Qt::DisplayRole)
          .toString(),
      QStringLiteral("First"));

  document.InsertSection(-1, MakeSection("Fourth", 10));
  ASSERT_EQ(model.rowCount(), 4);

  document.RemoveSection(0);
  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(
      model.data(model.index(0, SectionListModel::kNameColumn), Qt::DisplayRole)
          .toString(),
      QStringLiteral("Second"));

  document.MoveSection(0, 2);
  EXPECT_EQ(
      model.data(model.index(2, SectionListModel::kNameColumn), Qt::DisplayRole)
          .toString(),
      QStringLiteral("Second"));
  // Start frames recalculate after the move.
  EXPECT_EQ(model
                .data(model.index(0, SectionListModel::kStartFrameColumn),
                      Qt::DisplayRole)
                .toInt(),
            0);

  Section renamed = document.project().sections[0];
  renamed.name = "Renamed";
  document.SetSection(0, renamed);
  EXPECT_EQ(
      model.data(model.index(0, SectionListModel::kNameColumn), Qt::DisplayRole)
          .toString(),
      QStringLiteral("Renamed"));

  document.ResetProject(MakeProject(), QString());
  EXPECT_EQ(model.rowCount(), 3);
}

// Editing a section must not reset the model: a reset clears the dock's
// selection, which in turn closes the section editor (the newly-ticked block
// disappears until the section is re-selected).
TEST(SectionListModelTest, EditKeepsSelectionAndDoesNotReset) {
  ProjectDocument document;
  document.ResetProject(MakeProject(), QString());
  SectionListModel model(&document);

  QItemSelectionModel selection(&model);
  selection.setCurrentIndex(
      model.index(1, SectionListModel::kNameColumn),
      QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
  ASSERT_EQ(selection.currentIndex().row(), 1);

  Section edited = document.project().sections[1];
  edited.noise.enabled = true;
  edited.noise.noise_db = 48.0;
  document.SetSection(1, edited);

  // A model reset would invalidate the current index; an in-place refresh keeps
  // it, so the section editor stays open on the row just edited.
  EXPECT_TRUE(selection.currentIndex().isValid());
  EXPECT_EQ(selection.currentIndex().row(), 1);
}

// Reordering through the document must emit YAML identical to a hand-written
// project with the sections in the new order.
TEST(SectionListModelTest, ReorderEmitsYamlIdenticalToHandWritten) {
  ProjectDocument document;
  document.ResetProject(MakeProject(), QString());
  document.MoveSection(0, 2);

  Project expected = MakeProject();
  Section first = expected.sections[0];
  expected.sections.erase(expected.sections.begin());
  expected.sections.push_back(first);

  const YamlProjectEmitter emitter;
  EXPECT_EQ(emitter.EmitString(document.project()),
            emitter.EmitString(expected));
}

TEST(SectionListModelTest, DuplicateEmitsYamlIdenticalToHandWritten) {
  ProjectDocument document;
  document.ResetProject(MakeProject(), QString());
  const Project& project = document.project();
  document.InsertSection(
      1, MakeDuplicateSection(project.sections[0], project.sections));

  Project expected = MakeProject();
  Section copy = expected.sections[0];
  copy.name = "First (copy)";
  expected.sections.insert(expected.sections.begin() + 1, copy);

  const YamlProjectEmitter emitter;
  EXPECT_EQ(emitter.EmitString(document.project()),
            emitter.EmitString(expected));
}

TEST(SectionListModelTest, DuplicateNamesStayUnique) {
  Project project = MakeProject();
  const Section copy1 =
      MakeDuplicateSection(project.sections[0], project.sections);
  EXPECT_EQ(copy1.name, "First (copy)");
  project.sections.push_back(copy1);
  const Section copy2 =
      MakeDuplicateSection(project.sections[0], project.sections);
  EXPECT_EQ(copy2.name, "First (copy 2)");
}

// Every typed section template must produce a structurally valid project
// for its standard (warnings allowed; errors not).
TEST(SectionListModelTest, SectionTemplatesValidateStructurally) {
  for (const Standard standard : {Standard::kPal, Standard::kNtsc}) {
    Project project;
    project.cvbs_presets.video_standard_preset = standard;
    project.output.video_path = "out/video.composite";
    project.output.metadata_path = "out/video.meta";
    project.sections.push_back(MakeLaserdiscLeadInSectionTemplate(standard));
    project.sections.push_back(MakeLaserdiscProgrammeSectionTemplate(standard));
    project.sections.push_back(MakeLaserdiscLeadOutSectionTemplate(standard));
    project.sections.push_back(MakeProgressiveSectionTemplate(4));

    ProjectValidator validator;
    const ValidationResult result = validator.Validate(project);
    EXPECT_TRUE(result.is_valid)
        << "standard " << StandardToString(standard) << ": "
        << (result.errors.empty() ? "" : result.errors.front());
  }
}

}  // namespace
}  // namespace videosynth::gui
