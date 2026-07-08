/*
 * File:        test_project_document.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for ProjectDocument mutations, signals, dirty state
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QObject>
#include <QString>
#include <string>
#include <vector>

#include "project_document.h"
#include "videosynth/model.h"

namespace videosynth::gui {
namespace {

Project MakeProject() {
  Project project;
  project.name = "TestProject";
  project.version = "1.0";
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.output.video_path = "out/video.composite";
  project.output.metadata_path = "out/metadata.meta";

  Section first;
  first.name = "First";
  first.type = "progressive";
  first.source = "assets/first.exr";
  first.duration_frames = 10;
  project.sections.push_back(first);

  Section second;
  second.name = "Second";
  second.type = "progressive";
  second.source = "assets/second.exr";
  second.duration_frames = 20;
  project.sections.push_back(second);

  return project;
}

// Counts every ProjectDocument signal emission for assertion convenience.
class SignalCounter {
 public:
  explicit SignalCounter(ProjectDocument* document) {
    QObject::connect(document, &ProjectDocument::ProjectSettingsChanged,
                     [this] { ++project_settings_changed; });
    QObject::connect(document, &ProjectDocument::SectionAdded,
                     [this](int index) {
                       ++section_added;
                       last_index = index;
                     });
    QObject::connect(document, &ProjectDocument::SectionRemoved,
                     [this](int index) {
                       ++section_removed;
                       last_index = index;
                     });
    QObject::connect(document, &ProjectDocument::SectionMoved,
                     [this](int from, int to) {
                       ++section_moved;
                       last_index = from;
                       last_to_index = to;
                     });
    QObject::connect(document, &ProjectDocument::SectionEdited,
                     [this](int index) {
                       ++section_edited;
                       last_index = index;
                     });
    QObject::connect(document, &ProjectDocument::DiscSkipsChanged,
                     [this] { ++disc_skips_changed; });
    QObject::connect(document, &ProjectDocument::DocumentChanged,
                     [this] { ++document_changed; });
    QObject::connect(document, &ProjectDocument::DocumentReset,
                     [this] { ++document_reset; });
    QObject::connect(document, &ProjectDocument::ModifiedStateChanged,
                     [this](bool modified) {
                       ++modified_state_changed;
                       last_modified = modified;
                     });
    QObject::connect(document, &ProjectDocument::FilePathChanged,
                     [this](const QString&) { ++file_path_changed; });
  }

  int project_settings_changed = 0;
  int section_added = 0;
  int section_removed = 0;
  int section_moved = 0;
  int section_edited = 0;
  int disc_skips_changed = 0;
  int document_changed = 0;
  int document_reset = 0;
  int modified_state_changed = 0;
  int file_path_changed = 0;
  int last_index = -100;
  int last_to_index = -100;
  bool last_modified = false;
};

class ProjectDocumentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    document_.ResetProject(MakeProject(), QStringLiteral("/tmp/test.yaml"));
    counter_ = std::make_unique<SignalCounter>(&document_);
  }

  ProjectDocument document_;
  std::unique_ptr<SignalCounter> counter_;
};

TEST_F(ProjectDocumentTest, ResetProjectClearsModifiedAndEmitsReset) {
  SignalCounter counter(&document_);
  document_.ResetProject(MakeProject(), QString());

  EXPECT_EQ(counter.document_reset, 1);
  EXPECT_EQ(counter.document_changed, 0);
  EXPECT_FALSE(document_.is_modified());
  EXPECT_TRUE(document_.file_path().isEmpty());
}

TEST_F(ProjectDocumentTest, DisplayNameUsesFileNameThenProjectName) {
  EXPECT_EQ(document_.display_name(), QStringLiteral("test.yaml"));

  document_.ResetProject(MakeProject(), QString());
  EXPECT_EQ(document_.display_name(), QStringLiteral("TestProject"));

  Project anonymous;
  document_.ResetProject(anonymous, QString());
  EXPECT_EQ(document_.display_name(), QStringLiteral("Untitled"));
}

TEST_F(ProjectDocumentTest, SetProjectInfoMutatesAndSignalsAndDirties) {
  EXPECT_TRUE(document_.SetProjectInfo("NewName", "2.0", "described"));

  EXPECT_EQ(document_.project().name, "NewName");
  EXPECT_EQ(document_.project().version, "2.0");
  EXPECT_EQ(document_.project().description, "described");
  EXPECT_EQ(counter_->project_settings_changed, 1);
  EXPECT_EQ(counter_->document_changed, 1);
  EXPECT_TRUE(document_.is_modified());
  EXPECT_EQ(counter_->modified_state_changed, 1);
  EXPECT_TRUE(counter_->last_modified);
}

TEST_F(ProjectDocumentTest, NoOpMutationsEmitNothingAndStayClean) {
  const Project& project = document_.project();
  EXPECT_FALSE(document_.SetProjectInfo(project.name, project.version,
                                        project.description));
  EXPECT_FALSE(document_.SetCvbsPresets(project.cvbs_presets));
  EXPECT_FALSE(document_.SetOutputTargets(project.output));
  EXPECT_FALSE(document_.SetDiscSkips(project.disc_skips));
  EXPECT_FALSE(document_.SetSection(0, project.sections[0]));

  EXPECT_EQ(counter_->document_changed, 0);
  EXPECT_FALSE(document_.is_modified());
  EXPECT_EQ(counter_->modified_state_changed, 0);
}

TEST_F(ProjectDocumentTest, SetCvbsPresetsEmitsProjectSettingsChanged) {
  CvbsPresets presets = document_.project().cvbs_presets;
  presets.video_standard_preset = Standard::kNtsc;
  presets.ntsc_black_setup_ire = 0.0;
  presets.ntsc_black_setup_ire_specified = true;

  EXPECT_TRUE(document_.SetCvbsPresets(presets));
  EXPECT_TRUE(document_.project().cvbs_presets == presets);
  EXPECT_EQ(counter_->project_settings_changed, 1);
  EXPECT_TRUE(document_.is_modified());
}

TEST_F(ProjectDocumentTest, SetOutputTargetsEmitsProjectSettingsChanged) {
  OutputTargets output = document_.project().output;
  output.video_path = "out/other.y";
  output.signal_type = "yc";

  EXPECT_TRUE(document_.SetOutputTargets(output));
  EXPECT_TRUE(document_.project().output == output);
  EXPECT_EQ(counter_->project_settings_changed, 1);
}

TEST_F(ProjectDocumentTest, SetDiscSkipsEmitsDiscSkipsChanged) {
  std::vector<DiscSkip> skips;
  DiscSkip skip;
  skip.at_frame = 5;
  skip.direction = DiscSkipDirection::kForward;
  skip.count = 2;
  skips.push_back(skip);

  EXPECT_TRUE(document_.SetDiscSkips(skips));
  EXPECT_TRUE(document_.project().disc_skips == skips);
  EXPECT_EQ(counter_->disc_skips_changed, 1);
  EXPECT_EQ(counter_->document_changed, 1);
}

TEST_F(ProjectDocumentTest, InsertSectionAppendsWithMinusOneIndex) {
  Section section;
  section.name = "Appended";
  section.type = "progressive";
  section.duration_frames = 5;

  EXPECT_TRUE(document_.InsertSection(-1, section));
  ASSERT_EQ(document_.section_count(), 3);
  EXPECT_EQ(document_.project().sections[2].name, "Appended");
  EXPECT_EQ(counter_->section_added, 1);
  EXPECT_EQ(counter_->last_index, 2);
}

TEST_F(ProjectDocumentTest, InsertSectionAtIndexShiftsExisting) {
  Section section;
  section.name = "Inserted";
  section.type = "progressive";
  section.duration_frames = 5;

  EXPECT_TRUE(document_.InsertSection(0, section));
  ASSERT_EQ(document_.section_count(), 3);
  EXPECT_EQ(document_.project().sections[0].name, "Inserted");
  EXPECT_EQ(document_.project().sections[1].name, "First");
  EXPECT_EQ(counter_->last_index, 0);
}

TEST_F(ProjectDocumentTest, InsertSectionRejectsOutOfRangeIndex) {
  EXPECT_FALSE(document_.InsertSection(5, Section{}));
  EXPECT_FALSE(document_.InsertSection(-2, Section{}));
  EXPECT_EQ(document_.section_count(), 2);
  EXPECT_EQ(counter_->document_changed, 0);
  EXPECT_FALSE(document_.is_modified());
}

TEST_F(ProjectDocumentTest, RemoveSectionEmitsAndShrinks) {
  EXPECT_TRUE(document_.RemoveSection(0));
  ASSERT_EQ(document_.section_count(), 1);
  EXPECT_EQ(document_.project().sections[0].name, "Second");
  EXPECT_EQ(counter_->section_removed, 1);
  EXPECT_EQ(counter_->last_index, 0);
  EXPECT_TRUE(document_.is_modified());
}

TEST_F(ProjectDocumentTest, RemoveSectionRejectsOutOfRangeIndex) {
  EXPECT_FALSE(document_.RemoveSection(2));
  EXPECT_FALSE(document_.RemoveSection(-1));
  EXPECT_EQ(document_.section_count(), 2);
  EXPECT_EQ(counter_->document_changed, 0);
}

TEST_F(ProjectDocumentTest, MoveSectionReordersAndEmits) {
  EXPECT_TRUE(document_.MoveSection(0, 1));
  EXPECT_EQ(document_.project().sections[0].name, "Second");
  EXPECT_EQ(document_.project().sections[1].name, "First");
  EXPECT_EQ(counter_->section_moved, 1);
  EXPECT_EQ(counter_->last_index, 0);
  EXPECT_EQ(counter_->last_to_index, 1);
}

TEST_F(ProjectDocumentTest, MoveSectionRejectsNoOpAndOutOfRange) {
  EXPECT_FALSE(document_.MoveSection(0, 0));
  EXPECT_FALSE(document_.MoveSection(0, 2));
  EXPECT_FALSE(document_.MoveSection(-1, 1));
  EXPECT_EQ(counter_->document_changed, 0);
  EXPECT_FALSE(document_.is_modified());
}

TEST_F(ProjectDocumentTest, SetSectionReplacesContentAndEmits) {
  Section replacement = document_.project().sections[1];
  replacement.duration_frames = 99;
  replacement.noise.enabled = true;
  replacement.noise.noise_db = 48.0;

  EXPECT_TRUE(document_.SetSection(1, replacement));
  EXPECT_TRUE(document_.project().sections[1] == replacement);
  EXPECT_EQ(counter_->section_edited, 1);
  EXPECT_EQ(counter_->last_index, 1);
}

TEST_F(ProjectDocumentTest, MarkSavedClearsModifiedAndUpdatesPath) {
  document_.SetProjectInfo("Changed", "1.0", "");
  ASSERT_TRUE(document_.is_modified());

  document_.MarkSaved(QStringLiteral("/tmp/saved.yaml"));
  EXPECT_FALSE(document_.is_modified());
  EXPECT_EQ(document_.file_path(), QStringLiteral("/tmp/saved.yaml"));
  EXPECT_EQ(counter_->file_path_changed, 1);
  EXPECT_EQ(counter_->modified_state_changed, 2);  // dirty -> clean
  EXPECT_FALSE(counter_->last_modified);
}

TEST_F(ProjectDocumentTest, ModifiedStateChangedFiresOncePerTransition) {
  document_.SetProjectInfo("A", "1.0", "");
  document_.SetProjectInfo("B", "1.0", "");
  EXPECT_EQ(counter_->modified_state_changed, 1);
  EXPECT_EQ(counter_->document_changed, 2);
}

}  // namespace
}  // namespace videosynth::gui
