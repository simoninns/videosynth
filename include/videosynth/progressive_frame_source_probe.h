/*
 * File:        progressive_frame_source_probe.h
 * Module:      progressive_frame_source_probe
 * Purpose:     Probes progressive source metadata for profile validation and frame-count semantics.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class ProgressiveFrameSourceProbe final : public IProgressiveFrameSourceProbe {
 public:
  bool Probe(const Section& section,
             ProgressiveFrameSourceProfile* out_profile,
             std::string* error) override;
};

}  // namespace videosynth