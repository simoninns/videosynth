/*
 * File:        progressive_frame_source.h
 * Module:      progressive_frame_source
 * Purpose:     Defines fixed-format frame-source buffers for frame-based content.
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

class ProgressiveFrameSource final : public IProgressiveFrameProvider {
 public:
  bool SupportsSection(const Section& section) const;

  void ClearCache() const;

  bool ResolveFrameCount(const Section& section,
                         Standard standard,
                         int* out_frame_count,
                         std::string* error) const;

  bool GenerateFrame(const Section& section,
                     int frame_index,
                     Standard standard,
                     FrameSourceImage* out_image,
                     std::string* error) const override;

 private:
  mutable bool has_cached_png_frame_ = false;
  mutable std::string cached_png_source_;
  mutable Standard cached_png_standard_ = Standard::kUnknown;
  mutable FrameSourceImage cached_png_frame_;

  mutable bool has_cached_exr_frame_ = false;
  mutable std::string cached_exr_source_;
  mutable Standard cached_exr_standard_ = Standard::kUnknown;
  mutable FrameSourceImage cached_exr_frame_;

  mutable bool has_cached_mp4_frames_ = false;
  mutable std::string cached_mp4_source_;
  mutable Standard cached_mp4_standard_ = Standard::kUnknown;
  mutable bool cached_mp4_is_complete_ = false;
  mutable std::vector<FrameSourceImage> cached_mp4_frames_;

  mutable bool has_cached_mov_frames_ = false;
  mutable std::string cached_mov_source_;
  mutable Standard cached_mov_standard_ = Standard::kUnknown;
  mutable bool cached_mov_is_complete_ = false;
  mutable std::vector<FrameSourceImage> cached_mov_frames_;
};

}  // namespace videosynth