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
#include <cstdint>
#include <set>
#include <string>

#include "videosynth/biphase_types.h"
#include "videosynth/dropout_scale.h"
#include "videosynth/osd_token_resolver.h"
#include "videosynth/vits_definition_provider.h"

namespace {

// Noise parameter bounds from noise-injection-design.md.
// Lower: 20 dB — noise amplitude approaches sync pulse depth; sync separators
// fail. Upper: 61 dB — injected noise smaller than 10-bit quantisation floor
// (~0.085 IRE).
constexpr double kNoiseDbMin = 20.0;
constexpr double kNoiseDbMax = 61.0;

// Minimum CAV laserdisc section durations derived from IEC track-pitch limits.
// IEC 60856/60857: lead-in ≥ 1.5 mm, lead-out ≥ 2 mm at nominal 1.6 µm pitch.
constexpr int kLaserdiscLeadInMinFrames = 938;    // ceil(1500 µm / 1.6 µm)
constexpr int kLaserdiscLeadOutMinFrames = 1250;  // ceil(2000 µm / 1.6 µm)

// Maximum picture/timecode values per IEC 60856/60857.
constexpr int kPalMaxPictureNumber = 99999;
constexpr int kNtscMaxPictureNumber = 79999;
constexpr int kMaxChapterNumber = 79;
constexpr uint32_t kMaxUsersCodeX1Nibble = 7;

std::string Lowercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// Returns true if code_type is allowed in the given section_type per IEC
// 60856/60857 Appendix D.
bool IsCodeTypeValidForSectionType(const std::string& code_type,
                                   videosynth::SectionType section_type) {
  using ST = videosynth::SectionType;
  if (code_type == "lead_in") {
    return section_type == ST::kLeadIn;
  }
  if (code_type == "lead_out") {
    return section_type == ST::kLeadOut;
  }
  if (code_type == "users_code") {
    return section_type == ST::kLeadIn || section_type == ST::kLeadOut;
  }
  if (code_type == "fm_white_flag") {
    // Valid in all three section types.
    return section_type == ST::kLeadIn || section_type == ST::kProgrammeArea ||
           section_type == ST::kLeadOut;
  }
  // All remaining codes (picture_number, picture_stop, chapter_number,
  // programme_status, programme_time_code, clv_code, clv_picture_number,
  // fm_picture_number, fm_programme_time) are programme_area only.
  return section_type == ST::kProgrammeArea;
}

// Returns true if code_type requires NTSC standard (IEC 60857 FM codes).
bool IsNtscOnlyCodeType(const std::string& code_type) {
  return code_type == "fm_picture_number" || code_type == "fm_programme_time" ||
         code_type == "fm_white_flag";
}

// Parses a decimal or "0x"-prefixed hex string into a uint32_t.
bool ParseHexValue(const std::string& hex_str, uint32_t* out_value) {
  if (hex_str.empty() || out_value == nullptr) {
    return false;
  }
  try {
    std::size_t pos = 0;
    const auto val = std::stoul(hex_str, &pos, 0);
    if (pos != hex_str.size()) {
      return false;
    }
    *out_value = static_cast<uint32_t>(val);
    return true;
  } catch (...) {
    return false;
  }
}

bool EndsWith(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
         0;
}

bool ValidateProgressiveSourceFamily(const videosynth::Section& section,
                                     std::string* error) {
  const std::string source = Lowercase(section.source);

  if (EndsWith(source, ".exr") || EndsWith(source, ".mkv")) {
    return true;
  }

  if (error != nullptr) {
    *error =
        "Unsupported progressive source family. Supported source families are "
        "EXR and MKV.";
  }
  return false;
}

bool FrameRateMatchesStandard(double frame_rate_hz,
                              videosynth::Standard standard) {
  if (frame_rate_hz <= 0.0) {
    return true;
  }

  if (standard == videosynth::Standard::kPal) {
    return std::abs(frame_rate_hz - 25.0) <= 1.0e-3;
  }
  if (standard == videosynth::Standard::kNtsc ||
      standard == videosynth::Standard::kPalM) {
    const double ntsc_rate = 30000.0 / 1001.0;
    return std::abs(frame_rate_hz - ntsc_rate) <= 1.0e-3;
  }
  return false;
}

bool RasterMatchesStandard(int width, int height,
                           videosynth::Standard standard) {
  if (width <= 0 || height <= 0) {
    return true;
  }

  if (standard == videosynth::Standard::kPal) {
    return width == 720 && height == 576;
  }
  if (standard == videosynth::Standard::kNtsc ||
      standard == videosynth::Standard::kPalM) {
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
  if (standard == videosynth::Standard::kNtsc ||
      standard == videosynth::Standard::kPalM) {
    return std::abs(sample_aspect_ratio - (108.0 / 119.0)) <= 2.0e-3;
  }
  return false;
}

bool ValidateProfileBySourceFamily(
    const videosynth::Section& section, videosynth::Standard standard,
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
        *error =
            "Progressive MKV sections require a Matroska container profile.";
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
        *error =
            "Progressive MKV sections only support yuv422p10le pixel format.";
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
        *error =
            "Progressive MKV sections require smpte170m color matrix metadata.";
      }
      return false;
    }

