/*
 * File:        audio_frame_assembler.cpp
 * Module:      efm
 * Purpose:     Groups 16-bit stereo audio samples into the 24-symbol F1 frames
 *              consumed by the CIRC encoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/efm/audio_frame_assembler.h"

namespace videosynth::efm {
namespace {

// IEC 60908-1999, 16.2: WmA carries the higher and WmB the lower 8 bits of the
// 16-bit data word.
constexpr int kWordHighByteShift = 8;
constexpr std::uint16_t kByteMask = 0x00FFU;

std::uint8_t HighByte(std::int16_t sample) {
  const auto unsigned_sample = static_cast<std::uint16_t>(sample);
  return static_cast<std::uint8_t>((unsigned_sample >> kWordHighByteShift) &
                                   kByteMask);
}

std::uint8_t LowByte(std::int16_t sample) {
  const auto unsigned_sample = static_cast<std::uint16_t>(sample);
  return static_cast<std::uint8_t>(unsigned_sample & kByteMask);
}

}  // namespace

F1Frame AssembleF1Frame(
    const std::array<std::int16_t, kStereoSamplesPerF1Frame>& left,
    const std::array<std::int16_t, kStereoSamplesPerF1Frame>& right) {
  F1Frame frame{};
  for (std::size_t sample = 0; sample < kStereoSamplesPerF1Frame; ++sample) {
    // Word 2s is the left and word 2s + 1 the right sample of sampling period
    // s (IEC 60908-1999 clause 14); each word occupies two consecutive symbols.
    const std::size_t left_word_byte = 4 * sample;
    frame[left_word_byte] = HighByte(left[sample]);
    frame[left_word_byte + 1] = LowByte(left[sample]);
    frame[left_word_byte + 2] = HighByte(right[sample]);
    frame[left_word_byte + 3] = LowByte(right[sample]);
  }
  return frame;
}

bool AudioFrameAssembler::PushSamples(const std::vector<std::int16_t>& left,
                                      const std::vector<std::int16_t>& right,
                                      std::vector<F1Frame>* frames) {
  if (frames == nullptr || left.size() != right.size()) {
    return false;
  }

  for (std::size_t index = 0; index < left.size(); ++index) {
    left_[pending_samples_] = left[index];
    right_[pending_samples_] = right[index];
    ++pending_samples_;
    if (pending_samples_ == kStereoSamplesPerF1Frame) {
      frames->push_back(AssembleF1Frame(left_, right_));
      pending_samples_ = 0;
    }
  }
  return true;
}

bool AudioFrameAssembler::Flush(std::vector<F1Frame>* frames) {
  if (frames == nullptr) {
    return false;
  }
  if (pending_samples_ == 0) {
    return true;
  }

  // Pad the partial frame with digital silence so the stream ends on a frame
  // boundary (IEC 60908-1999 clause 12: silence is the zero sample).
  for (std::size_t index = pending_samples_; index < kStereoSamplesPerF1Frame;
       ++index) {
    left_[index] = 0;
    right_[index] = 0;
  }
  frames->push_back(AssembleF1Frame(left_, right_));
  pending_samples_ = 0;
  return true;
}

void AudioFrameAssembler::Reset() {
  left_ = {};
  right_ = {};
  pending_samples_ = 0;
}

}  // namespace videosynth::efm
