/*
 * File:        circ_encoder.h
 * Module:      efm
 * Purpose:     Cross Interleaved Reed-Solomon (CIRC) encoder turning F1 audio
 *              frames into the 32-symbol F2 frames recorded on disc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "videosynth/efm/audio_frame_assembler.h"

namespace videosynth::efm {

// IEC 60908-1999, 16.2: each frame results in a block of 32 symbols, of which
// 24 are data symbols and 8 are the parity symbols Pm and Qm.
inline constexpr std::size_t kF2FrameBytes = 32;

// ECMA-130, C.9 / IEC 60908-1999, 16.3: the C1 code is a (32,28) and the C2
// code a (28,24) Reed-Solomon code over GF(2^8).
inline constexpr std::size_t kC1CodewordBytes = 32;
inline constexpr std::size_t kC2CodewordBytes = 28;
inline constexpr std::size_t kCircParityBytes = 4;

// Position of the Q parity symbols within a C2 codeword and of the P parity
// symbols within a C1 codeword (ECMA-130, figure C.4).
inline constexpr std::size_t kC2ParityStart = 12;
inline constexpr std::size_t kC1ParityStart = 28;

// ECMA-130, C.9: the longest delay for a byte between input into, and output
// from, the encoder is 108 F1-frame times. Every symbol of an input frame has
// therefore left the encoder 108 frames later, and a complementary
// de-interleaving decoder recovers each input frame 108 output frames after it
// was pushed. Consumers use this constant to discover the pipeline offset
// instead of measuring it (see the EFM implementation plan, Timing Alignment
// Contract).
inline constexpr std::size_t kCircPipelineLatencyFrames = 108;

// One F2 frame: 24 data symbols plus the inverted Q and P parity symbols, in
// the order of ECMA-130, figure C.4.
using F2Frame = std::array<std::uint8_t, kF2FrameBytes>;

// Encodes a stream of F1 frames into F2 frames.
//
// The encoder is a pure function of the frames pushed since construction or the
// last Reset: its delay registers start filled with digital silence, which is
// exactly equivalent to silence having preceded the first frame, so identical
// input always produces identical output.
//
// Streaming contract: each EncodeFrame call consumes one F1 frame and returns
// the one F2 frame for the same frame time; that frame carries symbols of
// earlier input frames because of the CIRC interleave. Flush pushes
// kCircPipelineLatencyFrames silent frames so every symbol of every previously
// pushed frame reaches the output.
//
// Error reporting: the module reports failure by return value and never throws
// across its public API.
//
// Thread-safety: CircEncoder is NOT thread-safe. Its delay registers are
// mutated by EncodeFrame; a single instance must be driven from one thread.
class CircEncoder {
 public:
  CircEncoder();

  // Encodes one F1 frame, returning the F2 frame for that frame time.
  F2Frame EncodeFrame(const F1Frame& frame);

  // Encodes kCircPipelineLatencyFrames frames of digital silence, appending the
  // resulting F2 frames to `frames` (which is never cleared), so that all
  // previously pushed symbols are represented in the output. Returns false when
  // `frames` is null.
  bool Flush(std::vector<F2Frame>* frames);

  // Restores the encoder to its constructed state (delay registers filled with
  // digital silence, frame time zero).
  void Reset();

 private:
  // ECMA-130, C.3: the first delay section delays the words of the even
  // sampling periods by two F1-frame times.
  static constexpr std::size_t kFirstDelayFrames = 2;
  // ECMA-130, C.5: 28 delays of 0 to 27 x D F1-frame times, where D = 4.
  static constexpr std::size_t kSecondDelayUnitFrames = 4;
  static constexpr std::size_t kSecondDelayRingFrames =
      kCircPipelineLatencyFrames + 1;
  // ECMA-130, C.8: the third delay section delays every alternate byte out of
  // the C1 encoder by one F1-frame time.
  static constexpr std::size_t kThirdDelayBytes = kF2FrameBytes / 2;

  std::array<F1Frame, kFirstDelayFrames> first_delay_{};
  std::array<std::array<std::uint8_t, kSecondDelayRingFrames>, kC2CodewordBytes>
      second_delay_{};
  std::array<std::uint8_t, kThirdDelayBytes> third_delay_{};
  std::size_t frame_time_ = 0;
};

}  // namespace videosynth::efm
