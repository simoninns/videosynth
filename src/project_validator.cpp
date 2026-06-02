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
#include <set>
#include <string>

#include "videosynth/vits_definition_provider.h"

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

  if (EndsWith(source, ".exr") || EndsWith(source, ".mkv")) {
    return true;
  }

  if (error != nullptr) {
    *error =
        "Unsupported progressive source family. Supported source families are EXR and MKV.";
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
    return width == 720 && height == 576;
  }
  if (standard == videosynth::Standard::kNtsc) {
    return width == 720 && height == 486;
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

bool SampleAspectMatchesStandard(double sample_aspect_ratio,
                                 videosynth::Standard standard) {
  if (sample_aspect_ratio <= 0.0) {
    return true;
  }

  if (standard == videosynth::Standard::kPal) {
    return std::abs(sample_aspect_ratio - (128.0 / 117.0)) <= 2.0e-3;
  }
  if (standard == videosynth::Standard::kNtsc) {
    return std::abs(sample_aspect_ratio - (108.0 / 119.0)) <= 2.0e-3;
  }
  return false;
}

bool ValidateProfileBySourceFamily(const videosynth::Section& section,
                                   videosynth::Standard standard,
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
    if (pixel_format != "rgbf") {
      if (error != nullptr) {
        *error = "Progressive EXR sections require RGB FLOAT channel profile.";
      }
      return false;
    }
    if (profile.bit_depth != 32) {
      if (error != nullptr) {
        *error = "Progressive EXR sections only support 32-bit FLOAT channels.";
      }
      return false;
    }
    return true;
  }

  if (EndsWith(source, ".mkv")) {
    if (!ContainsCsvToken(container, "matroska")) {
      if (error != nullptr) {
        *error = "Progressive MKV sections require a Matroska container profile.";
      }
      return false;
    }
    if (codec != "ffv1") {
      if (error != nullptr) {
        *error = "Progressive MKV sections only support FFV1 video codec.";
      }
      return false;
    }
    if (pixel_format != "yuv422p10le") {
      if (error != nullptr) {
        *error = "Progressive MKV sections only support yuv422p10le pixel format.";
      }
      return false;
    }
    if (profile.bit_depth > 0 && profile.bit_depth != 10) {
      if (error != nullptr) {
        *error = "Progressive MKV sections only support 10-bit sample depth.";
      }
      return false;
    }

    if (profile.color_space != "smpte170m") {
      if (error != nullptr) {
        *error = "Progressive MKV sections require smpte170m color matrix metadata.";
      }
      return false;
    }

    if (standard == videosynth::Standard::kPal) {
      if (profile.field_order != "tb") {
        if (error != nullptr) {
          *error = "Progressive PAL MKV sections require top-field-first field order metadata (tb).";
        }
        return false;
      }
      if (profile.color_primaries != "bt470bg") {
        if (error != nullptr) {
          *error = "Progressive PAL MKV sections require bt470bg color primaries metadata.";
        }
        return false;
      }
      if (!(profile.color_transfer == "bt709" || profile.color_transfer == "bt470bg")) {
        if (error != nullptr) {
          *error = "Progressive PAL MKV sections require bt709 or bt470bg transfer metadata.";
        }
        return false;
      }
    } else if (standard == videosynth::Standard::kNtsc) {
      if (profile.field_order != "bt") {
        if (error != nullptr) {
          *error = "Progressive NTSC MKV sections require bottom-field-first field order metadata (bt).";
        }
        return false;
      }
      if (profile.color_primaries != "smpte170m") {
        if (error != nullptr) {
          *error = "Progressive NTSC MKV sections require smpte170m color primaries metadata.";
        }
        return false;
      }
      if (!(profile.color_transfer == "bt709" || profile.color_transfer == "smpte170m")) {
        if (error != nullptr) {
          *error = "Progressive NTSC MKV sections require bt709 or smpte170m transfer metadata.";
        }
        return false;
      }
    }

    if (!profile.color_range.empty() && profile.color_range != "tv") {
      if (error != nullptr) {
        *error = "Progressive MKV sections require tv color range when color_range metadata is present.";
      }
      return false;
    }

    if (!SampleAspectMatchesStandard(profile.sample_aspect_ratio, standard)) {
      if (error != nullptr) {
        *error = "Progressive MKV sections require BT.601 sample-aspect metadata for the selected standard.";
      }
      return false;
    }

    if (profile.crop_left != 0 || profile.crop_right != 0 ||
        profile.crop_top != 0 || profile.crop_bottom != 0) {
      if (error != nullptr) {
        *error = "Progressive MKV sections must not include stream crop metadata.";
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

  for (const videosynth::Section::LineInjection& injection : section.line_injections) {
    if (Lowercase(injection.type) == "vits") {
      continue;
    }

    result->is_valid = false;
    result->errors.push_back(
        "MVP constraint violation: line injection type '" + injection.type +
        "' is not implemented in the current runtime.");
    return;
  }
}

bool IsKnownLineInjectionType(const std::string& type) {
  return type == "vits" || type == "laserdisc" || type == "vitc" ||
         type == "line_content";
}

bool IsValidFrameLineForStandard(int line_1based, videosynth::Standard standard) {
  if (standard == videosynth::Standard::kPal) {
    return line_1based >= 1 && line_1based <= 625;
  }
  if (standard == videosynth::Standard::kNtsc) {
    return line_1based >= 1 && line_1based <= 525;
  }
  return false;
}

bool IsLaserdiscReservedLine(int line_1based, videosynth::Standard standard) {
  if (standard == videosynth::Standard::kPal) {
    return (line_1based >= 6 && line_1based <= 18) ||
           (line_1based >= 319 && line_1based <= 331);
  }
  if (standard == videosynth::Standard::kNtsc) {
    return (line_1based >= 10 && line_1based <= 18) ||
           (line_1based >= 273 && line_1based <= 281);
  }
  return false;
}

bool ValidateLineInjectionsForSection(const videosynth::Section& section,
                                      videosynth::Standard standard,
                                      videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return false;
  }

  videosynth::VitsDefinitionProvider vits_definition_provider;
  std::set<int> claimed_target_lines;
  bool has_laserdisc_injection = false;
  bool has_vitc_injection = false;

  for (const videosynth::Section::LineInjection& injection : section.line_injections) {
    const std::string injection_type = Lowercase(injection.type);

    if (!IsKnownLineInjectionType(injection_type)) {
      result->is_valid = false;
      result->errors.push_back(
          "Line injection validation error: unsupported injection type '" +
          injection.type + "'.");
      return false;
    }

    if (injection_type == "laserdisc") {
      has_laserdisc_injection = true;
      if (!injection.target_lines.empty()) {
        result->is_valid = false;
        result->errors.push_back(
            "Line injection validation error: target_lines must not be specified for laserdisc injections.");
        return false;
      }
    } else {
      if (injection.target_lines.empty()) {
        result->is_valid = false;
        result->errors.push_back(
            "Line injection validation error: target_lines must be provided and non-empty for injection type '" +
            injection.type + "'.");
        return false;
      }

      for (int line_1based : injection.target_lines) {
        if (!IsValidFrameLineForStandard(line_1based, standard)) {
          result->is_valid = false;
          result->errors.push_back(
              "Line injection validation error: target line " +
              std::to_string(line_1based) + " is outside the valid frame-line range for " +
              videosynth::StandardToString(standard) + ".");
          return false;
        }

        if (!claimed_target_lines.insert(line_1based).second) {
          result->is_valid = false;
          result->errors.push_back(
              "Line injection validation error: overlapping target line " +
              std::to_string(line_1based) + " within the same section.");
          return false;
        }
      }
    }

    if (injection_type == "vitc") {
      has_vitc_injection = true;
    }

    if (injection_type == "vits") {
      if (injection.vits_type.empty()) {
        result->is_valid = false;
        result->errors.push_back(
            "Line injection validation error: vits injections require a non-empty vits_type.");
        return false;
      }

      videosynth::VitsDefinition vits_definition;
      std::string vits_error;
      if (!vits_definition_provider.TryGetDefinition(standard,
                                                     injection.vits_type,
                                                     &vits_definition,
                                                     &vits_error)) {
        result->is_valid = false;
        result->errors.push_back("Line injection validation error: " + vits_error);
        return false;
      }

      // Strict policy: vits types with a defined placement line must target only that line.
      if (vits_definition.recommended_frame_line > 0) {
        for (int line_1based : injection.target_lines) {
          if (line_1based != vits_definition.recommended_frame_line) {
            result->is_valid = false;
            result->errors.push_back(
                "Line injection validation error: vits_type '" + injection.vits_type +
                "' must target frame line " +
                std::to_string(vits_definition.recommended_frame_line) +
                " for " + videosynth::StandardToString(standard) + ".");
            return false;
          }
        }
      }
    }
  }

  if (has_laserdisc_injection && has_vitc_injection) {
    result->is_valid = false;
    result->errors.push_back(
        "Line injection validation error: vitc and laserdisc injections cannot appear in the same section.");
    return false;
  }

  if (has_laserdisc_injection) {
    for (const videosynth::Section::LineInjection& injection : section.line_injections) {
      if (Lowercase(injection.type) == "laserdisc") {
        continue;
      }

      for (int line_1based : injection.target_lines) {
        if (IsLaserdiscReservedLine(line_1based, standard)) {
          result->is_valid = false;
          result->errors.push_back(
              "Line injection validation error: target line " +
              std::to_string(line_1based) +
              " conflicts with laserdisc reserved VBI ranges for " +
              videosynth::StandardToString(standard) + ".");
          return false;
        }
      }
    }
  }

  return true;
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
      if (!ValidateLineInjectionsForSection(section,
                                            project.cvbs_presets.video_standard_preset,
                                            &result)) {
        break;
      }

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
              "Progressive section validation error: source raster must be 720x576 for PAL and 720x486 for NTSC.");
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
        if (!ValidateProfileBySourceFamily(section,
                   project.cvbs_presets.video_standard_preset,
                   profile,
                   &profile_error)) {
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
