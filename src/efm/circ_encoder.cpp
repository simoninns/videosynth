/*
 * File:        circ_encoder.cpp
 * Module:      efm
 * Purpose:     Cross Interleaved Reed-Solomon (CIRC) encoder turning F1 audio
 *              frames into the 32-symbol F2 frames recorded on disc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/efm/circ_encoder.h"

namespace videosynth::efm {
namespace {

// GF(2^8) arithmetic per IEC 60908-1999, 16.2 / ECMA-130, C.7: the field is
// defined by the primitive polynomial P(x) = x^8 + x^4 + x^3 + x^2 + 1 and the
// primitive element alpha = 00000010 (right-most bit least significant).
constexpr unsigned kFieldPolynomial = 0x11DU;
constexpr std::size_t kFieldOrder = 256;
constexpr std::size_t kFieldNonZeroElements = kFieldOrder - 1;

struct GaloisTables {
  // exponent[i] = alpha^i; logarithm[alpha^i] = i (logarithm[0] is unused).
  std::array<std::uint8_t, kFieldNonZeroElements> exponent{};
  std::array<std::uint8_t, kFieldOrder> logarithm{};
};

constexpr GaloisTables MakeGaloisTables() {
  GaloisTables tables{};
  unsigned value = 1;
  for (std::size_t power = 0; power < kFieldNonZeroElements; ++power) {
    tables.exponent[power] = static_cast<std::uint8_t>(value);
    tables.logarithm[value] = static_cast<std::uint8_t>(power);
    value <<= 1U;
    if ((value & kFieldOrder) != 0U) {
      value ^= kFieldPolynomial;
    }
  }
  return tables;
}

constexpr GaloisTables kGaloisTables = MakeGaloisTables();

constexpr std::uint8_t GaloisPower(std::size_t exponent) {
  return kGaloisTables.exponent[exponent % kFieldNonZeroElements];
}

constexpr std::uint8_t GaloisMultiply(std::uint8_t left, std::uint8_t right) {
  if (left == 0 || right == 0) {
    return 0;
  }
  const std::size_t sum =
      static_cast<std::size_t>(kGaloisTables.logarithm[left]) +
      static_cast<std::size_t>(kGaloisTables.logarithm[right]);
  return GaloisPower(sum);
}

constexpr std::uint8_t GaloisInverse(std::uint8_t value) {
  return GaloisPower(kFieldNonZeroElements -
                     static_cast<std::size_t>(kGaloisTables.logarithm[value]));
}

using ParityMatrix =
    std::array<std::array<std::uint8_t, kCircParityBytes>, kCircParityBytes>;

// The parity check matrices H_P and H_Q of ECMA-130, C.7 have the entry
// alpha^(row x (codeword_bytes - 1 - column)); the four columns covering the
// parity symbols form the square matrix that determines them.
constexpr ParityMatrix MakeParityMatrix(std::size_t codeword_bytes,
                                        std::size_t parity_start) {
  ParityMatrix matrix{};
  for (std::size_t row = 0; row < kCircParityBytes; ++row) {
    for (std::size_t column = 0; column < kCircParityBytes; ++column) {
      matrix[row][column] =
          GaloisPower(row * (codeword_bytes - 1 - parity_start - column));
    }
  }
  return matrix;
}

// Gauss-Jordan inversion over GF(2^8). The matrices of MakeParityMatrix are
// Vandermonde matrices with distinct nodes and are therefore always invertible.
constexpr ParityMatrix InvertParityMatrix(ParityMatrix matrix) {
  ParityMatrix inverse{};
  for (std::size_t index = 0; index < kCircParityBytes; ++index) {
    inverse[index][index] = 1;
  }

  for (std::size_t pivot = 0; pivot < kCircParityBytes; ++pivot) {
    std::size_t pivot_row = pivot;
    while (pivot_row < kCircParityBytes && matrix[pivot_row][pivot] == 0) {
      ++pivot_row;
    }
    if (pivot_row != pivot) {
      for (std::size_t column = 0; column < kCircParityBytes; ++column) {
        const std::uint8_t matrix_value = matrix[pivot][column];
        matrix[pivot][column] = matrix[pivot_row][column];
        matrix[pivot_row][column] = matrix_value;
        const std::uint8_t inverse_value = inverse[pivot][column];
        inverse[pivot][column] = inverse[pivot_row][column];
        inverse[pivot_row][column] = inverse_value;
      }
    }

    const std::uint8_t scale = GaloisInverse(matrix[pivot][pivot]);
    for (std::size_t column = 0; column < kCircParityBytes; ++column) {
      matrix[pivot][column] = GaloisMultiply(matrix[pivot][column], scale);
      inverse[pivot][column] = GaloisMultiply(inverse[pivot][column], scale);
    }

    for (std::size_t row = 0; row < kCircParityBytes; ++row) {
      if (row == pivot || matrix[row][pivot] == 0) {
        continue;
      }
      const std::uint8_t factor = matrix[row][pivot];
      for (std::size_t column = 0; column < kCircParityBytes; ++column) {
        matrix[row][column] ^= GaloisMultiply(factor, matrix[pivot][column]);
        inverse[row][column] ^= GaloisMultiply(factor, inverse[pivot][column]);
      }
    }
  }
  return inverse;
}

constexpr ParityMatrix kC1ParitySolver =
    InvertParityMatrix(MakeParityMatrix(kC1CodewordBytes, kC1ParityStart));
constexpr ParityMatrix kC2ParitySolver =
    InvertParityMatrix(MakeParityMatrix(kC2CodewordBytes, kC2ParityStart));

// Fills the four parity symbols at `parity_start` so that H x V = 0 holds for
// the codeword (ECMA-130, C.7). The parity positions must be zero on entry.
template <std::size_t kCodewordBytes>
void ComputeParity(std::array<std::uint8_t, kCodewordBytes>* codeword,
                   std::size_t parity_start, const ParityMatrix& solver) {
  std::array<std::uint8_t, kCircParityBytes> syndrome{};
  for (std::size_t row = 0; row < kCircParityBytes; ++row) {
    std::uint8_t accumulator = 0;
    for (std::size_t column = 0; column < kCodewordBytes; ++column) {
      accumulator ^=
          GaloisMultiply(GaloisPower(row * (kCodewordBytes - 1 - column)),
                         (*codeword)[column]);
    }
    syndrome[row] = accumulator;
  }

  for (std::size_t index = 0; index < kCircParityBytes; ++index) {
    std::uint8_t parity = 0;
    for (std::size_t row = 0; row < kCircParityBytes; ++row) {
      parity ^= GaloisMultiply(solver[index][row], syndrome[row]);
    }
    (*codeword)[parity_start + index] = parity;
  }
}

// ECMA-130, figure C.4: the words of the even sampling periods (delayed by the
// first delay section) occupy C2 codeword positions 0 to 11 and those of the
// odd sampling periods positions 16 to 27, in the order given here.
constexpr std::array<std::size_t, 6> kDelayedGroupWords = {0, 4, 8, 1, 5, 9};
constexpr std::array<std::size_t, 6> kCurrentGroupWords = {2, 6, 10, 3, 7, 11};
constexpr std::size_t kDelayedGroupStart = 0;
constexpr std::size_t kCurrentGroupStart = 16;

// IEC 60908-1999, 16.2: the parity symbols are recorded inverted.
constexpr std::uint8_t kParityInversionMask = 0xFFU;

}  // namespace

CircEncoder::CircEncoder() { Reset(); }

void CircEncoder::Reset() {
  // The delay registers hold digital silence, which is equivalent to silence
  // having preceded the first pushed frame (EFM implementation plan, Timing
  // Alignment Contract).
  first_delay_ = {};
  second_delay_ = {};
  third_delay_ = {};
  frame_time_ = 0;
}

F2Frame CircEncoder::EncodeFrame(const F1Frame& frame) {
  // First delay section (ECMA-130, C.3): the words of the even sampling
  // periods are taken from the frame pushed two frame times ago.
  const std::size_t first_delay_slot = frame_time_ % kFirstDelayFrames;
  const F1Frame delayed_frame = first_delay_[first_delay_slot];
  first_delay_[first_delay_slot] = frame;

  std::array<std::uint8_t, kC2CodewordBytes> c2_codeword{};
  for (std::size_t index = 0; index < kDelayedGroupWords.size(); ++index) {
    const std::size_t word = kDelayedGroupWords[index];
    c2_codeword[kDelayedGroupStart + 2 * index] = delayed_frame[2 * word];
    c2_codeword[kDelayedGroupStart + 2 * index + 1] =
        delayed_frame[2 * word + 1];
  }
  for (std::size_t index = 0; index < kCurrentGroupWords.size(); ++index) {
    const std::size_t word = kCurrentGroupWords[index];
    c2_codeword[kCurrentGroupStart + 2 * index] = frame[2 * word];
    c2_codeword[kCurrentGroupStart + 2 * index + 1] = frame[2 * word + 1];
  }

  // Encoder C2 (ECMA-130, C.4): a (28,24) Reed-Solomon code whose four Q parity
  // symbols occupy the middle of the codeword.
  ComputeParity(&c2_codeword, kC2ParityStart, kC2ParitySolver);

  // Second delay section (ECMA-130, C.5): codeword position i is delayed by
  // i x D frame times, with D = 4.
  std::array<std::uint8_t, kC2CodewordBytes> c1_input{};
  for (std::size_t line = 0; line < kC2CodewordBytes; ++line) {
    second_delay_[line][frame_time_ % kSecondDelayRingFrames] =
        c2_codeword[line];
    const std::size_t delay = line * kSecondDelayUnitFrames;
    const std::size_t read_slot =
        (frame_time_ + kSecondDelayRingFrames - delay) % kSecondDelayRingFrames;
    c1_input[line] = second_delay_[line][read_slot];
  }

  // Encoder C1 (ECMA-130, C.6): a (32,28) Reed-Solomon code appending four P
  // parity symbols.
  std::array<std::uint8_t, kC1CodewordBytes> c1_codeword{};
  for (std::size_t index = 0; index < kC2CodewordBytes; ++index) {
    c1_codeword[index] = c1_input[index];
  }
  ComputeParity(&c1_codeword, kC1ParityStart, kC1ParitySolver);

  // Third delay section (ECMA-130, C.8): every alternate byte out of the C1
  // encoder is delayed by one frame time.
  F2Frame f2_frame{};
  for (std::size_t index = 0; index < kF2FrameBytes; ++index) {
    if (index % 2 == 0) {
      f2_frame[index] = third_delay_[index / 2];
      third_delay_[index / 2] = c1_codeword[index];
    } else {
      f2_frame[index] = c1_codeword[index];
    }
  }

  // ECMA-130, C.9: all parity bits are inverted before they leave the encoder.
  // The delay sections do not move symbols between positions, so the parity
  // positions of the output frame are those of the codewords.
  for (std::size_t index = 0; index < kCircParityBytes; ++index) {
    f2_frame[kC2ParityStart + index] ^= kParityInversionMask;
    f2_frame[kC1ParityStart + index] ^= kParityInversionMask;
  }

  ++frame_time_;
  return f2_frame;
}

bool CircEncoder::Flush(std::vector<F2Frame>* frames) {
  if (frames == nullptr) {
    return false;
  }
  frames->reserve(frames->size() + kCircPipelineLatencyFrames);
  for (std::size_t index = 0; index < kCircPipelineLatencyFrames; ++index) {
    frames->push_back(EncodeFrame(kSilentF1Frame));
  }
  return true;
}

}  // namespace videosynth::efm
