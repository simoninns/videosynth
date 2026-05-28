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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
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

  if (error != nullptr) {
    *error =
        "Progressive source decoding is not yet implemented for this source family.";
  }
  return false;
}

}  // namespace videosynth