    if (standard == videosynth::Standard::kPal) {
      if (profile.field_order != "tb") {
        if (error != nullptr) {
          *error =
              "Progressive PAL MKV sections require top-field-first field "
              "order metadata (tb).";
        }
        return false;
      }
      if (profile.color_primaries != "bt470bg") {
        if (error != nullptr) {
          *error =
              "Progressive PAL MKV sections require bt470bg color primaries "
              "metadata.";
        }
        return false;
      }
      if (!(profile.color_transfer == "bt709" ||
            profile.color_transfer == "bt470bg")) {
        if (error != nullptr) {
          *error =
              "Progressive PAL MKV sections require bt709 or bt470bg transfer "
              "metadata.";
        }
        return false;
      }
    } else if (standard == videosynth::Standard::kNtsc) {
      if (profile.field_order != "bt") {
        if (error != nullptr) {
          *error =
              "Progressive NTSC MKV sections require bottom-field-first field "
              "order metadata (bt).";
        }
        return false;
      }
      if (profile.color_primaries != "smpte170m") {
        if (error != nullptr) {
          *error =
              "Progressive NTSC MKV sections require smpte170m color primaries "
              "metadata.";
        }
        return false;
      }
      if (!(profile.color_transfer == "bt709" ||
            profile.color_transfer == "smpte170m")) {
        if (error != nullptr) {
          *error =
              "Progressive NTSC MKV sections require bt709 or smpte170m "
              "transfer metadata.";
        }
        return false;
      }
    } else if (standard == videosynth::Standard::kPalM) {
      if (profile.field_order != "bt") {
        if (error != nullptr) {
          *error =
              "Progressive PAL-M MKV sections require bottom-field-first field "
              "order metadata (bt).";
        }
        return false;
      }
      if (profile.color_primaries != "bt470bg" &&
          profile.color_primaries != "smpte170m") {
        if (error != nullptr) {
          *error =
              "Progressive PAL-M MKV sections require bt470bg or smpte170m "
              "color primaries metadata.";
        }
        return false;
      }
      if (!(profile.color_transfer == "bt709" ||
            profile.color_transfer == "bt470bg" ||
            profile.color_transfer == "smpte170m")) {
        if (error != nullptr) {
          *error =
              "Progressive PAL-M MKV sections require bt709, bt470bg, or "
              "smpte170m transfer metadata.";
        }
        return false;
      }
    }

    if (!profile.color_range.empty() && profile.color_range != "tv") {
      if (error != nullptr) {
        *error =
            "Progressive MKV sections require tv color range when color_range "
            "metadata is present.";
      }
      return false;
    }

    if (!SampleAspectMatchesStandard(profile.sample_aspect_ratio, standard)) {
      if (error != nullptr) {
        *error =
            "Progressive MKV sections require BT.601 sample-aspect metadata "
            "for the selected standard.";
      }
      return false;
    }

    if (profile.crop_left != 0 || profile.crop_right != 0 ||
        profile.crop_top != 0 || profile.crop_bottom != 0) {
      if (error != nullptr) {
        *error =
            "Progressive MKV sections must not include stream crop metadata.";
      }
      return false;
    }

    return true;
  }

  return true;
}

void ValidateDeferredLaserdiscPresetFlags(
    const videosynth::Project& project, videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return;
  }

  if (project.cvbs_presets.pal_laserdisc_pilot_burst &&
      project.cvbs_presets.video_standard_preset !=
          videosynth::Standard::kPal) {
    result->is_valid = false;
    result->errors.push_back(
        "Project configuration error: pal_laserdisc_pilot_burst can only be "
        "enabled for PAL projects.");
    return;
  }

  if (project.cvbs_presets.pal_laserdisc_pilot_burst &&
      !videosynth::IsSubSyncCapableSampleEncodingPreset(
          project.cvbs_presets.sample_encoding_preset)) {
    result->warnings.push_back(
        "pal_laserdisc_pilot_burst warning: preset '" +
        project.cvbs_presets.sample_encoding_preset +
        "' clips sub-sync excursions below -300 mV; the pilot burst trough "
        "reaches -600 mV. Use CVBS_S16_FSC or RAW_S16_28M/RAW_S16_40M to "
        "preserve the full burst waveform.");
  }

  if (project.cvbs_presets.ntsc_laserdisc_vbi_burst &&
      project.cvbs_presets.video_standard_preset !=
          videosynth::Standard::kNtsc) {
    result->is_valid = false;
    result->errors.push_back(
        "Project configuration error: ntsc_laserdisc_vbi_burst can only be "
        "enabled for NTSC projects.");
    return;
  }

  if (project.cvbs_presets.ntsc_laserdisc_vbi_burst) {
    result->is_valid = false;
    result->errors.push_back(
        "Project configuration error: ntsc_laserdisc_vbi_burst is parsed but "
        "not "
        "implemented in the current runtime.");
  }
}

