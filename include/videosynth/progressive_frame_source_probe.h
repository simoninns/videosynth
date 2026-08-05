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

#include <map>
#include <mutex>
#include <string>

#include "videosynth/interfaces.h"

namespace videosynth {

// Thread-safety: ProgressiveFrameSourceProbe IS thread-safe. Its only state is
// a memo of successful probe results, guarded by an internal mutex; Probe may
// be called concurrently from multiple threads.
class ProgressiveFrameSourceProbe final : public IProgressiveFrameSourceProbe {
 public:
  // Ownership: out_profile and error are output parameters. The caller owns
  // the pointed-to memory and must ensure the pointers are valid (non-null).
  // The implementation writes to these locations but does not take ownership.
  //
  // A source probed once is served from the memo for the rest of the run, so
  // projects that reference the same file from several sections pay for one
  // probe rather than one per section.
  bool Probe(const Section& section, ProgressiveFrameSourceProfile* out_profile,
             std::string* error) override;

 private:
  std::mutex profile_cache_mutex_;
  // Keyed by source path; holds successful probes only.
  std::map<std::string, ProgressiveFrameSourceProfile> profile_cache_;
};

}  // namespace videosynth