/*
 * File:        test_efm_circ_encoder.cpp
 * Module:      efm
 * Purpose:     Unit tests for the CIRC encoder of the EFM module.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "videosynth/efm/circ_encoder.h"

namespace videosynth::efm {
namespace {

// Origin of one data symbol of an F2 frame, transcribed from the output
// byte-sequence table of ECMA-130, figure C.4 with D = 4: symbol `position` of
// output frame n is byte `word`/`high_byte` of the F1 frame pushed
// `frame_delay` frames earlier.
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

std::size_t F1ByteIndex(const SymbolOrigin& origin) {
  return 2 * origin.word + (origin.high_byte ? 0 : 1);
}

bool IsParityPosition(std::size_t position) {
  return (position >= kC2ParityStart &&
          position < kC2ParityStart + kCircParityBytes) ||
         (position >= kC1ParityStart &&
          position < kC1ParityStart + kCircParityBytes);
}

// GF(2^8) multiplication implemented independently of the encoder, by carry-
// less multiplication modulo P(x) = x^8 + x^4 + x^3 + x^2 + 1
// (IEC 60908-1999, 16.2).
std::uint8_t Multiply(std::uint8_t left, std::uint8_t right) {
  unsigned product = 0;
  unsigned shifted = left;
  for (unsigned bit = 0; bit < 8U; ++bit) {
    if (((static_cast<unsigned>(right) >> bit) & 1U) != 0U) {
      product ^= shifted;
    }
    shifted <<= 1U;
    if ((shifted & 0x100U) != 0U) {
      shifted ^= 0x11DU;
    }
  }
  return static_cast<std::uint8_t>(product);
}

// alpha^exponent, with alpha = 00000010 (IEC 60908-1999, 16.2).
std::uint8_t AlphaPower(std::size_t exponent) {
  std::uint8_t value = 1;
  for (std::size_t step = 0; step < exponent; ++step) {
    value = Multiply(value, 0x02);
  }
  return value;
}

// Evaluates the four syndromes of ECMA-130, C.7: H x V, which must be zero for
// a valid C1 or C2 codeword.
template <std::size_t kCodewordBytes>
std::array<std::uint8_t, kCircParityBytes> Syndromes(
    const std::array<std::uint8_t, kCodewordBytes>& codeword) {
  std::array<std::uint8_t, kCircParityBytes> syndromes{};
  for (std::size_t row = 0; row < kCircParityBytes; ++row) {
    std::uint8_t accumulator = 0;
    for (std::size_t column = 0; column < kCodewordBytes; ++column) {
      accumulator ^= Multiply(AlphaPower(row * (kCodewordBytes - 1 - column)),
                              codeword[column]);
    }
    syndromes[row] = accumulator;
  }
  return syndromes;
}

// Complementary de-interleaver: the mirror image of the encoder's delays, with
// every symbol given the same total delay of `latency` frames. Decoded frame k
// carries the F1 frame pushed at frame time k - latency; the first `latency`
// decoded frames are pipeline warm-up. This is a test-only helper; it performs
// no error correction.
//
// kCircPipelineLatencyFrames is the smallest workable latency (ECMA-130, C.9,
// the encoder's longest delay) and models a de-interleaver that reads the
// stream backwards. kCircDrainFrames models the mirror-delay de-interleaver a
// streaming decoder is built from.
std::vector<F1Frame> DeinterleaveWithLatency(
    const std::vector<F2Frame>& f2_frames, std::size_t latency) {
  std::vector<F1Frame> f1_frames(f2_frames.size(), F1Frame{});
  for (std::size_t index = 0; index < f2_frames.size(); ++index) {
    for (const SymbolOrigin& origin : kFigureC4DataSymbols) {
      const std::size_t lookback = latency - origin.frame_delay;
      if (index < lookback) {
        continue;  // Before the start of the stream: digital silence.
      }
      f1_frames[index][F1ByteIndex(origin)] =
          f2_frames[index - lookback][origin.position];
    }
  }
  return f1_frames;
}

std::vector<F1Frame> Deinterleave(const std::vector<F2Frame>& f2_frames) {
  return DeinterleaveWithLatency(f2_frames, kCircPipelineLatencyFrames);
}

std::vector<F1Frame> MakePseudoRandomFrames(std::size_t count) {
  std::mt19937 generator(20260719U);
  std::uniform_int_distribution<unsigned> distribution(0, 255);
  std::vector<F1Frame> frames(count, F1Frame{});
  for (F1Frame& frame : frames) {
    for (std::uint8_t& symbol : frame) {
      symbol = static_cast<std::uint8_t>(distribution(generator));
    }
  }
  return frames;
}

std::vector<F2Frame> Encode(const std::vector<F1Frame>& input, bool flush) {
  CircEncoder encoder;
  std::vector<F2Frame> output;
  output.reserve(input.size() + kCircDrainFrames);
  for (const F1Frame& frame : input) {
    output.push_back(encoder.EncodeFrame(frame));
  }
  if (flush) {
    EXPECT_TRUE(encoder.Flush(&output));
  }
  return output;
}

TEST(EfmCircEncoderTest, SilenceYieldsZeroDataWithInvertedParity) {
  CircEncoder encoder;

  for (std::size_t index = 0; index < 200; ++index) {
    const F2Frame frame = encoder.EncodeFrame(kSilentF1Frame);
    for (std::size_t position = 0; position < kF2FrameBytes; ++position) {
      // IEC 60908-1999, 16.2: parity symbols are recorded inverted, so the
      // parity of the all-zero codeword leaves the encoder as 0xFF.
      const std::uint8_t expected = IsParityPosition(position) ? 0xFF : 0x00;
      ASSERT_EQ(frame[position], expected)
          << "frame " << index << " position " << position;
    }
  }
}

TEST(EfmCircEncoderTest, DataSymbolsAppearAtFigureC4Positions) {
  constexpr std::size_t kImpulseFrame = 5;
  constexpr std::uint8_t kImpulseValue = 0xA5;
  constexpr std::size_t kFrameCount = 200;

  for (const SymbolOrigin& origin : kFigureC4DataSymbols) {
    std::vector<F1Frame> input(kFrameCount, F1Frame{});
    input[kImpulseFrame][F1ByteIndex(origin)] = kImpulseValue;
    const std::vector<F2Frame> output = Encode(input, /*flush=*/false);

    const std::size_t expected_frame = kImpulseFrame + origin.frame_delay;
    for (std::size_t index = 0; index < output.size(); ++index) {
      for (std::size_t position = 0; position < kF2FrameBytes; ++position) {
        if (IsParityPosition(position)) {
          continue;  // Parity depends on the impulse across many frames.
        }
        const bool is_impulse =
            index == expected_frame && position == origin.position;
        const std::uint8_t expected = is_impulse ? kImpulseValue : 0x00;
        ASSERT_EQ(output[index][position], expected)
            << "origin position " << origin.position << " frame " << index
            << " position " << position;
      }
    }
  }
}

