/*
 * File:        test_efm_modulator.cpp
 * Module:      efm
 * Purpose:     Unit tests for the eight-to-fourteen modulator, merging-bit
 *              selection and T-value conversion of the EFM module.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <set>
#include <vector>

#include "videosynth/efm/efm_modulator.h"
#include "videosynth/efm/subcode_generator.h"

namespace videosynth::efm {
namespace {

// Positions of the one bits of a 14-channel-bit symbol, most significant first.
std::vector<std::size_t> OnePositions(std::uint16_t symbol) {
  std::vector<std::size_t> positions;
  for (std::size_t index = 0; index < kChannelBitsPerSymbol; ++index) {
    if (((symbol >> (kChannelBitsPerSymbol - 1 - index)) & 1U) != 0U) {
      positions.push_back(index);
    }
  }
  return positions;
}

// The run lengths of a channel bit stream, i.e. the distance from each one bit
// to the next. The trailing partial run is not returned.
std::vector<std::size_t> RunLengths(const std::vector<bool>& bits) {
  std::vector<std::size_t> runs;
  std::size_t last_one = 0;
  bool started = false;
  for (std::size_t index = 0; index < bits.size(); ++index) {
    if (!bits[index]) {
      continue;
    }
    if (started) {
      runs.push_back(index - last_one);
    }
    started = true;
    last_one = index;
  }
  return runs;
}

// Offsets at which the 24-bit sync header pattern occurs in `bits`.
std::vector<std::size_t> SyncOffsets(const std::vector<bool>& bits) {
  std::vector<std::size_t> offsets;
  if (bits.size() < kSyncHeaderBits) {
    return offsets;
  }
  for (std::size_t start = 0; start + kSyncHeaderBits <= bits.size(); ++start) {
    bool match = true;
    for (std::size_t index = 0; index < kSyncHeaderBits && match; ++index) {
      const bool expected =
          ((kSyncHeaderPattern >> (kSyncHeaderBits - 1 - index)) & 1U) != 0U;
      match = bits[start + index] == expected;
    }
    if (match) {
      offsets.push_back(start);
    }
  }
  return offsets;
}

F2Frame MakePseudoRandomF2Frame(std::mt19937* generator) {
  std::uniform_int_distribution<unsigned> distribution(0, 255);
  F2Frame frame{};
  for (std::uint8_t& symbol : frame) {
    symbol = static_cast<std::uint8_t>(distribution(*generator));
  }
  return frame;
}

// Modulates `frame_count` pseudo-random frames, placing the subcode sync
// patterns in the control position as a real subcode section does.
std::vector<bool> ModulatePseudoRandomStream(std::size_t frame_count,
                                             EfmModulator* modulator) {
  std::mt19937 generator(20260719U);
  std::vector<bool> bits;
  bits.reserve(frame_count * kChannelBitsPerFrame);
  for (std::size_t index = 0; index < frame_count; ++index) {
    const std::size_t position = index % kFramesPerSubcodeSection;
    std::uint16_t control = kSubcodeSyncS0;
    if (position == 1) {
      control = kSubcodeSyncS1;
    } else if (position > 1) {
      control = EfmSymbol(static_cast<std::uint8_t>(index & 0xFFU));
    }
    EXPECT_TRUE(modulator->ModulateFrame(
        control, MakePseudoRandomF2Frame(&generator), &bits));
  }
  return bits;
}

// ---------------------------------------------------------------------------
// Eight-to-fourteen table (ECMA-130, annex D).
// ---------------------------------------------------------------------------

TEST(EfmModulatorTest, TableEntriesAreFourteenBitsWithLegalRunLengths) {
  for (unsigned value = 0; value < 256U; ++value) {
    const std::uint16_t symbol = EfmSymbol(static_cast<std::uint8_t>(value));
    ASSERT_LT(symbol, 1U << kChannelBitsPerSymbol) << "value " << value;

    const std::vector<std::size_t> ones = OnePositions(symbol);
    ASSERT_FALSE(ones.empty()) << "value " << value;
    for (std::size_t index = 0; index + 1 < ones.size(); ++index) {
      const std::size_t zeros = ones[index + 1] - ones[index] - 1;
      EXPECT_GE(zeros, 2U) << "value " << value;
      EXPECT_LE(zeros, 10U) << "value " << value;
    }
    // The leading and trailing zeros must leave room for the three merging bits
    // to keep the same bounds across a symbol boundary (ECMA-130, annex E).
    EXPECT_LE(ones.front(), 8U) << "value " << value;
    EXPECT_LE(kChannelBitsPerSymbol - 1 - ones.back(), 8U) << "value " << value;
  }
}

TEST(EfmModulatorTest, TableEntriesAreDistinctAndExcludeTheSyncPatterns) {
  std::set<std::uint16_t> symbols;
  for (unsigned value = 0; value < 256U; ++value) {
    const std::uint16_t symbol = EfmSymbol(static_cast<std::uint8_t>(value));
    EXPECT_TRUE(symbols.insert(symbol).second) << "duplicate for " << value;
    // IEC 60908-1999, 17.3: S0 and S1 lie outside the table.
    EXPECT_NE(symbol, kSubcodeSyncS0) << "value " << value;
    EXPECT_NE(symbol, kSubcodeSyncS1) << "value " << value;
  }
}

TEST(EfmModulatorTest, TableMatchesAnnexDEntries) {
  // Transcribed from ECMA-130, table D.1.
  EXPECT_EQ(EfmSymbol(0x00), 0b01001000100000U);
  EXPECT_EQ(EfmSymbol(0x01), 0b10000100000000U);
  EXPECT_EQ(EfmSymbol(0x05), 0b00000100010000U);
  EXPECT_EQ(EfmSymbol(0x40), 0b01001000100100U);
  EXPECT_EQ(EfmSymbol(0x45), 0b00000000100100U);
  EXPECT_EQ(EfmSymbol(0x3F), 0b00100000001000U);
  EXPECT_EQ(EfmSymbol(0x7F), 0b00100000000010U);
  EXPECT_EQ(EfmSymbol(0x80), 0b01001000100001U);
  EXPECT_EQ(EfmSymbol(0xC0), 0b01000100100000U);
  EXPECT_EQ(EfmSymbol(0xBF), 0b00100000001001U);
  EXPECT_EQ(EfmSymbol(0xFF), 0b00100000010010U);
}

// ---------------------------------------------------------------------------
// Channel frame layout (ECMA-130, 19.4).
// ---------------------------------------------------------------------------

TEST(EfmModulatorTest, ChannelFrameLayoutIsFiveHundredEightyEightBits) {
  EXPECT_EQ(kChannelBitsPerFrame, 588U);

  EfmModulator modulator;
  std::vector<bool> bits;
  ASSERT_TRUE(modulator.ModulateFrame(kSubcodeSyncS0, F2Frame{}, &bits));
  EXPECT_EQ(bits.size(), kChannelBitsPerFrame);

  ASSERT_TRUE(modulator.ModulateFrame(kSubcodeSyncS1, F2Frame{}, &bits));
  EXPECT_EQ(bits.size(), 2U * kChannelBitsPerFrame);
}

TEST(EfmModulatorTest, EveryFrameOpensWithTheSyncHeader) {
  EfmModulator modulator;
  const std::vector<bool> bits = ModulatePseudoRandomStream(200, &modulator);

  for (std::size_t frame = 0; frame * kChannelBitsPerFrame < bits.size();
       ++frame) {
    const std::size_t start = frame * kChannelBitsPerFrame;
    for (std::size_t index = 0; index < kSyncHeaderBits; ++index) {
      const bool expected =
          ((kSyncHeaderPattern >> (kSyncHeaderBits - 1 - index)) & 1U) != 0U;
      ASSERT_EQ(bits[start + index], expected)
          << "frame " << frame << " sync bit " << index;
    }
  }
}

TEST(EfmModulatorTest, SymbolsSitAtTheirLayoutOffsets) {
  EfmModulator modulator;
  F2Frame f2_frame{};
  for (std::size_t index = 0; index < f2_frame.size(); ++index) {
    f2_frame[index] = static_cast<std::uint8_t>(index * 7U);
  }

  std::vector<bool> bits;
  ASSERT_TRUE(modulator.ModulateFrame(kSubcodeSyncS0, f2_frame, &bits));

  for (std::size_t symbol = 0; symbol < kSymbolsPerChannelFrame; ++symbol) {
    const std::size_t offset =
        kSyncHeaderBits + kMergingBitsPerBoundary +
        (symbol * (kChannelBitsPerSymbol + kMergingBitsPerBoundary));
    std::uint16_t decoded = 0;
    for (std::size_t index = 0; index < kChannelBitsPerSymbol; ++index) {
      decoded = static_cast<std::uint16_t>(decoded << 1U);
      if (bits[offset + index]) {
        decoded |= 1U;
      }
    }
    const std::uint16_t expected =
        symbol == 0 ? kSubcodeSyncS0 : EfmSymbol(f2_frame[symbol - 1]);
    EXPECT_EQ(decoded, expected) << "symbol " << symbol;
  }
}

// ---------------------------------------------------------------------------
// Merging-bit selection (ECMA-130, annex E).
// ---------------------------------------------------------------------------

TEST(EfmModulatorTest, RunLengthsStayWithinTMinAndTMax) {
  EfmModulator modulator;
  const std::vector<bool> bits = ModulatePseudoRandomStream(600, &modulator);

  const std::vector<std::size_t> runs = RunLengths(bits);
  ASSERT_FALSE(runs.empty());
  for (std::size_t index = 0; index < runs.size(); ++index) {
    ASSERT_GE(runs[index], kMinRunLengthT) << "run " << index;
    ASSERT_LE(runs[index], kMaxRunLengthT) << "run " << index;
  }
}

TEST(EfmModulatorTest, SyncPatternOccursOnlyAtChannelFrameStarts) {
  EfmModulator modulator;
  const std::vector<bool> bits = ModulatePseudoRandomStream(600, &modulator);

  const std::vector<std::size_t> offsets = SyncOffsets(bits);
  ASSERT_FALSE(offsets.empty());
  for (const std::size_t offset : offsets) {
    EXPECT_EQ(offset % kChannelBitsPerFrame, 0U)
        << "false sync at channel bit " << offset;
  }
}

TEST(EfmModulatorTest, DigitalSumValueStaysBounded) {
  EfmModulator modulator;
  std::mt19937 generator(20260720U);
  // The merging bits correct the DSV by at most a few units per frame, so a
  // bound well below one frame's 588 channel bits shows selection is working.
  constexpr int kDsvBound = 100;

  for (std::size_t index = 0; index < 600U; ++index) {
    std::vector<bool> bits;
    ASSERT_TRUE(modulator.ModulateFrame(
        EfmSymbol(static_cast<std::uint8_t>(index & 0xFFU)),
        MakePseudoRandomF2Frame(&generator), &bits));
    ASSERT_LT(std::abs(modulator.DigitalSumValue()), kDsvBound)
        << "frame " << index;
  }
}

TEST(EfmModulatorTest, ModulateFrameRejectsNullOutput) {
  EfmModulator modulator;

  EXPECT_FALSE(modulator.ModulateFrame(kSubcodeSyncS0, F2Frame{}, nullptr));
}

TEST(EfmModulatorTest, ResetRestoresTheConstructedState) {
  EfmModulator first;
  const std::vector<bool> reference = ModulatePseudoRandomStream(40, &first);

  EfmModulator second;
  const std::vector<bool> discarded = ModulatePseudoRandomStream(17, &second);
  ASSERT_FALSE(discarded.empty());
  second.Reset();
  EXPECT_EQ(second.DigitalSumValue(), 0);
  EXPECT_EQ(ModulatePseudoRandomStream(40, &second), reference);
}

// ---------------------------------------------------------------------------
// T-value conversion.
// ---------------------------------------------------------------------------

TEST(EfmModulatorTest, TValuesAreTheRunLengthsOfTheStream) {
  EfmModulator modulator;
  const std::vector<bool> bits = ModulatePseudoRandomStream(50, &modulator);

  TValueEncoder encoder;
  std::vector<std::uint8_t> t_values;
  ASSERT_TRUE(encoder.PushBits(bits, &t_values));

  const std::vector<std::size_t> runs = RunLengths(bits);
  ASSERT_EQ(t_values.size(), runs.size());
  for (std::size_t index = 0; index < runs.size(); ++index) {
    EXPECT_EQ(t_values[index], runs[index]) << "run " << index;
  }
  // The sync header holds two maximum-length runs (ECMA-130, 19.2).
  ASSERT_GE(t_values.size(), 2U);
  EXPECT_EQ(t_values[0], kMaxRunLengthT);
  EXPECT_EQ(t_values[1], kMaxRunLengthT);
}

TEST(EfmModulatorTest, TValuesReconstructTheChannelBits) {
  EfmModulator modulator;
  const std::vector<bool> bits = ModulatePseudoRandomStream(30, &modulator);

  TValueEncoder encoder;
  std::vector<std::uint8_t> t_values;
  ASSERT_TRUE(encoder.PushBits(bits, &t_values));
  ASSERT_TRUE(encoder.Flush(&t_values));

  // Every T value opens a run with a transition, so the stream is the
  // concatenation of one bits each followed by T - 1 zero bits.
  std::vector<bool> reconstructed;
  for (const std::uint8_t t_value : t_values) {
    EXPECT_GE(t_value, kMinRunLengthT);
    EXPECT_LE(t_value, kMaxRunLengthT);
    reconstructed.push_back(true);
    for (std::uint8_t index = 1; index < t_value; ++index) {
      reconstructed.push_back(false);
    }
  }

  ASSERT_GE(reconstructed.size(), bits.size());
  reconstructed.resize(bits.size());
  EXPECT_EQ(reconstructed, bits);
}

TEST(EfmModulatorTest, TValueEncoderOpensOnTheFirstTransition) {
  TValueEncoder encoder;
  std::vector<std::uint8_t> t_values;

  EXPECT_FALSE(encoder.PushBits({true, false, false}, nullptr));
  EXPECT_FALSE(encoder.Flush(nullptr));

  // Nothing is emitted before the first transition, and the run still in
  // progress at the end of the stream is extended to T_min.
  ASSERT_TRUE(encoder.PushBits({false, false, true, false}, &t_values));
  EXPECT_TRUE(t_values.empty());
  ASSERT_TRUE(encoder.Flush(&t_values));
  ASSERT_EQ(t_values.size(), 1U);
  EXPECT_EQ(t_values.front(), kMinRunLengthT);

  // A flush with no pending run emits nothing.
  t_values.clear();
  ASSERT_TRUE(encoder.Flush(&t_values));
  EXPECT_TRUE(t_values.empty());
}

}  // namespace
}  // namespace videosynth::efm
