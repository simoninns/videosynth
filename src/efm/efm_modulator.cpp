/*
 * File:        efm_modulator.cpp
 * Module:      efm
 * Purpose:     Eight-to-fourteen modulation of F3 frames into the 588-bit
 *              channel frames recorded on disc, and their T-value run lengths.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/efm/efm_modulator.h"

#include <array>

namespace videosynth::efm {
namespace {

// ECMA-130, annex D (IEC 60908-1999, clause 13, figures 6 and 7): the
// eight-to-fourteen conversion table. The left-most channel bit of an entry is
// its most significant bit and is sent first.
constexpr std::array<std::uint16_t, 256> kEightToFourteenTable = {
    0b01001000100000, 0b10000100000000, 0b10010000100000, 0b10001000100000,
    0b01000100000000, 0b00000100010000, 0b00010000100000, 0b00100100000000,
    0b01001001000000, 0b10000001000000, 0b10010001000000, 0b10001001000000,
    0b01000001000000, 0b00000001000000, 0b00010001000000, 0b00100001000000,
    0b10000000100000, 0b10000010000000, 0b10010010000000, 0b00100000100000,
    0b01000010000000, 0b00000010000000, 0b00010010000000, 0b00100010000000,
    0b01001000010000, 0b10000000010000, 0b10010000010000, 0b10001000010000,
    0b01000000010000, 0b00001000010000, 0b00010000010000, 0b00100000010000,
    0b00000000100000, 0b10000100001000, 0b00001000100000, 0b00100100100000,
    0b01000100001000, 0b00000100001000, 0b01000000100000, 0b00100100001000,
    0b01001001001000, 0b10000001001000, 0b10010001001000, 0b10001001001000,
    0b01000001001000, 0b00000001001000, 0b00010001001000, 0b00100001001000,
    0b00000100000000, 0b10000010001000, 0b10010010001000, 0b10000100010000,
    0b01000010001000, 0b00000010001000, 0b00010010001000, 0b00100010001000,
    0b01001000001000, 0b10000000001000, 0b10010000001000, 0b10001000001000,
    0b01000000001000, 0b00001000001000, 0b00010000001000, 0b00100000001000,
    0b01001000100100, 0b10000100100100, 0b10010000100100, 0b10001000100100,
    0b01000100100100, 0b00000000100100, 0b00010000100100, 0b00100100100100,
    0b01001001000100, 0b10000001000100, 0b10010001000100, 0b10001001000100,
    0b01000001000100, 0b00000001000100, 0b00010001000100, 0b00100001000100,
    0b10000000100100, 0b10000010000100, 0b10010010000100, 0b00100000100100,
    0b01000010000100, 0b00000010000100, 0b00010010000100, 0b00100010000100,
    0b01001000000100, 0b10000000000100, 0b10010000000100, 0b10001000000100,
    0b01000000000100, 0b00001000000100, 0b00010000000100, 0b00100000000100,
    0b01001000100010, 0b10000100100010, 0b10010000100010, 0b10001000100010,
    0b01000100100010, 0b00000000100010, 0b01000000100100, 0b00100100100010,
    0b01001001000010, 0b10000001000010, 0b10010001000010, 0b10001001000010,
    0b01000001000010, 0b00000001000010, 0b00010001000010, 0b00100001000010,
    0b10000000100010, 0b10000010000010, 0b10010010000010, 0b00100000100010,
    0b01000010000010, 0b00000010000010, 0b00010010000010, 0b00100010000010,
    0b01001000000010, 0b00001001001000, 0b10010000000010, 0b10001000000010,
    0b01000000000010, 0b00001000000010, 0b00010000000010, 0b00100000000010,
    0b01001000100001, 0b10000100100001, 0b10010000100001, 0b10001000100001,
    0b01000100100001, 0b00000000100001, 0b00010000100001, 0b00100100100001,
    0b01001001000001, 0b10000001000001, 0b10010001000001, 0b10001001000001,
    0b01000001000001, 0b00000001000001, 0b00010001000001, 0b00100001000001,
    0b10000000100001, 0b10000010000001, 0b10010010000001, 0b00100000100001,
    0b01000010000001, 0b00000010000001, 0b00010010000001, 0b00100010000001,
    0b01001000000001, 0b10000010010000, 0b10010000000001, 0b10001000000001,
    0b01000010010000, 0b00001000000001, 0b00010000000001, 0b00100010010000,
    0b00001000100001, 0b10000100001001, 0b01000100010000, 0b00000100100001,
    0b01000100001001, 0b00000100001001, 0b01000000100001, 0b00100100001001,
    0b01001001001001, 0b10000001001001, 0b10010001001001, 0b10001001001001,
    0b01000001001001, 0b00000001001001, 0b00010001001001, 0b00100001001001,
    0b00000100100000, 0b10000010001001, 0b10010010001001, 0b00100100010000,
    0b01000010001001, 0b00000010001001, 0b00010010001001, 0b00100010001001,
    0b01001000001001, 0b10000000001001, 0b10010000001001, 0b10001000001001,
    0b01000000001001, 0b00001000001001, 0b00010000001001, 0b00100000001001,
    0b01000100100000, 0b10000100010001, 0b10010010010000, 0b00001000100100,
    0b01000100010001, 0b00000100010001, 0b00010010010000, 0b00100100010001,
    0b00001001000001, 0b10000100000001, 0b00001001000100, 0b00001001000000,
    0b01000100000001, 0b00000100000001, 0b00000010010000, 0b00100100000001,
    0b00000100100100, 0b10000010010001, 0b10010010010001, 0b10000100100000,
    0b01000010010001, 0b00000010010001, 0b00010010010001, 0b00100010010001,
    0b01001000010001, 0b10000000010001, 0b10010000010001, 0b10001000010001,
    0b01000000010001, 0b00001000010001, 0b00010000010001, 0b00100000010001,
    0b01000100000010, 0b00000100000010, 0b10000100010010, 0b00100100000010,
    0b01000100010010, 0b00000100010010, 0b01000000100010, 0b00100100010010,
    0b10000100000010, 0b10000100000100, 0b00001001001001, 0b00001001000010,
    0b01000100000100, 0b00000100000100, 0b00010000100010, 0b00100100000100,
    0b00000100100010, 0b10000010010010, 0b10010010010010, 0b00001000100010,
    0b01000010010010, 0b00000010010010, 0b00010010010010, 0b00100010010010,
    0b01001000010010, 0b10000000010010, 0b10010000010010, 0b10001000010010,
    0b01000000010010, 0b00001000010010, 0b00010000010010, 0b00100000010010,
};

// ECMA-130, annex E: the three merging bits are one of four combinations. Only
// 000 and 100 are used ahead of a sync header, whose first channel bit is a
// one.
constexpr std::array<std::uint32_t, 4> kMergingCandidates = {0b000U, 0b001U,
                                                             0b010U, 0b100U};
constexpr std::array<std::uint32_t, 2> kSyncMergingCandidates = {0b000U,
                                                                 0b100U};

// Index of the last one bit of the sync header, which completes the second of
// its two maximum-length runs (ECMA-130, 19.2).
constexpr std::size_t kSyncHeaderSecondRunBit = 22;

}  // namespace

std::uint16_t EfmSymbol(std::uint8_t value) {
  return kEightToFourteenTable[value];
}

bool EfmModulator::ApplyBits(std::uint32_t pattern, std::size_t count,
                             std::size_t exempt_bit_index, RunState* state,
                             std::vector<bool>* bits) {
  bool valid = true;
  for (std::size_t index = 0; index < count; ++index) {
    const bool bit = ((pattern >> (count - 1 - index)) & 1U) != 0U;
    if (bits != nullptr) {
      bits->push_back(bit);
    }

    if (bit) {
      // ECMA-130, 19.4: a one channel bit is a change of pit to land or land to
      // pit, so it terminates the current run.
      const std::size_t run_length = state->bits_since_last_one + 1;
      if (state->started) {
        if (run_length < kMinRunLengthT || run_length > kMaxRunLengthT) {
          valid = false;
        }
        // ECMA-130, annex E rule ii: the sync header pattern is two adjacent
        // maximum-length runs and must not occur anywhere else.
        if (run_length == kMaxRunLengthT &&
            state->previous_run_length == kMaxRunLengthT &&
            index != exempt_bit_index) {
          valid = false;
        }
        state->previous_run_length = run_length;
      }
      state->started = true;
      state->bits_since_last_one = 0;
      state->polarity = -state->polarity;
    } else {
      ++state->bits_since_last_one;
      if (state->started && state->bits_since_last_one + 1 > kMaxRunLengthT) {
        valid = false;
      }
    }
    state->digital_sum_value += state->polarity;
  }
  return valid;
}

std::uint32_t EfmModulator::SelectMergingBits(std::uint32_t next_pattern,
                                              std::size_t next_bit_count,
                                              std::size_t next_exempt_bit_index,
                                              bool before_sync_header) const {
  const std::uint32_t* candidates = before_sync_header
                                        ? kSyncMergingCandidates.data()
                                        : kMergingCandidates.data();
  const std::size_t candidate_count = before_sync_header
                                          ? kSyncMergingCandidates.size()
                                          : kMergingCandidates.size();

  std::uint32_t best = candidates[0];
  bool have_best = false;
  int best_absolute_dsv = 0;

  for (std::size_t index = 0; index < candidate_count; ++index) {
    const std::uint32_t candidate = candidates[index];
    RunState trial = state_;
    bool valid = ApplyBits(candidate, kMergingBitsPerBoundary, kNoExemptBit,
                           &trial, nullptr);
    // The DSV is determined over the merging bits and the following byte
    // (ECMA-130, annex E).
    valid = ApplyBits(next_pattern, next_bit_count, next_exempt_bit_index,
                      &trial, nullptr) &&
            valid;
    if (!valid) {
      continue;
    }

    const int absolute_dsv = trial.digital_sum_value < 0
                                 ? -trial.digital_sum_value
                                 : trial.digital_sum_value;
    // Retain the combination giving the lowest DSV; if two give the same,
    // lowest DSV, a combination with a transition is chosen.
    if (!have_best || absolute_dsv < best_absolute_dsv ||
        (absolute_dsv == best_absolute_dsv && best == 0U && candidate != 0U)) {
      best = candidate;
      best_absolute_dsv = absolute_dsv;
      have_best = true;
    }
  }

  if (have_best) {
    return best;
  }

  // No combination satisfies every rule. The run-length rules govern
  // detectability, so the first combination that keeps them is used and the
  // false-sync rule is given up for this boundary.
  for (std::size_t index = 0; index < candidate_count; ++index) {
    RunState trial = state_;
    if (ApplyBits(candidates[index], kMergingBitsPerBoundary, kNoExemptBit,
                  &trial, nullptr)) {
      return candidates[index];
    }
  }
  return candidates[0];
}

bool EfmModulator::ModulateFrame(std::uint16_t control_symbol,
                                 const F2Frame& f2_frame,
                                 std::vector<bool>* channel_bits) {
  if (channel_bits == nullptr) {
    return false;
  }
  channel_bits->reserve(channel_bits->size() + kChannelBitsPerFrame);

  // ECMA-130, 19.4: the channel frame opens with the sync header, whose two
  // maximum-length runs are the pattern a decoder locks on to.
  ApplyBits(kSyncHeaderPattern, kSyncHeaderBits, kSyncHeaderSecondRunBit,
            &state_, channel_bits);

  for (std::size_t symbol = 0; symbol < kSymbolsPerChannelFrame; ++symbol) {
    const std::uint32_t pattern =
        symbol == 0 ? control_symbol : EfmSymbol(f2_frame[symbol - 1]);
    const std::uint32_t merging = SelectMergingBits(
        pattern, kChannelBitsPerSymbol, kNoExemptBit, /*before_sync_header=*/
        false);
    ApplyBits(merging, kMergingBitsPerBoundary, kNoExemptBit, &state_,
              channel_bits);
    ApplyBits(pattern, kChannelBitsPerSymbol, kNoExemptBit, &state_,
              channel_bits);
  }

  // The frame is closed by the merging bits that precede the next sync header.
  const std::uint32_t trailing_merging =
      SelectMergingBits(kSyncHeaderPattern, kSyncHeaderBits,
                        kSyncHeaderSecondRunBit, /*before_sync_header=*/true);
  ApplyBits(trailing_merging, kMergingBitsPerBoundary, kNoExemptBit, &state_,
            channel_bits);
  return true;
}

void EfmModulator::Reset() { state_ = RunState{}; }

bool TValueEncoder::PushBits(const std::vector<bool>& channel_bits,
                             std::vector<std::uint8_t>* t_values) {
  if (t_values == nullptr) {
    return false;
  }
  for (const bool bit : channel_bits) {
    if (!bit) {
      ++bits_since_last_one_;
      continue;
    }
    if (started_) {
      t_values->push_back(static_cast<std::uint8_t>(bits_since_last_one_ + 1));
    }
    started_ = true;
    bits_since_last_one_ = 0;
  }
  return true;
}

bool TValueEncoder::Flush(std::vector<std::uint8_t>* t_values) {
  if (t_values == nullptr) {
    return false;
  }
  if (!started_) {
    return true;
  }
  const std::size_t pending = bits_since_last_one_ + 1;
  t_values->push_back(static_cast<std::uint8_t>(
      pending < kMinRunLengthT ? kMinRunLengthT : pending));
  started_ = false;
  bits_since_last_one_ = 0;
  return true;
}

void TValueEncoder::Reset() {
  bits_since_last_one_ = 0;
  started_ = false;
}

}  // namespace videosynth::efm
