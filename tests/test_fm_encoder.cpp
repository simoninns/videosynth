/*
 * File:        test_fm_encoder.cpp
 * Module:      fm_encoder_tests
 * Purpose:     Unit tests for 40-bit FM signal encoding per IEC 60857 §10.2,
 *              Figure 13 (NTSC LaserDisc VBI).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

#include "videosynth/biphase_encoder.h"
#include "videosynth/fixed_point.h"
#include "videosynth/fm_encoder.h"
#include "videosynth/timing_constants.h"

namespace videosynth {
namespace {

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------

constexpr double kNtscSampleRate = 14318180.0;
constexpr double kPalSampleRate  = 17734475.0;
constexpr double kBaseline       = 0.0;    // 0 IRE in mV
constexpr double kPeak           = 714.3;  // 100 IRE in mV

// Returns a zero-data FmData with field_one set as specified.
FmData ZeroData(bool field_one = false) {
  return FmData{field_one, 0u, 0u, 0u, 0u, 0u};
}

// Returns the value at the first quarter of bit cell `bit_idx` in `samples`.
double FirstQuarterLevel(const std::vector<SampleFixed>& samples,
                          int bit_idx, int bit_cell_samples) {
  const int start = bit_idx * bit_cell_samples;
  const int quarter = bit_cell_samples / 4;
  // Average over the stable first-quarter region (a few samples in).
  double sum = 0.0;
  int count = 0;
  for (int i = start + 1; i < start + quarter - 1 && i < static_cast<int>(samples.size()); ++i) {
    sum += SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]);
    ++count;
  }
  return count > 0 ? sum / count : SampleFixedToMillivolts(samples[static_cast<std::size_t>(start)]);
}

// Returns the value at the last quarter of bit cell `bit_idx` in `samples`.
double LastQuarterLevel(const std::vector<SampleFixed>& samples,
                         int bit_idx, int bit_cell_samples) {
  const int start = bit_idx * bit_cell_samples;
  const int three_quarters = (3 * bit_cell_samples) / 4;
  const int end = start + bit_cell_samples;
  double sum = 0.0;
  int count = 0;
  for (int i = start + three_quarters + 1; i < end - 1 && i < static_cast<int>(samples.size()); ++i) {
    sum += SampleFixedToMillivolts(samples[static_cast<std::size_t>(i)]);
    ++count;
  }
  return count > 0 ? sum / count : SampleFixedToMillivolts(samples[static_cast<std::size_t>(end - 1)]);
}

// ---------------------------------------------------------------------------
// FmEncoderTest — construction
// ---------------------------------------------------------------------------

TEST(FmEncoderTest, BitCellSamplesCorrectForNtsc4Fsc) {
  // NTSC 4fsc: 14318180 Hz, 2 µs bit cell → round(2e-6 × 14318180) = 29
  const FmEncoder enc(kNtscSampleRate);
  EXPECT_EQ(enc.bit_cell_samples(), 29);
}

TEST(FmEncoderTest, BitCellSamplesCorrectForPal4Fsc) {
  // PAL 4fsc: 17734475 Hz, 2 µs bit cell → round(2e-6 × 17734475) = 35
  const FmEncoder enc(kPalSampleRate);
  EXPECT_EQ(enc.bit_cell_samples(), 35);
}

TEST(FmEncoderTest, RampSamplesPositiveForNtsc4Fsc) {
  const FmEncoder enc(kNtscSampleRate);
  EXPECT_GT(enc.ramp_samples(), 0);
  EXPECT_LT(enc.ramp_samples(), enc.bit_cell_samples() / 2);
}

TEST(FmEncoderTest, RampSamplesPositiveForPal4Fsc) {
  const FmEncoder enc(kPalSampleRate);
  EXPECT_GT(enc.ramp_samples(), 0);
  EXPECT_LT(enc.ramp_samples(), enc.bit_cell_samples() / 2);
}

// IEC 60857 §10.2 specifies 135 ns transitions for the FM signal.
// 24-bit biphase uses 225 ns.  At the same NTSC sample rate, the FM ramp must
// be strictly narrower.
TEST(FmEncoderTest, FmRampSamplesNarrowerThanBiphaseRampAtNtsc) {
  const FmEncoder     fm_enc(kNtscSampleRate);
  const BiphaseEncoder bi_enc(kNtscSampleRate);  // default 225 ns
  EXPECT_LT(fm_enc.ramp_samples(), bi_enc.ramp_samples());
}

// ---------------------------------------------------------------------------
// FmEncoderTest — BuildBitPattern
// ---------------------------------------------------------------------------

TEST(FmEncoderTest, ClockSyncBitsAreCorrect) {
  const auto bits = FmEncoder::BuildBitPattern(ZeroData());
  // Bits [0-3] = 0011
  EXPECT_FALSE(bits[0]);
  EXPECT_FALSE(bits[1]);
  EXPECT_TRUE(bits[2]);
  EXPECT_TRUE(bits[3]);
}

TEST(FmEncoderTest, FieldIndicatorBitTrueForFieldOne) {
  const auto bits = FmEncoder::BuildBitPattern(FmData{true, 0, 0, 0, 0, 0});
  EXPECT_TRUE(bits[4]);
}

TEST(FmEncoderTest, FieldIndicatorBitFalseForFieldTwo) {
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0, 0, 0, 0});
  EXPECT_FALSE(bits[4]);
}

TEST(FmEncoderTest, LeadingRecognitionBitsAreCorrect) {
  const auto bits = FmEncoder::BuildBitPattern(ZeroData());
  // Bits [5-11] = 1110010
  EXPECT_TRUE(bits[5]);
  EXPECT_TRUE(bits[6]);
  EXPECT_TRUE(bits[7]);
  EXPECT_FALSE(bits[8]);
  EXPECT_FALSE(bits[9]);
  EXPECT_TRUE(bits[10]);
  EXPECT_FALSE(bits[11]);
}

TEST(FmEncoderTest, TrailingRecognitionBitsAreCorrect) {
  const auto bits = FmEncoder::BuildBitPattern(ZeroData());
  // Bits [33-39] = 0001101
  EXPECT_FALSE(bits[33]);
  EXPECT_FALSE(bits[34]);
  EXPECT_FALSE(bits[35]);
  EXPECT_TRUE(bits[36]);
  EXPECT_TRUE(bits[37]);
  EXPECT_FALSE(bits[38]);
  EXPECT_TRUE(bits[39]);
}

TEST(FmEncoderTest, DataNibbleX5EncodedLsbFirst) {
  // X5 = 0xA = 1010 → LSB first: bit[12]=0, bit[13]=1, bit[14]=0, bit[15]=1
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0, 0, 0, 0x0A});
  EXPECT_FALSE(bits[12]);
  EXPECT_TRUE(bits[13]);
  EXPECT_FALSE(bits[14]);
  EXPECT_TRUE(bits[15]);
}

TEST(FmEncoderTest, DataNibbleX4EncodedLsbFirst) {
  // X4 = 0x5 = 0101 → LSB first: bit[16]=1, bit[17]=0, bit[18]=1, bit[19]=0
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0, 0, 0x05, 0});
  EXPECT_TRUE(bits[16]);
  EXPECT_FALSE(bits[17]);
  EXPECT_TRUE(bits[18]);
  EXPECT_FALSE(bits[19]);
}

TEST(FmEncoderTest, DataNibbleX3EncodedLsbFirst) {
  // X3 = 0x3 = 0011 → LSB first: bit[20]=1, bit[21]=1, bit[22]=0, bit[23]=0
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0, 0x03, 0, 0});
  EXPECT_TRUE(bits[20]);
  EXPECT_TRUE(bits[21]);
  EXPECT_FALSE(bits[22]);
  EXPECT_FALSE(bits[23]);
}

TEST(FmEncoderTest, DataNibbleX2EncodedLsbFirst) {
  // X2 = 0x9 = 1001 → LSB first: bit[24]=1, bit[25]=0, bit[26]=0, bit[27]=1
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0x09, 0, 0, 0});
  EXPECT_TRUE(bits[24]);
  EXPECT_FALSE(bits[25]);
  EXPECT_FALSE(bits[26]);
  EXPECT_TRUE(bits[27]);
}

TEST(FmEncoderTest, DataNibbleX1EncodedLsbFirst) {
  // X1 = 0x6 = 0110 → LSB first: bit[28]=0, bit[29]=1, bit[30]=1, bit[31]=0
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0x06, 0, 0, 0, 0});
  EXPECT_FALSE(bits[28]);
  EXPECT_TRUE(bits[29]);
  EXPECT_TRUE(bits[30]);
  EXPECT_FALSE(bits[31]);
}

TEST(FmEncoderTest, AllDataNibblesZeroGivesCorrectBitCount) {
  const auto bits = FmEncoder::BuildBitPattern(ZeroData(false));
  // Count 1s in bits [0-3]=0011, [4]=0, [5-11]=1110010, [12-31]=all 0.
  // Fixed 1s: bits 2,3,5,6,7,10 = 6 ones.  Bits [0-31] total = 6 (even).
  // Parity bit [32] must be 1 to make total odd.
  EXPECT_TRUE(bits[32]);
}

TEST(FmEncoderTest, ParityBitZeroWhenBitsAlreadyOdd) {
  // field_one=true adds bit[4]=1, giving 7 ones in [0-31] (odd).
  // Parity bit [32] must be 0.
  const auto bits = FmEncoder::BuildBitPattern(FmData{true, 0, 0, 0, 0, 0});
  EXPECT_FALSE(bits[32]);
}

TEST(FmEncoderTest, ParityIsOddOverBits0To32) {
  // Verify odd parity property holds for a variety of payloads.
  const std::vector<FmData> payloads = {
      {false, 0x0, 0x0, 0x0, 0x0, 0x0},
      {true,  0x5, 0x3, 0xA, 0x1, 0x7},
      {false, 0xF, 0xF, 0xF, 0xF, 0xF},
      {true,  0x1, 0x2, 0x3, 0x4, 0x5},
  };
  for (const auto& payload : payloads) {
    const auto bits = FmEncoder::BuildBitPattern(payload);
    int ones = 0;
    for (int i = 0; i <= 32; ++i) {
      if (bits[static_cast<std::size_t>(i)]) ++ones;
    }
    EXPECT_EQ(ones % 2, 1) << "Parity failed for payload";
  }
}

// ---------------------------------------------------------------------------
// FmEncoderTest — Generate40BitCode waveform shape
// ---------------------------------------------------------------------------

TEST(FmEncoderTest, Generate40BitWaveformHasCorrectSampleCount) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples =
      enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_EQ(static_cast<int>(samples.size()), 40 * enc.bit_cell_samples());
}

// Bit [0] = 0 (clock sync '0'): first quarter of bit cell 0 must be at peak.
TEST(FmEncoderTest, BitZeroIsClockSyncZeroStartsAtPeak) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_NEAR(FirstQuarterLevel(samples, 0, enc.bit_cell_samples()), kPeak, 1.0);
}

// Bit [2] = 1 (clock sync '1'): first quarter of bit cell 2 must be at baseline.
TEST(FmEncoderTest, BitTwoIsClockSyncOneStartsAtBaseline) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_NEAR(FirstQuarterLevel(samples, 2, enc.bit_cell_samples()), kBaseline, 1.0);
}

// Bit [5] = 1 (leading recognition '1'): last quarter of bit cell 5 at peak.
TEST(FmEncoderTest, BitFiveIsLeadingRecognitionOneEndsAtPeak) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_NEAR(LastQuarterLevel(samples, 5, enc.bit_cell_samples()), kPeak, 1.0);
}

// Bit [8] = 0 (leading recognition '0'): last quarter of bit cell 8 at baseline.
TEST(FmEncoderTest, BitEightIsLeadingRecognitionZeroEndsAtBaseline) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_NEAR(LastQuarterLevel(samples, 8, enc.bit_cell_samples()), kBaseline, 1.0);
}

// Field indicator bit [4] = 1 when field_one=true.
TEST(FmEncoderTest, FieldOneTrueMakesBit4AOneCell) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples =
      enc.Generate40BitWaveform(FmData{true, 0, 0, 0, 0, 0}, kBaseline, kPeak);
  // A '1' bit: first quarter at baseline.
  EXPECT_NEAR(FirstQuarterLevel(samples, 4, enc.bit_cell_samples()), kBaseline, 1.0);
}

// Field indicator bit [4] = 0 when field_one=false.
TEST(FmEncoderTest, FieldOneFalseMakesBit4AZeroCell) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples =
      enc.Generate40BitWaveform(FmData{false, 0, 0, 0, 0, 0}, kBaseline, kPeak);
  // A '0' bit: first quarter at peak.
  EXPECT_NEAR(FirstQuarterLevel(samples, 4, enc.bit_cell_samples()), kPeak, 1.0);
}

// Trailing recognition bit [36] = 1.
TEST(FmEncoderTest, TrailingBit36IsOneCell) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_NEAR(FirstQuarterLevel(samples, 36, enc.bit_cell_samples()), kBaseline, 1.0);
}

// Trailing recognition bit [35] = 0.
TEST(FmEncoderTest, TrailingBit35IsZeroCell) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_NEAR(FirstQuarterLevel(samples, 35, enc.bit_cell_samples()), kPeak, 1.0);
}

// Trailing recognition bit [39] = 1 (last bit of pattern).
TEST(FmEncoderTest, TrailingBit39IsOneCell) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  EXPECT_NEAR(LastQuarterLevel(samples, 39, enc.bit_cell_samples()), kPeak, 1.0);
}

// X5 = 0x5 = 0101 → bit[12]=1 (LSB), bit[13]=0, bit[14]=1, bit[15]=0
TEST(FmEncoderTest, X5NibbleEncodedCorrectlyInWaveform) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples =
      enc.Generate40BitWaveform(FmData{false, 0, 0, 0, 0, 0x05}, kBaseline, kPeak);
  const int bcs = enc.bit_cell_samples();
  // bit[12] = 1 → starts at baseline
  EXPECT_NEAR(FirstQuarterLevel(samples, 12, bcs), kBaseline, 1.0);
  // bit[13] = 0 → starts at peak
  EXPECT_NEAR(FirstQuarterLevel(samples, 13, bcs), kPeak, 1.0);
  // bit[14] = 1 → starts at baseline
  EXPECT_NEAR(FirstQuarterLevel(samples, 14, bcs), kBaseline, 1.0);
  // bit[15] = 0 → starts at peak
  EXPECT_NEAR(FirstQuarterLevel(samples, 15, bcs), kPeak, 1.0);
}

// X1 = 0x9 = 1001 → bit[28]=1 (LSB), bit[29]=0, bit[30]=0, bit[31]=1
TEST(FmEncoderTest, X1NibbleEncodedCorrectlyInWaveform) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples =
      enc.Generate40BitWaveform(FmData{false, 0x09, 0, 0, 0, 0}, kBaseline, kPeak);
  const int bcs = enc.bit_cell_samples();
  // bit[28] = 1 → starts at baseline
  EXPECT_NEAR(FirstQuarterLevel(samples, 28, bcs), kBaseline, 1.0);
  // bit[29] = 0 → starts at peak
  EXPECT_NEAR(FirstQuarterLevel(samples, 29, bcs), kPeak, 1.0);
  // bit[30] = 0 → starts at peak
  EXPECT_NEAR(FirstQuarterLevel(samples, 30, bcs), kPeak, 1.0);
  // bit[31] = 1 → starts at baseline
  EXPECT_NEAR(FirstQuarterLevel(samples, 31, bcs), kBaseline, 1.0);
}

// Inter-bit transition: bits [0,1] are both '0' → rising transition at boundary.
// Before the boundary (end of cell 0): near baseline.
// After the boundary (start of cell 1): near peak.
TEST(FmEncoderTest, ConsecutiveZeroBitsHaveRisingInterBitTransition) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  const int bcs = enc.bit_cell_samples();
  const int ramp = enc.ramp_samples();
  const int boundary = 1 * bcs;
  // Bits [0,1] are both '0'. Before boundary: baseline. After: peak.
  const int pre_idx = boundary - ramp - 1;
  const int post_idx = boundary + ramp + 1;
  EXPECT_NEAR(SampleFixedToMillivolts(samples[static_cast<std::size_t>(pre_idx)]),
              kBaseline, 1.0);
  EXPECT_NEAR(SampleFixedToMillivolts(samples[static_cast<std::size_t>(post_idx)]),
              kPeak, 1.0);
}

// Bits [2,3] are both '1' → falling inter-bit transition at boundary.
TEST(FmEncoderTest, ConsecutiveOneBitsHaveFallingInterBitTransition) {
  const FmEncoder enc(kNtscSampleRate);
  const auto samples = enc.Generate40BitWaveform(ZeroData(), kBaseline, kPeak);
  const int bcs = enc.bit_cell_samples();
  const int ramp = enc.ramp_samples();
  // Boundary between bit[2]='1' and bit[3]='1'
  const int boundary = 3 * bcs;
  const int pre_idx = boundary - ramp - 1;
  const int post_idx = boundary + ramp + 1;
  EXPECT_NEAR(SampleFixedToMillivolts(samples[static_cast<std::size_t>(pre_idx)]),
              kPeak, 1.0);
  EXPECT_NEAR(SampleFixedToMillivolts(samples[static_cast<std::size_t>(post_idx)]),
              kBaseline, 1.0);
}

// ---------------------------------------------------------------------------
// FmEncoderTest — Generate40BitCode full-line output
// ---------------------------------------------------------------------------

TEST(FmEncoderTest, Generate40BitCodeNtscLineHasCorrectLength) {
  const FmEncoder enc(kNtscSampleRate);
  const auto line =
      enc.Generate40BitCode(ZeroData(), Standard::kNtsc, kBaseline, kPeak);
  EXPECT_EQ(static_cast<int>(line.size()),
            GetTimingConstants(Standard::kNtsc).samples_per_line_4fsc);
}

TEST(FmEncoderTest, Generate40BitCodePalLineHasCorrectLength) {
  const FmEncoder enc(kPalSampleRate);
  const auto line =
      enc.Generate40BitCode(ZeroData(), Standard::kPal, kBaseline, kPeak);
  EXPECT_EQ(static_cast<int>(line.size()),
            GetTimingConstants(Standard::kPal).samples_per_line_4fsc);
}

TEST(FmEncoderTest, Generate40BitCodeLineHasBaselineAfterCode) {
  const FmEncoder enc(kNtscSampleRate);
  const auto line =
      enc.Generate40BitCode(ZeroData(), Standard::kNtsc, kBaseline, kPeak);
  const int code_end = 40 * enc.bit_cell_samples();
  const int line_size = static_cast<int>(line.size());
  for (int i = code_end; i < line_size; ++i) {
    EXPECT_NEAR(SampleFixedToMillivolts(line[static_cast<std::size_t>(i)]),
                kBaseline, 0.01)
        << "Sample " << i << " after FM code should be at baseline";
  }
}

// ---------------------------------------------------------------------------
// FmEncoderTest — GenerateWhiteFlag
// ---------------------------------------------------------------------------

TEST(FmEncoderTest, GenerateWhiteFlagNtscLineHasCorrectLength) {
  const FmEncoder enc(kNtscSampleRate);
  const auto line = enc.GenerateWhiteFlag(Standard::kNtsc, kPeak);
  EXPECT_EQ(static_cast<int>(line.size()),
            GetTimingConstants(Standard::kNtsc).samples_per_line_4fsc);
}

TEST(FmEncoderTest, GenerateWhiteFlagPalLineHasCorrectLength) {
  const FmEncoder enc(kPalSampleRate);
  const auto line = enc.GenerateWhiteFlag(Standard::kPal, kPeak);
  EXPECT_EQ(static_cast<int>(line.size()),
            GetTimingConstants(Standard::kPal).samples_per_line_4fsc);
}

TEST(FmEncoderTest, GenerateWhiteFlagEntireLineAtPeakLevel) {
  const FmEncoder enc(kNtscSampleRate);
  const auto line = enc.GenerateWhiteFlag(Standard::kNtsc, kPeak);
  for (std::size_t i = 0; i < line.size(); ++i) {
    EXPECT_NEAR(SampleFixedToMillivolts(line[i]), kPeak, 0.01)
        << "Sample " << i << " of white flag should be at 100 IRE";
  }
}

TEST(FmEncoderTest, GenerateWhiteFlagWorksWithDifferentPeakLevel) {
  const FmEncoder enc(kNtscSampleRate);
  constexpr double kCustomPeak = 500.0;
  const auto line = enc.GenerateWhiteFlag(Standard::kNtsc, kCustomPeak);
  for (std::size_t i = 0; i < line.size(); ++i) {
    EXPECT_NEAR(SampleFixedToMillivolts(line[i]), kCustomPeak, 0.01)
        << "Sample " << i << " should be at custom peak level";
  }
}

// ---------------------------------------------------------------------------
// FmEncoderTest — picture number encoding (fm_picture_number, IEC 60857 §10.2.3)
// ---------------------------------------------------------------------------

// fm_picture_number = 12345: X1=1, X2=2, X3=3, X4=4, X5=5
// X5 = 0x5 = 0101 → bit[12]=1, bit[13]=0, bit[14]=1, bit[15]=0
// X1 = 0x1 = 0001 → bit[28]=1, bit[29]=0, bit[30]=0, bit[31]=0
TEST(FmEncoderTest, PictureNumber12345EncodesCorrectly) {
  const FmEncoder enc(kNtscSampleRate);
  const FmData picture_12345{false, 0x1, 0x2, 0x3, 0x4, 0x5};
  const auto bits = FmEncoder::BuildBitPattern(picture_12345);

  // X5 = 0x5 = 0101 → LSB first: 1, 0, 1, 0
  EXPECT_TRUE(bits[12]);
  EXPECT_FALSE(bits[13]);
  EXPECT_TRUE(bits[14]);
  EXPECT_FALSE(bits[15]);

  // X4 = 0x4 = 0100 → LSB first: 0, 0, 1, 0
  EXPECT_FALSE(bits[16]);
  EXPECT_FALSE(bits[17]);
  EXPECT_TRUE(bits[18]);
  EXPECT_FALSE(bits[19]);

  // X3 = 0x3 = 0011 → LSB first: 1, 1, 0, 0
  EXPECT_TRUE(bits[20]);
  EXPECT_TRUE(bits[21]);
  EXPECT_FALSE(bits[22]);
  EXPECT_FALSE(bits[23]);

  // X2 = 0x2 = 0010 → LSB first: 0, 1, 0, 0
  EXPECT_FALSE(bits[24]);
  EXPECT_TRUE(bits[25]);
  EXPECT_FALSE(bits[26]);
  EXPECT_FALSE(bits[27]);

  // X1 = 0x1 = 0001 → LSB first: 1, 0, 0, 0
  EXPECT_TRUE(bits[28]);
  EXPECT_FALSE(bits[29]);
  EXPECT_FALSE(bits[30]);
  EXPECT_FALSE(bits[31]);
}

// ---------------------------------------------------------------------------
// FmEncoderTest — mode indicator (fm_programme_time, IEC 60857 §10.2)
// ---------------------------------------------------------------------------

// X5 = 0xA (mode A = lead-in): bit[12]=0, bit[13]=1, bit[14]=0, bit[15]=1
TEST(FmEncoderTest, ModeIndicatorALeadInEncodesCorrectly) {
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0, 0, 0, 0x0A});
  // 0xA = 1010 → LSB first: 0, 1, 0, 1
  EXPECT_FALSE(bits[12]);
  EXPECT_TRUE(bits[13]);
  EXPECT_FALSE(bits[14]);
  EXPECT_TRUE(bits[15]);
}

// X5 = 0xD (mode D = picture/active programme): 0xD = 1101 → LSB first: 1, 0, 1, 1
TEST(FmEncoderTest, ModeIndicatorDPictureEncodesCorrectly) {
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0, 0, 0, 0x0D});
  // 0xD = 1101 → LSB first: 1, 0, 1, 1
  EXPECT_TRUE(bits[12]);
  EXPECT_FALSE(bits[13]);
  EXPECT_TRUE(bits[14]);
  EXPECT_TRUE(bits[15]);
}

// X5 = 0xC (mode C = lead-out): 0xC = 1100 → LSB first: 0, 0, 1, 1
TEST(FmEncoderTest, ModeIndicatorCLeadOutEncodesCorrectly) {
  const auto bits = FmEncoder::BuildBitPattern(FmData{false, 0, 0, 0, 0, 0x0C});
  // 0xC = 1100 → LSB first: 0, 0, 1, 1
  EXPECT_FALSE(bits[12]);
  EXPECT_FALSE(bits[13]);
  EXPECT_TRUE(bits[14]);
  EXPECT_TRUE(bits[15]);
}

}  // namespace
}  // namespace videosynth
