/*
 * File:        test_active_sample_mapping.cpp
 * Module:      active_sample_mapping_tests
 * Purpose:     Validates deterministic active-sample mapping coverage and endpoint guarantees.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <algorithm>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "videosynth/active_sample_mapping.h"

namespace videosynth {
namespace {

TEST(ActiveSampleMappingTest, EndpointsMapToFirstAndLastSourcePixelForPalAndNtsc) {
  for (const int active_samples : {922, 745}) {
    for (const int width : {704, 720}) {
      const int x0 = (width == 704) ? 8 : 0;
      EXPECT_EQ(MapActiveSampleToSourcePixel(0, active_samples, width, x0), x0);
      EXPECT_EQ(MapActiveSampleToSourcePixel(active_samples - 1, active_samples, width, x0),
                x0 + width - 1);
    }
  }
}

TEST(ActiveSampleMappingTest, EverySourcePixelReceivesAtLeastOneMappedSample) {
  for (const int active_samples : {922, 745}) {
    for (const int width : {704, 720}) {
      const int x0 = (width == 704) ? 8 : 0;
      std::vector<int> per_pixel_counts(static_cast<std::size_t>(width), 0);

      for (int s = 0; s < active_samples; ++s) {
        const int pixel_x = MapActiveSampleToSourcePixel(s, active_samples, width, x0);
        ASSERT_GE(pixel_x, x0);
        ASSERT_LT(pixel_x, x0 + width);
        ++per_pixel_counts[static_cast<std::size_t>(pixel_x - x0)];
      }

      const int min_count = *std::min_element(per_pixel_counts.begin(), per_pixel_counts.end());
      EXPECT_GE(min_count, 1);
      EXPECT_EQ(std::accumulate(per_pixel_counts.begin(), per_pixel_counts.end(), 0), active_samples);
    }
  }
}

TEST(ActiveSampleMappingTest, SourcePixelSampleSpanDistributionIsDeterministic) {
  for (const int active_samples : {922, 745}) {
    for (const int width : {704, 720}) {
      std::vector<int> per_pixel_counts(static_cast<std::size_t>(width), 0);
      for (int x = 0; x < width; ++x) {
        per_pixel_counts[static_cast<std::size_t>(x)] =
            MappedSampleCountForSourcePixel(x, width, active_samples);
      }

      const int min_count = *std::min_element(per_pixel_counts.begin(), per_pixel_counts.end());
      const int max_count = *std::max_element(per_pixel_counts.begin(), per_pixel_counts.end());
      EXPECT_LE(max_count - min_count, 1);
      EXPECT_EQ(std::accumulate(per_pixel_counts.begin(), per_pixel_counts.end(), 0), active_samples);
    }
  }
}

}  // namespace
}  // namespace videosynth
