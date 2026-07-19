/*
 * File:        audio_frame_assembler.h
 * Module:      efm
 * Purpose:     Groups 16-bit stereo audio samples into the 24-symbol F1 frames
 *              consumed by the CIRC encoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace videosynth::efm {

// IEC 60908-1999 clause 14: one frame carries six sampling periods, each of two
// 16-bit samples (left and right), giving 12 data words of two symbols each.
inline constexpr std::size_t kStereoSamplesPerF1Frame = 6;
inline constexpr std::size_t kWordsPerF1Frame = 12;
inline constexpr std::size_t kF1FrameBytes = 24;

// One F1 frame: 24 audio symbols in the WmA/WmB order of IEC 60908-1999, 16.2.
// Byte 2w is WmA (the higher 8 bits of word w) and byte 2w + 1 is WmB (the
// lower 8 bits); word 2s is the left and word 2s + 1 the right sample of
// sampling period s.
using F1Frame = std::array<std::uint8_t, kF1FrameBytes>;

// Digital silence: IEC 60908-1999 clause 12 samples are 16-bit two's-complement
// values, so silence is the all-zero frame.
inline constexpr F1Frame kSilentF1Frame = {};

// Assembles one F1 frame from six left and six right samples. Pure function of
// its arguments; the split into WmA/WmB follows IEC 60908-1999, 16.2.
F1Frame AssembleF1Frame(
    const std::array<std::int16_t, kStereoSamplesPerF1Frame>& left,
    const std::array<std::int16_t, kStereoSamplesPerF1Frame>& right);

// Buffers a stereo sample stream and emits complete F1 frames in order.
//
// Error reporting: the module reports failure by return value and never throws
// across its public API (see the EFM implementation plan, phase 3).
//
// Thread-safety: AudioFrameAssembler is NOT thread-safe. It carries the partial
// frame between calls and must be used from a single thread.
class AudioFrameAssembler {
 public:
  // Appends interleaved stereo samples, appending every frame that the new
  // samples complete to `frames` (which is never cleared). Returns false when
  // the buffers differ in length or `frames` is null; in that case no samples
  // are consumed.
  bool PushSamples(const std::vector<std::int16_t>& left,
                   const std::vector<std::int16_t>& right,
                   std::vector<F1Frame>* frames);

  // Pads a partially filled frame with digital silence and emits it. No-op when
  // no samples are pending. Returns false when `frames` is null.
  bool Flush(std::vector<F1Frame>* frames);

  // Number of stereo samples held in the partial frame (0 to 5).
  std::size_t PendingSampleCount() const { return pending_samples_; }

  // Discards the partial frame.
  void Reset();

 private:
  std::array<std::int16_t, kStereoSamplesPerF1Frame> left_{};
  std::array<std::int16_t, kStereoSamplesPerF1Frame> right_{};
  std::size_t pending_samples_ = 0;
};

}  // namespace videosynth::efm