void ValidateDeferredLineInjectionSupport(
    const videosynth::Section& section, videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return;
  }

  for (const videosynth::Section::LineInjection& injection :
       section.line_injections) {
    const std::string type = Lowercase(injection.type);
    if (type == "vits" || type == "laserdisc") {
      continue;
    }

    result->is_valid = false;
    result->errors.push_back(
        "Project configuration error: line injection type '" + injection.type +
        "' is not implemented in the current runtime.");
    return;
  }
}

bool IsKnownLineInjectionType(const std::string& type) {
  return type == "vits" || type == "laserdisc" || type == "vitc" ||
         type == "line_content";
}

bool IsValidFrameLineForStandard(int line_1based,
                                 videosynth::Standard standard) {
  if (standard == videosynth::Standard::kPal) {
    return line_1based >= 1 && line_1based <= 625;
  }
  if (standard == videosynth::Standard::kNtsc ||
      standard == videosynth::Standard::kPalM) {
    return line_1based >= 1 && line_1based <= 525;
  }
  return false;
}

bool IsLaserdiscReservedLine(int line_1based, videosynth::Standard standard) {
  if (standard == videosynth::Standard::kPal) {
    return (line_1based >= 6 && line_1based <= 18) ||
           (line_1based >= 319 && line_1based <= 331);
  }
  if (standard == videosynth::Standard::kNtsc ||
      standard == videosynth::Standard::kPalM) {
    return (line_1based >= 10 && line_1based <= 18) ||
           (line_1based >= 273 && line_1based <= 281);
  }
  return false;
}

// Validates structural correctness of laserdisc injection fields when present.
// When disc_type is specified it must be CAV or CLV; code_types must be valid
// for the specified disc_type.  disc_type is optional in Phase 1 — it will be
// enforced when laserdisc injection is fully implemented.
bool ValidateLaserdiscInjectionStructure(const videosynth::Section& section,
                                         videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return false;
  }

  for (const videosynth::Section::LineInjection& injection :
       section.line_injections) {
    if (Lowercase(injection.type) != "laserdisc") {
      continue;
    }

    if (injection.disc_type.empty()) {
      continue;
    }

    const videosynth::DiscType disc_type =
        videosynth::DiscTypeFromString(injection.disc_type);
    if (disc_type == videosynth::DiscType::kUnknown) {
      result->is_valid = false;
      result->errors.push_back(
          "Laserdisc injection validation error: disc_type '" +
          injection.disc_type +
          "' is not recognised. Expected 'CAV' or 'CLV'.");
      return false;
    }

    for (const videosynth::Section::LineInjectionCode& code : injection.codes) {
      if (!videosynth::IsKnownLaserdiscCodeType(code.code_type)) {
        result->is_valid = false;
        result->errors.push_back(
            "Laserdisc injection validation error: code_type '" +
            code.code_type + "' is not a recognised laserdisc code type.");
        return false;
      }

      if (disc_type == videosynth::DiscType::kCAV &&
          !videosynth::IsValidCavCodeType(code.code_type)) {
        result->is_valid = false;
        result->errors.push_back(
            "Laserdisc injection validation error: code_type '" +
            code.code_type + "' is not valid for CAV discs.");
        return false;
      }

      if (disc_type == videosynth::DiscType::kCLV &&
          !videosynth::IsValidClvCodeType(code.code_type)) {
        result->is_valid = false;
        result->errors.push_back(
            "Laserdisc injection validation error: code_type '" +
            code.code_type + "' is not valid for CLV discs.");
        return false;
      }
    }
  }

  return true;
}

