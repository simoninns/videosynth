/*
 * File:        test_ntsc_virs_validation.cpp
 * Module:      project_validator_tests
 * Purpose:     Validates that NTSC laserdisc sections include the mandatory
 *              VIR signal injection for colour (IEC 60857 §9.1.3 and §§ 5.13,
 *              5.16).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>

#include "videosynth/project_validator.h"

namespace videosynth {
namespace {

// ─── helpers
// ──────────────────────────────────────────────────────────────────

Project MakeBaseNtscProject() {
  Project p;
  p.cvbs_presets.video_standard_preset = Standard::kNtsc;
  p.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  p.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  p.output.video_path = "/tmp/nv_test.composite";
  p.output.metadata_path = "/tmp/nv_test.meta";
  return p;
}

Project MakeBasePalProject() {
  Project p = MakeBaseNtscProject();
  p.cvbs_presets.video_standard_preset = Standard::kPal;
  return p;
}

Section MakeSection(int frames = 100) {
  Section s;
  s.name = "Section";
  s.type = "progressive";
  s.source = "fixture.exr";
  s.duration_frames = frames;
  return s;
}

Section::LineInjection MakeLaserdiscInjection() {
  Section::LineInjection inj;
  inj.type = "laserdisc";
  return inj;
}

Section::LineInjection MakeVirsInjection() {
  Section::LineInjection inj;
  inj.type = "vits";
  inj.vits_type = "virs";
  inj.target_lines = {19, 282};
  return inj;
}

// ─── Task 5.13: NTSC VIRS mandatory presence
// ──────────────────────────────────

TEST(NtscVirsValidationTest, RejectsNtscLaserdiscSectionWithoutVirs) {
  // IEC 60857 §9.1.3: VIR signal is mandatory for colour on NTSC laserdisc.
  Project p = MakeBaseNtscProject();
  Section s = MakeSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  // Deliberately omit the virs injection.
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("virs"), std::string::npos);
  EXPECT_NE(r.errors[0].find("9.1.3"), std::string::npos);
}

TEST(NtscVirsValidationTest, AcceptsNtscLaserdiscSectionWithVirs) {
  Project p = MakeBaseNtscProject();
  Section s = MakeSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVirsInjection());
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(NtscVirsValidationTest, AcceptsNtscSectionWithoutLaserdiscAndWithoutVirs) {
  // No laserdisc injection → no VIRS requirement.
  Project p = MakeBaseNtscProject();
  Section s = MakeSection();
  // No injections at all.
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(NtscVirsValidationTest, AcceptsPalLaserdiscSectionWithoutVirs) {
  // PAL laserdisc sections do not require virs (IEC 60856 only).
  Project p = MakeBasePalProject();
  Section s = MakeSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(NtscVirsValidationTest,
     RejectsNtscMultipleSectionsWhenOnlyOneMissingVirs) {
  // Each section that has a laserdisc injection must independently have virs.
  Project p = MakeBaseNtscProject();

  Section s1 = MakeSection();
  s1.line_injections.push_back(MakeLaserdiscInjection());
  s1.line_injections.push_back(MakeVirsInjection());  // OK
  p.sections.push_back(s1);

  Section s2 = MakeSection();
  s2.line_injections.push_back(MakeLaserdiscInjection());
  // Omit virs for s2.
  p.sections.push_back(s2);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("virs"), std::string::npos);
}

TEST(NtscVirsValidationTest, AcceptsNtscMultipleSectionsEachWithVirs) {
  Project p = MakeBaseNtscProject();

  Section s1 = MakeSection();
  s1.line_injections.push_back(MakeLaserdiscInjection());
  s1.line_injections.push_back(MakeVirsInjection());
  p.sections.push_back(s1);

  Section s2 = MakeSection();
  s2.line_injections.push_back(MakeLaserdiscInjection());
  s2.line_injections.push_back(MakeVirsInjection());
  p.sections.push_back(s2);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(NtscVirsValidationTest,
     AcceptsNtscSectionWithLaserdiscAndVirsOnLine19Only) {
  // VIRS may target line 19 (field 1) only; line 282 is optional.
  Project p = MakeBaseNtscProject();
  Section s = MakeSection();
  s.line_injections.push_back(MakeLaserdiscInjection());

  Section::LineInjection virs;
  virs.type = "vits";
  virs.vits_type = "virs";
  virs.target_lines = {19};  // field 1 only
  s.line_injections.push_back(virs);

  p.sections.push_back(s);
  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(NtscVirsValidationTest,
     AcceptsNtscSectionWithLaserdiscAndVirsOnLine282Only) {
  // VIRS may target line 282 (field 2) only.
  Project p = MakeBaseNtscProject();
  Section s = MakeSection();
  s.line_injections.push_back(MakeLaserdiscInjection());

  Section::LineInjection virs;
  virs.type = "vits";
  virs.vits_type = "virs";
  virs.target_lines = {282};  // field 2 only
  s.line_injections.push_back(virs);

  p.sections.push_back(s);
  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(NtscVirsValidationTest, VirsMissingErrorMessageContainsIecReference) {
  Project p = MakeBaseNtscProject();
  Section s = MakeSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  ASSERT_FALSE(r.errors.empty());
  // Error must name the missing signal and cite the IEC clause.
  EXPECT_NE(r.errors[0].find("virs"), std::string::npos);
  EXPECT_NE(r.errors[0].find("NTSC"), std::string::npos);
}

TEST(NtscVirsValidationTest, VirsMissingDoesNotBlockAfterReservedLineError) {
  // When an incompatible VITS type is on a reserved line, the reserved-line
  // error fires first (before the VIRS check).
  Project p = MakeBaseNtscProject();
  Section s = MakeSection();
  s.line_injections.push_back(MakeLaserdiscInjection());

  Section::LineInjection bad_vits;
  bad_vits.type = "vits";
  bad_vits.vits_type = "ntc7-composite";
  bad_vits.target_lines = {17};  // reserved line
  s.line_injections.push_back(bad_vits);
  // No virs.
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  // Reserved-line error, not VIRS error.
  EXPECT_NE(r.errors[0].find("17"), std::string::npos);
}

}  // namespace
}  // namespace videosynth
