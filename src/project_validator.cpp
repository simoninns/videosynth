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
  return pattern == "ebu_colour_bars" || pattern == "grayscale_ramp_horizontal" ||
         pattern == "pluge" || pattern == "colour_bars_75" ||
         pattern == "grayscale_ramp" || pattern == "pluge_basic";
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
          "'ebu_colour_bars', 'grayscale_ramp_horizontal', and 'pluge'.");
      break;
    }
  }

  return result;
}

}  // namespace videosynth
