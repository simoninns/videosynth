/*
 * File:        progressive_frame_source_probe.h
 * Module:      progressive_frame_source_probe
 * Purpose:     Probes progressive source metadata for profile validation and
 * frame-count semantics.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

// Thread-safety: ProgressiveFrameSourceProbe IS thread-safe. The Probe method
// has no internal state and only reads from its parameters. It may be called
// concurrently from multiple threads.
class ProgressiveFrameSourceProbe final : public IProgressiveFrameSourceProbe {
 public:
  // Ownership: out_profile and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  bool Probe(const Section& section, ProgressiveFrameSourceProfile* out_profile,
             std::string* error) override;
};

}  // namespace videosynth