TEST(EfmCircEncoderTest, C1CodewordsSatisfyParityCheckMatrix) {
  const std::vector<F1Frame> input = MakePseudoRandomFrames(300);
  const std::vector<F2Frame> output = Encode(input, /*flush=*/true);

  // ECMA-130, C.8: the third delay section delays every alternate symbol by one
  // frame, so a C1 codeword is split across two consecutive output frames.
  for (std::size_t time = 0; time + 1 < output.size(); ++time) {
    std::array<std::uint8_t, kC1CodewordBytes> codeword{};
    for (std::size_t position = 0; position < kC1CodewordBytes; ++position) {
      const std::size_t frame = position % 2 == 0 ? time + 1 : time;
      std::uint8_t symbol = output[frame][position];
      if (IsParityPosition(position)) {
        symbol = static_cast<std::uint8_t>(symbol ^ 0xFFU);
      }
      codeword[position] = symbol;
    }
    const std::array<std::uint8_t, kCircParityBytes> syndromes =
        Syndromes(codeword);
    for (std::size_t row = 0; row < kCircParityBytes; ++row) {
      ASSERT_EQ(syndromes[row], 0) << "C1 codeword at frame time " << time;
    }
  }
}

TEST(EfmCircEncoderTest, C2CodewordsSatisfyParityCheckMatrix) {
  const std::vector<F1Frame> input = MakePseudoRandomFrames(300);
  const std::vector<F2Frame> output = Encode(input, /*flush=*/true);

  // ECMA-130, C.5 / C.8: symbol i of a C2 codeword leaves the encoder 4i frames
  // later, plus one more frame for the alternate symbols of the third delay.
  constexpr std::size_t kMaxCodewordSpread = 4 * (kC2CodewordBytes - 1) + 1;
  for (std::size_t time = 0; time + kMaxCodewordSpread < output.size();
       ++time) {
    std::array<std::uint8_t, kC2CodewordBytes> codeword{};
    for (std::size_t position = 0; position < kC2CodewordBytes; ++position) {
      const std::size_t frame =
          time + 4 * position + (position % 2 == 0 ? 1 : 0);
      std::uint8_t symbol = output[frame][position];
      if (IsParityPosition(position)) {
        symbol = static_cast<std::uint8_t>(symbol ^ 0xFFU);
      }
      codeword[position] = symbol;
    }
    const std::array<std::uint8_t, kCircParityBytes> syndromes =
        Syndromes(codeword);
    for (std::size_t row = 0; row < kCircParityBytes; ++row) {
      ASSERT_EQ(syndromes[row], 0) << "C2 codeword at frame time " << time;
    }
  }
}