// Validates IEC section-type compatibility, standard restrictions, minimum
// durations, and code-parameter value ranges for laserdisc injections.
// Only runs when disc_type is present on an injection (Phase-1 injections
// without disc_type pass through without error).
bool ValidateLaserdiscSectionTypeAndCodes(
    const videosynth::Section& section, videosynth::Standard standard,
    videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return false;
  }

  for (const videosynth::Section::LineInjection& injection :
       section.line_injections) {
    if (Lowercase(injection.type) != "laserdisc") {
      continue;
    }
    if (injection.disc_type.empty()) {
      continue;
    }

    const videosynth::DiscType disc_type =
        videosynth::DiscTypeFromString(injection.disc_type);

    // 5.4: section_type must be set when disc_type is specified.
    if (section.section_type == videosynth::SectionType::kUnknown) {
      result->is_valid = false;
      result->errors.push_back(
          "Laserdisc injection validation error: section_type must be set to "
          "'lead_in', 'programme_area', or 'lead_out' when disc_type is "
          "specified.");
      return false;
    }

    // 5.9/5.10: Minimum section duration for CAV discs.
    // CLV track density varies, so frame-based minimum is not validated.
    if (disc_type == videosynth::DiscType::kCAV &&
        !section.duration_frames_all) {
      if (section.section_type == videosynth::SectionType::kLeadIn &&
          section.duration_frames < kLaserdiscLeadInMinFrames) {
        result->warnings.push_back(
            "Laserdisc injection warning: CAV lead_in section has " +
            std::to_string(section.duration_frames) +
            " frames; IEC 1.5 mm minimum at 1.6 um track pitch requires " +
            std::to_string(kLaserdiscLeadInMinFrames) + ".");
      }
      if (section.section_type == videosynth::SectionType::kLeadOut &&
          section.duration_frames < kLaserdiscLeadOutMinFrames) {
        result->warnings.push_back(
            "Laserdisc injection warning: CAV lead_out section has " +
            std::to_string(section.duration_frames) +
            " frames; IEC 2 mm minimum at 1.6 um track pitch requires " +
            std::to_string(kLaserdiscLeadOutMinFrames) + ".");
      }
    }

    // 5.4/5.5: Validate each code type.
    for (const videosynth::Section::LineInjectionCode& code : injection.codes) {
      // Section-type compatibility (IEC Appendix D matrix).
      if (!IsCodeTypeValidForSectionType(code.code_type,
                                         section.section_type)) {
        result->is_valid = false;
        result->errors.push_back(
            "Laserdisc injection validation error: code_type '" +
            code.code_type + "' is not allowed in '" +
            videosynth::SectionTypeToString(section.section_type) +
            "' sections.");
        return false;
      }

      // Standard restriction: FM codes require System M (NTSC or PAL-M).
      if (IsNtscOnlyCodeType(code.code_type) &&
          standard != videosynth::Standard::kNtsc &&
          standard != videosynth::Standard::kPalM) {
        result->is_valid = false;
        result->errors.push_back(
            "Laserdisc injection validation error: code_type '" +
            code.code_type + "' is only valid for NTSC or PAL-M projects.");
        return false;
      }

      // 5.5: picture_number value range (IEC 60856/60857).
      if (code.code_type == "picture_number" && code.start_value_specified) {
        const int max_pn = (standard == videosynth::Standard::kNtsc ||
                            standard == videosynth::Standard::kPalM)
                               ? kNtscMaxPictureNumber
                               : kPalMaxPictureNumber;
        if (code.start_value < 0 || code.start_value > max_pn) {
          result->is_valid = false;
          result->errors.push_back(
              "Laserdisc injection validation error: picture_number "
              "start_value must be in the range 0-" +
              std::to_string(max_pn) + " for " +
              videosynth::StandardToString(standard) + "; got " +
              std::to_string(code.start_value) + ".");
          return false;
        }
      }

      // 5.5: fm_picture_number value range (IEC 60857 Amendment 2 §10.2.3).
      if (code.code_type == "fm_picture_number" && code.start_value_specified) {
        if (code.start_value < 0 || code.start_value > kNtscMaxPictureNumber) {
          result->is_valid = false;
          result->errors.push_back(
              "Laserdisc injection validation error: fm_picture_number "
              "start_value must be in the range 0-" +
              std::to_string(kNtscMaxPictureNumber) + "; got " +
              std::to_string(code.start_value) + ".");
          return false;
        }
      }

      // 5.5: chapter_number range (IEC 60856/60857: max 79 chapters).
      if (code.code_type == "chapter_number" && code.chapter_specified) {
        if (code.chapter < 0 || code.chapter > kMaxChapterNumber) {
          result->is_valid = false;
          result->errors.push_back(
              "Laserdisc injection validation error: chapter_number chapter "
              "must be in the range 0-" +
              std::to_string(kMaxChapterNumber) + "; got " +
              std::to_string(code.chapter) + ".");
          return false;
        }
      }

      // 5.5: users_code format (IEC 60856/60857 §10.1.9): 8 X1 D X3 X4 X5.
      //   X1 (bits 19-16) must be 0-7; D (bits 15-12) must be 0xD.
      if (code.code_type == "users_code" && code.users_code_specified) {
        uint32_t hex_value = 0;
        if (!ParseHexValue(code.users_code, &hex_value)) {
          result->is_valid = false;
          result->errors.push_back(
              "Laserdisc injection validation error: users_code '" +
              code.users_code + "' is not a valid hex value.");
          return false;
        }
        if (hex_value > 0xFFFFFFu) {
          result->is_valid = false;
          result->errors.push_back(
              "Laserdisc injection validation error: users_code '" +
              code.users_code + "' exceeds 24-bit range (max 0xFFFFFF).");
          return false;
        }
        const uint32_t x1 = (hex_value >> 16u) & 0x0Fu;
        if (x1 > kMaxUsersCodeX1Nibble) {
          result->is_valid = false;
          result->errors.push_back(
              "Laserdisc injection validation error: users_code X1 nibble "
              "must be 0-7; '" +
              code.users_code + "' has X1=" + std::to_string(x1) + ".");
          return false;
        }
        const uint32_t d_nibble = (hex_value >> 12u) & 0x0Fu;
        if (d_nibble != 0xDu) {
          result->is_valid = false;
          result->errors.push_back(
              "Laserdisc injection validation error: users_code D nibble "
              "(bits 15-12) must be 0xD (IEC §10.1.9); '" +
              code.users_code + "' has D=0x" + std::to_string(d_nibble) + ".");
          return false;
        }
      }
    }
  }

  return true;
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

  for (const videosynth::Section::LineInjection& injection :
       section.line_injections) {
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
            "Line injection validation error: target_lines must not be "
            "specified for laserdisc injections.");
        return false;
      }
    } else {
      if (injection.target_lines.empty()) {
        result->is_valid = false;
        result->errors.push_back(
            "Line injection validation error: target_lines must be provided "
            "and non-empty for injection type '" +
            injection.type + "'.");
        return false;
      }

      for (int line_1based : injection.target_lines) {
        if (!IsValidFrameLineForStandard(line_1based, standard)) {
          result->is_valid = false;
          result->errors.push_back(
              "Line injection validation error: target line " +
              std::to_string(line_1based) +
              " is outside the valid frame-line range for " +
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
            "Line injection validation error: vits injections require a "
            "non-empty vits_type.");
        return false;
      }

      videosynth::VitsDefinition vits_definition;
      std::string vits_error;
      if (!vits_definition_provider.TryGetDefinition(
              standard, injection.vits_type, &vits_definition, &vits_error)) {
        result->is_valid = false;
        result->errors.push_back("Line injection validation error: " +
                                 vits_error);
        return false;
      }

      // Strict policy: vits types with a defined placement line must target
      // only that line.
      if (vits_definition.recommended_frame_line > 0) {
        for (int line_1based : injection.target_lines) {
          if (line_1based != vits_definition.recommended_frame_line) {
            result->is_valid = false;
            result->errors.push_back(
                "Line injection validation error: vits_type '" +
                injection.vits_type + "' must target frame line " +
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
        "Line injection validation error: vitc and laserdisc injections cannot "
        "appear in the same section.");
    return false;
  }

  if (has_laserdisc_injection) {
    // 5.11/5.12: All non-laserdisc injections must not target laserdisc
    // reserved VBI lines.
    for (const videosynth::Section::LineInjection& injection :
         section.line_injections) {
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

    // 5.13: NTSC and PAL-M laserdisc sections require a virs VITS injection
    // for colour (IEC 60857 §9.1.3).
    if (standard == videosynth::Standard::kNtsc ||
        standard == videosynth::Standard::kPalM) {
      bool has_virs = false;
      for (const videosynth::Section::LineInjection& injection :
           section.line_injections) {
        if (Lowercase(injection.type) == "vits" &&
            injection.vits_type == "virs") {
          has_virs = true;
          break;
        }
      }
      if (!has_virs) {
        result->is_valid = false;
        result->errors.push_back(
            "Line injection validation error: NTSC and PAL-M laserdisc "
            "sections require a virs VITS injection for colour "
            "(IEC 60857 §9.1.3).");
        return false;
      }
    }
  }

  return true;
}

// PAL/NTSC frame lines that orc-gui inspects for White SNR measurement.
// PAL: field-relative line 19. NTSC: field-relative line 20.
// Source: noise-injection-design.md §orc-gui Measurement Analysis.
constexpr int kOrcGuiWhiteSNRLinePal = 19;
constexpr int kOrcGuiWhiteSNRLineNtsc = 20;

void ValidateNoiseParameters(const videosynth::Section& section,
                             videosynth::Standard standard,
                             videosynth::ValidationResult* result) {
  if (result == nullptr || !section.noise.enabled) {
    return;
  }

  if (section.noise.noise_db < kNoiseDbMin ||
      section.noise.noise_db > kNoiseDbMax) {
    result->is_valid = false;
    result->errors.push_back(
        "Noise validation error in section '" + section.name +
        "': noise_db must be in the range [20.0, 61.0]; got " +
        std::to_string(section.noise.noise_db) + ".");
    return;
  }

  if (section.noise.noise_spread_db < 0.0) {
    result->is_valid = false;
    result->errors.push_back(
        "Noise validation error in section '" + section.name +
        "': noise_spread_db must be >= 0.0; got " +
        std::to_string(section.noise.noise_spread_db) + ".");
    return;
  }

  const double white_snr =
      section.noise.noise_db - section.noise.noise_spread_db;
  if (white_snr < kNoiseDbMin) {
    result->is_valid = false;
    result->errors.push_back("Noise validation error in section '" +
                             section.name +
                             "': noise_db - noise_spread_db must be >= 20.0 "
                             "(White SNR floor); got " +
                             std::to_string(white_snr) + ".");
    return;
  }

  // Warn if the noise spread target is non-zero but no suitable VITS white-flag
  // injection is present on the line orc-gui inspects for White SNR.
  if (section.noise.noise_spread_db > 0.0) {
    const int expected_line = (standard == videosynth::Standard::kPal)
                                  ? kOrcGuiWhiteSNRLinePal
                                  : kOrcGuiWhiteSNRLineNtsc;
    bool has_white_flag_vits = false;
    for (const videosynth::Section::LineInjection& injection :
         section.line_injections) {
      if (Lowercase(injection.type) == "vits") {
        for (int line : injection.target_lines) {
          if (line == expected_line) {
            has_white_flag_vits = true;
            break;
          }
        }
      }
      if (has_white_flag_vits) {
        break;
      }
    }
    if (!has_white_flag_vits) {
      result->warnings.push_back(
          "Noise warning in section '" + section.name +
          "': noise_spread_db is set, but no VITS injection targets line " +
          std::to_string(expected_line) +
          " — the White SNR target cannot be verified in orc-gui without a "
          "suitable VITS white flag on that line (" +
          videosynth::StandardToString(standard) + ").");
    }
  }
}

// Valid scale range for both random and scratch dropout.
constexpr int kDropoutScaleMin = 0;
constexpr int kDropoutScaleMax = 20;

void ValidateDropoutParameters(const videosynth::Section& section,
                               videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return;
  }

  const videosynth::DropoutParameters& dp = section.dropouts;
  const bool random_active =
      dp.random.enabled && dp.random.scale > kDropoutScaleMin;
  const bool scratch_active =
      dp.scratch.enabled && dp.scratch.scale > kDropoutScaleMin;

  // If neither sub-key is present (both at default/zero) there is nothing to
  // validate; the stage will be a no-op for this section.
  if (!random_active && !scratch_active && dp.random.scale == 0 &&
      dp.scratch.scale == 0) {
    return;
  }

  if (dp.random.scale < kDropoutScaleMin ||
      dp.random.scale > kDropoutScaleMax) {
    result->is_valid = false;
    result->errors.push_back("Dropout validation error in section '" +
                             section.name +
                             "': random.scale must be in [0, 20]; got " +
                             std::to_string(dp.random.scale) + ".");
    return;
  }

  if (dp.scratch.scale < kDropoutScaleMin ||
      dp.scratch.scale > kDropoutScaleMax) {
    result->is_valid = false;
    result->errors.push_back("Dropout validation error in section '" +
                             section.name +
                             "': scratch.scale must be in [0, 20]; got " +
                             std::to_string(dp.scratch.scale) + ".");
    return;
  }

  // A dropouts: block with all-zero scales has no effect — report as a
  // configuration mistake so the user knows the block is inert.
  if (!random_active && !scratch_active) {
    result->is_valid = false;
    result->errors.push_back(
        "Dropout validation error in section '" + section.name +
        "': dropouts block has no active type (both random.scale and "
        "scratch.scale are 0). Remove the dropouts block or set at least one "
        "scale to 1–20.");
    return;
  }

  // Warn when the derived scratch lifespan exceeds the section length.
  if (scratch_active && !section.duration_frames_all) {
    const videosynth::ScratchDropoutDerivedParams sp =
        videosynth::DeriveScratchDropoutParams(dp.scratch.scale);
    if (sp.max_dur_frames > section.duration_frames) {
      result->warnings.push_back(
          "Dropout warning in section '" + section.name +
          "': scratch lifespan derived from scale " +
          std::to_string(dp.scratch.scale) + " (" +
          std::to_string(sp.max_dur_frames) +
          " frames) exceeds section duration (" +
          std::to_string(section.duration_frames) +
          " frames). The envelope will be truncated asymmetrically.");
    }
  }
}

// Valid OSD scale range: 1 pixel-per-glyph-pixel (8×8) to 4 (32×32).
constexpr int kOsdScaleMin = 1;
constexpr int kOsdScaleMax = 4;

void ValidateOsdConfig(const videosynth::Section& section,
                       videosynth::ValidationResult* result) {
  if (result == nullptr) {
    return;
  }

  for (const videosynth::OsdOverlay& ov : section.osd.overlays) {
    if (ov.scale < kOsdScaleMin || ov.scale > kOsdScaleMax) {
      result->is_valid = false;
      result->errors.push_back("OSD validation error in section '" +
                               section.name +
                               "': overlay scale must be in [1, 4]; got " +
                               std::to_string(ov.scale) + ".");
      return;
    }

    if (ov.fg_luma < 0.0 || ov.fg_luma > 1.0) {
      result->is_valid = false;
      result->errors.push_back(
          "OSD validation error in section '" + section.name +
          "': overlay fg_luma must be in [0.0, 1.0]; got " +
          std::to_string(ov.fg_luma) + ".");
      return;
    }

    if (ov.bg_luma != -1.0 && (ov.bg_luma < 0.0 || ov.bg_luma > 1.0)) {
      result->is_valid = false;
      result->errors.push_back(
          "OSD validation error in section '" + section.name +
          "': overlay bg_luma must be -1.0 (transparent) or in [0.0, 1.0]; "
          "got " +
          std::to_string(ov.bg_luma) + ".");
      return;
    }

    std::string unknown_token;
    if (!videosynth::OsdTokenResolver::HasOnlyKnownTokens(ov.text,
                                                          &unknown_token)) {
      result->is_valid = false;
      result->errors.push_back(
          "OSD validation error in section '" + section.name +
          "': overlay text contains unknown token '{" + unknown_token +
          "}'. Supported tokens: {picture_number}, {biphase_hex}, {phase_id}, "
          "{section_name}.");
      return;
    }
  }
}

}  // namespace

namespace videosynth {

ProjectValidator::ProjectValidator(
    IProgressiveFrameSourceProbe* progressive_frame_source_probe,
    ILogger* logger)
    : progressive_frame_source_probe_(progressive_frame_source_probe),
      logger_(logger) {}

ValidationResult ProjectValidator::Validate(const Project& project) {
  ValidationResult result;
  result.is_valid = true;

  if (logger_ != nullptr) {
    logger_->Debug("Validating project with " +
                   std::to_string(project.sections.size()) + " section(s).");
  }

  if (project.cvbs_presets.video_standard_preset == Standard::kUnknown) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: video_standard_preset must be 'PAL', "
        "'NTSC', or 'PAL_M'.");
  }

  if (!IsSupportedSampleEncodingPreset(
          project.cvbs_presets.sample_encoding_preset)) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: sample_encoding_preset must be one of "
        "the "
        "supported CVBS or raw presets.");
  }

  if (project.cvbs_presets.signal_state_preset != "STANDARD_TBC_LOCKED") {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: signal_state_preset must be "
        "'STANDARD_TBC_LOCKED'.");
  }

  if (!IsLockedSignalStatePreset(project.cvbs_presets.signal_state_preset)) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: signal_state_preset must indicate locked "
        "state.");
  }

  ValidateDeferredLaserdiscPresetFlags(project, &result);

  if (project.cvbs_presets.video_standard_preset != Standard::kNtsc &&
      project.cvbs_presets.video_standard_preset != Standard::kPalM &&
      project.cvbs_presets.ntsc_black_setup_ire_specified) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: ntsc_black_setup_ire can only be "
        "specified "
        "for NTSC or PAL-M projects.");
  }

  if ((project.cvbs_presets.video_standard_preset == Standard::kNtsc ||
       project.cvbs_presets.video_standard_preset == Standard::kPalM) &&
      !IsSupportedNtscBlackSetupIre(
          project.cvbs_presets.ntsc_black_setup_ire)) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: ntsc_black_setup_ire must be 7.5 or "
        "0.0.");
  }

  if (project.output.video_path.empty()) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: output.video_path must be set.");
  }

  if (project.output.metadata_path.empty()) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: output.metadata_path must be set.");
  }

  if (!project.output.video_path.empty() &&
      !project.output.metadata_path.empty() &&
      project.output.video_path == project.output.metadata_path) {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: output.video_path and "
        "output.metadata_path "
        "must differ.");
  }

  const std::string& sig_type = project.output.signal_type;
  if (sig_type != "composite" && sig_type != "yc") {
    result.is_valid = false;
    result.errors.push_back(
        "Project configuration error: output.signal_type must be 'composite' "
        "or 'yc'.");
  }
  if (sig_type == "yc" && !project.output.video_path.empty()) {
    const std::string& vp = project.output.video_path;
    if (vp.size() < 2 || vp.compare(vp.size() - 2, 2, ".y") != 0) {
      result.is_valid = false;
      result.errors.push_back(
          "Project configuration error: output.video_path must end in '.y' "
          "when signal_type is 'yc'.");
    }
  }

  if (project.sections.empty()) {
    result.is_valid = false;
    result.errors.push_back("Project must contain at least one section.");
  }

  for (const Section& section : project.sections) {
    if (logger_ != nullptr) {
      logger_->Trace("Validating section '" + section.name + "' of type '" +
                     section.type + "'.");
    }

    if (section.type == "progressive") {
      if (!ValidateLaserdiscInjectionStructure(section, &result)) {
        break;
      }

      if (!ValidateLaserdiscSectionTypeAndCodes(
              section, project.cvbs_presets.video_standard_preset, &result)) {
        break;
      }

      if (!ValidateLineInjectionsForSection(
              section, project.cvbs_presets.video_standard_preset, &result)) {
        break;
      }

      ValidateDeferredLineInjectionSupport(section, &result);
      if (!result.is_valid) {
        break;
      }

      ValidateNoiseParameters(
          section, project.cvbs_presets.video_standard_preset, &result);
      if (!result.is_valid) {
        break;
      }

      ValidateDropoutParameters(section, &result);
      if (!result.is_valid) {
        break;
      }

      ValidateOsdConfig(section, &result);
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
            "Progressive section validation error: duration_frames must be > 0 "
            "or 'all'.");
        break;
      }

      std::string source_family_error;
      if (!ValidateProgressiveSourceFamily(section, &source_family_error)) {
        result.is_valid = false;
        result.errors.push_back(source_family_error);
        break;
      }

      if (progressive_frame_source_probe_ != nullptr) {
        ProgressiveFrameSourceProfile profile;
        std::string probe_error;
        if (!progressive_frame_source_probe_->Probe(section, &profile,
                                                    &probe_error)) {
          result.is_valid = false;
          result.errors.push_back(probe_error.empty()
                                      ? "Progressive section validation error: "
                                        "source profile probing failed."
                                      : probe_error);
          break;
        }

        if (!RasterMatchesStandard(
                profile.width, profile.height,
                project.cvbs_presets.video_standard_preset)) {
          result.is_valid = false;
          result.errors.push_back(
              "Progressive section validation error: source raster must be "
              "720x576 for PAL and 720x486 for NTSC or PAL-M.");
          break;
        }

        if (!FrameRateMatchesStandard(
                profile.frame_rate_hz,
                project.cvbs_presets.video_standard_preset)) {
          result.is_valid = false;
          result.errors.push_back(
              "Progressive section validation error: source frame rate must "
              "match selected video standard.");
          break;
        }

        std::string profile_error;
        if (!ValidateProfileBySourceFamily(
                section, project.cvbs_presets.video_standard_preset, profile,
                &profile_error)) {
          result.is_valid = false;
          result.errors.push_back(profile_error.empty()
                                      ? "Progressive section validation error: "
                                        "unsupported source profile."
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

  // Validate disc_skips (only when sections validated successfully).
  if (result.is_valid && !project.disc_skips.empty()) {
    // Compute total disc frames from sections.
    int total_disc_frames = 0;
    for (const Section& s : project.sections) {
      total_disc_frames += s.duration_frames;
    }

    for (std::size_t si = 0; si < project.disc_skips.size(); ++si) {
      const DiscSkip& skip = project.disc_skips[si];
      const std::string ctx = "disc_skips[" + std::to_string(si) + "]: ";

      if (skip.at_frame < 1 || skip.at_frame > total_disc_frames) {
        result.is_valid = false;
        result.errors.push_back(
            ctx + "at_frame " + std::to_string(skip.at_frame) +
            " is out of range [1, " + std::to_string(total_disc_frames) + "].");
      }

      if (skip.count < 1) {
        result.is_valid = false;
        result.errors.push_back(ctx + "count must be >= 1 (got " +
                                std::to_string(skip.count) + ").");
      }

      if (skip.direction == DiscSkipDirection::kForward) {
        // Forward: frames at_frame .. at_frame+count-1 must exist.
        const int last = skip.at_frame + skip.count - 1;
        if (last > total_disc_frames) {
          result.is_valid = false;
          result.errors.push_back(ctx + "forward skip extends to frame " +
                                  std::to_string(last) +
                                  " which is beyond total disc frames (" +
                                  std::to_string(total_disc_frames) + ").");
        }
      } else {
        // Backward: replay frames at_frame-count+1 .. at_frame; must not
        // go before frame 1.
        const int first_replay = skip.at_frame - skip.count + 1;
        if (first_replay < 1) {
          result.is_valid = false;
          result.errors.push_back(
              ctx + "backward skip of count " + std::to_string(skip.count) +
              " at frame " + std::to_string(skip.at_frame) +
              " would replay before frame 1 (first replay frame: " +
              std::to_string(first_replay) + ").");
        }
      }
    }
  }

  if (logger_ != nullptr && result.is_valid) {
    logger_->Debug("Project validation completed successfully.");
  }

  return result;
}

}  // namespace videosynth
