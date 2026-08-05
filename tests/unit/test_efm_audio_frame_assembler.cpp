/*
 * File:        test_efm_audio_frame_assembler.cpp
 * Module:      efm
 * Purpose:     Unit tests for the F1 frame assembler of the EFM module.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "videosynth/efm/audio_frame_assembler.h"

namespace videosynth::efm {
namespace {

TEST(EfmAudioFrameAssemblerTest, AssemblesWordsAsHighByteThenLowByte) {
  // IEC 60908-1999, 16.2: WmA carries the higher and WmB the lower 8 bits.
  const std::array<std::int16_t, kStereoSamplesPerF1Frame> left = {
      0x0102, 0x0304, 0x0506, 0x0708, 0x090A, 0x0B0C};
  const std::array<std::int16_t, kStereoSamplesPerF1Frame> right = {
      0x1112, 0x1314, 0x1516, 0x1718, 0x191A, 0x1B1C};

  const F1Frame frame = AssembleF1Frame(left, right);

  const F1Frame expected = {0x01, 0x02, 0x11, 0x12, 0x03, 0x04, 0x13, 0x14,
                            0x05, 0x06, 0x15, 0x16, 0x07, 0x08, 0x17, 0x18,
                            0x09, 0x0A, 0x19, 0x1A, 0x0B, 0x0C, 0x1B, 0x1C};
  EXPECT_EQ(frame, expected);
}

TEST(EfmAudioFrameAssemblerTest, AssemblesNegativeSamplesAsTwosComplement) {
  std::array<std::int16_t, kStereoSamplesPerF1Frame> left{};
  std::array<std::int16_t, kStereoSamplesPerF1Frame> right{};
  left[0] = -1;
  right[0] = -32768;

  const F1Frame frame = AssembleF1Frame(left, right);

  EXPECT_EQ(frame[0], 0xFF);
  EXPECT_EQ(frame[1], 0xFF);
  EXPECT_EQ(frame[2], 0x80);
  EXPECT_EQ(frame[3], 0x00);
}

TEST(EfmAudioFrameAssemblerTest, EmitsOneFrameEverySixStereoSamples) {
  AudioFrameAssembler assembler;
  std::vector<F1Frame> frames;

  const std::vector<std::int16_t> left(13, 0x0100);
  const std::vector<std::int16_t> right(13, 0x0200);

  ASSERT_TRUE(assembler.PushSamples(left, right, &frames));

  EXPECT_EQ(frames.size(), 2U);
  EXPECT_EQ(assembler.PendingSampleCount(), 1U);
}

TEST(EfmAudioFrameAssemblerTest, RetainsPartialFrameAcrossPushes) {
  AudioFrameAssembler assembler;
  std::vector<F1Frame> frames;

  const std::vector<std::int16_t> first_left = {1, 2, 3, 4};
  const std::vector<std::int16_t> first_right = {5, 6, 7, 8};
  ASSERT_TRUE(assembler.PushSamples(first_left, first_right, &frames));
  EXPECT_TRUE(frames.empty());

  const std::vector<std::int16_t> second_left = {9, 10};
  const std::vector<std::int16_t> second_right = {11, 12};
  ASSERT_TRUE(assembler.PushSamples(second_left, second_right, &frames));

  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(assembler.PendingSampleCount(), 0U);

  const std::array<std::int16_t, kStereoSamplesPerF1Frame> expected_left = {
      1, 2, 3, 4, 9, 10};
  const std::array<std::int16_t, kStereoSamplesPerF1Frame> expected_right = {
      5, 6, 7, 8, 11, 12};
  EXPECT_EQ(frames.front(), AssembleF1Frame(expected_left, expected_right));
}

TEST(EfmAudioFrameAssemblerTest, FlushPadsPartialFrameWithDigitalSilence) {
  AudioFrameAssembler assembler;
  std::vector<F1Frame> frames;

  const std::vector<std::int16_t> left = {0x1234, 0x5678};
  const std::vector<std::int16_t> right = {0x2345, 0x6789};
  ASSERT_TRUE(assembler.PushSamples(left, right, &frames));
  ASSERT_TRUE(assembler.Flush(&frames));

  ASSERT_EQ(frames.size(), 1U);
  const std::array<std::int16_t, kStereoSamplesPerF1Frame> expected_left = {
      0x1234, 0x5678, 0, 0, 0, 0};
  const std::array<std::int16_t, kStereoSamplesPerF1Frame> expected_right = {
      0x2345, 0x6789, 0, 0, 0, 0};
  EXPECT_EQ(frames.front(), AssembleF1Frame(expected_left, expected_right));
  EXPECT_EQ(assembler.PendingSampleCount(), 0U);
}

TEST(EfmAudioFrameAssemblerTest, FlushWithoutPendingSamplesEmitsNothing) {
  AudioFrameAssembler assembler;
  std::vector<F1Frame> frames;

  ASSERT_TRUE(assembler.Flush(&frames));

  EXPECT_TRUE(frames.empty());
}

TEST(EfmAudioFrameAssemblerTest, ResetDiscardsPartialFrame) {
  AudioFrameAssembler assembler;
  std::vector<F1Frame> frames;

  const std::vector<std::int16_t> left = {1, 2, 3};
  ASSERT_TRUE(assembler.PushSamples(left, left, &frames));
  assembler.Reset();
  ASSERT_TRUE(assembler.Flush(&frames));

  EXPECT_EQ(assembler.PendingSampleCount(), 0U);
  EXPECT_TRUE(frames.empty());
}

TEST(EfmAudioFrameAssemblerTest, RejectsMismatchedChannelLengths) {
  AudioFrameAssembler assembler;
  std::vector<F1Frame> frames;

  const std::vector<std::int16_t> left(6, 0);
  const std::vector<std::int16_t> right(5, 0);

  EXPECT_FALSE(assembler.PushSamples(left, right, &frames));
  EXPECT_EQ(assembler.PendingSampleCount(), 0U);
  EXPECT_TRUE(frames.empty());
}

TEST(EfmAudioFrameAssemblerTest, RejectsNullOutput) {
  AudioFrameAssembler assembler;
  const std::vector<std::int16_t> samples(6, 0);

  EXPECT_FALSE(assembler.PushSamples(samples, samples, nullptr));
  EXPECT_FALSE(assembler.Flush(nullptr));
}

}  // namespace
}  // namespace videosynth::efm
