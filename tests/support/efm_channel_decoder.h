/*
 * File:        efm_channel_decoder.h
 * Module:      efm_tests
 * Purpose:     Test-only EFM channel decoder: reconstructs channel frames,
 *              subcode symbols and audio samples from a T-value stream.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "videosynth/efm/efm_stream_encoder.h"

namespace videosynth::efm::test_support {

// Origin of one data symbol of an F2 frame, transcribed from the output
// byte-sequence table of ECMA-130, figure C.4 with D = 4 (see
// test_efm_circ_encoder.cpp, which uses the same table to check the encoder).
struct SymbolOrigin {
  std::size_t position;
  std::size_t word;
  bool high_byte;
  std::size_t frame_delay;
};

inline constexpr std::array<SymbolOrigin, 24> kFigureC4DataSymbols = {{
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
inline std::vector<F1Frame> Deinterleave(
    const std::vector<DecodedFrame>& frames) {
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
inline void ExtractSamples(const std::vector<F1Frame>& frames,
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

}  // namespace videosynth::efm::test_support
