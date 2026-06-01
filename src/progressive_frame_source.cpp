/*
 * File:        progressive_frame_source.cpp
 * Module:      progressive_frame_source
 * Purpose:     Generates fixed-format 10-bit 4:4:4 BT.601 frame data for frame-based sources.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/progressive_frame_source.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include <Imath/half.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfStringAttribute.h>

namespace videosynth {

namespace {

namespace Imf = OPENEXR_IMF_NAMESPACE;
namespace ImathNs = IMATH_NAMESPACE;

constexpr int kPalWidth = 720;
constexpr int kPalHeight = 576;
constexpr int kPalActiveX = 9;
constexpr int kPalActiveY = 0;
constexpr int kPalActiveWidth = 702;
constexpr int kPalActiveHeight = 576;

constexpr int kNtscWidth = 720;
constexpr int kNtscHeight = 480;
constexpr int kNtscActiveX = 4;
constexpr int kNtscActiveY = 0;
constexpr int kNtscActiveWidth = 711;
constexpr int kNtscActiveHeight = 480;

int ClampCode(int code, int lo, int hi) {
  return std::max(lo, std::min(code, hi));
}

std::int16_t ClampYCode(int code) {
  return static_cast<std::int16_t>(ClampCode(code, 64, 940));
}

std::int16_t ClampChromaCode(int code) {
  return static_cast<std::int16_t>(ClampCode(code, 64, 960));
}

YCbCr444Pixel MakePixel(int y_code, int cb_code, int cr_code) {
  return YCbCr444Pixel{
      .y = ClampYCode(y_code),
      .cb = ClampChromaCode(cb_code),
      .cr = ClampChromaCode(cr_code),
  };
}

YCbCr444Pixel MakeRawPixel(std::uint16_t y_code, std::uint16_t cb_code, std::uint16_t cr_code) {
  return YCbCr444Pixel{
      .y = static_cast<std::int16_t>(ClampCode(static_cast<int>(y_code), 0, 1023)),
      .cb = static_cast<std::int16_t>(ClampCode(static_cast<int>(cb_code), 0, 1023)),
      .cr = static_cast<std::int16_t>(ClampCode(static_cast<int>(cr_code), 0, 1023)),
  };
}

bool EndsWithLowercase(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void SetFrameGeometryForStandard(Standard standard, FrameSourceImage* image);

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

int ParseIntegerOrZero(const std::string& value) {
  if (value.empty() || value == "N/A") {
    return 0;
  }
  return std::atoi(value.c_str());
}

bool ProbeVideoRasterWithFfprobe(const std::string& source,
                                 int* out_width,
                                 int* out_height,
                                 std::string* error) {
  if (out_width == nullptr || out_height == nullptr) {
    if (error != nullptr) {
      *error = "Progressive probe output pointers must not be null.";
    }
    return false;
  }

  const std::string escaped_source = EscapeForSingleQuotedShell(source);
  const std::string command =
      "ffprobe -v error -select_streams v:0 "
      "-show_entries stream=width,height "
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
      *error = "ffprobe failed while probing progressive video source raster.";
    }
    return false;
  }

  std::map<std::string, std::string> values;
  ParseFfprobeKeyValueOutput(output, &values);
  *out_width = ParseIntegerOrZero(values["width"]);
  *out_height = ParseIntegerOrZero(values["height"]);
  if (*out_width <= 0 || *out_height <= 0) {
    if (error != nullptr) {
      *error = "Unable to determine progressive video source raster.";
    }
    return false;
  }
  return true;
}

bool ProbeVideoFrameCountWithFfprobe(const std::string& source,
                                     int* out_frame_count,
                                     std::string* error) {
  if (out_frame_count == nullptr) {
    if (error != nullptr) {
      *error = "Progressive probe frame count output pointer must not be null.";
    }
    return false;
  }

  const std::string escaped_source = EscapeForSingleQuotedShell(source);
  const std::string command =
      "ffprobe -v error -select_streams v:0 -count_frames "
      "-show_entries stream=nb_read_frames "
      "-of default=noprint_wrappers=1:nokey=0 '" +
      escaped_source + "' 2>/dev/null";

  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    if (error != nullptr) {
      *error = "Unable to run ffprobe for progressive source frame counting.";
    }
    return false;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int rc = pclose(pipe);
  if (rc != 0) {
    if (error != nullptr) {
      *error = "ffprobe failed while counting progressive source frames.";
    }
    return false;
  }

  std::map<std::string, std::string> values;
  ParseFfprobeKeyValueOutput(output, &values);
  *out_frame_count = ParseIntegerOrZero(values["nb_read_frames"]);
  if (*out_frame_count <= 0) {
    if (error != nullptr) {
      *error = "Unable to determine progressive source frame count.";
    }
    return false;
  }

  return true;
}

bool DecodeMovFrames(const std::string& source,
                     Standard standard,
                     int max_frames,
                     std::vector<FrameSourceImage>* out_frames,
                     std::string* error) {
  if (out_frames == nullptr) {
    if (error != nullptr) {
      *error = "Decoded MOV frame output pointer must not be null.";
    }
    return false;
  }

  int source_width = 0;
  int source_height = 0;
  if (!ProbeVideoRasterWithFfprobe(source, &source_width, &source_height, error)) {
    return false;
  }

  const bool is_pal = standard == Standard::kPal;
  const int expected_height = is_pal ? kPalHeight : kNtscHeight;
  if (source_height != expected_height || (source_width != 720 && source_width != 704)) {
    if (error != nullptr) {
      *error =
          "Progressive MOV raster must be 720x576 or 704x576 for PAL, and 720x480 or 704x480 for NTSC.";
    }
    return false;
  }

  int source_frame_count = 0;
  if (!ProbeVideoFrameCountWithFfprobe(source, &source_frame_count, error)) {
    return false;
  }

  const std::string escaped_source = EscapeForSingleQuotedShell(source);
    const std::string frame_limit_arg =
      max_frames > 0 ? (" -frames:v " + std::to_string(max_frames)) : "";
  const std::string command =
      "ffmpeg -v error -i '" + escaped_source +
      "' -an -sn -dn" + frame_limit_arg +
      " -pix_fmt yuv422p10le -vsync 0 -f rawvideo - 2>/dev/null";

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    if (error != nullptr) {
      *error = "Unable to run ffmpeg for progressive MOV decoding.";
    }
    return false;
  }

  std::vector<std::uint8_t> decoded_bytes;
  std::array<std::uint8_t, 8192> chunk{};
  while (true) {
    const std::size_t read_count = std::fread(chunk.data(), 1, chunk.size(), pipe);
    if (read_count > 0) {
      decoded_bytes.insert(decoded_bytes.end(), chunk.data(), chunk.data() + read_count);
    }
    if (read_count < chunk.size()) {
      break;
    }
  }

  const int rc = pclose(pipe);
  if (rc != 0) {
    if (error != nullptr) {
      *error = "ffmpeg failed while decoding progressive MOV source.";
    }
    return false;
  }

  if (source_frame_count <= 0) {
    if (error != nullptr) {
      *error = "Unable to determine progressive source frame count.";
    }
    return false;
  }

  const int expected_frames = max_frames > 0 ? std::min(source_frame_count, max_frames) : source_frame_count;
  if (expected_frames <= 0) {
    if (error != nullptr) {
      *error = "Progressive MOV source does not contain decodable video frames.";
    }
    return false;
  }

  const std::size_t bytes_per_frame = decoded_bytes.size() / static_cast<std::size_t>(expected_frames);
  if (decoded_bytes.empty() ||
      decoded_bytes.size() != bytes_per_frame * static_cast<std::size_t>(expected_frames)) {
    if (error != nullptr) {
      *error = "Decoded MOV frame payload size is not aligned to frame boundaries.";
    }
    return false;
  }

  const std::size_t bytes_per_line = static_cast<std::size_t>(source_height) * sizeof(std::uint16_t) * 2;
  if (bytes_per_line == 0 || (bytes_per_frame % bytes_per_line) != 0) {
    if (error != nullptr) {
      *error = "Decoded MOV frame payload size is not aligned to yuv422p10le frame boundaries.";
    }
    return false;
  }

  const int decoded_width = static_cast<int>(bytes_per_frame / bytes_per_line);
  if ((is_pal && decoded_width != 720 && decoded_width != 704 && decoded_width != 702) ||
      (!is_pal && decoded_width != 720 && decoded_width != 704)) {
    if (error != nullptr) {
      *error =
          "Decoded MOV raster must be 720/704 for NTSC and 720/704/702 for PAL after display aperture handling.";
    }
    return false;
  }

  const std::size_t frame_count = static_cast<std::size_t>(expected_frames);
  const std::size_t y_plane_size = static_cast<std::size_t>(decoded_width * source_height);
  const std::size_t chroma_plane_size = static_cast<std::size_t>((decoded_width / 2) * source_height);
  const std::size_t frame_size = (y_plane_size + chroma_plane_size + chroma_plane_size) *
                                 sizeof(std::uint16_t);

  out_frames->clear();
  out_frames->reserve(frame_count);

  int source_x_offset = 0;
  if (decoded_width == 704) {
    source_x_offset = 8;
  } else if (decoded_width == 702) {
    source_x_offset = 9;
  }
  for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    FrameSourceImage frame;
    SetFrameGeometryForStandard(standard, &frame);
    frame.pixels.assign(static_cast<std::size_t>(frame.width * frame.height),
                        MakePixel(64, 512, 512));

    const std::size_t frame_offset = frame_index * frame_size;
    const std::uint8_t* frame_data = decoded_bytes.data() + frame_offset;
    const std::uint16_t* y_plane = reinterpret_cast<const std::uint16_t*>(frame_data);
    const std::uint16_t* cb_plane = y_plane + y_plane_size;
    const std::uint16_t* cr_plane = cb_plane + chroma_plane_size;

    for (int y = 0; y < source_height; ++y) {
      for (int x = 0; x < decoded_width; ++x) {
        const int dst_x = x + source_x_offset;
        const int dst_y = y;
        const std::size_t index = static_cast<std::size_t>(y * decoded_width + x);
        const std::size_t chroma_index =
            static_cast<std::size_t>(y * (decoded_width / 2) + (x / 2));

        const std::uint16_t y_code = static_cast<std::uint16_t>(y_plane[index] & 0x03FFu);
        const std::uint16_t cb_code = static_cast<std::uint16_t>(cb_plane[chroma_index] & 0x03FFu);
        const std::uint16_t cr_code = static_cast<std::uint16_t>(cr_plane[chroma_index] & 0x03FFu);

        frame.pixels[static_cast<std::size_t>((dst_y * frame.width) + dst_x)] =
            MakeRawPixel(y_code, cb_code, cr_code);
      }
    }

    out_frames->push_back(std::move(frame));
  }

  return true;
}

bool EnsureMovCache(const Section& section,
                    Standard standard,
                    int min_required_frames,
                    bool require_complete,
                    std::vector<FrameSourceImage>* cached_frames,
                    std::string* cached_source,
                    Standard* cached_standard,
                    bool* has_cache,
                    bool* is_complete,
                    std::string* error) {
  if (cached_frames == nullptr || cached_source == nullptr || cached_standard == nullptr ||
      has_cache == nullptr || is_complete == nullptr) {
    if (error != nullptr) {
      *error = "Internal progressive MOV cache pointers must not be null.";
    }
    return false;
  }

  if (min_required_frames < 0) {
    min_required_frames = 0;
  }

  const bool key_matches = *has_cache && *cached_source == section.source && *cached_standard == standard;
  if (key_matches) {
    const bool has_required_frames =
        static_cast<int>(cached_frames->size()) >= min_required_frames;
    if ((require_complete && *is_complete) || (!require_complete && has_required_frames) ||
        (*is_complete && !has_required_frames)) {
      return true;
    }
  }

  std::vector<FrameSourceImage> decoded_frames;
  std::string decode_error;
  const int decode_limit = require_complete ? 0 : min_required_frames;
  if (!DecodeMovFrames(section.source, standard, decode_limit, &decoded_frames, &decode_error)) {
    if (error != nullptr) {
      *error = decode_error.empty() ? "Failed to decode progressive MOV source."
                                    : decode_error;
    }
    return false;
  }

  *cached_frames = std::move(decoded_frames);
  *cached_source = section.source;
  *cached_standard = standard;
  *has_cache = true;
  *is_complete = require_complete;
  return true;
}

void SetFrameGeometryForStandard(Standard standard, FrameSourceImage* image) {
  if (standard == Standard::kPal) {
    image->width = kPalWidth;
    image->height = kPalHeight;
    image->active_x = kPalActiveX;
    image->active_y = kPalActiveY;
    image->active_width = kPalActiveWidth;
    image->active_height = kPalActiveHeight;
    return;
  }

  image->width = kNtscWidth;
  image->height = kNtscHeight;
  image->active_x = kNtscActiveX;
  image->active_y = kNtscActiveY;
  image->active_width = kNtscActiveWidth;
  image->active_height = kNtscActiveHeight;
}

YCbCr444Pixel ConvertRgbFloatToBt601(std::float_t r,
                                     std::float_t g,
                                     std::float_t b) {
  const double r_norm = static_cast<double>(r);
  const double g_norm = static_cast<double>(g);
  const double b_norm = static_cast<double>(b);

  const double y = (0.299 * r_norm) + (0.587 * g_norm) + (0.114 * b_norm);
  const double cb = (-0.168736 * r_norm) - (0.331264 * g_norm) + (0.5 * b_norm);
  const double cr = (0.5 * r_norm) - (0.418688 * g_norm) - (0.081312 * b_norm);

  const int y_code = static_cast<int>(std::lround(64.0 + (876.0 * y)));
  const int cb_code = static_cast<int>(std::lround(512.0 + (896.0 * cb)));
  const int cr_code = static_cast<int>(std::lround(512.0 + (896.0 * cr)));
  return MakeRawPixel(static_cast<std::uint16_t>(ClampCode(y_code, 0, 1023)),
                      static_cast<std::uint16_t>(ClampCode(cb_code, 0, 1023)),
                      static_cast<std::uint16_t>(ClampCode(cr_code, 0, 1023)));
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

bool ValidateExrMetadata(const Imf::Header& header,
                         int width,
                         int height,
                         Standard standard,
                         std::string* error) {
  const std::string standard_hint =
      standard == Standard::kPal ? "PAL" : (standard == Standard::kNtsc ? "NTSC" : "");
  if (standard_hint.empty()) {
    if (error != nullptr) {
      *error = "Unsupported video standard for progressive EXR source.";
    }
    return false;
  }

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
      !ReadRequiredExrStringAttribute(
          header, "videosynth.standard_hint", standard_hint, error) ||
      !ReadRequiredExrIntAttribute(header, "videosynth.source_width", width, error) ||
      !ReadRequiredExrIntAttribute(header, "videosynth.source_height", height, error)) {
    return false;
  }

  return true;
}

bool LoadExrFrame(const std::string& source,
                  Standard standard,
                  FrameSourceImage* out_image,
                  std::string* error) {
  if (out_image == nullptr) {
    if (error != nullptr) {
      *error = "Frame source output image pointer must not be null.";
    }
    return false;
  }

  try {
    Imf::InputFile input_file(source.c_str());
    const Imf::Header& header = input_file.header();
    const ImathNs::Box2i data_window = header.dataWindow();
    const int width = (data_window.max.x - data_window.min.x) + 1;
    const int height = (data_window.max.y - data_window.min.y) + 1;

    const bool is_pal = standard == Standard::kPal;
    const int expected_height = is_pal ? kPalHeight : kNtscHeight;
    if (height != expected_height || (width != 720 && width != 704)) {
      if (error != nullptr) {
        *error =
            "Progressive EXR raster must be 720x576 or 704x576 for PAL, and 720x480 or 704x480 for NTSC.";
      }
      return false;
    }

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

    const Imf::PixelType channel_type = r_channel->type;
    if (channel_type != Imf::HALF && channel_type != Imf::FLOAT) {
      if (error != nullptr) {
        *error = "Progressive EXR channels R/G/B must use HALF or FLOAT type.";
      }
      return false;
    }

    if (!ValidateExrMetadata(header, width, height, standard, error)) {
      return false;
    }

    SetFrameGeometryForStandard(standard, out_image);
    out_image->pixels.assign(static_cast<std::size_t>(out_image->width * out_image->height),
                             MakePixel(64, 512, 512));

    const int source_x_offset = width == 704 ? 8 : 0;
    const std::ptrdiff_t pixel_offset = static_cast<std::ptrdiff_t>(data_window.min.x) +
                                        (static_cast<std::ptrdiff_t>(data_window.min.y) * width);
    Imf::FrameBuffer frame_buffer;

    if (channel_type == Imf::FLOAT) {
      std::vector<float> red(static_cast<std::size_t>(width * height), 0.0F);
      std::vector<float> green(static_cast<std::size_t>(width * height), 0.0F);
      std::vector<float> blue(static_cast<std::size_t>(width * height), 0.0F);

      frame_buffer.insert(
          "R", Imf::Slice(Imf::FLOAT,
                           reinterpret_cast<char*>(red.data() - pixel_offset),
                           sizeof(float),
                           static_cast<std::size_t>(sizeof(float) * width)));
      frame_buffer.insert(
          "G", Imf::Slice(Imf::FLOAT,
                           reinterpret_cast<char*>(green.data() - pixel_offset),
                           sizeof(float),
                           static_cast<std::size_t>(sizeof(float) * width)));
      frame_buffer.insert(
          "B", Imf::Slice(Imf::FLOAT,
                           reinterpret_cast<char*>(blue.data() - pixel_offset),
                           sizeof(float),
                           static_cast<std::size_t>(sizeof(float) * width)));

      input_file.setFrameBuffer(frame_buffer);
      input_file.readPixels(data_window.min.y, data_window.max.y);

      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const std::size_t source_index = static_cast<std::size_t>(y * width + x);
          const int dst_x = x + source_x_offset;
          out_image->pixels[static_cast<std::size_t>((y * out_image->width) + dst_x)] =
              ConvertRgbFloatToBt601(red[source_index], green[source_index], blue[source_index]);
        }
      }
      return true;
    }

    std::vector<ImathNs::half> red(static_cast<std::size_t>(width * height), ImathNs::half(0.0F));
    std::vector<ImathNs::half> green(static_cast<std::size_t>(width * height), ImathNs::half(0.0F));
    std::vector<ImathNs::half> blue(static_cast<std::size_t>(width * height), ImathNs::half(0.0F));

    frame_buffer.insert(
        "R",
        Imf::Slice(Imf::HALF,
                   reinterpret_cast<char*>(red.data() - pixel_offset),
                   sizeof(ImathNs::half),
                   static_cast<std::size_t>(sizeof(ImathNs::half) * width)));
    frame_buffer.insert(
        "G",
        Imf::Slice(Imf::HALF,
                   reinterpret_cast<char*>(green.data() - pixel_offset),
                   sizeof(ImathNs::half),
                   static_cast<std::size_t>(sizeof(ImathNs::half) * width)));
    frame_buffer.insert(
        "B",
        Imf::Slice(Imf::HALF,
                   reinterpret_cast<char*>(blue.data() - pixel_offset),
                   sizeof(ImathNs::half),
                   static_cast<std::size_t>(sizeof(ImathNs::half) * width)));

    input_file.setFrameBuffer(frame_buffer);
    input_file.readPixels(data_window.min.y, data_window.max.y);

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const std::size_t source_index = static_cast<std::size_t>(y * width + x);
        const int dst_x = x + source_x_offset;
        out_image->pixels[static_cast<std::size_t>((y * out_image->width) + dst_x)] =
            ConvertRgbFloatToBt601(static_cast<float>(red[source_index]),
                                   static_cast<float>(green[source_index]),
                                   static_cast<float>(blue[source_index]));
      }
    }

    return true;
  } catch (const std::exception& ex) {
    if (error != nullptr) {
      *error = std::string("Failed while decoding progressive EXR source: ") + ex.what();
    }
    return false;
  }
}

}  // namespace

const YCbCr444Pixel& FrameSourceImage::PixelAt(int x, int y) const {
  return pixels[static_cast<std::size_t>((y * width) + x)];
}

bool ProgressiveFrameSource::SupportsSection(const Section& section) const {
  if (section.type != "progressive" || section.source.empty()) {
    return false;
  }

  std::string source = section.source;
  std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return source.size() >= 4 &&
      (EndsWithLowercase(source, ".exr") ||
       EndsWithLowercase(source, ".mov"));
}

void ProgressiveFrameSource::ClearCache() const {
  has_cached_exr_frame_ = false;
  cached_exr_source_.clear();
  cached_exr_standard_ = Standard::kUnknown;
  cached_exr_frame_ = FrameSourceImage{};

  has_cached_mov_frames_ = false;
  cached_mov_source_.clear();
  cached_mov_standard_ = Standard::kUnknown;
  cached_mov_is_complete_ = false;
  cached_mov_frames_.clear();
}

bool ProgressiveFrameSource::ResolveFrameCount(const Section& section,
                                               Standard standard,
                                               int* out_frame_count,
                                               std::string* error) const {
  if (out_frame_count == nullptr) {
    if (error != nullptr) {
      *error = "Progressive frame count output pointer must not be null.";
    }
    return false;
  }

  if (!SupportsSection(section)) {
    if (error != nullptr) {
      *error = "Progressive section source family is not supported.";
    }
    return false;
  }

  std::string source = section.source;
  std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (EndsWithLowercase(source, ".exr")) {
    *out_frame_count = 1;
    return true;
  }

  if (EndsWithLowercase(source, ".mov")) {
    if (!EnsureMovCache(section,
                        standard,
                        0,
                        true,
                        &cached_mov_frames_,
                        &cached_mov_source_,
                        &cached_mov_standard_,
                        &has_cached_mov_frames_,
                        &cached_mov_is_complete_,
                        error)) {
      return false;
    }

    *out_frame_count = static_cast<int>(cached_mov_frames_.size());
    return true;
  }

  if (error != nullptr) {
    *error = "Progressive source frame count probing is not yet implemented for this source family.";
  }
  return false;
}

bool ProgressiveFrameSource::GenerateFrame(const Section& section,
                                           int frame_index,
                                           Standard standard,
                                           FrameSourceImage* out_image,
                                           std::string* error) const {
  (void)frame_index;

  if (!SupportsSection(section)) {
    if (error != nullptr) {
      *error = "Progressive section source family is not supported.";
    }
    return false;
  }

  std::string source = section.source;
  std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (EndsWithLowercase(source, ".exr")) {
    const bool cache_hit = has_cached_exr_frame_ &&
                           cached_exr_source_ == section.source &&
                           cached_exr_standard_ == standard;
    if (!cache_hit) {
      FrameSourceImage decoded;
      std::string decode_error;
      if (!LoadExrFrame(section.source, standard, &decoded, &decode_error)) {
        if (error != nullptr) {
          *error = decode_error.empty() ? "Failed to decode progressive EXR source."
                                        : decode_error;
        }
        return false;
      }
      cached_exr_frame_ = decoded;
      cached_exr_source_ = section.source;
      cached_exr_standard_ = standard;
      has_cached_exr_frame_ = true;
    }

    if (out_image == nullptr) {
      if (error != nullptr) {
        *error = "Frame source output image pointer must not be null.";
      }
      return false;
    }
    *out_image = cached_exr_frame_;
    return true;
  }

  if (EndsWithLowercase(source, ".mov")) {
    if (frame_index < 0) {
      if (error != nullptr) {
        *error = "Progressive frame index must be non-negative.";
      }
      return false;
    }

    int required_frames = frame_index + 1;
    if (!section.duration_frames_all && section.duration_frames > 0) {
      required_frames = section.start_frame + section.duration_frames;
    }

    if (!EnsureMovCache(section,
                        standard,
                        required_frames,
                        section.duration_frames_all,
                        &cached_mov_frames_,
                        &cached_mov_source_,
                        &cached_mov_standard_,
                        &has_cached_mov_frames_,
                        &cached_mov_is_complete_,
                        error)) {
      return false;
    }

    if (frame_index >= static_cast<int>(cached_mov_frames_.size())) {
      if (error != nullptr) {
        *error = "Requested frame index exceeds decoded progressive MOV source length.";
      }
      return false;
    }

    if (out_image == nullptr) {
      if (error != nullptr) {
        *error = "Frame source output image pointer must not be null.";
      }
      return false;
    }

    *out_image = cached_mov_frames_[static_cast<std::size_t>(frame_index)];
    return true;
  }

  if (error != nullptr) {
    *error =
        "Progressive source decoding is not yet implemented for this source family.";
  }
  return false;
}

}  // namespace videosynth