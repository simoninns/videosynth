/*
 * File:        audio_track_generator.cpp
 * Module:      audio_track_generator
 * Purpose:     Orchestrates synthesis of up to eight stereo audio channel pairs
 *              frame-locked to the output video stream.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "videosynth/audio_track_generator.h"

#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

// Returns the section's parameters for channel pair `pair`, or nullptr if the
// section does not declare that pair (all frames silent for the pair).
const AudioChannelPair* FindPair(const Section* section, int pair) {
  if (section == nullptr) {
    return nullptr;
  }
  for (const AudioChannelPair& cp : section->audio_channel_pairs) {
    if (cp.pair == pair) {
      return &cp;
    }
  }
  return nullptr;
}

}  // namespace

AudioTrackGenerator::AudioTrackGenerator(ILogger* logger) : logger_(logger) {}

bool AudioTrackGenerator::Begin(
    const Project& project,
    const std::vector<const Section*>& output_frame_sections,
    std::vector<std::string>* errors) {
  if (errors == nullptr) {
    return false;
  }
  active_ = false;
  pairs_.clear();
  pair_numbers_.clear();
  frame_sections_.clear();
  frame_sample_counts_.clear();
  is_run_start_.clear();
  run_total_samples_.clear();

  pair_numbers_ = ProjectAudioChannelPairs(project);
  if (pair_numbers_.empty()) {
    // No audio declared: succeed as an inert generator.
    return true;
  }

  standard_ = project.cvbs_presets.video_standard_preset;
  const std::size_t frame_count = output_frame_sections.size();
  frame_sections_ = output_frame_sections;

  // Per-frame sample counts follow output position (SMPTE 272M §14.3).
  frame_sample_counts_.resize(frame_count);
  for (std::size_t k = 0; k < frame_count; ++k) {
    frame_sample_counts_[k] =
        AudioSamplesForFrame(standard_, static_cast<std::int64_t>(k));
  }

  // Section runs in output order: reset phase at each run start and total the
  // run's samples so a section-spanning ramp can normalise its sweep.
  is_run_start_.assign(frame_count, 0);
  run_total_samples_.assign(frame_count, 0);
  std::size_t run_start = 0;
  while (run_start < frame_count) {
    const Section* section = frame_sections_[run_start];
    std::size_t run_end = run_start;
    std::int64_t total = 0;
    while (run_end < frame_count && frame_sections_[run_end] == section) {
      total += frame_sample_counts_[run_end];
      ++run_end;
    }
    is_run_start_[run_start] = 1;
    for (std::size_t i = run_start; i < run_end; ++i) {
      run_total_samples_[i] = total;
    }
    run_start = run_end;
  }

  // Open one writer and two channel synthesisers per declared pair.
  const double sample_rate_hz = AudioSampleRateHz(standard_);
  pairs_.reserve(pair_numbers_.size());
  for (const int pair : pair_numbers_) {
    PairState state;
    state.pair = pair;
    state.writer = std::make_unique<AudioWavWriter>(logger_);
    state.left = std::make_unique<AudioSynthesizer>(sample_rate_hz);
    state.right = std::make_unique<AudioSynthesizer>(sample_rate_hz);
    if (!state.writer->BeginWrite(project, pair, errors)) {
      // Abort any writers already opened before failing.
      for (PairState& opened : pairs_) {
        opened.writer->AbortWrite();
      }
      pairs_.clear();
      return false;
    }
    pairs_.push_back(std::move(state));
  }

  active_ = true;
  if (logger_ != nullptr) {
    logger_->Info("Audio: writing " + std::to_string(pairs_.size()) +
                  " channel pair(s).");
  }
  return true;
}

void AudioTrackGenerator::BeginRun(std::size_t output_index) {
  const Section* section = frame_sections_[output_index];
  const std::int64_t total = run_total_samples_[output_index];
  for (PairState& state : pairs_) {
    const AudioChannelPair* cp = FindPair(section, state.pair);
    // Absent pair or absent channel → a disabled AudioParameters, which the
    // synthesiser renders as silence.
    const AudioParameters left = cp != nullptr ? cp->left : AudioParameters{};
    const AudioParameters right = cp != nullptr ? cp->right : AudioParameters{};
    state.left->BeginSection(left, total);
    state.right->BeginSection(right, total);
  }
}

bool AudioTrackGenerator::EmitFrame(std::size_t output_index,
                                    std::vector<std::string>* errors) {
  if (!active_) {
    return true;
  }
  if (errors == nullptr) {
    return false;
  }
  if (output_index >= frame_sections_.size()) {
    errors->push_back("Audio EmitFrame called out of range for output frame " +
                      std::to_string(output_index) + ".");
    return false;
  }

  if (is_run_start_[output_index] != 0) {
    BeginRun(output_index);
  }

  const int sample_count = frame_sample_counts_[output_index];
  for (PairState& state : pairs_) {
    const std::vector<std::int32_t> left = state.left->Synthesize(sample_count);
    const std::vector<std::int32_t> right =
        state.right->Synthesize(sample_count);
    if (!state.writer->AppendFrameAudio(left, right, errors)) {
      return false;
    }
  }
  return true;
}

bool AudioTrackGenerator::Finalize(std::vector<std::string>* errors) {
  if (!active_) {
    return true;
  }
  if (errors == nullptr) {
    return false;
  }
  bool ok = true;
  for (PairState& state : pairs_) {
    if (!state.writer->FinalizeWrite(errors)) {
      ok = false;
    }
  }
  return ok;
}

void AudioTrackGenerator::Abort() {
  if (!active_) {
    return;
  }
  for (PairState& state : pairs_) {
    state.writer->AbortWrite();
  }
  active_ = false;
}

}  // namespace videosynth
