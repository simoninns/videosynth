/*
 * File:        progressive_source_probe.h
 * Module:      progressive_source_probe
 * Purpose:     Probes progressive source metadata for profile validation and frame-count semantics.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "videosynth/interfaces.h"

namespace videosynth {

class ProgressiveSourceProbe final : public IProgressiveSourceProbe {
 public:
  bool Probe(const Section& section,
             ProgressiveSourceProfile* out_profile,
             std::string* error) override;
};

}  // namespace videosynth