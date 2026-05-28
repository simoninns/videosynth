/*
 * File:        frame_source.cpp
 * Module:      frame_source
 * Purpose:     Generates fixed-format 10-bit 4:4:4 BT.601 frame data for frame-based sources.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/frame_source.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include <png.h>

#include "videosynth/ntsc_pattern_generator.h"
#include "videosynth/pal_pattern_generator.h"

namespace videosynth {

namespace {

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

bool DecodeMp4Frames(const std::string& source,
                     Standard standard,
                     std::vector<FrameSourceImage>* out_frames,
                     std::string* error) {
  if (out_frames == nullptr) {
    if (error != nullptr) {
      *error = "Decoded MP4 frame output pointer must not be null.";
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
          "Progressive MP4 raster must be 720x576 or 704x576 for PAL, and 720x480 or 704x480 for NTSC.";
    }
    return false;
  }

  if ((source_width % 2) != 0 || (source_height % 2) != 0) {
    if (error != nullptr) {
      *error = "Progressive MP4 raster must be even for yuv420p decoding.";
    }
    return false;
  }

  const std::string escaped_source = EscapeForSingleQuotedShell(source);
  const std::string command =
      "ffmpeg -v error -i '" + escaped_source +
      "' -an -sn -dn -pix_fmt yuv420p -vsync 0 -f rawvideo - 2>/dev/null";

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    if (error != nullptr) {
      *error = "Unable to run ffmpeg for progressive MP4 decoding.";
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
      *error = "ffmpeg failed while decoding progressive MP4 source.";
    }
    return false;
  }

  const std::size_t y_plane_size = static_cast<std::size_t>(source_width * source_height);
  const std::size_t chroma_plane_size = static_cast<std::size_t>((source_width / 2) * (source_height / 2));
  const std::size_t frame_size = y_plane_size + chroma_plane_size + chroma_plane_size;
  if (frame_size == 0 || (decoded_bytes.size() % frame_size) != 0) {
    if (error != nullptr) {
      *error = "Decoded MP4 frame payload size is not aligned to yuv420p frame boundaries.";
    }
    return false;
  }

  const std::size_t frame_count = decoded_bytes.size() / frame_size;
  if (frame_count == 0) {
    if (error != nullptr) {
      *error = "Progressive MP4 source does not contain decodable video frames.";
    }
    return false;
  }

  out_frames->clear();
  out_frames->reserve(frame_count);

  const int source_x_offset = source_width == 704 ? 8 : 0;
  for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    FrameSourceImage frame;
    SetFrameGeometryForStandard(standard, &frame);
    frame.pixels.assign(static_cast<std::size_t>(frame.width * frame.height),
                        MakePixel(64, 512, 512));

    const std::size_t frame_offset = frame_index * frame_size;
    const std::uint8_t* y_plane = decoded_bytes.data() + frame_offset;
    const std::uint8_t* cb_plane = y_plane + y_plane_size;
    const std::uint8_t* cr_plane = cb_plane + chroma_plane_size;

    for (int y = 0; y < source_height; ++y) {
      for (int x = 0; x < source_width; ++x) {
        const int dst_x = x + source_x_offset;
        const int dst_y = y;

        const std::size_t y_index = static_cast<std::size_t>(y * source_width + x);
        const std::size_t chroma_index =
            static_cast<std::size_t>((y / 2) * (source_width / 2) + (x / 2));
        const int y_code = static_cast<int>(y_plane[y_index]) * 4;
        const int cb_code = static_cast<int>(cb_plane[chroma_index]) * 4;
        const int cr_code = static_cast<int>(cr_plane[chroma_index]) * 4;

        frame.pixels[static_cast<std::size_t>((dst_y * frame.width) + dst_x)] =
            MakePixel(y_code, cb_code, cr_code);
      }
    }

    out_frames->push_back(std::move(frame));
  }

  return true;
}

bool DecodeMovFrames(const std::string& source,
                     Standard standard,
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
  const std::string command =
      "ffmpeg -v error -i '" + escaped_source +
      "' -an -sn -dn -pix_fmt yuv422p10le -vsync 0 -f rawvideo - 2>/dev/null";

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

  const std::size_t bytes_per_frame = decoded_bytes.size() / static_cast<std::size_t>(source_frame_count);
  if (source_frame_count <= 0 ||
      decoded_bytes.size() != bytes_per_frame * static_cast<std::size_t>(source_frame_count)) {
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

  const std::size_t frame_count = static_cast<std::size_t>(source_frame_count);
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

YCbCr444Pixel ConvertRgbToBt601Studio(std::uint16_t r,
                                      std::uint16_t g,
                                      std::uint16_t b,
                                      std::uint16_t max_value) {
  const double scale = max_value == 0 ? 1.0 : static_cast<double>(max_value);
  const double r_norm = static_cast<double>(r) / scale;
  const double g_norm = static_cast<double>(g) / scale;
  const double b_norm = static_cast<double>(b) / scale;

  const double y = (0.299 * r_norm) + (0.587 * g_norm) + (0.114 * b_norm);
  const double cb = (-0.168736 * r_norm) - (0.331264 * g_norm) + (0.5 * b_norm);
  const double cr = (0.5 * r_norm) - (0.418688 * g_norm) - (0.081312 * b_norm);

  const int y_code = static_cast<int>(std::lround(64.0 + (876.0 * y)));
  const int cb_code = static_cast<int>(std::lround(512.0 + (896.0 * cb)));
  const int cr_code = static_cast<int>(std::lround(512.0 + (896.0 * cr)));
  return MakePixel(y_code, cb_code, cr_code);
}

bool LoadPngFrame(const std::string& source,
                  Standard standard,
                  FrameSourceImage* out_image,
                  std::string* error) {
  if (out_image == nullptr) {
    if (error != nullptr) {
      *error = "Frame source output image pointer must not be null.";
    }
    return false;
  }

  FILE* file = std::fopen(source.c_str(), "rb");
  if (file == nullptr) {
    if (error != nullptr) {
      *error = "Unable to open progressive PNG source for reading.";
    }
    return false;
  }

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (png_ptr == nullptr) {
    std::fclose(file);
    if (error != nullptr) {
      *error = "Failed to initialize PNG reader state.";
    }
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == nullptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    std::fclose(file);
    if (error != nullptr) {
      *error = "Failed to initialize PNG reader metadata state.";
    }
    return false;
  }

  if (setjmp(png_jmpbuf(png_ptr)) != 0) {
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    std::fclose(file);
    if (error != nullptr) {
      *error = "Failed while decoding progressive PNG source.";
    }
    return false;
  }

  png_init_io(png_ptr, file);
  png_read_info(png_ptr, info_ptr);

  const png_uint_32 width = png_get_image_width(png_ptr, info_ptr);
  const png_uint_32 height = png_get_image_height(png_ptr, info_ptr);
  const int bit_depth = png_get_bit_depth(png_ptr, info_ptr);
  const int color_type = png_get_color_type(png_ptr, info_ptr);

  if (bit_depth != 8 && bit_depth != 16) {
    if (error != nullptr) {
      *error = "Progressive PNG must use 8-bit or 16-bit integer channels.";
    }
    return false;
  }

  if (color_type != PNG_COLOR_TYPE_RGB && color_type != PNG_COLOR_TYPE_RGBA) {
    if (error != nullptr) {
      *error = "Progressive PNG must be truecolour RGB or RGBA.";
    }
    return false;
  }

  const bool is_pal = standard == Standard::kPal;
  const int expected_height = is_pal ? kPalHeight : kNtscHeight;
  if (height != static_cast<png_uint_32>(expected_height) ||
      (width != 720U && width != 704U)) {
    if (error != nullptr) {
      *error =
          "Progressive PNG raster must be 720x576 or 704x576 for PAL, and 720x480 or 704x480 for NTSC.";
    }
    return false;
  }

  const int channels = png_get_channels(png_ptr, info_ptr);
  png_read_update_info(png_ptr, info_ptr);
  const png_size_t row_bytes = png_get_rowbytes(png_ptr, info_ptr);

  std::vector<png_byte> buffer(static_cast<std::size_t>(row_bytes * height));
  std::vector<png_bytep> rows(static_cast<std::size_t>(height));
  for (png_uint_32 y = 0; y < height; ++y) {
    rows[static_cast<std::size_t>(y)] =
        buffer.data() + static_cast<std::size_t>(y * row_bytes);
  }

  png_read_image(png_ptr, rows.data());
  png_read_end(png_ptr, nullptr);

  SetFrameGeometryForStandard(standard, out_image);
  out_image->pixels.assign(static_cast<std::size_t>(out_image->width * out_image->height),
                           MakePixel(64, 512, 512));

  const int source_x_offset = width == 704U ? 8 : 0;
  const std::uint16_t max_value = bit_depth == 16 ? 65535U : 255U;

  for (png_uint_32 y = 0; y < height; ++y) {
    const png_bytep row = rows[static_cast<std::size_t>(y)];
    for (png_uint_32 x = 0; x < width; ++x) {
      std::uint16_t r = 0;
      std::uint16_t g = 0;
      std::uint16_t b = 0;

      if (bit_depth == 8) {
        const std::size_t index = static_cast<std::size_t>(x * channels);
        r = row[index + 0];
        g = row[index + 1];
        b = row[index + 2];
      } else {
        const std::size_t index = static_cast<std::size_t>(x * channels * 2);
        r = static_cast<std::uint16_t>((row[index + 0] << 8) | row[index + 1]);
        g = static_cast<std::uint16_t>((row[index + 2] << 8) | row[index + 3]);
        b = static_cast<std::uint16_t>((row[index + 4] << 8) | row[index + 5]);
      }

      const int dst_x = static_cast<int>(x) + source_x_offset;
      const int dst_y = static_cast<int>(y);
      out_image->pixels[static_cast<std::size_t>((dst_y * out_image->width) + dst_x)] =
          ConvertRgbToBt601Studio(r, g, b, max_value);
    }
  }

  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  std::fclose(file);

  return true;
}

int RawHeightForStandard(Standard standard) {
  if (standard == Standard::kPal) {
    return kPalHeight;
  }
  if (standard == Standard::kNtsc) {
    return kNtscHeight;
  }
  return 0;
}

bool InferRawWidthFromByteSize(std::size_t file_size,
                               int height,
                               const std::string& pixel_format,
                               int* out_width) {
  if (out_width == nullptr || height <= 0) {
    return false;
  }

  const std::size_t bytes_per_sample = sizeof(std::uint16_t);
  std::size_t expected_720 = 0;
  std::size_t expected_704 = 0;
  if (pixel_format == "yuv422p10le") {
    expected_720 = static_cast<std::size_t>(720 * height * 2) * bytes_per_sample;
    expected_704 = static_cast<std::size_t>(704 * height * 2) * bytes_per_sample;
  } else {
    return false;
  }

  if (file_size == expected_720) {
    *out_width = 720;
    return true;
  }
  if (file_size == expected_704) {
    *out_width = 704;
    return true;
  }
  return false;
}

bool LoadRawFrame(const Section& section,
                  Standard standard,
                  FrameSourceImage* out_image,
                  std::string* error) {
  if (out_image == nullptr) {
    if (error != nullptr) {
      *error = "Frame source output image pointer must not be null.";
    }
    return false;
  }

  std::string pixel_format = section.source_pixel_format;
  std::transform(pixel_format.begin(), pixel_format.end(), pixel_format.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (pixel_format != "yuv422p10le") {
    if (error != nullptr) {
      *error = "Progressive RAW source_pixel_format must be yuv422p10le.";
    }
    return false;
  }

  const int height = RawHeightForStandard(standard);
  if (height <= 0) {
    if (error != nullptr) {
      *error = "Unsupported video standard for RAW progressive source.";
    }
    return false;
  }

  std::ifstream stream(section.source, std::ios::binary | std::ios::ate);
  if (!stream) {
    if (error != nullptr) {
      *error = "Unable to open progressive RAW source for reading.";
    }
    return false;
  }

  const std::size_t file_size = static_cast<std::size_t>(stream.tellg());
  stream.seekg(0, std::ios::beg);

  int source_width = 0;
  if (!InferRawWidthFromByteSize(file_size, height, pixel_format, &source_width)) {
    if (error != nullptr) {
      *error = "Progressive RAW raster does not match 720/704 width for selected standard and pixel format.";
    }
    return false;
  }

  std::vector<std::uint16_t> samples(file_size / sizeof(std::uint16_t), 0);
  stream.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(file_size));
  if (!stream) {
    if (error != nullptr) {
      *error = "Failed while reading progressive RAW source.";
    }
    return false;
  }

  SetFrameGeometryForStandard(standard, out_image);
  out_image->pixels.assign(static_cast<std::size_t>(out_image->width * out_image->height),
                           MakePixel(64, 512, 512));

  const int source_x_offset = source_width == 704 ? 8 : 0;
  // yuv422p10le fixture RAW files are packed as Y0 Cb Y1 Cr in 16-bit little-endian words.
  // Each word carries a 10-bit studio-domain code in the lower bits.
  const std::uint16_t* packed = samples.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < source_width; x += 2) {
      const std::size_t pair_index = static_cast<std::size_t>(y * (source_width / 2) + (x / 2));
      const std::size_t component_index = pair_index * 4;

      const std::uint16_t y0 = static_cast<std::uint16_t>(packed[component_index + 0] & 0x03FFu);
      const std::uint16_t cb = static_cast<std::uint16_t>(packed[component_index + 1] & 0x03FFu);
      const std::uint16_t y1 = static_cast<std::uint16_t>(packed[component_index + 2] & 0x03FFu);
      const std::uint16_t cr = static_cast<std::uint16_t>(packed[component_index + 3] & 0x03FFu);

      const int dst_y = y;
      const int dst_x0 = x + source_x_offset;
      const int dst_x1 = dst_x0 + 1;

      out_image->pixels[static_cast<std::size_t>(dst_y * out_image->width + dst_x0)] =
          MakeRawPixel(y0, cb, cr);
      out_image->pixels[static_cast<std::size_t>(dst_y * out_image->width + dst_x1)] =
          MakeRawPixel(y1, cb, cr);
    }
  }

  return true;
}

}  // namespace

const YCbCr444Pixel& FrameSourceImage::PixelAt(int x, int y) const {
  return pixels[static_cast<std::size_t>((y * width) + x)];
}

bool TestPatternFrameSource::SupportsPattern(const std::string& pattern) const {
  return IsSupportedPalPattern(pattern) || IsSupportedNtscPattern(pattern);
}

bool TestPatternFrameSource::GenerateFrame(const std::string& pattern,
                                           Standard standard,
                                           FrameSourceImage* out_image,
                                           std::string* error) const {
  if (out_image == nullptr) {
    if (error != nullptr) {
      *error = "Frame source output image pointer must not be null.";
    }
    return false;
  }

  if (standard == Standard::kPal) {
    if (!IsSupportedPalPattern(pattern)) {
      if (error != nullptr) {
        *error = "Pattern is not valid for PAL projects.";
      }
      return false;
    }
    return GeneratePalPatternFrame(pattern, out_image, error);
  }

  if (standard == Standard::kNtsc) {
    if (!IsSupportedNtscPattern(pattern)) {
      if (error != nullptr) {
        *error = "Pattern is not valid for NTSC projects.";
      }
      return false;
    }
    return GenerateNtscPatternFrame(pattern, out_image, error);
  }

  if (error != nullptr) {
    *error = "Unsupported video standard for frame source generation.";
  }
  return false;
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
      (EndsWithLowercase(source, ".png") ||
       EndsWithLowercase(source, ".raw") ||
       EndsWithLowercase(source, ".mp4") ||
       EndsWithLowercase(source, ".mov"));
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

  if (EndsWithLowercase(source, ".png") || EndsWithLowercase(source, ".raw")) {
    *out_frame_count = 1;
    return true;
  }

  if (EndsWithLowercase(source, ".mp4")) {
    const bool cache_hit = has_cached_mp4_frames_ &&
                           cached_mp4_source_ == section.source &&
                           cached_mp4_standard_ == standard;
    if (!cache_hit) {
      std::vector<FrameSourceImage> decoded_frames;
      std::string decode_error;
      if (!DecodeMp4Frames(section.source, standard, &decoded_frames, &decode_error)) {
        if (error != nullptr) {
          *error = decode_error.empty() ? "Failed to decode progressive MP4 source."
                                        : decode_error;
        }
        return false;
      }
      cached_mp4_frames_ = std::move(decoded_frames);
      cached_mp4_source_ = section.source;
      cached_mp4_standard_ = standard;
      has_cached_mp4_frames_ = true;
    }

    *out_frame_count = static_cast<int>(cached_mp4_frames_.size());
    return true;
  }

  if (EndsWithLowercase(source, ".mov")) {
    const bool cache_hit = has_cached_mov_frames_ &&
                           cached_mov_source_ == section.source &&
                           cached_mov_standard_ == standard;
    if (!cache_hit) {
      std::vector<FrameSourceImage> decoded_frames;
      std::string decode_error;
      if (!DecodeMovFrames(section.source, standard, &decoded_frames, &decode_error)) {
        if (error != nullptr) {
          *error = decode_error.empty() ? "Failed to decode progressive MOV source."
                                        : decode_error;
        }
        return false;
      }
      cached_mov_frames_ = std::move(decoded_frames);
      cached_mov_source_ = section.source;
      cached_mov_standard_ = standard;
      has_cached_mov_frames_ = true;
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

  if (EndsWithLowercase(source, ".png")) {
    const bool cache_hit = has_cached_png_frame_ &&
                           cached_png_source_ == section.source &&
                           cached_png_standard_ == standard;
    if (!cache_hit) {
      FrameSourceImage decoded;
      std::string decode_error;
      if (!LoadPngFrame(section.source, standard, &decoded, &decode_error)) {
        if (error != nullptr) {
          *error = decode_error.empty() ? "Failed to decode progressive PNG source."
                                        : decode_error;
        }
        return false;
      }
      cached_png_frame_ = decoded;
      cached_png_source_ = section.source;
      cached_png_standard_ = standard;
      has_cached_png_frame_ = true;
    }

    if (out_image == nullptr) {
      if (error != nullptr) {
        *error = "Frame source output image pointer must not be null.";
      }
      return false;
    }
    *out_image = cached_png_frame_;
    return true;
  }

  if (EndsWithLowercase(source, ".raw")) {
    return LoadRawFrame(section, standard, out_image, error);
  }

  if (EndsWithLowercase(source, ".mp4")) {
    if (frame_index < 0) {
      if (error != nullptr) {
        *error = "Progressive frame index must be non-negative.";
      }
      return false;
    }

    int frame_count = 0;
    std::string frame_count_error;
    if (!ResolveFrameCount(section, standard, &frame_count, &frame_count_error)) {
      if (error != nullptr) {
        *error = frame_count_error.empty() ? "Failed to resolve progressive MP4 frame count."
                                           : frame_count_error;
      }
      return false;
    }

    if (frame_index >= frame_count) {
      if (error != nullptr) {
        *error = "Requested frame index exceeds decoded progressive MP4 source length.";
      }
      return false;
    }

    if (out_image == nullptr) {
      if (error != nullptr) {
        *error = "Frame source output image pointer must not be null.";
      }
      return false;
    }

    *out_image = cached_mp4_frames_[static_cast<std::size_t>(frame_index)];
    return true;
  }

  if (EndsWithLowercase(source, ".mov")) {
    if (frame_index < 0) {
      if (error != nullptr) {
        *error = "Progressive frame index must be non-negative.";
      }
      return false;
    }

    int frame_count = 0;
    std::string frame_count_error;
    if (!ResolveFrameCount(section, standard, &frame_count, &frame_count_error)) {
      if (error != nullptr) {
        *error = frame_count_error.empty() ? "Failed to resolve progressive MOV frame count."
                                           : frame_count_error;
      }
      return false;
    }

    if (frame_index >= frame_count) {
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