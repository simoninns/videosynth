/*
 * File:        model.h
 * Module:      model
 * Purpose:     Defines project data models and video-standard enums.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "videosynth/biphase_types.h"

namespace videosynth {

// Thread-safety: All types, enums, and functions in this module are
// thread-safe. They are plain data containers or stateless functions. May be
// accessed concurrently from multiple threads.
enum class Standard {
  kPal,
  kNtsc,
  // PAL-M: System M line structure (525 lines, ~60 Hz) with PAL colour
  // encoding. Used in Brazil. ITU-R BT.470-6 Table 2 (M/PAL column).
  kPalM,
  kUnknown,
};

inline Standard StandardFromString(const std::string& value) {
  if (value == "PAL") {
    return Standard::kPal;
  }
  if (value == "NTSC") {
    return Standard::kNtsc;
  }
  if (value == "PAL_M" || value == "PAL-M") {
    return Standard::kPalM;
  }
  return Standard::kUnknown;
}

inline std::string StandardToString(Standard standard) {
  switch (standard) {
    case Standard::kPal:
      return "PAL";
    case Standard::kNtsc:
      return "NTSC";
    case Standard::kPalM:
      return "PAL_M";
    default:
      return "UNKNOWN";
  }
}

// Placeholder for project-level biphase configuration.
// All disc-specific biphase parameters are specified per section and
// per injection; this struct is reserved for any future global settings.
struct BiphasePresets {};

struct CvbsPresets {
  Standard video_standard_preset = Standard::kUnknown;
  std::string sample_encoding_preset = "CVBS_U10_4FSC";
  std::string signal_state_preset = "STANDARD_TBC_LOCKED";
  bool pal_laserdisc_pilot_burst = false;
  bool ntsc_laserdisc_vbi_burst = false;
  double ntsc_black_setup_ire = 7.5;
  bool ntsc_black_setup_ire_specified = false;
  BiphasePresets biphase_presets;
};

inline bool IsSupportedNtscBlackSetupIre(double setup_ire) {
  constexpr double kEpsilon = 1e-9;
  return std::abs(setup_ire - 7.5) < kEpsilon || std::abs(setup_ire) < kEpsilon;
}

inline bool Is4fscSampleEncodingPreset(const std::string& preset) {
  return preset == "CVBS_U10_4FSC" || preset == "CVBS_U16_4FSC" ||
         preset == "CVBS_TPG21_4FSC" || preset == "CVBS_S16_FSC";
}

inline bool IsSupportedSampleEncodingPreset(const std::string& preset) {
  return Is4fscSampleEncodingPreset(preset) || preset == "RAW_S16_28M" ||
         preset == "RAW_S16_40M";
}

// Returns true if the preset can represent sub-sync excursions (below -300 mV).
// The PAL pilot burst swings ±300 mV about sync tip, so troughs reach -600 mV.
// U10/U16/TPG21 presets clamp at the 10-bit legal-code floor (~-300 mV);
// CVBS_S16_FSC and RAW_S16_* presets carry the full excursion.
inline bool IsSubSyncCapableSampleEncodingPreset(const std::string& preset) {
  return preset == "CVBS_S16_FSC" || preset == "RAW_S16_28M" ||
         preset == "RAW_S16_40M";
}

inline std::string SampleRateModeFromEncodingPreset(const std::string& preset) {
  if (Is4fscSampleEncodingPreset(preset)) {
    return "4fsc";
  }
  if (preset == "RAW_S16_28M") {
    return "28M";
  }
  if (preset == "RAW_S16_40M") {
    return "40M";
  }
  return "unknown";
}

inline bool IsLockedSignalStatePreset(const std::string& preset) {
  return preset == "STANDARD_TBC_LOCKED";
}

struct NoiseParameters {
  bool enabled = false;
  // Noise floor level in dB. Sets Black PSNR target. Valid range: [20.0, 61.0].
  double noise_db = 61.0;
  // White is this many dB noisier than black. Valid range: [0.0,
  // noise_db-20.0].
  double noise_spread_db = 0.0;
  // When noise_seed_specified is true, this seed is mixed into the per-frame
  // RNG seed to produce deterministic output across runs. When false, a
  // random base seed captured at pipeline construction time is used instead,
  // so every run produces different noise.
  int64_t noise_seed = 0;
  bool noise_seed_specified = false;
};

struct RandomDropoutParameters {
  bool enabled = false;
  int scale = 0;  // 1–20; maps to frequency and max_duration via scale mapping
  bool seed_specified = false;
  int64_t seed = 0;
};

struct ScratchDropoutParameters {
  bool enabled = false;
  int scale = 0;  // 1–20; maps to count, max_duration_frames, max_width_samples
  bool seed_specified = false;
  int64_t seed = 0;
};

struct DropoutParameters {
  RandomDropoutParameters random = {};
  ScratchDropoutParameters scratch = {};
};

// Thread-safety: OsdOverlay and OsdConfig are plain data containers with no
// mutable state. They may be read concurrently from multiple threads.
struct OsdOverlay {
  // Literal text or template containing {picture_number}, {biphase_hex},
  // {phase_id}, or {section_name} tokens (resolved per-frame at render time).
  std::string text;
  // Active-area x offset in pixels (0 = left edge of active area).
  int x = 0;
  // Active-area y offset in lines (0 = first active line of frame).
  int y = 0;
  // Glyph scale factor: 1 = 8×8 px per glyph, 2 = 16×16 px. Range [1, 4].
  int scale = 1;
  // Foreground luma E_Y' in [0.0, 1.0].
  double fg_luma = 1.0;
  // Background luma E_Y' in [0.0, 1.0], or -1.0 for transparent (no write).
  double bg_luma = -1.0;
};

struct OsdConfig {
  std::vector<OsdOverlay> overlays;
};

// Oscillator shape for synthetic per-section audio. kUnknown marks an
// unrecognised YAML value so the validator can reject it with a clear message.
enum class AudioWaveform { kSine, kSquare, kSawtooth, kTriangle, kUnknown };

// Direction of a frequency ramp within a section (or within one ramp period).
enum class AudioRampMode { kUp, kDown, kBounce, kUnknown };

inline AudioWaveform AudioWaveformFromString(const std::string& value) {
  if (value == "sine") {
    return AudioWaveform::kSine;
  }
  if (value == "square") {
    return AudioWaveform::kSquare;
  }
  if (value == "sawtooth") {
    return AudioWaveform::kSawtooth;
  }
  if (value == "triangle") {
    return AudioWaveform::kTriangle;
  }
  return AudioWaveform::kUnknown;
}

inline AudioRampMode AudioRampModeFromString(const std::string& value) {
  if (value == "up") {
    return AudioRampMode::kUp;
  }
  if (value == "down") {
    return AudioRampMode::kDown;
  }
  if (value == "bounce") {
    return AudioRampMode::kBounce;
  }
  return AudioRampMode::kUnknown;
}

// Thread-safety: AudioParameters is a plain data container with no mutable
// state. It may be read concurrently from multiple threads.
//
// Per-section synthetic test-tone description. Frequency is either a fixed
// tone (frequency_hz, used when !ramp_enabled) or a linear ramp between
// ramp_start_hz and ramp_end_hz. A ramp spans the whole section when
// ramp_period_seconds == 0, or repeats every ramp_period_seconds when > 0.
struct AudioParameters {
  bool enabled = false;
  AudioWaveform waveform = AudioWaveform::kSine;
  // Raw YAML waveform value retained for validation error messages.
  std::string waveform_text;

  // Fixed-frequency mode (used when !ramp_enabled).
  double frequency_hz = 1000.0;

  // Ramp mode.
  bool ramp_enabled = false;
  double ramp_start_hz = 0.0;
  double ramp_end_hz = 0.0;
  bool ramp_start_specified = false;
  bool ramp_end_specified = false;
  AudioRampMode ramp_mode = AudioRampMode::kUp;
  // Raw YAML ramp-mode value retained for validation error messages.
  std::string ramp_mode_text;
  // 0.0 => ramp spans the whole section; > 0.0 => one ramp cycle lasts this
  // many seconds and repeats across the section duration.
  double ramp_period_seconds = 0.0;

  // Peak amplitude as a fraction of full scale [0.0, 1.0].
  double amplitude = 0.5;
};

struct Section {
  struct LineInjectionCode {
    std::string code_type;
    int start_value = 0;
    bool start_value_specified = false;
    int chapter = 0;
    bool chapter_specified = false;
    std::string programme_status;
    bool programme_status_specified = false;
    std::string users_code;
    bool users_code_specified = false;
  };

  struct LineInjection {
    std::string type;
    std::vector<int> target_lines;
    std::string vits_type;
    std::string disc_type;
    std::vector<LineInjectionCode> codes;
  };

  std::string name = {};
  std::string type = {};
  SectionType section_type = SectionType::kUnknown;
  std::vector<LineInjection> line_injections = {};
  std::string source = {};
  bool duration_frames_all = false;
  int duration_frames = 0;
  int start_frame = 0;
  NoiseParameters noise = {};
  DropoutParameters dropouts = {};
  OsdConfig osd = {};
  AudioParameters audio = {};
};

struct OutputTargets {
  std::string video_path;
  std::string metadata_path;
  // "composite" (default) or "yc" (dual-file luma+chroma).
  std::string signal_type = "composite";
};

enum class DiscSkipDirection {
  kForward,
  kBackward,
};

// Describes a single skip event in the disc output stream.
//
// Forward skip: disc frames [at_frame .. at_frame+count-1] (1-based) are
// generated to maintain burst-phase continuity but withheld from the output.
// The capture therefore has a gap at that position.
//
// Backward skip: after disc frame at_frame (1-based) has been written,
// disc frames [at_frame-count+1 .. at_frame] are re-emitted as bit-identical
// copies, simulating the player rewinding and replaying the same grooves.
struct DiscSkip {
  // 1-based disc frame number at which the skip event starts (forward) or
  // after which replay begins (backward).
  int at_frame = 0;
  DiscSkipDirection direction = DiscSkipDirection::kForward;
  // Frames to discard (forward) or replay (backward). Must be >= 1.
  int count = 0;
};

struct Project {
  std::string name;
  std::string version;
  CvbsPresets cvbs_presets;
  OutputTargets output;
  std::vector<Section> sections;
  // Optional list of disc skip events applied after generation. Empty = no
  // skips; the pipeline uses the standard batch loop.
  std::vector<DiscSkip> disc_skips;
};

// True if any section requests synthetic audio. When false the pipeline emits
// no WAV track and the metadata audio_locked field stays NULL.
inline bool ProjectEnablesAudio(const Project& project) {
  for (const Section& section : project.sections) {
    if (section.audio.enabled) {
      return true;
    }
  }
  return false;
}

}  // namespace videosynth
