/*
 * File:        progressive_frame_source.h
 * Module:      progressive_frame_source
 * Purpose:     Defines fixed-format frame-source buffers for frame-based
 * content.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth {

struct YCbCr444Pixel {
  std::int16_t y = 64;
  std::int16_t cb = 512;
  std::int16_t cr = 512;
};

struct FrameSourceImage {
  int width = 0;
  int height = 0;
  int active_x = 0;
  int active_y = 0;
  int active_width = 0;
  int active_height = 0;
  std::vector<YCbCr444Pixel> pixels;

  const YCbCr444Pixel& PixelAt(int x, int y) const;
};

// Thread-safety: ProgressiveFrameSource is NOT thread-safe due to mutable
// cache state (has_cached_exr_frame_, cached_exr_source_, etc.). Inherits the
// thread-safe requirement from IProgressiveFrameProvider but implementations
// must handle their own synchronization. Concurrent calls will result in race
// conditions on the cache.
class ProgressiveFrameSource final : public IProgressiveFrameProvider {
 public:
  // Checks if this source supports the given section.
  //
  // Args:
  //   section: The project section to check.
  //
  // Returns:
  //   true if the section is supported, false otherwise.
  bool SupportsSection(const Section& section) const;

  // Clears any cached frame data.
  void ClearCache() const;

  // Determines the frame count for a section.
  //
  // Ownership: out_frame_count and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  //
  // Args:
  //   section: The project section.
  //   standard: The video standard.
  //   out_frame_count: Output pointer for the resolved frame count.
  //   error: Output pointer for any error message.
  //
  // Returns:
  //   true on success, false on any error.
  bool ResolveFrameCount(const Section& section, Standard standard,
                         int* out_frame_count, std::string* error) const;

  // Generates a frame of image data for the given section and frame index.
  //
  // Ownership: out_image and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  //
  // Args:
  //   section: The project section.
  //   frame_index: Index of the frame to generate.
  //   standard: The video standard.
  //   out_image: Output pointer for the generated frame image.
  //   error: Output pointer for any error message.
  //
  // Returns:
  //   true on success, false on any error.
  bool GenerateFrame(const Section& section, int frame_index, Standard standard,
                     FrameSourceImage* out_image,
                     std::string* error) const override;

 private:
  mutable bool has_cached_exr_frame_ = false;
  mutable std::string cached_exr_source_;
  mutable Standard cached_exr_standard_ = Standard::kUnknown;
  mutable FrameSourceImage cached_exr_frame_;

  mutable bool has_cached_mkv_frames_ = false;
  mutable std::string cached_mkv_source_;
  mutable Standard cached_mkv_standard_ = Standard::kUnknown;
  mutable bool cached_mkv_is_complete_ = false;
  mutable std::vector<FrameSourceImage> cached_mkv_frames_;
};

}  // namespace videosynth