/*
 * File:        test_efm_stream_encoder.cpp
 * Module:      efm
 * Purpose:     Unit tests for the EFM stream encoder facade, decoding its
 *              T-value output back to subcode and audio samples.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "videosynth/efm/efm_stream_encoder.h"

namespace videosynth::efm {
namespace {

// Origin of one data symbol of an F2 frame, transcribed from the output
// byte-sequence table of ECMA-130, figure C.4 with D = 4 (see
// test_efm_circ_encoder.cpp, which uses the same table to check the encoder).
struct SymbolOrigin {
  std::size_t position;
  std::size_t word;
  bool high_byte;
  std::size_t frame_delay;
};

constexpr std::array<SymbolOrigin, 24> kFigureC4DataSymbols = {{
    {0, 0, true, 3},     {1, 0, false, 6},    {2, 4, true, 11},
    {3, 4, false, 14},   {4, 8, true, 19},    {5, 8, false, 22},
    {6, 1, true, 27},    {7, 1, false, 30},   {8, 5, true, 35},
    {9, 5, false, 38},   {10, 9, true, 43},   {11, 9, false, 46},
    {16, 2, true, 65},   {17, 2, false, 68},  {18, 6, true, 73},
    {19, 6, false, 76},  {20, 10, true, 81},  {21, 10, false, 84},
    {22, 3, true, 89},   {23, 3, false, 92},  {24, 7, true, 97},
    {25, 7, false, 100}, {26, 11, true, 105}, {27, 11, false, 108},
}};

// One decoded channel frame: the control byte of its subcode symbol (zero for
// the S0/S1 sync frames) and the 32 symbols of its F2 frame.
struct DecodedFrame {
  std::uint16_t control_symbol = 0;
  std::uint8_t control_byte = 0;
  F2Frame f2_frame{};
};

// Test-only EFM demodulator: reconstructs channel bits from T values, checks
// that a sync header sits at every 588-bit boundary and translates the 14-bit
// symbols back to bytes. It performs no error correction.
class TestChannelDecoder {
 public:
  TestChannelDecoder() {
    for (unsigned value = 0; value < 256U; ++value) {
      symbols_[EfmSymbol(static_cast<std::uint8_t>(value))] =
          static_cast<std::uint8_t>(value);
    }
  }

  std::vector<bool> ChannelBits(
      const std::vector<std::uint8_t>& t_values) const {
    std::vector<bool> bits;
    for (const std::uint8_t t_value : t_values) {
      bits.push_back(true);
      for (std::uint8_t index = 1; index < t_value; ++index) {
        bits.push_back(false);
      }
    }
    return bits;
  }

  std::vector<DecodedFrame> Decode(
      const std::vector<std::uint8_t>& t_values) const {
    const std::vector<bool> bits = ChannelBits(t_values);
    std::vector<DecodedFrame> frames;
    for (std::size_t start = 0; start + kChannelBitsPerFrame <= bits.size();
         start += kChannelBitsPerFrame) {
      EXPECT_EQ(ReadBits(bits, start, kSyncHeaderBits), kSyncHeaderPattern)
          << "missing sync at channel bit " << start;

      DecodedFrame frame;
      for (std::size_t symbol = 0; symbol < kSymbolsPerChannelFrame; ++symbol) {
        const std::size_t offset =
            start + kSyncHeaderBits + kMergingBitsPerBoundary +
            (symbol * (kChannelBitsPerSymbol + kMergingBitsPerBoundary));
        const auto pattern = static_cast<std::uint16_t>(
            ReadBits(bits, offset, kChannelBitsPerSymbol));
        if (symbol == 0) {
          frame.control_symbol = pattern;
          const auto entry = symbols_.find(pattern);
          frame.control_byte = entry == symbols_.end() ? 0 : entry->second;
        } else {
          const auto entry = symbols_.find(pattern);
          EXPECT_NE(entry, symbols_.end())
              << "unknown symbol at channel bit " << offset;
          frame.f2_frame[symbol - 1] =
              entry == symbols_.end() ? 0 : entry->second;
        }
      }
      frames.push_back(frame);
    }
    return frames;
  }

 private:
  static std::uint32_t ReadBits(const std::vector<bool>& bits,
                                std::size_t offset, std::size_t count) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
      value <<= 1U;
      if (bits[offset + index]) {
        value |= 1U;
      }
    }
    return value;
  }

  std::map<std::uint16_t, std::uint8_t> symbols_;
};

// Complementary de-interleaver: the mirror image of the encoder's delays, with
// every symbol given the same total delay of kCircPipelineLatencyFrames
// (ECMA-130, C.9). Decoded frame k carries the F1 frame pushed at frame time
// k - kCircPipelineLatencyFrames.
std::vector<F1Frame> Deinterleave(const std::vector<DecodedFrame>& frames) {
  std::vector<F1Frame> f1_frames(frames.size(), F1Frame{});
  for (std::size_t index = 0; index < frames.size(); ++index) {
    for (const SymbolOrigin& origin : kFigureC4DataSymbols) {
      const std::size_t lookback =
          kCircPipelineLatencyFrames - origin.frame_delay;
      if (index < lookback) {
        continue;  // Before the start of the stream: digital silence.
      }
      const std::size_t byte = 2 * origin.word + (origin.high_byte ? 0 : 1);
      f1_frames[index][byte] =
          frames[index - lookback].f2_frame[origin.position];
    }
  }
  return f1_frames;
}

// The stereo samples of a run of F1 frames, in source order
// (IEC 60908-1999, 16.2: WmA is the higher and WmB the lower eight bits).
void ExtractSamples(const std::vector<F1Frame>& frames,
                    std::vector<std::int16_t>* left,
                    std::vector<std::int16_t>* right) {
  for (const F1Frame& frame : frames) {
    for (std::size_t period = 0; period < kStereoSamplesPerF1Frame; ++period) {
      const std::size_t word = 2 * period;
      const auto sample = [&frame](std::size_t index) {
        return static_cast<std::int16_t>(
            static_cast<std::uint16_t>(frame[2 * index] << 8) |
            frame[2 * index + 1]);
      };
      left->push_back(sample(word));
      right->push_back(sample(word + 1));
    }
  }
}

// A track table with a lead-in, two programme tracks and a lead-out, sized in
// subcode sections (75 per second, IEC 60908-1999, 17.3).
TrackTable MakeTrackTable(std::size_t lead_in_sections,
                          std::size_t track_sections,
                          std::size_t lead_out_sections) {
  TrackTable table;
  table.video_system = VideoSystem::kPal;
  table.entries.push_back(
      TrackTableEntry{SubcodeArea::kLeadIn, 0, 0, lead_in_sections});
  table.entries.push_back(TrackTableEntry{SubcodeArea::kProgramme, 1,
                                          lead_in_sections, track_sections});
  table.entries.push_back(TrackTableEntry{SubcodeArea::kProgramme, 2,
                                          lead_in_sections + track_sections,
                                          track_sections});
  table.entries.push_back(TrackTableEntry{
      SubcodeArea::kLeadOut, 0, lead_in_sections + (2 * track_sections),
      lead_out_sections});
  return table;
}

// A deterministic stereo test signal.
void MakeSamples(std::size_t count, std::vector<std::int16_t>* left,
                 std::vector<std::int16_t>* right) {
  for (std::size_t index = 0; index < count; ++index) {
    left->push_back(static_cast<std::int16_t>((index * 251U) % 65536U - 32768));
    right->push_back(
        static_cast<std::int16_t>((index * 937U) % 65536U - 32768));
  }
}

std::vector<std::uint8_t> EncodeStream(const TrackTable& table,
                                       const std::vector<std::int16_t>& left,
                                       const std::vector<std::int16_t>& right) {
  EfmStreamEncoder encoder;
  EXPECT_TRUE(encoder.Begin(table));
  std::vector<std::uint8_t> t_values;
  EXPECT_TRUE(encoder.PushSamples(left, right, &t_values));
  EXPECT_TRUE(encoder.Flush(&t_values));
  return t_values;
}

TEST(EfmStreamEncoderTest, BeginRejectsAnInvalidTrackTable) {
  EfmStreamEncoder encoder;
  TrackTable table;

  // Empty, and then a table whose entries do not tile the timeline.
  EXPECT_FALSE(encoder.Begin(table));
  table.entries.push_back(TrackTableEntry{SubcodeArea::kProgramme, 1, 5, 10});
  EXPECT_FALSE(encoder.Begin(table));

  std::vector<std::uint8_t> t_values;
  EXPECT_FALSE(encoder.PushSamples({0}, {0}, &t_values));
  EXPECT_FALSE(encoder.Flush(&t_values));
}

TEST(EfmStreamEncoderTest, RejectsMismatchedChannelsAndNullOutput) {
  EfmStreamEncoder encoder;
  ASSERT_TRUE(encoder.Begin(MakeTrackTable(75, 300, 75)));

  std::vector<std::uint8_t> t_values;
  EXPECT_FALSE(encoder.PushSamples({0, 1}, {0}, &t_values));
  EXPECT_FALSE(encoder.PushSamples({0}, {0}, nullptr));
  EXPECT_FALSE(encoder.Flush(nullptr));
  EXPECT_TRUE(t_values.empty());
}

TEST(EfmStreamEncoderTest, EmitsOneChannelFrameForEverySixStereoSamples) {
  const TrackTable table = MakeTrackTable(75, 300, 75);
  std::vector<std::int16_t> left;
  std::vector<std::int16_t> right;
  MakeSamples(std::size_t{6} * 500U, &left, &right);

  EfmStreamEncoder encoder;
  ASSERT_TRUE(encoder.Begin(table));
  std::vector<std::uint8_t> t_values;
  ASSERT_TRUE(encoder.PushSamples(left, right, &t_values));
  EXPECT_EQ(encoder.ChannelFrameCount(), 500U);

  ASSERT_TRUE(encoder.Flush(&t_values));
  // The flush adds the CIRC pipeline latency in silent frames.
  EXPECT_EQ(encoder.ChannelFrameCount(), 500U + kCircPipelineLatencyFrames);
}

TEST(EfmStreamEncoderTest, TValuesSpanOnlyTMinToTMax) {
  std::vector<std::int16_t> left;
  std::vector<std::int16_t> right;
  MakeSamples(std::size_t{6} * 400U, &left, &right);

  const std::vector<std::uint8_t> t_values =
      EncodeStream(MakeTrackTable(75, 300, 75), left, right);

  ASSERT_FALSE(t_values.empty());
  for (std::size_t index = 0; index < t_values.size(); ++index) {
    ASSERT_GE(t_values[index], kMinRunLengthT) << "T value " << index;
    ASSERT_LE(t_values[index], kMaxRunLengthT) << "T value " << index;
  }
}

TEST(EfmStreamEncoderTest, OutputIsAPureFunctionOfTheInput) {
  const TrackTable table = MakeTrackTable(75, 300, 75);
  std::vector<std::int16_t> left;
  std::vector<std::int16_t> right;
  MakeSamples(std::size_t{6} * 300U, &left, &right);

  EXPECT_EQ(EncodeStream(table, left, right), EncodeStream(table, left, right));
}

TEST(EfmStreamEncoderTest, DecodedSubcodeMatchesTheTrackTable) {
  const TrackTable table = MakeTrackTable(75, 300, 75);
  // Enough samples to cover several whole subcode sections.
  std::vector<std::int16_t> left;
  std::vector<std::int16_t> right;
  MakeSamples(6U * kFramesPerSubcodeSection * 5U, &left, &right);

  const std::vector<DecodedFrame> frames =
      TestChannelDecoder().Decode(EncodeStream(table, left, right));
  ASSERT_GE(frames.size(), kFramesPerSubcodeSection * 5U);

  SubcodeGenerator generator;
  ASSERT_TRUE(generator.Begin(table));

  for (std::size_t section_index = 0; section_index < 5U; ++section_index) {
    SubcodeSection expected;
    ASSERT_TRUE(generator.GenerateSection(section_index, &expected));

    const std::size_t base = section_index * kFramesPerSubcodeSection;
    // IEC 60908-1999, 17.3: the first two control bytes are replaced by S0/S1.
    EXPECT_EQ(frames[base].control_symbol, kSubcodeSyncS0)
        << "section " << section_index;
    EXPECT_EQ(frames[base + 1].control_symbol, kSubcodeSyncS1)
        << "section " << section_index;
    for (std::size_t frame = kSubcodeSyncFrames;
         frame < kFramesPerSubcodeSection; ++frame) {
      ASSERT_EQ(frames[base + frame].control_byte, expected.ControlByte(frame))
          << "section " << section_index << " frame " << frame;
    }
  }
}

TEST(EfmStreamEncoderTest, DecodedAudioIsSampleExactAfterTheWarmUp) {
  const TrackTable table = MakeTrackTable(75, 300, 75);
  constexpr std::size_t kSourceFrames = 400;
  std::vector<std::int16_t> left;
  std::vector<std::int16_t> right;
  MakeSamples(kStereoSamplesPerF1Frame * kSourceFrames, &left, &right);

  const std::vector<DecodedFrame> frames =
      TestChannelDecoder().Decode(EncodeStream(table, left, right));
  ASSERT_EQ(frames.size(), kSourceFrames + kCircPipelineLatencyFrames);

  const std::vector<F1Frame> f1_frames = Deinterleave(frames);
  std::vector<std::int16_t> decoded_left;
  std::vector<std::int16_t> decoded_right;
  ExtractSamples(f1_frames, &decoded_left, &decoded_right);

  // Exactly kCircPipelineLatencyFrames frames of warm-up silence precede source
  // sample 0 (EFM implementation plan, Timing Alignment Contract).
  const std::size_t warm_up =
      kCircPipelineLatencyFrames * kStereoSamplesPerF1Frame;
  ASSERT_EQ(decoded_left.size(), warm_up + left.size());
  for (std::size_t index = 0; index < warm_up; ++index) {
    ASSERT_EQ(decoded_left[index], 0) << "warm-up sample " << index;
    ASSERT_EQ(decoded_right[index], 0) << "warm-up sample " << index;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    ASSERT_EQ(decoded_left[warm_up + index], left[index])
        << "source sample " << index;
    ASSERT_EQ(decoded_right[warm_up + index], right[index])
        << "source sample " << index;
  }
}

TEST(EfmStreamEncoderTest, PartialFramesArePaddedWithDigitalSilenceOnFlush) {
  const TrackTable table = MakeTrackTable(75, 300, 75);
  // Four stereo samples: two short of a whole F1 frame.
  const std::vector<std::int16_t> left = {1000, -1000, 2000, -2000};
  const std::vector<std::int16_t> right = {-1000, 1000, -2000, 2000};

  const std::vector<DecodedFrame> frames =
      TestChannelDecoder().Decode(EncodeStream(table, left, right));
  ASSERT_EQ(frames.size(), 1U + kCircPipelineLatencyFrames);

  std::vector<std::int16_t> decoded_left;
  std::vector<std::int16_t> decoded_right;
  ExtractSamples(Deinterleave(frames), &decoded_left, &decoded_right);

  const std::size_t warm_up =
      kCircPipelineLatencyFrames * kStereoSamplesPerF1Frame;
  for (std::size_t index = 0; index < left.size(); ++index) {
    EXPECT_EQ(decoded_left[warm_up + index], left[index]) << "sample " << index;
    EXPECT_EQ(decoded_right[warm_up + index], right[index])
        << "sample " << index;
  }
  for (std::size_t index = left.size(); index < kStereoSamplesPerF1Frame;
       ++index) {
    EXPECT_EQ(decoded_left[warm_up + index], 0) << "padding " << index;
    EXPECT_EQ(decoded_right[warm_up + index], 0) << "padding " << index;
  }
}

TEST(EfmStreamEncoderTest, ResetRestoresTheConstructedState) {
  const TrackTable table = MakeTrackTable(75, 300, 75);
  std::vector<std::int16_t> left;
  std::vector<std::int16_t> right;
  MakeSamples(std::size_t{6} * 120U, &left, &right);

  EfmStreamEncoder encoder;
  ASSERT_TRUE(encoder.Begin(table));
  std::vector<std::uint8_t> first;
  ASSERT_TRUE(encoder.PushSamples(left, right, &first));

  encoder.Reset();
  EXPECT_EQ(encoder.ChannelFrameCount(), 0U);
  std::vector<std::uint8_t> rejected;
  EXPECT_FALSE(encoder.PushSamples(left, right, &rejected));

  ASSERT_TRUE(encoder.Begin(table));
  std::vector<std::uint8_t> second;
  ASSERT_TRUE(encoder.PushSamples(left, right, &second));
  EXPECT_EQ(second, first);
}

}  // namespace
}  // namespace videosynth::efm
