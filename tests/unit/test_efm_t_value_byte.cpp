/*
 * File:        test_efm_t_value_byte.cpp
 * Module:      efm_tests
 * Purpose:     Unit tests for the EFM extension stream byte layout: the
 *              t-value / doubt packing of efm/t_value_byte.h.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "videosynth/efm/efm_modulator.h"
#include "videosynth/efm/t_value_byte.h"

namespace videosynth::efm {
namespace {

// efm-extension-format.md, "Binary Data File": a fully trusted t-value packs to
// its own plain value, so a stream with nothing to doubt is simply the raw run
// lengths.
TEST(EfmTValueByteTest, ZeroDoubtPacksToThePlainTValue) {
  for (std::uint8_t t_value = kMinRunLengthT; t_value <= kMaxRunLengthT;
       ++t_value) {
    EXPECT_EQ(PackTValueByte(t_value, kNoDoubt), t_value);
  }
}

// The doubt videosynth writes is maximum confidence, i.e. no doubt at all.
TEST(EfmTValueByteTest, SynthesisedStreamCarriesNoDoubt) {
  EXPECT_EQ(kSynthesisedDoubt, kNoDoubt);
  EXPECT_EQ(kSynthesisedDoubt, 0U);
}

// Doubt occupies bits 7-4 and the t-value bits 3-0.
TEST(EfmTValueByteTest, DoubtOccupiesTheUpperNibble) {
  EXPECT_EQ(PackTValueByte(0x03U, 0x00U), 0x03U);
  EXPECT_EQ(PackTValueByte(0x03U, 0x01U), 0x13U);
  EXPECT_EQ(PackTValueByte(0x0BU, kMaxDoubt), 0xFBU);
  EXPECT_EQ(PackTValueByte(0x00U, kMaxDoubt), 0xF0U);
}

// Every 4-bit t-value and doubt combination survives a pack/unpack round trip.
TEST(EfmTValueByteTest, RoundTripsEveryFieldCombination) {
  for (unsigned t_value = 0; t_value <= kTValueMask; ++t_value) {
    for (unsigned doubt = 0; doubt <= kMaxDoubt; ++doubt) {
      const std::uint8_t byte = PackTValueByte(
          static_cast<std::uint8_t>(t_value), static_cast<std::uint8_t>(doubt));
      EXPECT_EQ(TValueOfByte(byte), t_value);
      EXPECT_EQ(DoubtOfByte(byte), doubt);
    }
  }
}

// Every byte value is a legal stream byte, so unpacking never rejects one.
TEST(EfmTValueByteTest, UnpacksEveryByteValue) {
  for (unsigned value = 0; value < 256U; ++value) {
    const auto byte = static_cast<std::uint8_t>(value);
    EXPECT_LE(TValueOfByte(byte), kTValueMask);
    EXPECT_LE(DoubtOfByte(byte), kMaxDoubt);
    EXPECT_EQ(PackTValueByte(TValueOfByte(byte), DoubtOfByte(byte)), byte);
  }
}

// An out-of-range field is truncated rather than allowed to corrupt the other.
TEST(EfmTValueByteTest, TruncatesOutOfRangeFields) {
  EXPECT_EQ(PackTValueByte(0x1BU, kNoDoubt), 0x0BU);
  EXPECT_EQ(PackTValueByte(0x0BU, 0x10U), 0x0BU);
}

}  // namespace
}  // namespace videosynth::efm
