/*
 * File:        efm_modulator.h
 * Module:      efm
 * Purpose:     Eight-to-fourteen modulation of F3 frames into the 588-bit
 *              channel frames recorded on disc, and their T-value run lengths.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "videosynth/efm/circ_encoder.h"

namespace videosynth::efm {

// ECMA-130, 19.1 / IEC 60908-1999, clause 13: each 8-bit byte is represented by
// 14 channel bits, with at least two and at most ten zeros between two ones.
inline constexpr std::size_t kChannelBitsPerSymbol = 14;

// ECMA-130, 19.3: three merging channel bits are inserted between the bytes of
// 14 channel bits and between the sync header and the adjacent bytes.
inline constexpr std::size_t kMergingBitsPerBoundary = 3;

// ECMA-130, 19.2 / IEC 60908-1999, clause 14: the sync header is 24 channel
// bits holding two maximum-length runs. The vendored ECMA-130 transcription
// prints only 22 digits; the canonical pattern is used here.
inline constexpr std::size_t kSyncHeaderBits = 24;
inline constexpr std::uint32_t kSyncHeaderPattern =
    0b1000'0000'0001'0000'0000'0010U;

// ECMA-130, 19.4: a channel frame carries the control byte and the 32 bytes of
// one F2 frame, each as 14 channel bits.
inline constexpr std::size_t kSymbolsPerChannelFrame = 1 + kF2FrameBytes;

// ECMA-130, 19.4: 24 sync bits + 34 merging groups + 33 symbols
// = 24 + 34 x 3 + 33 x 14 = 588 channel bits.
inline constexpr std::size_t kMergingGroupsPerChannelFrame =
    kSymbolsPerChannelFrame + 1;
inline constexpr std::size_t kChannelBitsPerFrame =
    kSyncHeaderBits +
    (kMergingGroupsPerChannelFrame * kMergingBitsPerBoundary) +
    (kSymbolsPerChannelFrame * kChannelBitsPerSymbol);

// IEC 60908-1999, clause 13: the run length between two transitions is at least
// T_min = 3 and at most T_max = 11 channel bit times.
inline constexpr std::uint8_t kMinRunLengthT = 3;
inline constexpr std::uint8_t kMaxRunLengthT = 11;

// The 14 channel bits representing `value`, in the 14 least significant bits of
// the result and most significant channel bit first (ECMA-130, annex D).
std::uint16_t EfmSymbol(std::uint8_t value);

// Serialises F3 frames (a control byte plus an F2 frame) as channel frames.
//
// Streaming contract: each ModulateFrame call appends exactly
// kChannelBitsPerFrame channel bits. The merging bits and the digital sum value
// carry over between frames, so a frame's encoding depends on everything
// modulated before it; Reset restores the constructed state.
//
// Error reporting: the module reports failure by return value and never throws
// across its public API.
//
// Thread-safety: EfmModulator is NOT thread-safe. Its run-length and DSV state
// is mutated by ModulateFrame; a single instance must be driven from one
// thread.
class EfmModulator {
 public:
  // Appends the channel frame for `control_symbol` and `f2_frame` to
  // `channel_bits` (which is never cleared). `control_symbol` holds the 14
  // channel bits of the frame's first byte: the eight-to-fourteen translation
  // of the subcode control byte, or the out-of-table sync patterns
  // kSubcodeSyncS0 / kSubcodeSyncS1 for the first two frames of a subcode
  // section (IEC 60908-1999, 17.3). Returns false when `channel_bits` is null.
  bool ModulateFrame(std::uint16_t control_symbol, const F2Frame& f2_frame,
                     std::vector<bool>* channel_bits);

  // The digital sum value accumulated over everything modulated so far
  // (ECMA-130, annex E). Merging-bit selection keeps it close to zero.
  int DigitalSumValue() const { return state_.digital_sum_value; }

  // Restores the modulator to its constructed state.
  void Reset();

 private:
  // Run-length and DSV state of the channel bit stream.
  struct RunState {
    // Channel bits emitted since the most recent one bit.
    std::size_t bits_since_last_one = 0;
    // Length of the last completed run, or zero before the first two ones.
    std::size_t previous_run_length = 0;
    // NRZI level of the current run: a one bit is a transition
    // (ECMA-130, 19.4).
    int polarity = 1;
    int digital_sum_value = 0;
    bool started = false;
  };

  // Index of a one bit within a pattern at which the false-sync rule does not
  // apply, and the sentinel meaning "no such bit".
  static constexpr std::size_t kNoExemptBit =
      std::numeric_limits<std::size_t>::max();

  // Applies `count` bits of `pattern` (most significant bit first) to `state`,
  // appending them to `bits` when that is not null. Returns false when the
  // bits violate the run-length rules of IEC 60908-1999, clause 13 or would
  // produce a sync header away from its own position (ECMA-130, annex E rule
  // ii). `exempt_bit_index` names the one bit of `pattern` that legitimately
  // completes a second maximum-length run, i.e. the last one of a sync header.
  static bool ApplyBits(std::uint32_t pattern, std::size_t count,
                        std::size_t exempt_bit_index, RunState* state,
                        std::vector<bool>* bits);

  // Chooses the three merging bits preceding a pattern of `next_bit_count`
  // bits, per ECMA-130, annex E: candidates that break a rule are skipped and
  // the remaining one minimising the absolute DSV over the merging bits and the
  // following symbol is retained, a candidate with a transition winning a tie.
  std::uint32_t SelectMergingBits(std::uint32_t next_pattern,
                                  std::size_t next_bit_count,
                                  std::size_t next_exempt_bit_index,
                                  bool before_sync_header) const;

  RunState state_{};
};

// Converts a channel bit stream into the pit/land run lengths (T values) of the
// EFM output file: every one bit is a transition, so the stream is a sequence
// of run lengths from T_min to T_max (IEC 60908-1999, clause 13).
//
// The first one bit of the stream is the start of the first sync header and
// opens the first run; no T value is emitted for it.
//
// Thread-safety: TValueEncoder is NOT thread-safe. It carries the pending run
// between calls and must be used from a single thread.
class TValueEncoder {
 public:
  // Appends the T value of every run that `channel_bits` completes to
  // `t_values` (which is never cleared). Returns false when `t_values` is null.
  bool PushBits(const std::vector<bool>& channel_bits,
                std::vector<std::uint8_t>* t_values);

  // Emits the run still in progress at the end of the stream. Because no
  // transition terminates it, it is extended to T_min when shorter; a consumer
  // that reconstructs channel bits and truncates to the known stream length
  // recovers the stream exactly. No-op when no run is pending. Returns false
  // when `t_values` is null.
  bool Flush(std::vector<std::uint8_t>* t_values);

  // Discards the pending run.
  void Reset();

 private:
  std::size_t bits_since_last_one_ = 0;
  bool started_ = false;
};

}  // namespace videosynth::efm
