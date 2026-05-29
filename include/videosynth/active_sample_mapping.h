/*
 * File:        active_sample_mapping.h
 * Module:      active_sample_mapping
 * Purpose:     Defines deterministic active-sample to source-pixel mapping helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <algorithm>

namespace videosynth {

inline int MapActiveSampleToSourcePixel(int sample_index,
                                        int active_sample_count,
                                        int source_active_width,
                                        int source_active_x) {
  if (active_sample_count <= 0 || source_active_width <= 0) {
    return source_active_x;
  }

  const int last_pixel = source_active_x + source_active_width - 1;
  const int mapped = source_active_x + ((sample_index * source_active_width) / active_sample_count);
  return std::max(source_active_x, std::min(last_pixel, mapped));
}

inline int MappedSampleCountForSourcePixel(int source_pixel_index,
                                           int source_active_width,
                                           int active_sample_count) {
  if (active_sample_count <= 0 || source_active_width <= 0 || source_pixel_index < 0 ||
      source_pixel_index >= source_active_width) {
    return 0;
  }

  const int start = (source_pixel_index * active_sample_count + source_active_width - 1) /
                    source_active_width;
  const int end = (((source_pixel_index + 1) * active_sample_count + source_active_width - 1) /
                   source_active_width) -
                  1;
  return std::max(0, end - start + 1);
}

}  // namespace videosynth
