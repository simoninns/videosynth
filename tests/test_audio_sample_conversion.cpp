/*
 * File:        test_audio_sample_conversion.cpp
 * Module:      audio_sample_conversion_tests
 * Purpose:     Validates 24-bit to 16-bit PCM conversion used by the EFM
 *              digital audio path.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "videosynth/audio_sample_conversion.h"
#include "videosynth/audio_synthesizer.h"

namespace videosynth {
namespace {

TEST(AudioSampleConversionTest, ScalesExactMultiplesOfTheDivisor) {
  EXPECT_EQ(ConvertSample24To16(0), 0);
  EXPECT_EQ(ConvertSample24To16(256), 1);
  EXPECT_EQ(ConvertSample24To16(-256), -1);
  EXPECT_EQ(ConvertSample24To16(256 * 1000), 1000);
  EXPECT_EQ(ConvertSample24To16(-256 * 1000), -1000);
}

TEST(AudioSampleConversionTest, RoundsToNearestWithTiesAwayFromZero) {
  EXPECT_EQ(ConvertSample24To16(127), 0);
  EXPECT_EQ(ConvertSample24To16(128), 1);
  EXPECT_EQ(ConvertSample24To16(129), 1);
  EXPECT_EQ(ConvertSample24To16(383), 1);
  EXPECT_EQ(ConvertSample24To16(384), 2);
  EXPECT_EQ(ConvertSample24To16(-127), 0);
  EXPECT_EQ(ConvertSample24To16(-128), -1);
  EXPECT_EQ(ConvertSample24To16(-384), -2);
}

TEST(AudioSampleConversionTest, IsSymmetricAboutZeroBelowSaturation) {
  for (std::int32_t sample = -8000000; sample <= 8000000; sample += 9973) {
    EXPECT_EQ(ConvertSample24To16(sample), -ConvertSample24To16(-sample))
        << "sample " << sample;
  }
}

TEST(AudioSampleConversionTest, SaturatesAtFullScale) {
  // The symmetric 24-bit peak (2^23 - 1) rounds to magnitude 32768, so the
  // positive peak saturates at 32767 while the negative peak reaches the
  // 16-bit minimum exactly.
  EXPECT_EQ(ConvertSample24To16(AudioSynthesizer::kFullScale), 32767);
  EXPECT_EQ(ConvertSample24To16(-AudioSynthesizer::kFullScale), -32768);
  EXPECT_EQ(ConvertSample24To16(8388608), 32767);
  EXPECT_EQ(ConvertSample24To16(-8388608), -32768);
  // Out-of-domain values clamp rather than wrap.
  EXPECT_EQ(ConvertSample24To16(100000000), 32767);
  EXPECT_EQ(ConvertSample24To16(-100000000), -32768);
}

TEST(AudioSampleConversionTest, ConvertsBlocksPreservingOrder) {
  const std::vector<std::int32_t> input = {
      0, 256, -256, 128, -128, AudioSynthesizer::kFullScale};
  const std::vector<std::int16_t> converted = ConvertSamples24To16(input);

  ASSERT_EQ(converted.size(), input.size());
  const std::vector<std::int16_t> expected = {0, 1, -1, 1, -1, 32767};
  EXPECT_EQ(converted, expected);
}

}  // namespace
}  // namespace videosynth
