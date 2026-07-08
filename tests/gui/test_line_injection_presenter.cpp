/*
 * File:        test_line_injection_presenter.cpp
 * Module:      gui_tests
 * Purpose:     Unit tests for the line-injection catalogues against the
 *              validator's compatibility matrix
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "line_injection_presenter.h"
#include "videosynth/biphase_types.h"
#include "videosynth/model.h"
#include "videosynth/project_validator.h"

namespace videosynth::gui {
namespace {

bool Contains(const std::vector<std::string>& values,
              const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

// Builds a structurally valid laserdisc project carrying exactly one code of
// `code_type` so the validator's verdict isolates the compatibility matrix.
Project MakeLaserdiscProject(Standard standard, DiscType disc_type,
                             SectionType section_type,
                             const std::string& code_type) {
  Project project;
  project.cvbs_presets.video_standard_preset = standard;
  project.output.video_path = "out/video.composite";
  project.output.metadata_path = "out/video.meta";

  Section section;
  section.name = "Disc";
  section.type = "progressive";
  section.source = "assets/disc.exr";
  section.duration_frames = 2000;
  section.section_type = section_type;

  Section::LineInjection laserdisc;
  laserdisc.type = "laserdisc";
  laserdisc.disc_type = DiscTypeToString(disc_type);
  Section::LineInjectionCode code;
  code.code_type = code_type;
  laserdisc.codes.push_back(code);
  section.line_injections.push_back(laserdisc);

  if (standard == Standard::kNtsc || standard == Standard::kPalM) {
    // IEC 60857 §9.1.3: System M laserdisc sections need a virs reference.
    Section::LineInjection virs;
    virs.type = "vits";
    virs.target_lines = {19, 282};
    virs.vits_type = "virs";
    section.line_injections.push_back(virs);
  }

  project.sections.push_back(section);
  return project;
}

TEST(LineInjectionPresenterTest, InjectionTypesAreGeneratable) {
  const std::vector<std::string> types = AvailableInjectionTypes();
  EXPECT_TRUE(Contains(types, "vits"));
  EXPECT_TRUE(Contains(types, "laserdisc"));
  // vitc/line_content parse but are rejected by the runtime as
  // unimplemented, so the editor must not offer them.
  EXPECT_FALSE(Contains(types, "vitc"));
  EXPECT_FALSE(Contains(types, "line_content"));
}

TEST(LineInjectionPresenterTest, VitsCatalogueIsStandardFiltered) {
  const std::vector<std::string> pal = AvailableVitsTypes(Standard::kPal);
  EXPECT_TRUE(Contains(pal, "vits17"));
  EXPECT_TRUE(Contains(pal, "itu-multiburst"));
  EXPECT_FALSE(Contains(pal, "virs"));
  EXPECT_FALSE(Contains(pal, "ntc7-composite"));

  const std::vector<std::string> ntsc = AvailableVitsTypes(Standard::kNtsc);
  EXPECT_TRUE(Contains(ntsc, "virs"));
  EXPECT_TRUE(Contains(ntsc, "ntc7-composite"));
  EXPECT_FALSE(Contains(ntsc, "vits17"));

  // PAL-M uses the System M catalogue (mirrors TryGetDefinition fallback).
  EXPECT_EQ(AvailableVitsTypes(Standard::kPalM), ntsc);
}

TEST(LineInjectionPresenterTest, RecommendedLinesComeFromCatalogue) {
  EXPECT_EQ(RecommendedVitsLine(Standard::kPal, "vits17"), 17);
  EXPECT_EQ(RecommendedVitsLine(Standard::kPal, "itu-multiburst"), 18);
  // virs has no fixed placement line.
  EXPECT_EQ(RecommendedVitsLine(Standard::kNtsc, "virs"), 0);
  EXPECT_EQ(RecommendedVitsLine(Standard::kPal, "no-such-type"), 0);
}

// The offered code types must be exactly the master-list entries the
// validator accepts for that disc type, section type, and standard.
TEST(LineInjectionPresenterTest, CodeTypeCatalogueMatchesValidatorMatrix) {
  ProjectValidator validator;

  for (const Standard standard : {Standard::kPal, Standard::kNtsc}) {
    for (const DiscType disc_type : {DiscType::kCAV, DiscType::kCLV}) {
      for (const SectionType section_type :
           {SectionType::kLeadIn, SectionType::kProgrammeArea,
            SectionType::kLeadOut}) {
        const std::vector<std::string> offered =
            AvailableLaserdiscCodeTypes(disc_type, section_type, standard);

        for (const std::string& code_type : AllLaserdiscCodeTypes()) {
          const Project project = MakeLaserdiscProject(standard, disc_type,
                                                       section_type, code_type);
          const bool accepted = validator.Validate(project).is_valid;
          EXPECT_EQ(accepted, Contains(offered, code_type))
              << StandardToString(standard) << "/"
              << DiscTypeToString(disc_type) << "/"
              << SectionTypeToString(section_type) << ": " << code_type;
        }
      }
    }
  }
}

TEST(LineInjectionPresenterTest, CodeParameterMappingFollowsSchema) {
  EXPECT_TRUE(CodeTypeUsesStartValue("picture_number"));
  EXPECT_TRUE(CodeTypeUsesStartValue("fm_picture_number"));
  EXPECT_FALSE(CodeTypeUsesStartValue("chapter_number"));
  EXPECT_TRUE(CodeTypeUsesChapter("chapter_number"));
  EXPECT_TRUE(CodeTypeUsesProgrammeStatus("programme_status"));
  EXPECT_TRUE(CodeTypeUsesUsersCode("users_code"));
  EXPECT_FALSE(CodeTypeUsesUsersCode("lead_in"));
}

TEST(LineInjectionPresenterTest, ParseTargetLinesAcceptsListsRejectsGarbage) {
  std::vector<int> lines;
  EXPECT_TRUE(ParseTargetLines("19, 282", &lines));
  EXPECT_EQ(lines, (std::vector<int>{19, 282}));

  EXPECT_TRUE(ParseTargetLines("17", &lines));
  EXPECT_EQ(lines, (std::vector<int>{17}));

  EXPECT_TRUE(ParseTargetLines("10 11 12", &lines));
  EXPECT_EQ(lines, (std::vector<int>{10, 11, 12}));

  EXPECT_TRUE(ParseTargetLines("", &lines));
  EXPECT_TRUE(lines.empty());

  EXPECT_FALSE(ParseTargetLines("abc", &lines));
  EXPECT_FALSE(ParseTargetLines("19,x", &lines));
  EXPECT_FALSE(ParseTargetLines("19a", &lines));
}

TEST(LineInjectionPresenterTest, FormatTargetLinesRoundTrips) {
  const std::vector<int> lines = {19, 282};
  std::vector<int> parsed;
  EXPECT_TRUE(ParseTargetLines(FormatTargetLines(lines), &parsed));
  EXPECT_EQ(parsed, lines);
  EXPECT_EQ(FormatTargetLines({}), "");
}

TEST(LineInjectionPresenterTest, TotalDiscFramesSumsSections) {
  Project project;
  Section a;
  a.duration_frames = 100;
  Section b;
  b.duration_frames = 50;
  project.sections = {a, b};
  EXPECT_EQ(TotalDiscFrames(project), 150);
}

}  // namespace
}  // namespace videosynth::gui
