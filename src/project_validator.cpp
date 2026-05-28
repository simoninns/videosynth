/*
 * File:        project_validator.cpp
 * Module:      project_validator
 * Purpose:     Validates project constraints for supported generation profiles.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/project_validator.h"

#include <string>

namespace {

bool IsSupportedPattern(const std::string& pattern) {
  return pattern == "pal_ebu_colour_bars_100" || pattern == "pal_ebu_colour_bars_75" ||
         pattern == "pal_linear_grayscale_ramp_horizontal" ||
         pattern == "pal_linear_grayscale_ramp_vertical" ||
         pattern == "pal_luma_checkerboard_8x8" ||
         pattern == "pal_luma_checkerboard_16x16" ||
         pattern == "pal_full_field_black" || pattern == "pal_full_field_white" ||
         pattern == "pal_pluge_5patch_near_black" || pattern == "pal_crosshatch_visible_area_grid" ||
         pattern == "ntsc_smpte_170m_colour_bars_100" ||
         pattern == "ntsc_smpte_170m_colour_bars_75" ||
         pattern == "ntsc_linear_grayscale_ramp_horizontal" ||
         pattern == "ntsc_linear_grayscale_ramp_vertical" ||
         pattern == "ntsc_luma_checkerboard_8x8" ||
         pattern == "ntsc_luma_checkerboard_16x16" ||
         pattern == "ntsc_full_field_black" || pattern == "ntsc_full_field_white" ||
         pattern == "ntsc_pluge_5patch_near_black" || pattern == "ntsc_crosshatch_visible_area_grid";
}

bool PatternSupportsStandard(const std::string& pattern, videosynth::Standard standard) {
  if (pattern.rfind("pal_", 0) == 0) {
    return standard == videosynth::Standard::kPal;
  }
  if (pattern.rfind("ntsc_", 0) == 0) {
    return standard == videosynth::Standard::kNtsc;
  }
  return false;
}

bool IsSupportedGenerationEncodingPreset(const std::string& preset) {
  return preset == "CVBS_U10_4FSC" || preset == "CVBS_TPG21_4FSC";
}

}  // namespace

namespace videosynth {

ValidationResult ProjectValidator::Validate(const Project& project) {
  ValidationResult result;
  result.is_valid = true;

  if (project.cvbs_presets.video_standard_preset == Standard::kUnknown) {
    result.is_valid = false;
    result.errors.push_back("MVP constraint violation: video_standard_preset must be 'PAL' or 'NTSC'.");
  }

  if (!Is4fscSampleEncodingPreset(project.cvbs_presets.sample_encoding_preset)) {
    result.is_valid = false;
    result.errors.push_back(
        "MVP constraint violation: sample_encoding_preset must be a 4fsc preset.");
  }

    if (!IsSupportedGenerationEncodingPreset(project.cvbs_presets.sample_encoding_preset)) {
    result.is_valid = false;
    result.errors.push_back(
      "MVP constraint violation: sample_encoding_preset must be 'CVBS_U10_4FSC' or 'CVBS_TPG21_4FSC'.");
  }

  if (project.cvbs_presets.signal_state_preset != "STANDARD_TBC_LOCKED") {
    result.is_valid = false;
    result.errors.push_back(
        "MVP constraint violation: signal_state_preset must be 'STANDARD_TBC_LOCKED'.");
  }

  if (!IsLockedSignalStatePreset(project.cvbs_presets.signal_state_preset)) {
    result.is_valid = false;
    result.errors.push_back("MVP constraint violation: signal_state_preset must indicate locked state.");
  }

  if (project.output.video_path.empty()) {
    result.is_valid = false;
    result.errors.push_back("MVP constraint violation: output.video_path must be set.");
  }

  if (project.output.metadata_path.empty()) {
    result.is_valid = false;
    result.errors.push_back("MVP constraint violation: output.metadata_path must be set.");
  }

  if (!project.output.video_path.empty() &&
      !project.output.metadata_path.empty() &&
      project.output.video_path == project.output.metadata_path) {
    result.is_valid = false;
    result.errors.push_back("MVP constraint violation: output.video_path and output.metadata_path must differ.");
  }

  if (project.sections.empty()) {
    result.is_valid = false;
    result.errors.push_back("Project must contain at least one section.");
  }

  for (const Section& section : project.sections) {
    if (section.type != "software_generated") {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: section type must be 'software_generated'.");
      break;
    }

    if (section.pattern.empty()) {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: software_generated sections must define a pattern.");
      break;
    }

    if (section.duration_frames <= 0) {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: software_generated sections must define duration_frames > 0.");
      break;
    }

    if (!IsSupportedPattern(section.pattern)) {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: unsupported pattern. Supported patterns are "
          "'pal_ebu_colour_bars_100', 'pal_ebu_colour_bars_75', "
          "'pal_linear_grayscale_ramp_horizontal', 'pal_linear_grayscale_ramp_vertical', "
          "'pal_luma_checkerboard_8x8', 'pal_luma_checkerboard_16x16', "
          "'pal_full_field_black', 'pal_full_field_white', "
          "'pal_pluge_5patch_near_black', 'pal_crosshatch_visible_area_grid', "
          "'ntsc_smpte_170m_colour_bars_100', 'ntsc_smpte_170m_colour_bars_75', "
          "'ntsc_linear_grayscale_ramp_horizontal', 'ntsc_linear_grayscale_ramp_vertical', "
          "'ntsc_luma_checkerboard_8x8', 'ntsc_luma_checkerboard_16x16', "
          "'ntsc_full_field_black', 'ntsc_full_field_white', "
          "'ntsc_pluge_5patch_near_black', and 'ntsc_crosshatch_visible_area_grid'.");
      break;
    }

    if (!PatternSupportsStandard(section.pattern, project.cvbs_presets.video_standard_preset)) {
      result.is_valid = false;
      result.errors.push_back(
          "MVP constraint violation: section pattern does not match video standard. "
          "Use 'pal_*' patterns for PAL and 'ntsc_*' patterns for NTSC.");
      break;
    }
  }

  return result;
}

}  // namespace videosynth
