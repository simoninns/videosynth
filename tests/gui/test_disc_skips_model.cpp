/*
 * File:        test_disc_skips_model.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the editable disc skips table model
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <QString>

#include "disc_skips_model.h"
#include "project_document.h"
#include "videosynth/model.h"

namespace videosynth::gui {
namespace {

Project MakeProject() {
  Project project;
  project.name = "Test";
  project.cvbs_presets.video_standard_preset = Standard::kPal;
  project.output.video_path = "out/video.cvbs";
  project.output.metadata_path = "out/video.meta";
  Section section;
  section.name = "Bars";
  section.type = "progressive";
  section.source = "assets/bars.exr";
  section.duration_frames = 100;
  project.sections.push_back(section);

  DiscSkip skip;
  skip.at_frame = 10;
  skip.direction = DiscSkipDirection::kForward;
  skip.count = 2;
  project.disc_skips.push_back(skip);
  return project;
}

class DiscSkipsModelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    document_.ResetProject(MakeProject(), QString());
    model_ = new DiscSkipsModel(&document_, &document_);
  }

  ProjectDocument document_;
  DiscSkipsModel* model_ = nullptr;
};

TEST_F(DiscSkipsModelTest, DisplaysDocumentSkips) {
  ASSERT_EQ(model_->rowCount(), 1);
  EXPECT_EQ(model_
                ->data(model_->index(0, DiscSkipsModel::kAtFrameColumn),
                       Qt::DisplayRole)
                .toInt(),
            10);
  EXPECT_EQ(model_
                ->data(model_->index(0, DiscSkipsModel::kDirectionColumn),
                       Qt::DisplayRole)
                .toString(),
            QStringLiteral("forward"));
  EXPECT_EQ(model_
                ->data(model_->index(0, DiscSkipsModel::kCountColumn),
                       Qt::DisplayRole)
                .toInt(),
            2);
}

TEST_F(DiscSkipsModelTest, AddSkipAppendsRowAndMarksDocumentModified) {
  model_->AddSkip();
  EXPECT_EQ(model_->rowCount(), 2);
  ASSERT_EQ(document_.project().disc_skips.size(), 2U);
  EXPECT_EQ(document_.project().disc_skips[1].at_frame, 1);
  EXPECT_TRUE(document_.is_modified());
}

TEST_F(DiscSkipsModelTest, RemoveSkipDeletesRow) {
  model_->RemoveSkip(0);
  EXPECT_EQ(model_->rowCount(), 0);
  EXPECT_TRUE(document_.project().disc_skips.empty());
}

TEST_F(DiscSkipsModelTest, SetDataEditsReachTheDocument) {
  EXPECT_TRUE(model_->setData(model_->index(0, DiscSkipsModel::kAtFrameColumn),
                              42, Qt::EditRole));
  EXPECT_TRUE(
      model_->setData(model_->index(0, DiscSkipsModel::kDirectionColumn),
                      QStringLiteral("backward"), Qt::EditRole));
  EXPECT_TRUE(model_->setData(model_->index(0, DiscSkipsModel::kCountColumn), 5,
                              Qt::EditRole));

  const DiscSkip& skip = document_.project().disc_skips[0];
  EXPECT_EQ(skip.at_frame, 42);
  EXPECT_EQ(skip.direction, DiscSkipDirection::kBackward);
  EXPECT_EQ(skip.count, 5);
}

TEST_F(DiscSkipsModelTest, RejectsUnknownDirection) {
  EXPECT_FALSE(
      model_->setData(model_->index(0, DiscSkipsModel::kDirectionColumn),
                      QStringLiteral("sideways"), Qt::EditRole));
  EXPECT_EQ(document_.project().disc_skips[0].direction,
            DiscSkipDirection::kForward);
}

TEST_F(DiscSkipsModelTest, ExternalDocumentChangesReloadTheTable) {
  std::vector<DiscSkip> skips = document_.project().disc_skips;
  DiscSkip extra;
  extra.at_frame = 50;
  extra.direction = DiscSkipDirection::kBackward;
  extra.count = 3;
  skips.push_back(extra);
  document_.SetDiscSkips(skips);
  EXPECT_EQ(model_->rowCount(), 2);

  document_.ResetProject(MakeProject(), QString());
  EXPECT_EQ(model_->rowCount(), 1);
}

}  // namespace
}  // namespace videosynth::gui
