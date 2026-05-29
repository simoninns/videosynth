/*
 * File:        progressive_frame_source_probe.cpp
 * Module:      progressive_frame_source_probe
 * Purpose:     Probes progressive source metadata for profile validation and frame-count semantics.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/progressive_frame_source_probe.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <png.h>

namespace videosynth {
namespace {

namespace Imf = OPENEXR_IMF_NAMESPACE;

std::string Lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool EndsWithLowercase(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string EscapeForSingleQuotedShell(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (char c : value) {
    if (c == '\'') {
      escaped += "'\\''";
      continue;
    }
    escaped.push_back(c);
  }
  return escaped;
}

bool ParseFfprobeKeyValueOutput(const std::string& output,
                                std::map<std::string, std::string>* out_values) {
  if (out_values == nullptr) {
    return false;
  }

  out_values->clear();
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    const std::size_t equals_pos = line.find('=');
    if (equals_pos == std::string::npos || equals_pos == 0) {
      continue;
    }

    const std::string key = line.substr(0, equals_pos);
    const std::string value = line.substr(equals_pos + 1);
    if (!key.empty()) {
      (*out_values)[key] = value;
    }
  }

  return true;
}

double ParseFrameRate(const std::string& rate_text) {
  if (rate_text.empty()) {
    return 0.0;
  }

  const std::size_t slash_pos = rate_text.find('/');
  if (slash_pos == std::string::npos) {
    return std::atof(rate_text.c_str());
  }

  const std::string numerator_text = rate_text.substr(0, slash_pos);
  const std::string denominator_text = rate_text.substr(slash_pos + 1);
  const double numerator = std::atof(numerator_text.c_str());
  const double denominator = std::atof(denominator_text.c_str());
  if (denominator == 0.0) {
    return 0.0;
  }
  return numerator / denominator;
}

int ParseIntegerOrZero(const std::string& value) {
  if (value.empty() || value == "N/A") {
    return 0;
  }
  return std::atoi(value.c_str());
}

bool ProbePng(const Section& section,
              ProgressiveFrameSourceProfile* out_profile,
              std::string* error) {
  FILE* file = std::fopen(section.source.c_str(), "rb");
  if (file == nullptr) {
    if (error != nullptr) {
      *error = "Unable to open progressive PNG source for probing.";
    }
    return false;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png_ptr == nullptr) {
    std::fclose(file);
    if (error != nullptr) {
      *error = "Failed to initialize PNG probe state.";
    }
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == nullptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    std::fclose(file);
    if (error != nullptr) {
      *error = "Failed to initialize PNG probe metadata state.";
    }
    return false;
  }

  if (setjmp(png_jmpbuf(png_ptr)) != 0) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    std::fclose(file);
    if (error != nullptr) {
      *error = "Failed while probing progressive PNG source.";
    }
    return false;
  }

  png_init_io(png_ptr, file);
  png_read_info(png_ptr, info_ptr);

  const int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
  const int color_type = png_get_color_type(png_ptr, info_ptr);
  out_profile->container = "png";
  out_profile->codec = "png";
  out_profile->pixel_format = (color_type == PNG_COLOR_TYPE_RGBA) ? "rgba" : "rgb";
  out_profile->bit_depth = bit_depth;
  out_profile->width = static_cast<int>(png_get_image_width(png_ptr, info_ptr));
  out_profile->height = static_cast<int>(png_get_image_height(png_ptr, info_ptr));
  out_profile->frame_rate_hz = 0.0;
  out_profile->frame_count = 1;

  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  std::fclose(file);
  return true;
}

bool ReadRequiredExrStringAttribute(const Imf::Header& header,
                                    const char* attribute_name,
                                    const std::string& expected_value,
                                    std::string* error) {
  const Imf::StringAttribute* attribute =
      header.findTypedAttribute<Imf::StringAttribute>(attribute_name);
  if (attribute == nullptr) {
    if (error != nullptr) {
      *error = std::string("Progressive EXR is missing required metadata attribute: ") +
               attribute_name + ".";
    }
    return false;
  }

  if (attribute->value() != expected_value) {
    if (error != nullptr) {
      *error = std::string("Progressive EXR metadata attribute '") + attribute_name +
               "' must be '" + expected_value + "'.";
    }
    return false;
  }

  return true;
}

bool ReadRequiredExrIntAttribute(const Imf::Header& header,
                                 const char* attribute_name,
                                 int expected_value,
                                 std::string* error) {
  const Imf::IntAttribute* attribute =
      header.findTypedAttribute<Imf::IntAttribute>(attribute_name);
  if (attribute == nullptr) {
    if (error != nullptr) {
      *error = std::string("Progressive EXR is missing required metadata attribute: ") +
               attribute_name + ".";
    }
    return false;
  }

  if (attribute->value() != expected_value) {
    if (error != nullptr) {
      *error = std::string("Progressive EXR metadata attribute '") + attribute_name +
               "' must be " + std::to_string(expected_value) + ".";
    }
    return false;
  }

  return true;
}

bool ProbeExr(const Section& section,
              ProgressiveFrameSourceProfile* out_profile,
              std::string* error) {
  try {
    Imf::InputFile input_file(section.source.c_str());
    const Imf::Header& header = input_file.header();
    const Imf::ChannelList& channels = header.channels();
    const Imf::Channel* r_channel = channels.findChannel("R");
    const Imf::Channel* g_channel = channels.findChannel("G");
    const Imf::Channel* b_channel = channels.findChannel("B");
    if (r_channel == nullptr || g_channel == nullptr || b_channel == nullptr) {
      if (error != nullptr) {
        *error = "Progressive EXR must provide R, G, and B channels.";
      }
      return false;
    }

    if (r_channel->type != g_channel->type || r_channel->type != b_channel->type) {
      if (error != nullptr) {
        *error = "Progressive EXR channels R/G/B must use a single shared channel type.";
      }
      return false;
    }

    if (r_channel->type != Imf::HALF && r_channel->type != Imf::FLOAT) {
      if (error != nullptr) {
        *error = "Progressive EXR channels R/G/B must use HALF or FLOAT type.";
      }
      return false;
    }

    const IMATH_NAMESPACE::Box2i data_window = header.dataWindow();
    const int width = (data_window.max.x - data_window.min.x) + 1;
    const int height = (data_window.max.y - data_window.min.y) + 1;

    if (!ReadRequiredExrStringAttribute(
            header, "videosynth.source_pixel_format", "yuv422p10le", error) ||
        !ReadRequiredExrStringAttribute(
            header, "videosynth.source_sampling", "422_to_444_expanded", error) ||
        !ReadRequiredExrStringAttribute(header, "videosynth.color_model", "rgb", error) ||
        !ReadRequiredExrStringAttribute(
            header, "videosynth.color_primaries", "bt601", error) ||
        !ReadRequiredExrStringAttribute(header, "videosynth.transfer", "bt601", error) ||
        !ReadRequiredExrStringAttribute(
            header, "videosynth.matrix", "bt601_ycbcr_to_rgb", error) ||
        !ReadRequiredExrStringAttribute(header, "videosynth.code_range", "studio", error) ||
        !ReadRequiredExrIntAttribute(header, "videosynth.source_width", width, error) ||
        !ReadRequiredExrIntAttribute(header, "videosynth.source_height", height, error)) {
      return false;
    }

    out_profile->container = "exr";
    out_profile->codec = "openexr";
    out_profile->pixel_format = r_channel->type == Imf::HALF ? "rgbh" : "rgbf";
    out_profile->bit_depth = r_channel->type == Imf::HALF ? 16 : 32;
    out_profile->width = width;
    out_profile->height = height;
    out_profile->frame_rate_hz = 0.0;
    out_profile->frame_count = 1;
    return true;
  } catch (const std::exception& ex) {
    if (error != nullptr) {
      *error = std::string("Failed while probing progressive EXR source: ") + ex.what();
    }
    return false;
  }
}

bool ProbeWithFfprobe(const std::string& source,
                      ProgressiveFrameSourceProfile* out_profile,
                      std::string* error) {
  const std::string escaped_source = EscapeForSingleQuotedShell(source);
  const std::string command =
      "ffprobe -v error -select_streams v:0 -count_frames "
      "-show_entries format=format_name "
      "-show_entries stream=codec_name,pix_fmt,width,height,r_frame_rate,nb_read_frames,bits_per_raw_sample "
      "-of default=noprint_wrappers=1:nokey=0 '" +
      escaped_source + "' 2>/dev/null";

  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    if (error != nullptr) {
      *error = "Unable to run ffprobe for progressive source probing.";
    }
    return false;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int rc = pclose(pipe);
  if (rc != 0) {
    if (error != nullptr) {
      *error = "ffprobe failed while probing progressive source profile.";
    }
    return false;
  }

  std::map<std::string, std::string> values;
  ParseFfprobeKeyValueOutput(output, &values);

  const std::string format_name = Lowercase(values["format_name"]);
  const std::string codec_name = Lowercase(values["codec_name"]);
  const std::string pix_fmt = Lowercase(values["pix_fmt"]);
  const int width = ParseIntegerOrZero(values["width"]);
  const int height = ParseIntegerOrZero(values["height"]);
  const double frame_rate = ParseFrameRate(values["r_frame_rate"]);
  const int frame_count = ParseIntegerOrZero(values["nb_read_frames"]);

  int bit_depth = ParseIntegerOrZero(values["bits_per_raw_sample"]);
  if (bit_depth == 0 && pix_fmt == "yuv420p") {
    bit_depth = 8;
  }

  out_profile->container = format_name;
  out_profile->codec = codec_name;
  out_profile->pixel_format = pix_fmt;
  out_profile->bit_depth = bit_depth;
  out_profile->width = width;
  out_profile->height = height;
  out_profile->frame_rate_hz = frame_rate;
  out_profile->frame_count = frame_count;
  return true;
}

}  // namespace

bool ProgressiveFrameSourceProbe::Probe(const Section& section,
                                        ProgressiveFrameSourceProfile* out_profile,
                                        std::string* error) {
  if (out_profile == nullptr) {
    if (error != nullptr) {
      *error = "Progressive source probe output profile pointer must not be null.";
    }
    return false;
  }

  const std::string source = Lowercase(section.source);
  if (EndsWithLowercase(source, ".png")) {
    return ProbePng(section, out_profile, error);
  }
  if (EndsWithLowercase(source, ".exr")) {
    return ProbeExr(section, out_profile, error);
  }
  if (EndsWithLowercase(source, ".mp4") || EndsWithLowercase(source, ".mov")) {
    return ProbeWithFfprobe(section.source, out_profile, error);
  }

  if (error != nullptr) {
    *error = "Unsupported progressive source family during profile probing.";
  }
  return false;
}

}  // namespace videosynth