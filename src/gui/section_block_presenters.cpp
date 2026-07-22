/*
 * File:        section_block_presenters.cpp
 * Module:      gui
 * Purpose:     Widget-free mapping layer for the optional per-section YAML
 *              blocks (audio, noise, dropouts, OSD)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "section_block_presenters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace videosynth::gui {

namespace {

// Mid-range noise floor for a freshly-enabled block: clearly audible in
// preview measurements yet far from both validator bounds.
constexpr double kDefaultNoiseDb = 48.0;

// Freshly-enabled ramp: audible sweep well inside [0, 22000] Hz.
constexpr double kDefaultRampStartHz = 200.0;
constexpr double kDefaultRampEndHz = 4000.0;

}  // namespace

void SetNoiseBlockEnabled(Section* section, bool enabled) {
  if (!enabled) {
    section->noise = NoiseParameters{};
    return;
  }
  if (section->noise.enabled) {
    return;
  }
  section->noise = NoiseParameters{};
  section->noise.enabled = true;
  section->noise.noise_db = kDefaultNoiseDb;
}

void SetNoiseSeedSpecified(Section* section, bool specified) {
  section->noise.noise_seed_specified = specified;
  if (!specified) {
    section->noise.noise_seed = 0;
  }
}

void SetRandomDropoutsEnabled(Section* section, bool enabled) {
  if (!enabled) {
    section->dropouts.random = RandomDropoutParameters{};
    return;
  }
  if (section->dropouts.random.enabled) {
    return;
  }
  section->dropouts.random = RandomDropoutParameters{};
  section->dropouts.random.enabled = true;
  section->dropouts.random.scale = editor_limits::kDropoutScaleMin;
}

void SetScratchDropoutsEnabled(Section* section, bool enabled) {
  if (!enabled) {
    section->dropouts.scratch = ScratchDropoutParameters{};
    return;
  }
  if (section->dropouts.scratch.enabled) {
    return;
  }
  section->dropouts.scratch = ScratchDropoutParameters{};
  section->dropouts.scratch.enabled = true;
  section->dropouts.scratch.scale = editor_limits::kDropoutScaleMin;
}

AudioParameters MakeDefaultAudioChannel() {
  AudioParameters channel;
  channel.enabled = true;
  // waveform_text mirrors what the YAML will carry; the emitter only writes
  // the key when the text is non-empty.
  channel.waveform = AudioWaveform::kSine;
  channel.waveform_text = "sine";
  return channel;
}

AudioChannelPair MakeDefaultAudioChannelPair(int pair) {
  AudioChannelPair channel_pair;
  channel_pair.pair = pair;
  channel_pair.pair_specified = true;
  // Both channels carry the same default tone; the editor treats identical
  // left/right as "same tone on both channels" until the user splits them.
  channel_pair.left = MakeDefaultAudioChannel();
  channel_pair.right = channel_pair.left;
  return channel_pair;
}

void SetAudioChannelRampEnabled(AudioParameters* channel, bool enabled) {
  if (channel == nullptr) {
    return;
  }
  if (!enabled) {
    channel->ramp_enabled = false;
    channel->ramp_start_hz = 0.0;
    channel->ramp_end_hz = 0.0;
    channel->ramp_start_specified = false;
    channel->ramp_end_specified = false;
    channel->ramp_mode = AudioRampMode::kUp;
    channel->ramp_mode_text.clear();
    channel->ramp_period_seconds = 0.0;
    return;
  }
  if (channel->ramp_enabled) {
    return;
  }
  channel->ramp_enabled = true;
  channel->ramp_start_hz = kDefaultRampStartHz;
  channel->ramp_end_hz = kDefaultRampEndHz;
  channel->ramp_start_specified = true;
  channel->ramp_end_specified = true;
  channel->ramp_mode = AudioRampMode::kUp;
  channel->ramp_mode_text = "up";
  channel->ramp_period_seconds = 0.0;
}

void SetAudioChannelWaveform(AudioParameters* channel,
                             const std::string& waveform_name) {
  if (channel == nullptr) {
    return;
  }
  channel->waveform = AudioWaveformFromString(waveform_name);
  channel->waveform_text = waveform_name;
}

int NextFreeAudioChannelPair(const std::vector<AudioChannelPair>& pairs) {
  for (int candidate = 0; candidate < kMaxAudioChannelPairs; ++candidate) {
    bool used = false;
    for (const AudioChannelPair& channel_pair : pairs) {
      if (channel_pair.pair == candidate) {
        used = true;
        break;
      }
    }
    if (!used) {
      return candidate;
    }
  }
  return -1;
}

bool OsdBlockEnabled(const Section& section) {
  return !section.osd.overlays.empty();
}

OsdOverlay MakeDefaultOsdOverlay() {
  OsdOverlay overlay;
  overlay.text = "{picture_number}";
  overlay.x = 0;
  overlay.y = 0;
  overlay.scale = 1;
  overlay.fg_level = OsdFgLevel::kWhite;
  overlay.bg_level = OsdBgLevel::kTransparent;
  return overlay;
}

std::vector<std::string> AudioWaveformOptions() {
  return {"sine", "square", "sawtooth", "triangle"};
}

std::vector<std::string> AudioRampModeOptions() {
  return {"up", "down", "bounce"};
}

double DurationFramesToSeconds(int frames, double frame_rate_hz) {
  if (frame_rate_hz <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(frames) / frame_rate_hz;
}

int DurationSecondsToFrames(double seconds, double frame_rate_hz,
                            int max_frames) {
  if (frame_rate_hz <= 0.0) {
    return 1;
  }
  const int64_t frames = std::llround(seconds * frame_rate_hz);
  return static_cast<int>(
      std::clamp<int64_t>(frames, 1, std::max(1, max_frames)));
}

Section ApplySectionEditDelta(const Section& before, const Section& after,
                              Section target) {
  if (after.section_type != before.section_type) {
    target.section_type = after.section_type;
  }
  if (after.source != before.source) {
    target.source = after.source;
  }
  // The duration widgets couple these three fields (frame count vs "all
  // frames" x repeat), so a change to any of them propagates all of them.
  if (after.duration_frames_all != before.duration_frames_all ||
      after.duration_frames != before.duration_frames ||
      after.duration_frames_repeat != before.duration_frames_repeat) {
    target.duration_frames_all = after.duration_frames_all;
    target.duration_frames = after.duration_frames;
    target.duration_frames_repeat = after.duration_frames_repeat;
  }
  if (!(after.noise == before.noise)) {
    target.noise = after.noise;
  }
  // Random and scratch dropouts are independent blocks in the editor; diff
  // them separately so toggling one never overwrites the other on targets.
  if (!(after.dropouts.random == before.dropouts.random)) {
    target.dropouts.random = after.dropouts.random;
  }
  if (!(after.dropouts.scratch == before.dropouts.scratch)) {
    target.dropouts.scratch = after.dropouts.scratch;
  }
  if (!(after.osd == before.osd)) {
    target.osd = after.osd;
  }
  if (!(after.audio_channel_pairs == before.audio_channel_pairs)) {
    target.audio_channel_pairs = after.audio_channel_pairs;
  }
  if (!(after.line_injections == before.line_injections)) {
    target.line_injections = after.line_injections;
  }
  return target;
}

std::vector<OsdTokenHelp> OsdTokenCatalogue() {
  return {
      {"{picture_number}",
       "Current CAV picture number (or frame index when no biphase codes)"},
      {"{biphase_hex}", "24-bit biphase code word for the frame, in hex"},
      {"{phase_id}", "Colour sequence phase index (PAL 0–7, NTSC 0–3)"},
      {"{section_name}", "Name of the owning section"},
      {"{timecode}",
       "CLV programme timecode HH:MM:SS:FF, counting from output start "
       "(CLV discs only)"},
      {"{frame_number}", "Sequential output frame number (1-based)"},
  };
}

}  // namespace videosynth::gui