TEST(EfmCircEncoderTest, DeinterleavedStreamRecoversInputAfterWarmUp) {
  const std::vector<F1Frame> input = MakePseudoRandomFrames(500);
  const std::vector<F2Frame> output = Encode(input, /*flush=*/true);
  ASSERT_EQ(output.size(), input.size() + kCircDrainFrames);

  const std::vector<F1Frame> decoded = Deinterleave(output);
  ASSERT_EQ(decoded.size(), output.size());

  // Exactly kCircPipelineLatencyFrames frames of warm-up silence precede the
  // first source frame (EFM implementation plan, Timing Alignment Contract).
  for (std::size_t index = 0; index < kCircPipelineLatencyFrames; ++index) {
    ASSERT_EQ(decoded[index], kSilentF1Frame) << "warm-up frame " << index;
  }
  for (std::size_t index = 0; index < input.size(); ++index) {
    ASSERT_EQ(decoded[kCircPipelineLatencyFrames + index], input[index])
        << "source frame " << index;
  }
}

// A mirror-delay decoder emits the frame pushed at time t only once output
// frame t + kCircDrainFrames has arrived, so the flush must extend the stream
// by that many frames or the last source frames stay in its delay registers.
TEST(EfmCircEncoderTest, MirrorDelayDecoderRecoversTheFinalSourceFrames) {
  const std::vector<F1Frame> input = MakePseudoRandomFrames(500);
  const std::vector<F2Frame> output = Encode(input, /*flush=*/true);

  const std::vector<F1Frame> decoded =
      DeinterleaveWithLatency(output, kCircDrainFrames);
  ASSERT_EQ(decoded.size(), input.size() + kCircDrainFrames);

  for (std::size_t index = 0; index < kCircDrainFrames; ++index) {
    ASSERT_EQ(decoded[index], kSilentF1Frame) << "warm-up frame " << index;
  }
  for (std::size_t index = 0; index < input.size(); ++index) {
    ASSERT_EQ(decoded[kCircDrainFrames + index], input[index])
        << "source frame " << index;
  }
}

TEST(EfmCircEncoderTest, FlushEmitsDrainFrames) {
  CircEncoder encoder;
  std::vector<F2Frame> frames;

  ASSERT_TRUE(encoder.Flush(&frames));

  EXPECT_EQ(frames.size(), kCircDrainFrames);
}

TEST(EfmCircEncoderTest, FlushRejectsNullOutput) {
  CircEncoder encoder;

  EXPECT_FALSE(encoder.Flush(nullptr));
}

TEST(EfmCircEncoderTest, OutputIsAPureFunctionOfTheInput) {
  const std::vector<F1Frame> input = MakePseudoRandomFrames(250);

  EXPECT_EQ(Encode(input, /*flush=*/true), Encode(input, /*flush=*/true));
}

TEST(EfmCircEncoderTest, ResetRestoresTheConstructedState) {
  const std::vector<F1Frame> input = MakePseudoRandomFrames(120);

  CircEncoder encoder;
  std::vector<F2Frame> first;
  for (const F1Frame& frame : input) {
    first.push_back(encoder.EncodeFrame(frame));
  }

  encoder.Reset();
  std::vector<F2Frame> second;
  for (const F1Frame& frame : input) {
    second.push_back(encoder.EncodeFrame(frame));
  }

  EXPECT_EQ(first, second);
}

}  // namespace
}  // namespace videosynth::efm
