/*
 * File:        audio_track_generator.h
 * Module:      audio_track_generator
 * Purpose:     Orchestrates synthesis of up to eight stereo audio channel pairs
 *              frame-locked to the output video stream.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "videosynth/audio_synthesizer.h"
#include "videosynth/audio_wav_writer.h"
#include "videosynth/interfaces.h"
#include "videosynth/model.h"

namespace videosynth {

// Generates every audio channel pair declared by a project and streams each as
// a separate stereo 24-bit RIFF/WAVE file (`_audio_<pair>.wav`) frame-locked to
// the output video stream, per the CVBS File Format Specification (Audio Data).
//
// The set of emitted channel pairs is the union of the pair numbers declared
// across all sections (ProjectAudioChannelPairs). Every pair file spans the
// whole output; sections that omit a pair emit silence for their frames, and a
// channel with `enabled == false` within a declared pair emits all-zero silence
// (SMPTE 272M §6.4).
//
// Audio is a pure function of output position: frame k carries
// AudioSamplesForFrame(standard, k) samples per channel (SMPTE 272M §14.3), and
// each contiguous run of output frames sharing one section resets oscillator
// phase at the run start. This keeps the track correct under disc-skip
// withhold/replay without caching: the caller emits output frames strictly in
// output order.
//
// Usage: Begin(project, output_frame_sections) opens the writers and computes
// the per-frame plan; EmitFrame(k) must then be called for k = 0, 1, ... in
// increasing output order; Finalize() closes the files. Abort() discards a
// partially-written set (e.g. on cancellation).
//
// Thread-safety: NOT thread-safe. Its synthesiser and writer state is mutated
// by EmitFrame; it must not be called concurrently from multiple threads.
class AudioTrackGenerator {
 public:
  explicit AudioTrackGenerator(ILogger* logger = nullptr);

  // Prepares the plan and opens one WAV writer per declared channel pair.
  // output_frame_sections lists, in output order, the section shown by each
  // stored output frame (its size is the total output frame count). Returns
  // false and appends messages to errors on any failure; on failure any opened
  // writers are aborted. When the project declares no audio, Begin() succeeds,
  // active() stays false, and EmitFrame/Finalize are no-ops.
  bool Begin(const Project& project,
             const std::vector<const Section*>& output_frame_sections,
             std::vector<std::string>* errors);

  // Synthesises and appends output frame `output_index` (0-based) across all
  // channel pairs. Must be called once per output frame in increasing order.
  // Returns false and appends to errors on any writer failure or if called out
  // of range.
  bool EmitFrame(std::size_t output_index, std::vector<std::string>* errors);

  // Closes every channel-pair file, back-patching its RIFF sizes.
  bool Finalize(std::vector<std::string>* errors);

  // Discards every in-progress channel-pair file. No-op when inactive.
  void Abort();

  // True when at least one channel pair is being written.
  bool active() const { return active_; }

  // Channel-pair numbers being written, sorted ascending.
  const std::vector<int>& channel_pairs() const { return pair_numbers_; }

 private:
  // Per channel pair: its number, WAV writer, and the two channel synthesisers.
  struct PairState {
    int pair = 0;
    std::unique_ptr<AudioWavWriter> writer;
    std::unique_ptr<AudioSynthesizer> left;
    std::unique_ptr<AudioSynthesizer> right;
  };

  // Reconfigures every pair's channel synthesisers for the section run that
  // starts at output frame `output_index`.
  void BeginRun(std::size_t output_index);

  ILogger* logger_;
  bool active_ = false;
  Standard standard_ = Standard::kUnknown;
  std::vector<int> pair_numbers_;
  std::vector<PairState> pairs_;

  // Per-output-frame plan (indexed by output frame).
  std::vector<const Section*> frame_sections_;
  std::vector<int> frame_sample_counts_;
  std::vector<char> is_run_start_;
  std::vector<std::int64_t> run_total_samples_;
};

}  // namespace videosynth
