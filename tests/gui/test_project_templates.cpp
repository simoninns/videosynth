/*
 * File:        test_project_templates.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for built-in project/section templates and the
 *              bundled-default source remapping used when the standard changes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include "project_templates.h"
#include "videosynth/model.h"

namespace videosynth::gui {
namespace {

TEST(ProjectTemplatesTest, DefaultProjectSeedsStandardMatchedSource) {
  EXPECT_EQ(MakeDefaultProject(Standard::kPal).sections.front().source,
            DefaultBundledSource(Standard::kPal));
  EXPECT_EQ(MakeDefaultProject(Standard::kNtsc).sections.front().source,
            DefaultBundledSource(Standard::kNtsc));
}

TEST(ProjectTemplatesTest, DefaultBundledSourceRasterFollowsStandard) {
  // System-M (NTSC and PAL-M) share the 525-line 720x486 raster.
  EXPECT_EQ(DefaultBundledSource(Standard::kNtsc),
            DefaultBundledSource(Standard::kPalM));
  EXPECT_NE(DefaultBundledSource(Standard::kPal),
            DefaultBundledSource(Standard::kNtsc));
}

TEST(ProjectTemplatesTest, BundledRasterMapsStandardToSubfolder) {
  // The section editor composes {bundled}/<type>/<raster>/<file> from this.
  EXPECT_EQ(BundledRaster(Standard::kPal), "720x576");
  EXPECT_EQ(BundledRaster(Standard::kNtsc), "720x486");
  EXPECT_EQ(BundledRaster(Standard::kPalM), "720x486");
}

TEST(ProjectTemplatesTest, RemapRepointsBundledDefaultToNewStandard) {
  Project project = MakeDefaultProject(Standard::kPal);
  ASSERT_EQ(project.sections.front().source,
            DefaultBundledSource(Standard::kPal));

  EXPECT_EQ(RemapBundledDefaultSources(&project, Standard::kNtsc), 1);
  EXPECT_EQ(project.sections.front().source,
            DefaultBundledSource(Standard::kNtsc));
}

TEST(ProjectTemplatesTest, RemapLeavesUserChosenSourceUntouched) {
  Project project = MakeDefaultProject(Standard::kPal);
  project.sections.front().source = "assets/my_footage.exr";

  EXPECT_EQ(RemapBundledDefaultSources(&project, Standard::kNtsc), 0);
  EXPECT_EQ(project.sections.front().source, "assets/my_footage.exr");
}

TEST(ProjectTemplatesTest, RemapIsNoOpWhenAlreadyOnTargetRaster) {
  Project project = MakeDefaultProject(Standard::kNtsc);

  // NTSC -> PAL-M keeps the shared 525-line raster, so nothing changes.
  EXPECT_EQ(RemapBundledDefaultSources(&project, Standard::kPalM), 0);
  EXPECT_EQ(project.sections.front().source,
            DefaultBundledSource(Standard::kNtsc));
}

TEST(ProjectTemplatesTest, RemapHandlesMultipleSectionsAndNullProject) {
  Project project = MakeDefaultProject(Standard::kPal);
  Section extra = project.sections.front();
  extra.name = "Section 2";
  project.sections.push_back(extra);

  EXPECT_EQ(RemapBundledDefaultSources(&project, Standard::kNtsc), 2);
  for (const Section& section : project.sections) {
    EXPECT_EQ(section.source, DefaultBundledSource(Standard::kNtsc));
  }

  EXPECT_EQ(RemapBundledDefaultSources(nullptr, Standard::kPal), 0);
}

}  // namespace
}  // namespace videosynth::gui
