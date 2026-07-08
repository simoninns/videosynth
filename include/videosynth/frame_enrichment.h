/*
 * File:        frame_enrichment.h
 * Module:      frame_enrichment
 * Purpose:     Fully-resolved per-frame VBI payload and context produced by
 *              the sequential schedule-enrichment pass, enabling stateless
 *              (order-independent) frame sample synthesis.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "videosynth/fm_encoder.h"

namespace videosynth {

// Per-frame snapshot captured after code resolution and before generator
// advance. Used for token-based OSD rendering.
struct PerFrameContext {
  // Decoded CAV picture number for this frame (0 if no picture_number
  // generator is active).
  int picture_number = 0;
  // Raw 24-bit biphase code values produced by all active generators.
  std::vector<uint32_t> biphase_words;
  // Colour-frame sequence index: 0–3 for PAL (V-axis cycle), 0–1 for NTSC.
  int colour_frame_index = 0;
};

// One fully-resolved VBI code injection for a single line of one frame.
// Produced by BiphaseInjectionManager::ResolveFrame and consumed by
// InjectResolvedVbiLines; carries everything the waveform writer needs so
// injection requires no generator state.
struct VbiLineInjection {
  enum class Kind {
    kBiphase24,  // IEC 60856/60857 24-bit Manchester biphase code
    kFm40,       // IEC 60857 Figure 13 40-bit FM code (NTSC only)
    kWhiteFlag,  // IEC 60857 Figure 12 100 IRE white flag (NTSC only)
  };

  int line_number_1based = 0;
  Kind kind = Kind::kBiphase24;
  // 24-bit code word for this frame (Kind::kBiphase24 only).
  uint32_t biphase_code = 0;
  // Resolved 40-bit FM payload for this frame (Kind::kFm40 only).
  FmData fm_data = {};
  // Horizontal start offset of the signal within the line, in samples.
  int start_sample = 0;
  // White flag pulse length in samples (Kind::kWhiteFlag only).
  int flag_length_samples = 0;
};

// Fully-resolved per-frame payload attached to a FrameScheduleItem by the
// sequential enrichment pass in BuildFrameSchedule. With this payload, sample
// synthesis is a pure function of (Project, enriched FrameScheduleItem):
// frames may be synthesised out of order and on concurrent worker threads.
struct FrameEnrichment {
  // Disc frame index, i.e. (picture number - 1) when the frame is anchored to
  // a CAV picture_number code, otherwise the schedule position plus the
  // project's first-picture-number offset. Drives colour-subcarrier phase,
  // PAL burst blanking parity, and pilot-burst phase.
  std::size_t disc_frame_index = 0;
  // Context snapshot for OSD token resolution.
  PerFrameContext context;
  // Resolved VBI code injections for this frame, in ascending line order.
  std::vector<VbiLineInjection> vbi_lines;
  // Token-resolved OSD overlay strings, one per section OSD overlay.
  std::vector<std::string> osd_texts;
};

}  // namespace videosynth
