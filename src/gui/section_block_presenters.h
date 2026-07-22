/*
 * File:        section_block_presenters.h
 * Module:      gui
 * Purpose:     Widget-free mapping layer for the optional per-section YAML
 *              blocks (audio, noise, dropouts, OSD)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>

#include "videosynth/model.h"

namespace videosynth::gui {

// Thread-safety: all functions in this module are thread-safe pure functions
// over their arguments.

// Editor input limits mirrored from the rules in project_validator.cpp (and
// the design documents it cites) so widgets constrain values to exactly what
// validation accepts.
namespace editor_limits {
// Noise: noise-injection-design.md bounds enforced by ValidateNoiseParameters.
inline constexpr double kNoiseDbMin = 20.0;
inline constexpr double kNoiseDbMax = 61.0;
// Dropouts: active scale range enforced by ValidateDropoutParameters (0 only
// as "block disabled", so editors offer 1–20).
inline constexpr int kDropoutScaleMin = 1;
inline constexpr int kDropoutScaleMax = 20;
// OSD: glyph scale and luma ranges enforced by ValidateOsdConfig.
inline constexpr int kOsdScaleMin = 1;
inline constexpr int kOsdScaleMax = 4;
inline constexpr double kLumaMin = 0.0;
inline constexpr double kLumaMax = 1.0;
// Audio: audio-generation-design.md bounds enforced by
// ValidateAudioParameters.
inline constexpr double kAudioFrequencyMinHz = 0.0;
inline constexpr double kAudioFrequencyMaxHz = 22000.0;
inline constexpr double kAudioAmplitudeMin = 0.0;
inline constexpr double kAudioAmplitudeMax = 1.0;
}  // namespace editor_limits

// Enable/disable helpers for the optional blocks. Disabling always resets
// the block to its default-constructed state so YamlProjectEmitter drops it
// from the emitted file (a disabled block must never emit defaults);
// enabling seeds validator-clean defaults.

void SetNoiseBlockEnabled(Section* section, bool enabled);
void SetNoiseSeedSpecified(Section* section, bool specified);

void SetRandomDropoutsEnabled(Section* section, bool enabled);
void SetScratchDropoutsEnabled(Section* section, bool enabled);

// A freshly-enabled audio channel (validator-clean fixed-frequency sine).
AudioParameters MakeDefaultAudioChannel();

// A freshly-added channel pair carrying `pair` with the same default tone on
// both channels (the editor's linked "same tone on both channels" mode).
AudioChannelPair MakeDefaultAudioChannelPair(int pair);

// Switching a channel to ramp mode seeds a validator-clean up-ramp; switching
// back clears every ramp field so the emitted block returns to fixed-frequency
// form.
void SetAudioChannelRampEnabled(AudioParameters* channel, bool enabled);
// Keeps waveform and waveform_text consistent; `waveform_name` must be one
// of "sine", "square", "sawtooth", "triangle".
void SetAudioChannelWaveform(AudioParameters* channel,
                             const std::string& waveform_name);
// The lowest channel-pair number 0–7 not already used by `pairs`, or -1 when
// all eight are taken.
int NextFreeAudioChannelPair(const std::vector<AudioChannelPair>& pairs);

// The OSD block exists exactly when the overlay list is non-empty.
bool OsdBlockEnabled(const Section& section);
OsdOverlay MakeDefaultOsdOverlay();

// Waveform / ramp-mode option lists for editors, in model.h parse order.
std::vector<std::string> AudioWaveformOptions();
std::vector<std::string> AudioRampModeOptions();

// Supported OSD template tokens with help text for the overlay editor.
struct OsdTokenHelp {
  std::string token;
  std::string description;
};
std::vector<OsdTokenHelp> OsdTokenCatalogue();

// Multi-select batch editing: mirrors onto `target` exactly the editor-visible
// fields that differ between `before` and `after` (an edit applied to the
// primary selected section), leaving everything else in `target` untouched.
// The name is never mirrored — it identifies the individual section. The three
// duration fields propagate as a unit because the editor's widgets couple
// them (frames vs "all frames" x repeat). Returns the updated target.
Section ApplySectionEditDelta(const Section& before, const Section& after,
                              Section target);

}  // namespace videosynth::gui
