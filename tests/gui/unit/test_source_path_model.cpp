/*
 * File:        test_source_path_model.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the section-editor source path picker model:
 *              built-in vs my-own classification and composition round-trips
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>

#include "source_path_model.h"

namespace videosynth::gui {
namespace {

// A stored source is round-tripped by parsing it and re-composing against the
// raster the parse discarded; the two must match for a stable form.
std::string RoundTrip(const std::string& source, const std::string& raster) {
  return ComposeSource(ParseSourceSelection(source), raster);
}

TEST(SourcePathModelTest, ParsesBundledSourceIntoBuiltinTypeAndFile) {
  const SourceSelection selection =
      ParseSourceSelection("{bundled}/exr/720x576/75_BARS.exr");
  EXPECT_TRUE(selection.builtin);
  EXPECT_EQ(selection.type, "exr");
  EXPECT_EQ(selection.file, "75_BARS.exr");
}

TEST(SourcePathModelTest, ParsesBundledMkvSource) {
  const SourceSelection selection =
      ParseSourceSelection("{bundled}/mkv/720x486/CLIP.mkv");
  EXPECT_TRUE(selection.builtin);
  EXPECT_EQ(selection.type, "mkv");
  EXPECT_EQ(selection.file, "CLIP.mkv");
}

TEST(SourcePathModelTest, ParsesProjectTokenAsRelativeWithTailPath) {
  const SourceSelection selection =
      ParseSourceSelection("{project}/clips/a.exr");
  EXPECT_FALSE(selection.builtin);
  EXPECT_TRUE(selection.relative);
  EXPECT_EQ(selection.text, "clips/a.exr");
}

TEST(SourcePathModelTest, ParsesBareRelativePathAsProjectRelative) {
  const SourceSelection selection = ParseSourceSelection("assets/source.exr");
  EXPECT_FALSE(selection.builtin);
  EXPECT_TRUE(selection.relative);
  EXPECT_EQ(selection.text, "assets/source.exr");
}

TEST(SourcePathModelTest, ParsesAbsolutePathAsNonRelative) {
  const SourceSelection selection = ParseSourceSelection("/media/clip.exr");
  EXPECT_FALSE(selection.builtin);
  EXPECT_FALSE(selection.relative);
  EXPECT_EQ(selection.text, "/media/clip.exr");
}

TEST(SourcePathModelTest, ParsesForeignTokenVerbatimAsNonRelative) {
  const SourceSelection selection = ParseSourceSelection("{user}/lib/y.exr");
  EXPECT_FALSE(selection.builtin);
  EXPECT_FALSE(selection.relative);
  EXPECT_EQ(selection.text, "{user}/lib/y.exr");
}

TEST(SourcePathModelTest, ComposeBuiltinUsesSuppliedRaster) {
  SourceSelection selection;
  selection.builtin = true;
  selection.type = "exr";
  selection.file = "PLUGE.exr";
  EXPECT_EQ(ComposeSource(selection, "720x486"),
            "{bundled}/exr/720x486/PLUGE.exr");
}

TEST(SourcePathModelTest, ComposeBuiltinWithNoFileYieldsEmpty) {
  SourceSelection selection;
  selection.builtin = true;
  selection.type = "exr";
  EXPECT_TRUE(ComposeSource(selection, "720x576").empty());
}

TEST(SourcePathModelTest, ComposeRelativeWrapsInProjectToken) {
  SourceSelection selection;
  selection.relative = true;
  selection.text = "clips/a.exr";
  EXPECT_EQ(ComposeSource(selection, "720x576"), "{project}/clips/a.exr");
}

TEST(SourcePathModelTest, ComposeAbsoluteStoresVerbatim) {
  SourceSelection selection;
  selection.relative = false;
  selection.text = "/media/clip.exr";
  EXPECT_EQ(ComposeSource(selection, "720x576"), "/media/clip.exr");
}

TEST(SourcePathModelTest, ComposePreservesForeignTokenEvenWhenRelative) {
  SourceSelection selection;
  selection.relative = true;  // ignored: an explicit token wins.
  selection.text = "{user}/lib/y.exr";
  EXPECT_EQ(ComposeSource(selection, "720x576"), "{user}/lib/y.exr");
}

TEST(SourcePathModelTest, BuiltinSourceSelfHealsRasterOnRecompose) {
  // A source stored for the PAL raster re-composes onto the System-M raster
  // when the project standard changed, keeping the type and file name.
  EXPECT_EQ(RoundTrip("{bundled}/exr/720x576/75_BARS.exr", "720x486"),
            "{bundled}/exr/720x486/75_BARS.exr");
}

TEST(SourcePathModelTest, StableSourcesRoundTripUnchanged) {
  EXPECT_EQ(RoundTrip("{bundled}/exr/720x576/75_BARS.exr", "720x576"),
            "{bundled}/exr/720x576/75_BARS.exr");
  EXPECT_EQ(RoundTrip("{project}/clips/a.exr", "720x576"),
            "{project}/clips/a.exr");
  EXPECT_EQ(RoundTrip("/media/clip.exr", "720x576"), "/media/clip.exr");
  EXPECT_EQ(RoundTrip("{user}/lib/y.exr", "720x576"), "{user}/lib/y.exr");
}

TEST(SourcePathModelTest, BareRelativeNormalizesToProjectToken) {
  // A bare relative path becomes the explicit {project} form on first save;
  // thereafter it is stable.
  const std::string once = RoundTrip("assets/source.exr", "720x576");
  EXPECT_EQ(once, "{project}/assets/source.exr");
  EXPECT_EQ(RoundTrip(once, "720x576"), once);
}

TEST(SourcePathModelTest, EmptySourceRoundTripsToEmpty) {
  EXPECT_TRUE(RoundTrip("", "720x576").empty());
}

}  // namespace
}  // namespace videosynth::gui
