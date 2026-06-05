/*
 * File:        test_cross_injection_validation.cpp
 * Module:      project_validator_tests
 * Purpose:     Validates cross-injection line conflict rules for laserdisc
 *              sections — incompatible VITS types, line_content on reserved
 *              ranges, and PAL subtitle mutual exclusion (IEC 60856/60857
 *              §§ 5.11, 5.12, 5.14).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <string>

#include "videosynth/project_validator.h"

namespace videosynth {
namespace {

// ─── helpers ──────────────────────────────────────────────────────────────────

Project MakeBasePalProject() {
  Project p;
  p.cvbs_presets.video_standard_preset = Standard::kPal;
  p.cvbs_presets.sample_encoding_preset = "CVBS_U10_4FSC";
  p.cvbs_presets.signal_state_preset = "STANDARD_TBC_LOCKED";
  p.output.video_path = "/tmp/ci_test.composite";
  p.output.metadata_path = "/tmp/ci_test.meta";
  return p;
}

Project MakeBaseNtscProject() {
  Project p = MakeBasePalProject();
  p.cvbs_presets.video_standard_preset = Standard::kNtsc;
  return p;
}

Section MakePalSection() {
  Section s;
  s.name = "Section";
  s.type = "progressive";
  s.source = "fixture.exr";
  s.duration_frames = 100;
  return s;
}

Section::LineInjection MakeLaserdiscInjection() {
  Section::LineInjection inj;
  inj.type = "laserdisc";
  return inj;
}

Section::LineInjection MakeVitsInjection(const std::string& vits_type, int line) {
  Section::LineInjection inj;
  inj.type = "vits";
  inj.vits_type = vits_type;
  inj.target_lines = {line};
  return inj;
}

Section::LineInjection MakeVirsInjection() {
  Section::LineInjection inj;
  inj.type = "vits";
  inj.vits_type = "virs";
  inj.target_lines = {19, 282};
  return inj;
}

// ─── Task 5.11: PAL incompatible VITS types ───────────────────────────────────

TEST(CrossInjectionValidationTest,
     RejectsPalVits17WhenLaserdiscIsActive) {
  // vits17 targets line 17 which is in the PAL reserved range 6–18.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("vits17", 17));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("17"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsPalItuMultiburstWhenLaserdiscIsActive) {
  // itu-multiburst targets line 18, inside PAL reserved range 6–18.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("itu-multiburst", 18));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("18"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsPalItuCompositeWhenLaserdiscIsActive) {
  // itu-composite targets line 330, inside PAL reserved range 319–331.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("itu-composite", 330));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("330"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsPalItuCombinationWhenLaserdiscIsActive) {
  // itu-combination targets line 331, inside PAL reserved range 319–331.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("itu-combination", 331));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("331"), std::string::npos);
}

// ─── Task 5.11: NTSC incompatible VITS types ──────────────────────────────────

TEST(CrossInjectionValidationTest,
     RejectsNtscNtc7CompositeWhenLaserdiscIsActive) {
  // ntc7-composite targets line 17, inside NTSC reserved range 10–18.
  Project p = MakeBaseNtscProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("ntc7-composite", 17));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("17"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsNtscFccMultiburstWhenLaserdiscIsActive) {
  // fcc-multiburst targets line 18, inside NTSC reserved range 10–18.
  Project p = MakeBaseNtscProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("fcc-multiburst", 18));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("18"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsNtscNtc7CombinationWhenLaserdiscIsActive) {
  // ntc7-combination targets line 280, inside NTSC reserved range 273–281.
  Project p = MakeBaseNtscProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("ntc7-combination", 280));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("280"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsNtscFccCompositeWhenLaserdiscIsActive) {
  // fcc-composite targets line 281, inside NTSC reserved range 273–281.
  Project p = MakeBaseNtscProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("fcc-composite", 281));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("281"), std::string::npos);
}

// ─── Compatible VITS types (outside reserved ranges) ─────────────────────────

TEST(CrossInjectionValidationTest,
     AcceptsPalUkNationalOnLine19WhenLaserdiscIsActive) {
  // uk-national targets line 19, outside the PAL reserved range 6–18 / 319–331.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("uk-national", 19));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(CrossInjectionValidationTest,
     AcceptsPalVits20OnLine20WhenLaserdiscIsActive) {
  // vits20 targets line 20, outside the PAL reserved range.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("vits20", 20));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

TEST(CrossInjectionValidationTest,
     AcceptsNtscVirsOnLines19And282WhenLaserdiscIsActive) {
  // virs targets lines 19 and 282 — both outside NTSC reserved ranges.
  Project p = MakeBaseNtscProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVirsInjection());
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

// ─── Task 5.12: line_content on reserved ranges ───────────────────────────────

TEST(CrossInjectionValidationTest,
     RejectsPalLineContentOnReservedLine6WhenLaserdiscIsActive) {
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());

  Section::LineInjection lc;
  lc.type = "line_content";
  lc.target_lines = {6};  // first PAL reserved line
  s.line_injections.push_back(lc);
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("6"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsPalLineContentOnReservedLine319WhenLaserdiscIsActive) {
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());

  Section::LineInjection lc;
  lc.type = "line_content";
  lc.target_lines = {319};
  s.line_injections.push_back(lc);
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("319"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsNtscLineContentOnReservedLine10WhenLaserdiscIsActive) {
  Project p = MakeBaseNtscProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVirsInjection());  // satisfy VIRS requirement

  Section::LineInjection lc;
  lc.type = "line_content";
  lc.target_lines = {10};  // first NTSC reserved line
  s.line_injections.push_back(lc);
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("10"), std::string::npos);
}

// ─── Task 5.14: PAL subtitle mutual exclusion ─────────────────────────────────
// IEC 60856 §9.1.4: line_content subtitles on line 20 or 333 are mutually
// exclusive with VITS injections targeting those same lines.

TEST(CrossInjectionValidationTest,
     RejectsVits20WhenLineContentAlsoTargetsLine20) {
  // Both vits20 and a line_content injection claim line 20 → overlapping lines.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("vits20", 20));

  Section::LineInjection lc;
  lc.type = "line_content";
  lc.target_lines = {20};
  s.line_injections.push_back(lc);
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  // Rejected by the overlapping-target-line check.
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("20"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     RejectsDualLineContentOnLine333) {
  // Two injections both targeting line 333 are caught by the overlapping check.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());

  Section::LineInjection lc1;
  lc1.type = "line_content";
  lc1.target_lines = {333};

  Section::LineInjection lc2;
  lc2.type = "line_content";
  lc2.target_lines = {333};

  s.line_injections.push_back(lc1);
  s.line_injections.push_back(lc2);
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("333"), std::string::npos);
}

TEST(CrossInjectionValidationTest,
     AcceptsVits20AndLineContentOnDifferentSafeLines) {
  // vits20 on line 20; line_content on line 21 — no conflict.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeLaserdiscInjection());
  s.line_injections.push_back(MakeVitsInjection("vits20", 20));

  Section::LineInjection lc;
  lc.type = "line_content";
  lc.target_lines = {21};
  s.line_injections.push_back(lc);
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  // line_content is still an MVP-deferred type, so it will be blocked by the
  // deferred check — but for a different reason than a line conflict.
  EXPECT_FALSE(r.is_valid);
  ASSERT_FALSE(r.errors.empty());
  EXPECT_NE(r.errors[0].find("line_content"), std::string::npos);
}

// ─── No-laserdisc baseline ────────────────────────────────────────────────────

TEST(CrossInjectionValidationTest,
     AllowsAllVitsTypesWhenNoLaserdiscInjectionIsPresent) {
  // Without laserdisc injection, reserved-range checks don't apply.
  Project p = MakeBasePalProject();
  Section s = MakePalSection();
  s.line_injections.push_back(MakeVitsInjection("vits17", 17));
  s.line_injections.push_back(MakeVitsInjection("itu-multiburst", 18));
  p.sections.push_back(s);

  ProjectValidator v;
  const auto r = v.Validate(p);
  EXPECT_TRUE(r.is_valid) << (!r.errors.empty() ? r.errors[0] : "");
}

}  // namespace
}  // namespace videosynth
