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

#include <algorithm>

#include "videosynth/audio_sample_conversion.h"
#include "videosynth/timing_constants.h"

namespace videosynth {

namespace {

// True when the standard has a LaserDisc digital audio specification: IEC
// 60856-1986 Amd 2 clause 13 (PAL) and IEC 60857-1986 Amd 2 clause 13 (NTSC).
// No other standard defines one.
bool StandardSupportsEfmAudio(Standard standard) {
  return standard == Standard::kPal || standard == Standard::kNtsc;
}

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
  efm_active_ = false;
  efm_pair_ = 0;
  efm_frame_sample_counts_.clear();
  efm_run_total_samples_.clear();
  efm_frame_left_.clear();
  efm_frame_right_.clear();

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

  // LaserDisc digital audio runs on its own 44.1 kHz grid alongside the 48 kHz
  // WAV path, for the one selected pair (IEC 60856/60857 Amd 2 clause 13.2).
  // A selection naming an undeclared pair, or a standard without a LaserDisc
  // digital audio specification, leaves the EFM path inactive; the project
  // validator reports both conditions.
  const EfmAudioOutput& efm = project.output.efm_audio;
  efm_active_ = efm.enabled && StandardSupportsEfmAudio(standard_) &&
                std::find(pair_numbers_.begin(), pair_numbers_.end(),
                          efm.pair) != pair_numbers_.end();
  if (efm_active_) {
    efm_pair_ = efm.pair;
    efm_frame_sample_counts_.resize(frame_count);
    for (std::size_t k = 0; k < frame_count; ++k) {
      efm_frame_sample_counts_[k] =
          EfmAudioSamplesForFrame(standard_, static_cast<std::int64_t>(k));
    }
  }

  // Section runs in output order: reset phase at each run start and total the
  // run's samples so a section-spanning ramp can normalise its sweep.
  is_run_start_.assign(frame_count, 0);
  run_total_samples_.assign(frame_count, 0);
  if (efm_active_) {
    efm_run_total_samples_.assign(frame_count, 0);
  }
  std::size_t run_start = 0;
  while (run_start < frame_count) {
    const Section* section = frame_sections_[run_start];
    std::size_t run_end = run_start;
    std::int64_t total = 0;
    std::int64_t efm_total = 0;
    while (run_end < frame_count && frame_sections_[run_end] == section) {
      total += frame_sample_counts_[run_end];
      if (efm_active_) {
        efm_total += efm_frame_sample_counts_[run_end];
      }
      ++run_end;
    }
    is_run_start_[run_start] = 1;
    for (std::size_t i = run_start; i < run_end; ++i) {
      run_total_samples_[i] = total;
      if (efm_active_) {
        efm_run_total_samples_[i] = efm_total;
      }
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
    if (efm_active_ && pair == efm_pair_) {
      state.efm_left =
          std::make_unique<AudioSynthesizer>(EfmAudioSampleRateHz());
      state.efm_right =
          std::make_unique<AudioSynthesizer>(EfmAudioSampleRateHz());
    }
    if (!state.writer->BeginWrite(project, pair, errors)) {
      // Abort any writers already opened before failing.
      for (PairState& opened : pairs_) {
        opened.writer->AbortWrite();
      }
      pairs_.clear();
      efm_active_ = false;
      return false;
    }
    pairs_.push_back(std::move(state));
  }

  active_ = true;
  if (logger_ != nullptr) {
    logger_->Info("Audio: writing " + std::to_string(pairs_.size()) +
                  " channel pair(s).");
    if (efm_active_) {
      logger_->Info("Audio: channel pair " + std::to_string(efm_pair_) +
                    " also synthesised at 44.1 kHz for EFM output.");
    } else if (project.output.efm_audio.enabled) {
      logger_->Debug(
          "Audio: EFM output requested but inactive (pair not declared or "
          "standard has no LaserDisc digital audio specification).");
    }
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
    if (state.efm_left != nullptr) {
      // The 44.1 kHz path shares the section's AudioParameters but normalises
      // section-spanning ramps against its own sample total.
      const std::int64_t efm_total = efm_run_total_samples_[output_index];
      state.efm_left->BeginSection(left, efm_total);
      state.efm_right->BeginSection(right, efm_total);
    }
  }
}

int AudioTrackGenerator::efm_samples_for_frame(std::size_t output_index) const {
  if (!efm_active_ || output_index >= efm_frame_sample_counts_.size()) {
    return 0;
  }
  return efm_frame_sample_counts_[output_index];
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

    if (state.efm_left != nullptr) {
      // IEC 60908-1999 clause 12: compact-disc audio is 16-bit, so the
      // 44.1 kHz samples are converted from the synthesiser's 24-bit domain.
      const int efm_sample_count = efm_frame_sample_counts_[output_index];
      efm_frame_left_ =
          ConvertSamples24To16(state.efm_left->Synthesize(efm_sample_count));
      efm_frame_right_ =
          ConvertSamples24To16(state.efm_right->Synthesize(efm_sample_count));
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
