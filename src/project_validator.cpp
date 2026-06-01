/*
 * File:        project_validator.cpp
 * Module:      project_validator
 * Purpose:     Validates project constraints for supported generation profiles.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/project_validator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>

namespace {

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool EndsWith(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool ValidateProgressiveSourceFamily(const videosynth::Section& section, std::string* error) {
  const std::string source = Lowercase(section.source);

  if (EndsWith(source, ".exr") || EndsWith(source, ".mov")) {
    return true;
  }

  if (error != nullptr) {
    *error =
        "Unsupported progressive source family. Supported source families are EXR and MOV.";
  }
  return false;
}

bool FrameRateMatchesStandard(double frame_rate_hz, videosynth::Standard standard) {
  if (frame_rate_hz <= 0.0) {
    return true;
  }

  if (standard == videosynth::Standard::kPal) {
    return std::abs(frame_rate_hz - 25.0) <= 1.0e-3;
  }
  if (standard == videosynth::Standard::kNtsc) {
    const double ntsc_rate = 30000.0 / 1001.0;
    return std::abs(frame_rate_hz - ntsc_rate) <= 1.0e-3;
  }
  return false;
}

bool RasterMatchesStandard(int width, int height, videosynth::Standard standard) {
  if (width <= 0 || height <= 0) {
    return true;
  }

  if (standard == videosynth::Standard::kPal) {
    return (width == 720 || width == 704) && height == 576;
  }
  if (standard == videosynth::Standard::kNtsc) {
    return (width == 720 || width == 704) && height == 480;
  }
  return false;
}

bool ContainsCsvToken(const std::string& csv, const std::string& token) {
  if (token.empty()) {
    return false;
  }

  std::size_t start = 0;
  while (start <= csv.size()) {
    const std::size_t comma = csv.find(',', start);
    const std::size_t end = (comma == std::string::npos) ? csv.size() : comma;
    const std::string item = csv.substr(start, end - start);
    if (item == token) {
      return true;
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return false;
}

bool ValidateProfileBySourceFamily(const videosynth::Section& section,
                                   const videosynth::ProgressiveFrameSourceProfile& profile,
                                   std::string* error) {
  const std::string source = Lowercase(section.source);
  const std::string container = Lowercase(profile.container);
  const std::string codec = Lowercase(profile.codec);
  const std::string pixel_format = Lowercase(profile.pixel_format);

  if (EndsWith(source, ".exr")) {
    if (container != "exr") {
      if (error != nullptr) {
        *error = "Progressive EXR sections require an EXR container profile.";
      }
      return false;
    }
    if (codec != "openexr") {
      if (error != nullptr) {
        *error = "Progressive EXR sections only support OpenEXR codec profile.";
      }
      return false;
    }
    if (pixel_format != "rgbh" && pixel_format != "rgbf") {
      if (error != nullptr) {
        *error = "Progressive EXR sections require RGB HALF or FLOAT channel profile.";
      }
      return false;
    }
    if (profile.bit_depth != 16 && profile.bit_depth != 32) {
      if (error != nullptr) {
        *error = "Progressive EXR sections only support 16-bit HALF or 32-bit FLOAT channels.";
      }
      return false;
    }
    return true;
  }

  if (EndsWith(source, ".mov")) {
    if (!ContainsCsvToken(container, "mov")) {
      if (error != nullptr) {
        *error = "Progressive MOV sections require a MOV container profile.";
      }
      return false;
    }
    if (codec != "v210") {
      if (error != nullptr) {
        *error = "Progressive MOV sections only support v210 video codec.";
      }
      return false;
    }
    if (pixel_format != "yuv422p10le") {
      if (error != nullptr) {
        *error = "Progressive MOV sections only support yuv422p10le pixel format.";
      }
      return false;
    }
    if (profile.bit_depth > 0 && profile.bit_depth != 10) {
      if (error != nullptr) {
        *error = "Progressive MOV sections only support 10-bit sample depth.";
      }
      return false;
    }
    return true;
  }

  return true;
}

void ValidateDeferredLaserdiscPresetFlags(const videosynth::Project& project,
                                          videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return;
  }

  if (project.cvbs_presets.pal_laserdisc_pilot_burst &&
      project.cvbs_presets.video_standard_preset != videosynth::Standard::kPal) {
    result->is_valid = false;
    result->errors.push_back(
        "MVP constraint violation: pal_laserdisc_pilot_burst can only be enabled for PAL projects.");
    return;
  }

  if (project.cvbs_presets.ntsc_laserdisc_vbi_burst &&
      project.cvbs_presets.video_standard_preset != videosynth::Standard::kNtsc) {
    result->is_valid = false;
    result->errors.push_back(
        "MVP constraint violation: ntsc_laserdisc_vbi_burst can only be enabled for NTSC projects.");
    return;
  }

  if (project.cvbs_presets.pal_laserdisc_pilot_burst) {
    result->is_valid = false;
    result->errors.push_back(
        "MVP constraint violation: pal_laserdisc_pilot_burst is parsed but not implemented in the current runtime.");
    return;
  }

  if (project.cvbs_presets.ntsc_laserdisc_vbi_burst) {
    result->is_valid = false;
    result->errors.push_back(
        "MVP constraint violation: ntsc_laserdisc_vbi_burst is parsed but not implemented in the current runtime.");
  }
}

void ValidateDeferredLineInjectionSupport(const videosynth::Section& section,
                                          videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return;
  }

  if (!section.line_injections.empty()) {
    result->is_valid = false;
    result->errors.push_back(
        "MVP constraint violation: line_injections are parsed but not implemented in the current runtime.");
  }
}

}  // namespace

namespace videosynth {

ProjectValidator::ProjectValidator(IProgressiveFrameSourceProbe* progressive_frame_source_probe,
                                   ILogger* logger)
    : progressive_frame_source_probe_(progressive_frame_source_probe), logger_(logger) {}

ValidationResult ProjectValidator::Validate(const Project& project) {
  ValidationResult result;
  result.is_valid = true;

  if (logger_ != nullptr) {
    logger_->Debug("Validating project with " + std::to_string(project.sections.size()) +
                   " section(s).");
  }

  if (project.cvbs_presets.video_standard_preset == Standard::kUnknown) {
    result.is_valid = false;
    result.errors.push_back("MVP constraint violation: video_standard_preset must be 'PAL' or 'NTSC'.");
  }

  if (!IsSupportedSampleEncodingPreset(project.cvbs_presets.sample_encoding_preset)) {
    result.is_valid = false;
    result.errors.push_back(
        "MVP constraint violation: sample_encoding_preset must be one of the supported CVBS or raw presets.");
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

  ValidateDeferredLaserdiscPresetFlags(project, &result);

  if (project.cvbs_presets.video_standard_preset != Standard::kNtsc &&
      project.cvbs_presets.ntsc_black_setup_ire_specified) {
    result.is_valid = false;
    result.errors.push_back(
        "MVP constraint violation: ntsc_black_setup_ire can only be specified for NTSC projects.");
  }

  if (project.cvbs_presets.video_standard_preset == Standard::kNtsc &&
      !IsSupportedNtscBlackSetupIre(project.cvbs_presets.ntsc_black_setup_ire)) {
    result.is_valid = false;
    result.errors.push_back(
        "MVP constraint violation: ntsc_black_setup_ire must be 7.5 or 0.0.");
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
    if (logger_ != nullptr) {
      logger_->Trace("Validating section '" + section.name + "' of type '" + section.type + "'.");
    }

    if (section.type == "progressive") {
      ValidateDeferredLineInjectionSupport(section, &result);
      if (!result.is_valid) {
        break;
      }

      if (section.source.empty()) {
        result.is_valid = false;
        result.errors.push_back(
            "Progressive section validation error: source must be set.");
        break;
      }

      if (!section.duration_frames_all && section.duration_frames <= 0) {
        result.is_valid = false;
        result.errors.push_back(
            "Progressive section validation error: duration_frames must be > 0 or 'all'.");
        break;
      }

      std::string source_family_error;
      if (!ValidateProgressiveSourceFamily(section, &source_family_error)) {
        result.is_valid = false;
        result.errors.push_back(source_family_error);
        break;
      }

      const std::filesystem::path source_path(section.source);
      if (!std::filesystem::exists(source_path) || !std::filesystem::is_regular_file(source_path)) {
        result.is_valid = false;
        result.errors.push_back(
            "Progressive section validation error: source file is not readable.");
        break;
      }

      if (progressive_frame_source_probe_ != nullptr) {
        ProgressiveFrameSourceProfile profile;
        std::string probe_error;
        if (!progressive_frame_source_probe_->Probe(section, &profile, &probe_error)) {
          result.is_valid = false;
          result.errors.push_back(
              probe_error.empty()
                  ? "Progressive section validation error: source profile probing failed."
                  : probe_error);
          break;
        }

        if (!RasterMatchesStandard(profile.width,
                                   profile.height,
                                   project.cvbs_presets.video_standard_preset)) {
          result.is_valid = false;
          result.errors.push_back(
              "Progressive section validation error: source raster must be 720x576 or 704x576 for PAL, and 720x480 or 704x480 for NTSC.");
          break;
        }

        if (!FrameRateMatchesStandard(profile.frame_rate_hz,
                                      project.cvbs_presets.video_standard_preset)) {
          result.is_valid = false;
          result.errors.push_back(
              "Progressive section validation error: source frame rate must match selected video standard.");
          break;
        }

        std::string profile_error;
        if (!ValidateProfileBySourceFamily(section, profile, &profile_error)) {
          result.is_valid = false;
          result.errors.push_back(profile_error.empty()
                                      ? "Progressive section validation error: unsupported source profile."
                                      : profile_error);
          break;
        }
      }

      continue;
    }

    result.is_valid = false;
    result.errors.push_back(
    "Section validation error: section type must be 'progressive'.");
    break;
  }

  if (logger_ != nullptr && result.is_valid) {
    logger_->Debug("Project validation completed successfully.");
  }

  return result;
}

}  // namespace videosynth
